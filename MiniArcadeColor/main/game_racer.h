// MiniArcade Color: Racer. Part of console.cpp, which includes it.
// An endless road seen from above: three lanes between kerbs, grass and
// trees, gently winding. LEFT/RIGHT steer. The other cars come in rows;
// a row blocks at most two lanes, and the distance to the row before is
// chosen from the speed so that a free lane can always be reached in time
// - the road never closes up. A little faster every few hundred metres.

#define RCR_Q     16                   // positions in 1/16 px
#define RCR_TICK  15                   // ms per step
#define RCR_CARY  188                  // player car top
#define RCR_CW    18                   // car size
#define RCR_CH    24
#define RCR_OPP   4
#define RCR_HALF  65                   // half road width
#define RCR_LANE  42                   // lane centres at -42, 0, +42 from the middle
#define RCR_STEP  48                   // sideways per step, 1/16 px (200 px/s)
#define RCR_MOVE  12                   // cars come closer by speed x this per step (1/16 px)
#define RCR_VMAX  7                    // speed 3 = 150 px/s .. 7 = 350 px/s
#define RCR_HIDE  (TOP_H - RCR_CH - 6) // new rows start above this

struct RcrState {
  uint8_t  spd;
  uint16_t curve;
  uint32_t dist;                       // score = dist / 40
  int16_t  opx[RCR_OPP];               // left edge relative to the road middle, px
  int32_t  opy[RCR_OPP];               // top on screen, 1/16 px
  uint8_t  opc[RCR_OPP];               // colour
  int32_t  topY, prevY;                // newest row of cars and the one below it
  uint8_t  topMask, prevMask;          // lanes they block (bit 0 = left lane)
};
static RcrState rcr;

static const int8_t RCR_SIN[65] = {    // a quarter of a sine, 0..127
  0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46, 49, 51, 54, 57, 60, 63, 65, 68, 71, 73, 76, 78,
  81, 83, 85, 88, 90, 92, 94, 96, 98, 100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116, 117, 118, 120,
  121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127, 127 };
static int16_t rcrSin(uint8_t a) {
  uint8_t i = a & 63;
  switch (a >> 6) {
  case 0:  return RCR_SIN[i];
  case 1:  return RCR_SIN[64 - i];
  case 2:  return -RCR_SIN[i];
  default: return -RCR_SIN[64 - i];
  }
}

// road centre on screen row y; the bends drift slowly towards the player
static int16_t rcrMid(int16_t y) {
  return (int16_t)(SCR_W / 2 + rcrSin((uint8_t)((rcr.curve >> 1) - y * 2 / 5)) * 45 / 127);
}

// worst case number of lanes to cross from a free lane of one row to the next
static uint8_t rcrNeed(uint8_t from, uint8_t to) {
  uint8_t worst = 0;
  for (int8_t a = 0; a < 3; a++) {
    if (from >> a & 1) continue;
    uint8_t best = 3;
    for (int8_t b = 0; b < 3; b++)
      if (!(to >> b & 1)) { uint8_t d = (uint8_t)abs(a - b); if (d < best) best = d; }
    if (best > worst) worst = best;
  }
  return worst;
}

// rows must be this far apart (car tops, 1/16 px) for the player to switch lanes
static int32_t rcrGap(uint8_t lanes, uint8_t spd) {
  int32_t steps = (lanes * RCR_LANE * RCR_Q + RCR_STEP - 1) / RCR_STEP + 6;   // + reaction and bends
  return 2 * RCR_CH * RCR_Q + steps * spd * RCR_MOVE;                         // two car lengths
}

static void rcrPlace(uint8_t i, uint8_t lane) {
  rcr.opx[i] = (int16_t)((lane - 1) * RCR_LANE - RCR_CW / 2 + random(-2, 3));
  rcr.opc[i] = (uint8_t)random(5);
}

// moves car i above the screen, in a row that keeps the road passable
static void rcrSpawn(uint8_t i) {
  // join the newest row as its second car - only while it is still hidden
  if (rcr.topMask && rcr.topY < RCR_HIDE * RCR_Q && __builtin_popcount(rcr.topMask) == 1 &&
      random(100) < 20 + rcr.spd * 4) {
    uint8_t ok[3], n = 0;
    for (uint8_t l = 0; l < 3; l++) {
      if (rcr.topMask >> l & 1) continue;
      uint8_t m = (uint8_t)(rcr.topMask | (1 << l));
      if (rcr.prevY - rcr.topY >= rcrGap(rcrNeed(rcr.prevMask, m), rcr.spd)) ok[n++] = l;
    }
    if (n) {
      uint8_t l = ok[random(n)];
      rcr.topMask |= (uint8_t)(1 << l);
      rcr.opy[i] = rcr.topY;
      rcrPlace(i, l);
      return;
    }
  }
  // otherwise a new row with one car
  uint8_t l = (uint8_t)random(3), m = (uint8_t)(1 << l);
  int32_t y = rcr.topY - rcrGap(rcrNeed(rcr.topMask, m), rcr.spd) - random(0, 90) * RCR_Q;
  if (y > RCR_HIDE * RCR_Q) y = RCR_HIDE * RCR_Q;               // always appear from above
  rcr.prevY = rcr.topY;  rcr.prevMask = rcr.topMask;
  rcr.topY  = y;         rcr.topMask  = m;
  rcr.opy[i] = y;
  rcrPlace(i, l);
}

