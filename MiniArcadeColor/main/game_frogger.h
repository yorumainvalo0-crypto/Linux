// MiniArcade Color: Frogger. Part of console.cpp, which includes it.
// Arrows hop the frog over a three-lane road and a river with three lanes
// of logs to the far bank (50 points). Every lane gets its own random mix
// of object sizes, gaps and speed, and the whole river and road are
// rolled again after each crossing - a little faster every time.
// Neighbouring lanes always move in opposite directions, so a log to jump
// on comes by sooner or later. Three lives; easy, normal or hard.

#define FRG_RH    26                   // one lane is 26 px high
#define FRG_ROWS  8                    // bank, 3 x river, 3 x road, bank
#define FRG_LANES 6                    // lanes 1..6 are water and traffic
#define FRG_MAXO  5                    // objects per lane
#define FRG_Q     16                   // positions and speeds in 1/16 px
#define FRG_RING  (SCR_W * FRG_Q)
#define FRG_Y0    (SCR_H - FRG_ROWS * FRG_RH)
#define FRG_TICK  20                   // ms per step
#define FRG_FW    12                   // the frog's width for the checks
#define FRG_HOP   15                   // sideways hop
#define FRG_X0    154                  // start position

struct FrgLane {
  uint8_t n;
  int16_t x[FRG_MAXO];                 // left edge, 0..FRG_RING-1
  uint8_t w[FRG_MAXO];                 // width in px
  int16_t spd;                         // 1/16 px per 20 ms step
};
static FrgLane frgL[FRG_LANES];

static bool frgWater(uint8_t l) { return l < 3; }

// n gaps between lo and hi that add up to exactly free
static bool frgGaps(uint8_t *g, uint8_t n, int16_t free, uint8_t lo, uint8_t hi) {
  if (free < n * lo || free > n * hi) return false;
  int16_t extra = free - n * lo;
  for (uint8_t k = 0; k < n; k++) g[k] = lo;
  while (extra > 0) {                                  // hand out the rest in random bits
    uint8_t k = (uint8_t)random(n);
    int16_t room = hi - g[k];
    if (room <= 0) continue;
    int16_t add = (int16_t)(1 + random(room < extra ? room : extra));
    g[k] += (uint8_t)add;
    extra -= add;
  }
  return true;
}

static void frgRoll(uint8_t l, uint8_t diff, uint8_t level, int8_t dir) {
  FrgLane &L = frgL[l];
  bool water = frgWater(l);
  uint8_t wlo, whi, glo, ghi;
  if (water) {                                         // logs: shorter and further apart when harder
    wlo = diff == 0 ? 65 : (diff == 1 ? 45 : 35);
    whi = wlo + 35;
    glo = 15;
    ghi = diff == 0 ? 55 : (diff == 1 ? 70 : 85);
  } else {                                             // cars, now and then a truck
    wlo = diff == 0 ? 20 : (diff == 1 ? 25 : 30);
    whi = wlo + 15;
    glo = diff == 0 ? 55 : 40;
    ghi = 150;
  }
  uint8_t g[FRG_MAXO];
  L.n = 0;
  for (uint8_t tries = 0; tries < 40 && !L.n; tries++) {
    uint8_t n = (uint8_t)(2 + random(FRG_MAXO - 1));  // 2..5 objects
    int16_t used = 0;
    for (uint8_t k = 0; k < n; k++) {
      L.w[k] = (uint8_t)random(wlo, whi + 1);
      if (!water && diff && random(100) < 20) L.w[k] += 25;   // truck
      used += L.w[k];
    }
    if (frgGaps(g, n, SCR_W - used, glo, ghi)) L.n = n;
  }
  if (!L.n) {                                          // cannot happen with the ranges above
    L.n = 2; L.w[0] = L.w[1] = wlo; g[0] = g[1] = (uint8_t)((SCR_W - 2 * wlo) / 2);
  }
  int16_t x = (int16_t)random(SCR_W);
  for (uint8_t k = 0; k < L.n; k++) {
    L.x[k] = (int16_t)((x % SCR_W) * FRG_Q);
    x = (int16_t)(x % SCR_W + L.w[k] + g[k]);
  }
  uint8_t lo = diff == 0 ? 8 : (diff == 1 ? 11 : 15);  // OLED units: 1/8 px per 40 ms
  uint8_t hi = diff == 0 ? 14 : (diff == 1 ? 20 : 28);
  int16_t v = (int16_t)random(lo, hi + 1);
  v = (int16_t)(v * (100 + (level < 10 ? level : 10) * 6) / 100);   // +6 % per crossing
  if (v > 40) v = 40;
  L.spd = (int16_t)(dir * (v * 5 / 2));                // 2.5 x as wide: 62 .. 310 px/s
}

