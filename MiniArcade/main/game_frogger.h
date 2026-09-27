// MiniArcade: Frogger. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  FROGGER
// =========================================================
/* Every lane gets its own random mix of object sizes, gaps and speed, and
   the whole river and road are rolled again after each crossing - a little
   faster every time. Neighbouring lanes always move in opposite
   directions, so a log to jump on comes by sooner or later.            */
#define FR_ROWH  6            // one lane is 6 px high
#define FR_ROWS  8            // 48 px viewport = 8 lanes
#define FR_LANES 6            // lanes 1..6 are traffic and water
#define FR_MAXO  5            // objects per lane
#define FR_Q     8            // positions and speeds in 1/8 px
#define FR_RING  (SCR_W * FR_Q)

struct FrLane {
  uint8_t n;
  int16_t x[FR_MAXO];         // left edge, 0..FR_RING-1
  uint8_t w[FR_MAXO];         // width in px
  int8_t  spd;                // 1/8 px per 40 ms step
};
static FrLane frL[FR_LANES];

static bool frWater(uint8_t l) { return l < 3; }

/* n gaps between min and max that add up to exactly free */
static bool frGaps(uint8_t *g, uint8_t n, int16_t free, uint8_t lo, uint8_t hi) {
  if (free < n * lo || free > n * hi) return false;
  int16_t extra = free - n * lo;
  for (uint8_t k = 0; k < n; k++) g[k] = lo;
  while (extra > 0) {                             // hand out the rest in random bits
    uint8_t k = random(n);
    int16_t room = hi - g[k];
    if (room <= 0) continue;
    int16_t add = 1 + random(room < extra ? room : extra);
    g[k] += add;
    extra -= add;
  }
  return true;
}

static void frRoll(uint8_t l, uint8_t diff, uint8_t level, int8_t dir) {
  FrLane &L = frL[l];
  bool water = frWater(l);
  uint8_t wlo, whi, glo, ghi;
  if (water) {                                    // logs: shorter and further apart when harder
    wlo = (diff == 0) ? 26 : (diff == 1 ? 18 : 14);
    whi = wlo + 14;
    glo = 6;
    ghi = (diff == 0) ? 22 : (diff == 1 ? 28 : 34);
  } else {                                        // cars, now and then a truck
    wlo = (diff == 0) ? 8 : (diff == 1 ? 10 : 12);
    whi = wlo + 6;
    glo = (diff == 0) ? 22 : 16;
    ghi = 60;
  }
  uint8_t g[FR_MAXO];
  for (uint8_t tries = 0; tries < 40; tries++) {
    uint8_t n = 2 + random(FR_MAXO - 1);          // 2..5 objects
    int16_t used = 0;
    for (uint8_t k = 0; k < n; k++) {
      L.w[k] = random(wlo, whi + 1);
      if (!water && diff && random(100) < 20) L.w[k] += 10;   // truck
      used += L.w[k];
    }
    if (frGaps(g, n, SCR_W - used, glo, ghi)) { L.n = n; break; }
    L.n = 0;
  }
  if (!L.n) {                                     // cannot happen with the ranges above
    L.n = 2; L.w[0] = L.w[1] = wlo; g[0] = g[1] = (SCR_W - 2 * wlo) / 2;
  }
  int16_t x = random(SCR_W);
  for (uint8_t k = 0; k < L.n; k++) {
    L.x[k] = (x % SCR_W) * FR_Q;
    x += L.w[k] + g[k];
  }
  uint8_t lo = (diff == 0) ? 8 : (diff == 1 ? 11 : 15);
  uint8_t hi = (diff == 0) ? 14 : (diff == 1 ? 20 : 28);
  int16_t v = random(lo, hi + 1);
  v = v * (100 + (level < 10 ? level : 10) * 6) / 100;       // +6 % per crossing
  L.spd = (int8_t)(dir * (v > 60 ? 60 : v));
}

static void frRollAll(uint8_t diff, uint8_t level) {
  int8_t dir = random(2) ? 1 : -1;
  for (uint8_t l = 0; l < FR_LANES; l++) { frRoll(l, diff, level, dir); dir = -dir; }
}