static void rcrStart() {
  rcr.spd = 3; rcr.curve = 0; rcr.dist = 0;
  rcr.topY = rcr.prevY = RCR_CARY * RCR_Q;                      // an empty row where the player is
  rcr.topMask = rcr.prevMask = 0;
  for (uint8_t i = 0; i < RCR_OPP; i++) rcrSpawn(i);
}

// one 15 ms step of the road and the other cars; true when it got faster
static bool rcrAdvance() {
  bool faster = false;
  rcr.curve++;
  rcr.dist += rcr.spd;
  if (rcr.spd < RCR_VMAX && rcr.dist / 40 > (uint32_t)rcr.spd * 40) { rcr.spd++; faster = true; }
  int32_t v = rcr.spd * RCR_MOVE;
  rcr.topY += v;
  rcr.prevY += v;
  for (uint8_t i = 0; i < RCR_OPP; i++) {
    rcr.opy[i] += v;
    if (rcr.opy[i] > SCR_H * RCR_Q) rcrSpawn(i);
  }
  return faster;
}

// the player car (left edge in 1/16 px) is off the road or touches another car
static bool rcrCrash(int32_t carq) {
  int16_t x = (int16_t)(carq / RCR_Q), mid = rcrMid(RCR_CARY), c = x + RCR_CW / 2;
  if (c < mid - RCR_HALF || c > mid + RCR_HALF) return true;
  for (uint8_t i = 0; i < RCR_OPP; i++) {
    int16_t oy = (int16_t)(rcr.opy[i] / RCR_Q);
    if (oy + RCR_CH - 2 <= RCR_CARY || oy >= RCR_CARY + RCR_CH - 2) continue;
    int16_t ox = rcrMid(oy) + rcr.opx[i];
    if (ox + RCR_CW - 2 > x && ox < x + RCR_CW - 2) return true;
  }
  return false;
}

// ---------------- drawing ----------------
static const char *const RCR_CAR[2][12] = {
  { ".ybbbbby.", ".bbbbbbb.", "kbcccccbk", "kbcccccbk", ".bbbbbbb.", ".blbbblb.",
    ".blbbblb.", ".bbbbbbb.", "kbcccccbk", "kbbbbbbbk", ".bbbbbbb.", ".rbbbbbr." },
  { ".ybbwbby.", ".bbbwbbb.", "kbcccccbk", "kbcccccbk", ".bbbwbbb.", ".blbwblb.",
    ".blbwblb.", ".bbbwbbb.", "kbcccccbk", "kbbbwbbbk", ".bbbwbbb.", ".rbbwbbr." } };
static const char RCR_KEYS[] = "bylckrw";
static const col RCR_BODY[5] = { C::BLUE, C::YELLOW, C::GREEN, C::PURPLE, C::ORANGE };

static void rcrCar(int16_t x, int16_t y, col body, bool player) {
  col pal[7] = { body, rgb(255, 255, 180), colLight(body), rgb(40, 60, 90), C::BLACK, C::RED, C::WHITE };
  gfx.fill(x + 2, y + 3, RCR_CW, RCR_CH, rgb(30, 60, 30));                     // shadow
  gfx.sprite(x, y, RCR_CAR[player], 12, RCR_KEYS, pal, 2);
}

static void rcrTree(int16_t x, int16_t y, uint32_t h) {
  if ((h >> 5) % 3 == 0) {                                                       // a bush
    gfx.disc(x, y, 7, C::DKGRN); gfx.disc(x + 7, y + 2, 6, C::DKGRN); gfx.disc(x + 2, y - 2, 4, C::GREEN);
    return;
  }
  gfx.disc(x + 3, y + 4, 13, rgb(20, 70, 20));                                   // shadow
  gfx.disc(x, y, 13, rgb(30, 130, 40));
  gfx.disc(x - 3, y - 3, 8, rgb(70, 170, 60));
  gfx.disc(x - 5, y - 5, 3, rgb(140, 210, 100));
}