static void frgRollAll(uint8_t diff, uint8_t level) {
  int8_t dir = random(2) ? 1 : -1;
  for (uint8_t l = 0; l < FRG_LANES; l++) { frgRoll(l, diff, level, dir); dir = (int8_t)-dir; }
}

// is the frog (x .. x + FRG_FW) on something in lane l?
static bool frgTouch(uint8_t l, int16_t fx) {
  const FrgLane &L = frgL[l];
  for (uint8_t k = 0; k < L.n; k++) {
    int16_t ox = L.x[k] / FRG_Q, ow = L.w[k];
    if (fx + FRG_FW > ox && fx < ox + ow) return true;
    if (ox + ow > SCR_W && fx < ox + ow - SCR_W) return true;   // wrapped around
  }
  return false;
}

// ---------------- drawing ----------------
static const char *const FRG_FROG[2][8] = {
  { ".ww..ww.", ".wk..kw.", "g.gggg.g", "gggllggg", ".gglllg.", ".gggggg.", "gg.gg.gg", "g......g" },
  { ".ww..ww.", ".wk..kw.", "..gggg..", ".ggllgg.", ".gglllg.", "g.gggg.g", "g......g", "g......g" } };
static const char FRG_KEYS[] = "glwk";
static const col FRG_CARS[5] = { C::RED, C::YELLOW, C::CYAN, C::PINK, C::ORANGE };

static void frgLog(int16_t x, int16_t y, int16_t w) {
  col bark = rgb(140, 85, 40), dk = rgb(90, 50, 20), lt = rgb(190, 130, 70);
  gfx.rbox(x, y + 4, w, FRG_RH - 8, 8, bark);
  gfx.hline(x + 6, y + 6, w - 12, lt);
  for (int16_t i = 10; i < w - 10; i += 14) gfx.hline(x + i, y + 10 + (i / 14) % 2 * 6, 7, dk);
  gfx.disc(x + w - 8, y + FRG_RH / 2, 6, rgb(220, 180, 110));
  gfx.circle(x + w - 8, y + FRG_RH / 2, 3, dk);
}

static void frgCar(int16_t x, int16_t y, int16_t w, bool right, col c) {
  int16_t y0 = y + 4, h = FRG_RH - 8;
  for (int16_t i = 4; i < w - 4; i += w - 14) {                  // wheels
    gfx.fill(x + i, y0 - 2, 7, 3, C::BLACK);
    gfx.fill(x + i, y0 + h - 1, 7, 3, C::BLACK);
  }
  if (w >= 45) {                                                  // a truck: cab and box
    int16_t cab = 14, bx = right ? x : x + cab + 1;
    gfx.rbox(bx, y0, w - cab - 1, h, 2, C::LTGREY);
    gfx.hline(bx + 2, y0 + 3, w - cab - 5, C::WHITE);
    gfx.hline(bx + 2, y0 + h - 4, w - cab - 5, C::GREY);
    int16_t cx = right ? x + w - cab : x;
    gfx.rbox(cx, y0, cab, h, 4, c);
    gfx.fill(right ? cx + cab - 6 : cx + 2, y0 + 3, 4, h - 6, rgb(40, 60, 90));
  } else {
    gfx.rbox(x, y0, w, h, 5, c);
    gfx.rbox(x + w / 2 - 7, y0 + 3, 14, h - 6, 3, colDark(c));    // roof
    gfx.fill(right ? x + w / 2 + 7 : x + w / 2 - 10, y0 + 3, 3, h - 6, rgb(40, 60, 90));
  }
  int16_t fx = right ? x + w - 2 : x;                             // headlights
  gfx.fill(fx, y0 + 2, 2, 3, C::YELLOW);
  gfx.fill(fx, y0 + h - 5, 2, 3, C::YELLOW);
}