// is the frog (px x .. x+4) on something in lane l?
static bool frTouch(uint8_t l, int16_t fx) {
  const FrLane &L = frL[l];
  for (uint8_t k = 0; k < L.n; k++) {
    int16_t ox = L.x[k] / FR_Q, ow = L.w[k];
    if (fx + 4 > ox && fx < ox + ow) return true;
    if (ox + ow > SCR_W && fx < ox + ow - SCR_W) return true;   // wrapped around
  }
  return false;
}

void froggerRun() {
  static const char *const lvl[3] = { "easy", "normal", "hard" };
  uint8_t diff = chooseMode("FROGGER", lvl, 3);
  if (diff == 255) return;

  bool again = true;
  while (again) {
    again = false;
    int16_t  fq = 60 * FR_Q;                          // frog x in 1/8 px
    int8_t   fy = FR_ROWS - 1;
    uint8_t  lives = 3, level = 0;
    uint16_t score = 0;
    uint32_t next = 0, step = 0;
    frRollAll(diff, level);
    btnClear();

    while (poll()) {
      uint32_t now = gameMillis();
      int16_t fx = fq / FR_Q;
      if (now >= step) {                              // hopping
        step = now + 90;
        if (btn(B_LEFT)  && fx > 1)            { fq -= 6 * FR_Q; sfx(600, 20); }
        if (btn(B_RIGHT) && fx < SCR_W - 7)    { fq += 6 * FR_Q; sfx(600, 20); }
        if (btn(B_UP)    && fy > 0)            { fy--;    sfx(800, 20); }
        if (btn(B_DOWN)  && fy < FR_ROWS - 1)  { fy++;    sfx(500, 20); }
      }

      if (now >= next) {
        next = now + 40;
        for (uint8_t l = 0; l < FR_LANES; l++)
          for (uint8_t k = 0; k < frL[l].n; k++)
            frL[l].x[k] = ((frL[l].x[k] + frL[l].spd) % FR_RING + FR_RING) % FR_RING;

        if (fy == 0) {                                // reached the far bank
          score += 50;
          sfx(1400, 150);
          if (level < 255) level++;
          frRollAll(diff, level);                     // a new river and road
          fy = FR_ROWS - 1;
          fq = 60 * FR_Q;
        } else if (fy >= 1 && fy <= FR_LANES) {
          uint8_t l = fy - 1;
          bool onIt = frTouch(l, fq / FR_Q);
          bool dead = frWater(l) ? !onIt : onIt;
          if (frWater(l) && onIt) fq += frL[l].spd;  // ride the log
          fx = fq / FR_Q;
          if (fq < 0 || fx > SCR_W - 5) dead = true;
          if (dead) {
            sfx(150, 200);
            if (--lives == 0) { again = gameOver(score); break; }
            fy = FR_ROWS - 1; fq = 60 * FR_Q;
          }
        }
      }

      statusBar("FROGGER", score, curHigh);
      for (uint8_t l = 0; l < FR_LANES; l++) {
        int16_t ly = TOP_H + (l + 1) * FR_ROWH;
        for (uint8_t k = 0; k < frL[l].n; k++) {
          int16_t ox = frL[l].x[k] / FR_Q;
          uint8_t w = frL[l].w[k];
          if (frWater(l)) {
            oled.drawFrame(ox, ly, w, FR_ROWH - 1);
            if (ox + w > SCR_W) oled.drawFrame(ox - SCR_W, ly, w, FR_ROWH - 1);
          } else {
            oled.drawBox(ox, ly, w, FR_ROWH - 1);
            if (ox + w > SCR_W) oled.drawBox(ox - SCR_W, ly, w, FR_ROWH - 1);
          }
        }
      }
      fx = fq / FR_Q;
      oled.drawHLine(0, TOP_H + FR_ROWH - 1, SCR_W);
      oled.drawHLine(0, TOP_H + (FR_LANES + 1) * FR_ROWH - 1, SCR_W);
      oled.setDrawColor(0);
      oled.drawBox(fx - 1, TOP_H + fy * FR_ROWH, 7, FR_ROWH);
      oled.setDrawColor(1);
      oled.drawBox(fx, TOP_H + fy * FR_ROWH + 1, 5, 4);
      oled.drawPixel(fx - 1, TOP_H + fy * FR_ROWH);
      oled.drawPixel(fx + 5, TOP_H + fy * FR_ROWH);
      for (uint8_t i = 1; i < lives; i++) oled.drawBox(SCR_W - i * 5, 18, 3, 3);
      oled.sendBuffer();
    }
  }
}