void racerRun() {
  bool again = true;
  while (again) {
    again = false;
    uint16_t score = 0;
    rcrStart();
    int32_t  carx = (rcrMid(RCR_CARY) - RCR_CW / 2) * RCR_Q;
    AxisStep steer;                                              // tilting: steer as far as tilted
    uint32_t scroll = 0, next = gameMillis(), flash = 0;      // scroll: the ground, 1/16 px
    bool     crashed = false;
    btnClear();

    while (poll()) {
      uint32_t now = gameMillis();
      for (uint8_t k = 0; k < 3 && !crashed && (int32_t)(now - next) >= 0; k++) {   // at most 45 ms per frame
        next += RCR_TICK;
        carx += steer(keyAxis(B_LEFT, B_RIGHT), RCR_STEP);
        if (carx < 4 * RCR_Q) carx = 4 * RCR_Q;
        if (carx > (SCR_W - RCR_CW - 4) * RCR_Q) carx = (SCR_W - RCR_CW - 4) * RCR_Q;
        if (rcrAdvance()) { sfx(1000, 60); flash = now + 1200; }
        scroll += (uint32_t)rcr.spd * RCR_MOVE * 3 / 2;          // the ground passes quicker than the cars
        score = rcr.dist / 40 > 65535 ? 65535 : (uint16_t)(rcr.dist / 40);
        if (rcrCrash(carx)) { crashed = true; sfx(150, 500); }
      }
      if ((int32_t)(now - next) > 0) next = now;

      // ---------------- draw ----------------
      statusBar("RACER", score, curHigh, C::RED);
      int32_t sp = (int32_t)(scroll / RCR_Q);
      for (int16_t y = TOP_H; y < SCR_H; y++) {                  // grass, kerbs, road, lane marks
        int32_t w = sp - y;                                      // ground position of this row
        int16_t mid = rcrMid(y);
        gfx.hline(0, y, SCR_W, (w / 24) & 1 ? rgb(60, 160, 50) : rgb(80, 180, 60));
        gfx.hline(mid - RCR_HALF - 6, y, 2 * RCR_HALF + 12, (w / 12) & 1 ? C::RED : C::WHITE);
        gfx.hline(mid - RCR_HALF, y, 2 * RCR_HALF, (w / 48) & 1 ? rgb(90, 90, 95) : rgb(100, 100, 105));
        gfx.pixel(mid - RCR_HALF + 2, y, C::LTGREY);
        gfx.pixel(mid + RCR_HALF - 3, y, C::LTGREY);
        if (((w % 40) + 40) % 40 < 20) {
          gfx.hline(mid - RCR_LANE / 2 - 1, y, 3, C::WHITE);
          gfx.hline(mid + RCR_LANE / 2 - 1, y, 3, C::WHITE);
        }
      }
      for (int32_t s = (sp - SCR_H - 40) / 56; s <= (sp - TOP_H + 40) / 56; s++) {   // trees by the road
        if (s < 0) continue;
        int16_t y = (int16_t)(sp - s * 56);
        uint32_t h = (uint32_t)(s + 7) * 2654435761u;
        int16_t mid = rcrMid(y);
        if (h & 1) rcrTree(mid - RCR_HALF - 26 - (int16_t)((h >> 8) % 40), y, h);
        if (h & 2) rcrTree(mid + RCR_HALF + 26 + (int16_t)((h >> 12) % 40), y, h >> 3);
      }
      for (uint8_t i = 0; i < RCR_OPP; i++) {
        int16_t oy = (int16_t)(rcr.opy[i] / RCR_Q);
        if (oy < TOP_H - RCR_CH || oy > SCR_H) continue;
        rcrCar(rcrMid(oy) + rcr.opx[i], oy, RCR_BODY[rcr.opc[i]], false);
      }
      int16_t px = (int16_t)(carx / RCR_Q);
      rcrCar(px, RCR_CARY, C::RED, true);
      if (crashed) {
        gfx.disc(px + 9, RCR_CARY + 10, 16, C::ORANGE);
        gfx.disc(px + 6, RCR_CARY + 8, 10, C::YELLOW);
        gfx.disc(px + 12, RCR_CARY + 14, 6, C::WHITE);
      }
      char b[16];
      snprintf(b, sizeof(b), "%u km/h", (unsigned)(rcr.spd * 30));
      gfx.rbox(4, SCR_H - 18, 64, 15, 4, C::BLACK);
      gfx.text(9, SCR_H - 14, b, flash > now ? C::YELLOW : C::WHITE, F_SMALL);
      gfx.send();
      if (crashed) { delay(800); break; }
    }
    if (!crashed) return;                                        // left from the pause menu
    again = gameOver(score);
  }
}

static const char *const IC_RACER[12] = {
  "gw.ssssss.wg", "gr.sssyss.rg", "gw.ssswss.wg", "gr.ssssss.rg", "gw.srrrrs.wg", "gr.rcccrs.rg",
  "gw.srrrrs.wg", "gr.srwwrs.wg", "gw.rrrrrr.wg", "gr.srrrrs.rg", "gw.ssswss.wg", "gr.ssssss.rg" };
