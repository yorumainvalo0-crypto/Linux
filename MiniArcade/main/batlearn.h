// MiniArcade: the battery curve and how it learns the own cell. No hardware
// in here, so the host tests can feed it a whole discharge in a moment.
#pragma once
#include <stdint.h>
#include <string.h>

/* Resting voltage of a Li-ion cell against its charge. The curve is flat in
   the middle, so a straight line from empty to full shows nonsense there. */
static const uint16_t BAT_CURVE[][2] = {
  {3300, 0}, {3450, 5}, {3600, 10}, {3680, 20}, {3710, 30}, {3750, 40},
  {3800, 50}, {3850, 60}, {3920, 70}, {4000, 80}, {4100, 90}, {4180, 100}
};
#define BAT_GEN_EMPTY 3300
#define BAT_GEN_FULL  4180

uint8_t batCurve(uint16_t mv) {
  const uint8_t n = sizeof(BAT_CURVE) / sizeof(BAT_CURVE[0]);
  if (mv <= BAT_CURVE[0][0]) return 0;
  for (uint8_t i = 1; i < n; i++)
    if (mv < BAT_CURVE[i][0]) {
      uint16_t v0 = BAT_CURVE[i - 1][0], v1 = BAT_CURVE[i][0];
      uint16_t p0 = BAT_CURVE[i - 1][1], p1 = BAT_CURVE[i][1];
      return (uint8_t)(p0 + (uint32_t)(mv - v0) * (p1 - p0) / (v1 - v0));
    }
  return 100;
}

/* Learning the own cell while it is used - it never has to be run flat on
   purpose:
   - full:  the voltage at the start of a session right after charging
            (a session that starts while charging does not count).
   - empty: the last voltage written before the board went off without
            going to sleep - that is where the cell gave up under load.
   - shape: the console uses about the same current all the time (screen
            on, no radio), so play time is used charge. A run from full to
            empty gives the minutes the cell lasts; after that, the minute
            each voltage step is passed tells its true percentage. Every
            later run from full refines the curve, also when it is charged
            again long before empty.
   WLAN and the multiplayer radio use more current: such minutes count
   double, and a run with more than a fifth of them does not teach the
   run length. Charging in between ends a run.                          */
#define BL_VER    1
#define BL_LEVELS 17                  // voltage steps of 50 mV ...
#define BL_TOP    4150                // ... from 4.15 V down to 3.35 V
#define BL_NONE   0xFFFF              // step not passed yet
#define BL_SKIP   0xFFFE              // step was below the start already

enum { BL_RUN = 1, BL_T = 2, BL_SHAPE = 4, BL_SLEPT = 8 };

struct BatLearn {
  uint8_t  ver, flags;
  uint16_t full, empty;               // mV, 0 = not learned yet
  uint16_t last;                      // last voltage seen while running (BL_SLEPT: then it slept)
  uint16_t runMin, radioMin;          // minutes since the run started at full, of them with radio
  uint16_t T;                         // minutes from full to empty (radio minutes count double)
  uint16_t cross[BL_LEVELS];          // run minute each step was passed
  uint16_t pt[11];                    // learned voltage at 0, 10, ..., 100 %

  void clear() { memset(this, 0, sizeof(*this)); ver = BL_VER; }
  uint16_t fullMv() const  { return full ? full : BAT_GEN_FULL; }
  uint16_t emptyMv() const { return empty ? empty : BAT_GEN_EMPTY; }
  uint16_t runNow() const  { return runMin + radioMin; }
  static uint16_t level(uint8_t j) { return BL_TOP - 50 * j; }

  /* The charge in percent for a voltage: the learned curve, else the
     general one stretched between the learned full and empty voltage. */
  uint8_t percent(uint16_t mv) const {
    if (flags & BL_SHAPE) {
      if (mv <= pt[0]) return 0;
      for (uint8_t k = 1; k <= 10; k++)
        if (mv < pt[k]) return (uint8_t)(10 * (k - 1) + (uint32_t)(mv - pt[k - 1]) * 10 / (pt[k] - pt[k - 1]));
      return 100;
    }
    int32_t e = emptyMv(), f = fullMv();
    if (f - e < 300) return batCurve(mv);
    int32_t s = BAT_GEN_EMPTY + ((int32_t)mv - e) * (BAT_GEN_FULL - BAT_GEN_EMPTY) / (f - e);
    return batCurve((uint16_t)(s < 0 ? 0 : (s > 65535 ? 65535 : s)));
  }

  // the voltage the curve in use gives for q percent
  uint16_t voltageAt(uint8_t q) const {
    uint16_t lo = 2500, hi = 4500;
    while (lo < hi) {
      uint16_t m = (lo + hi) / 2;
      if (percent(m) >= q) hi = m; else lo = m + 1;
    }
    return lo;
  }

  // the learned curve keeps its ends on full and empty and always rises
  void tidy() {
    if (!(flags & BL_SHAPE)) return;
    pt[0] = emptyMv();
    pt[10] = fullMv();
    for (uint8_t k = 1; k <= 10; k++) if (pt[k] <= pt[k - 1]) pt[k] = pt[k - 1] + 1;
  }