void froggerRun() {
  static const char *const lvl[3] = { "easy", "normal", "hard" };
  uint8_t diff = chooseMode("FROGGER", lvl, 3, C::LIME);
  if (diff == 255) return;

  bool again = true;
  while (again) {
    again = false;
    int16_t  fq = FRG_X0 * FRG_Q;                              // frog x in 1/16 px
    int8_t   fy = FRG_ROWS - 1;
    uint8_t  lives = 3, level = 0;
    uint16_t score = 0;
    uint32_t next = gameMillis(), hopReady = 0, hopAt = 0, deadUntil = 0;
    int16_t  hopDx = 0, hopDy = 0;                             // where the last hop came from (drawing)
    int16_t  deadX = 0;
    int8_t   deadY = 0;
    bool     over = false;
    frgRollAll(diff, level);
    btnClear();

    while (poll()) {
      uint32_t now = gameMillis();
      int16_t fx = fq / FRG_Q;
      if (deadUntil) { for (uint8_t k = 0; k < 4; k++) btn(k); }   // no hops while it is lost
      else if ((int32_t)(now - hopReady) >= 0) {               // hopping
        int16_t ox = fq, oy = fy;
        if (btn(B_LEFT)  && fx - FRG_HOP >= 0)                { fq -= FRG_HOP * FRG_Q; sfx(600, 20); }
        if (btn(B_RIGHT) && fx + FRG_HOP <= SCR_W - FRG_FW)   { fq += FRG_HOP * FRG_Q; sfx(600, 20); }
        if (btn(B_UP)    && fy > 0)                           { fy--; sfx(800, 20); }
        if (btn(B_DOWN)  && fy < FRG_ROWS - 1)                { fy++; sfx(500, 20); }
        if (fq != ox || fy != oy) {
          hopReady = now + 90; hopAt = now;
          hopDx = (int16_t)((ox - fq) / FRG_Q); hopDy = (int16_t)((oy - fy) * FRG_RH);
        }
      }

      for (uint8_t k = 0; k < 2 && (int32_t)(now - next) >= 0; k++) {   // at most 40 ms per frame
        next += FRG_TICK;
        for (uint8_t l = 0; l < FRG_LANES; l++)
          for (uint8_t i = 0; i < frgL[l].n; i++)
            frgL[l].x[i] = (int16_t)(((frgL[l].x[i] + frgL[l].spd) % FRG_RING + FRG_RING) % FRG_RING);
        if (deadUntil) continue;
        if (fy == 0) {                                         // reached the far bank
          score += 50;
          sfx(1400, 150);
          if (level < 255) level++;
          frgRollAll(diff, level);                             // a new river and road
          fy = FRG_ROWS - 1;
          fq = FRG_X0 * FRG_Q;
          hopAt = 0;
        } else if (fy >= 1 && fy <= FRG_LANES) {
          uint8_t l = (uint8_t)(fy - 1);
          bool onIt = frgTouch(l, fq / FRG_Q);
          bool dead = frgWater(l) ? !onIt : onIt;
          if (frgWater(l) && onIt) fq += frgL[l].spd;         // ride the log
          if (fq < 0 || fq / FRG_Q > SCR_W - FRG_FW) dead = true;
          if (dead) {
            sfx(150, 300);
            deadUntil = now + 700; deadX = fq / FRG_Q; deadY = fy;
          }
        }
      }
      if ((int32_t)(now - next) > 0) next = now;
      if (deadUntil && (int32_t)(now - deadUntil) >= 0) {
        deadUntil = 0;
        if (--lives == 0) over = true;
        fy = FRG_ROWS - 1; fq = FRG_X0 * FRG_Q; hopAt = 0;
      }

      // ---------------- draw ----------------
      statusBar("FROGGER", score, curHigh, C::LIME);
      gfx.fill(0, TOP_H, SCR_W, FRG_Y0 - TOP_H, rgb(20, 80, 30));          // hedge
      for (int16_t x = 4; x < SCR_W; x += 11) gfx.disc(x, FRG_Y0 - 1, 4, rgb(30, 110, 40));
      // the far bank
      gfx.fill(0, FRG_Y0, SCR_W, FRG_RH, rgb(60, 150, 60));
      for (int16_t x = 7; x < SCR_W; x += 23) {
        gfx.disc(x, FRG_Y0 + 7 + (x * 7) % 12, 2, (x / 23) % 2 ? C::YELLOW : C::PINK);
        gfx.pixel(x, FRG_Y0 + 7 + (x * 7) % 12, C::WHITE);
      }
      // the river
      int16_t ry = FRG_Y0 + FRG_RH;
      gfx.gradient(0, ry, SCR_W, 3 * FRG_RH, rgb(30, 110, 220), rgb(20, 60, 160));
      for (uint8_t i = 0; i < 18; i++) {
        int16_t wx = (int16_t)((i * 53 + now / (30 + i % 3 * 10)) % (SCR_W + 20)) - 10;
        gfx.hline(wx, ry + 5 + (i * 29) % (3 * FRG_RH - 8), 8, rgb(120, 180, 255));
      }
      // the road
      int16_t rd = FRG_Y0 + 4 * FRG_RH;
      gfx.fill(0, rd, SCR_W, 3 * FRG_RH, rgb(55, 55, 60));
      gfx.fill(0, rd - 2, SCR_W, 4, C::LTGREY);
      for (uint8_t r = 1; r < 3; r++)
        for (int16_t x = 6; x < SCR_W; x += 32) gfx.fill(x, rd + r * FRG_RH - 1, 16, 2, C::WHITE);
      // the start bank
      int16_t sb = FRG_Y0 + 7 * FRG_RH;
      gfx.fill(0, sb, SCR_W, FRG_RH, rgb(120, 90, 150));
      gfx.fill(0, sb, SCR_W, 3, C::LTGREY);
      for (int16_t x = 0; x < SCR_W; x += 20) gfx.vline(x, sb + 3, FRG_RH - 3, rgb(100, 70, 130));
      // logs and cars
      for (uint8_t l = 0; l < FRG_LANES; l++) {
        int16_t ly = FRG_Y0 + (l + 1) * FRG_RH;
        for (uint8_t i = 0; i < frgL[l].n; i++) {
          int16_t ox = frgL[l].x[i] / FRG_Q, w = frgL[l].w[i];
          for (uint8_t rep = 0; rep < 2; rep++) {
            int16_t x = rep ? ox - SCR_W : ox;
            if (rep && ox + w <= SCR_W) break;
            if (frgWater(l)) frgLog(x, ly, w);
            else frgCar(x, ly, w, frgL[l].spd > 0, FRG_CARS[(l * 2 + i) % 5]);
          }
        }
      }
      // the frog, or what is left of it
      if (deadUntil) {
        int16_t dy = FRG_Y0 + deadY * FRG_RH + FRG_RH / 2, dx = deadX + FRG_FW / 2;
        uint8_t t = (uint8_t)((700 - (deadUntil - now)) / 60);
        if (frgWater((uint8_t)(deadY - 1))) { gfx.circle(dx, dy, 4 + t, C::WHITE); gfx.circle(dx, dy, 2 + t / 2, rgb(160, 210, 255)); }
        else { gfx.disc(dx, dy, 9, C::RED); gfx.disc(dx - 3, dy - 2, 4, C::ORANGE); gfx.disc(dx + 4, dy + 3, 3, C::YELLOW); }
      } else {
        int16_t x = fq / FRG_Q - 2, y = FRG_Y0 + fy * FRG_RH + 5;
        bool jumping = hopAt && now - hopAt < 90;
        if (jumping) {                                         // slide along the hop
          int32_t r = 90 - (int32_t)(now - hopAt);
          x += (int16_t)(hopDx * r / 90);
          y += (int16_t)(hopDy * r / 90);
        }
        col fp[4] = { rgb(80, 200, 60), C::YELLOW, C::WHITE, C::BLACK };
        gfx.sprite(x, y, FRG_FROG[jumping], 8, FRG_KEYS, fp, 2);
      }
      // lives and crossing
      for (uint8_t i = 1; i < lives; i++) {
        gfx.disc(SCR_W - 10 - (i - 1) * 14, sb + 14, 5, rgb(80, 200, 60));
        gfx.pixel(SCR_W - 12 - (i - 1) * 14, sb + 11, C::WHITE);
        gfx.pixel(SCR_W - 8 - (i - 1) * 14, sb + 11, C::WHITE);
      }
      char b[12];
      snprintf(b, sizeof(b), "LEVEL %u", (unsigned)(level + 1));
      gfx.text(6, sb + 10, b, C::WHITE, F_SMALL);
      gfx.send();
      if (over) { delay(300); break; }
    }
    if (!over) return;                                         // left from the pause menu
    again = gameOver(score);
  }
}

static const char *const IC_FROGGER[12] = {
  "gggggggggggg", "bbbbbbbbbbbb", "bnnnnbbbnnnb", "bbbbbbbbbbbb", "ssssssssssss", "srrrsssssyys",
  "ssssssssssss", "ssssswwsssss", "....gwwg....", "...gggggg...", "...gg..gg...", "pppppppppppp" };