  /* What this run tells about the curve. complete: it went down to empty,
     so 0 % is known too. Points the run did not reach stay as they are. */
  void learnShape(bool complete) {
    if (!T) return;
    // (percent, voltage) from full down, in falling order
    int16_t q[BL_LEVELS + 2];
    uint16_t v[BL_LEVELS + 2];
    uint8_t n = 0;
    q[n] = 100; v[n++] = fullMv();
    for (uint8_t j = 0; j < BL_LEVELS; j++) {
      if (cross[j] >= BL_SKIP || level(j) >= fullMv() || level(j) <= emptyMv()) continue;
      int32_t p = 100 - (int32_t)cross[j] * 100 / T;
      if (p < 0) p = 0;
      if (p >= q[n - 1]) continue;                   // out of order: noise
      q[n] = (int16_t)p; v[n++] = level(j);
    }
    if (complete && q[n - 1] > 0) { q[n] = 0; v[n++] = emptyMv(); }
    if (n < 2) return;
    uint16_t fresh[11];
    for (uint8_t k = 0; k <= 10; k++) fresh[k] = voltageAt(10 * k);   // the curve so far
    bool any = false;
    for (uint8_t k = 1; k < 10; k++) {
      int16_t want = 10 * k;
      for (uint8_t i = 1; i < n; i++)
        if (want <= q[i - 1] && want >= q[i]) {
          int32_t dv = (int32_t)v[i - 1] - v[i], dq = q[i - 1] - q[i];
          uint16_t mv = (uint16_t)(v[i] + (dq ? dv * (want - q[i]) / dq : 0));
          // a whole run counts half, a part of one a quarter
          fresh[k] = (flags & BL_SHAPE) ? (uint16_t)(complete ? (pt[k] + mv) / 2 : (3 * pt[k] + mv) / 4) : mv;
          any = true;
          break;
        }
    }
    if (!any) return;
    if (!(flags & BL_SHAPE)) {                       // first time: the rest from the curve so far
      for (uint8_t k = 0; k <= 10; k++) pt[k] = fresh[k];
      flags |= BL_SHAPE;
    } else {
      for (uint8_t k = 1; k < 10; k++) pt[k] = fresh[k];
    }
    tidy();
  }

  void startRun() {
    flags |= BL_RUN;
    runMin = radioMin = 0;
    for (uint8_t j = 0; j < BL_LEVELS; j++) cross[j] = BL_NONE;
  }

  /* A voltage while not charging, after a start or after charging ended.
     Learns full, starts a run from full, ends one after topping up.    */
  void fromRest(uint16_t mv) {
    // the top of the start voltages: a full charge lifts it at once, the
    // others pull it down a little (so one high reading does not stick)
    if (mv >= 3950) full = mv > full ? mv : (uint16_t)(full - (full - mv) / 8);
    if (mv + 40 >= fullMv()) {
      startRun();
      for (uint8_t j = 0; j < BL_LEVELS; j++) if (level(j) >= mv) cross[j] = BL_SKIP;
    } else if (last && mv > last + 40) {
      flags &= ~BL_RUN;                              // charged a bit, not full: no use
    }
    tidy();
  }

  /* Once per start, about a minute in, when the voltage is not rising.
     A last voltage near empty means: the cell gave up there.          */
  void start(uint16_t mv) {
    bool slept = flags & BL_SLEPT;
    flags &= ~BL_SLEPT;
    if (!slept && last > 2900 && last < emptyMv() + 200) {
      empty = empty ? (uint16_t)((empty + last) / 2) : last;
      if ((flags & BL_RUN) && runMin >= 30 && radioMin * 5 <= runMin) {
        T = runNow();
        flags |= BL_T;
        learnShape(true);
      }
      flags &= ~BL_RUN;
      tidy();
    }
    fromRest(mv);
    last = mv;
  }

  /* Every minute of use. charging: the voltage keeps climbing.
     Returns true when something worth keeping changed at once.     */
  uint8_t calm;                      // minutes since charging stopped, 0 = no charging seen
  bool minute(uint16_t mv, bool radio, bool charging) {
    last = mv;
    if (charging) {
      bool had = flags & BL_RUN;
      flags &= ~BL_RUN;
      calm = 1;
      return had;
    }
    if (calm) {                      // just off the charger: the voltage settles for a while
      if (++calm <= 6) return false;
      calm = 0;
      fromRest(mv);
      return true;
    }
    if (!(flags & BL_RUN)) return false;
    if (runMin < 60000) { runMin++; if (radio) radioMin++; }
    bool passed = false;
    for (uint8_t j = 0; j < BL_LEVELS; j++)
      if (cross[j] == BL_NONE && mv < level(j)) { cross[j] = runNow(); passed = true; }
    if (passed && (flags & BL_T)) learnShape(false);
    return passed;
  }

  void stop() { flags |= BL_SLEPT; }   // went to sleep: not empty
};
