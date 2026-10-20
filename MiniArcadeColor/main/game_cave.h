// MiniArcade Color: Cave. Part of console.cpp, which includes it.
// Hold UP to climb, let go to sink. The cave scrolls from right to left;
// its middle wanders up and down and it gets narrower the further you
// fly, and now and then a rock hangs in the way. The score is the
// distance (one point per 10 columns). Rock walls shaded by depth, far
// stalactites and crystals at half speed, smoke behind the helicopter.

#define CV_TOP    (TOP_H + 1)
#define CV_BOT    (SCR_H - 1)
#define CV_CW     3                    // a column is 3 px wide ...
#define CV_TICK   25                   // ... and a new one comes every 25 ms: 120 px/s
#define CV_N      (SCR_W / CV_CW + 2)  // columns kept
#define CV_X      50                   // the copter's left edge
#define CV_CPW    20                   // its size
#define CV_CPH    10
#define CV_GAP0   90                   // opening at the start ...
#define CV_GAPMIN 45                   // ... narrows down to this
#define CV_ROCK   12                   // a rock: 12 px high, 4 columns wide

struct CvState {
  uint8_t top[CV_N], bot[CV_N];        // walls per column: last row of the roof, first of the floor
  uint8_t rock[CV_N];                  // 0 = none, else the rock's top row
  int16_t mid, drift;                  // middle of the opening, 1/16 px
};
static CvState *cv = NULL;             // only while the game runs

// a new column at i; calm = keeps its direction (the straight start)
static void cvColumn(uint8_t i, uint16_t dist, bool calm = false) {
  uint8_t gap = (uint8_t)(dist / 24 < CV_GAP0 - CV_GAPMIN ? CV_GAP0 - dist / 24 : CV_GAPMIN);
  if (!calm && random(12) == 0) cv->drift = (int16_t)random(9) - 4;   // a new direction now and then
  cv->mid += cv->drift * 12;
  int16_t lo = (CV_TOP + gap / 2 + 1) * 16, hi = (CV_BOT - gap / 2 - 1) * 16;
  if (cv->mid < lo) { cv->mid = lo; cv->drift = 2; }
  if (cv->mid > hi) { cv->mid = hi; cv->drift = -2; }
  int16_t m = cv->mid / 16;
  cv->top[i] = (uint8_t)(m - gap / 2);
  cv->bot[i] = (uint8_t)(m + gap / 2);
  cv->rock[i] = 0;
}

static uint8_t cvHash(uint32_t a, uint32_t b) {
  uint32_t h = a * 2654435761u ^ b * 2246822519u;
  h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12;
  return (uint8_t)h;
}

/* Rock, darker the deeper into the wall: zones by the distance from the
   open side, every other one a mix of its neighbours (dithered). Cracks
   and pebbles sit at fixed rows in 6 px steps, chosen by the column's
   own number, so they scroll with it and do not crawl.               */
static const int8_t CV_ZONE[5] = { 0, 9, 15, 27, 35 };       // zone starts, px from the edge
static const col    CV_ZC[5]   = { rgb(153, 85, 51), rgb(153, 85, 51), rgb(102, 42, 51), rgb(102, 42, 51), rgb(51, 0, 0) };
static const col    CV_ZMIX[5] = { 0, rgb(102, 42, 51), 0, rgb(51, 0, 0), 0 };

static void cvRock(int16_t x, uint16_t a, int16_t y0, int16_t y1, int16_t edge) {   // rows y0..y1-1, edge = the open side
  bool roof = edge >= y1;
  for (uint8_t z = 0; z < 5; z++) {
    int16_t d0 = CV_ZONE[z], d1 = z < 4 ? CV_ZONE[z + 1] : 240;
    int16_t ya = roof ? edge - d1 + 1 : edge + d0, yb = roof ? edge - d0 + 1 : edge + d1;
    if (ya < y0) ya = y0;
    if (yb > y1) yb = y1;
    if (ya >= yb) continue;
    gfx.fill(x, ya, CV_CW, yb - ya, CV_ZC[z]);
    if (CV_ZMIX[z]) fxDitherFill(x, ya, CV_CW, yb - ya, CV_ZMIX[z]);
  }
  for (int16_t y = (int16_t)(y0 - (y0 - CV_TOP) % 6 + 6); y < y1; y += 6) {
    uint8_t h = cvHash(a, (uint32_t)y);
    int16_t d = (int16_t)abs(edge - y);
    if ((h & 7) == 0) gfx.hline(x, y, CV_CW, d < 15 ? rgb(102, 42, 0) : rgb(51, 0, 0));        // a crack
    else if ((h & 15) == 1 && d < 20) gfx.pixel(x + (h >> 5) % 3, y, rgb(204, 127, 51));     // a pebble
  }
}

// the cave at scroll offset off (0..CV_CW-1 px), dist = columns flown so far
static void cvDraw(uint16_t dist, int16_t off) {
  gfx.fill(0, CV_TOP, SCR_W, CV_BOT - CV_TOP + 1, rgb(51, 0, 51));      // the dark, bluer further down
  fxDitherFill(0, CV_TOP + 90, SCR_W, 50, rgb(0, 0, 51));
  gfx.fill(0, CV_TOP + 140, SCR_W, CV_BOT - CV_TOP - 139, rgb(0, 0, 51));
  int32_t far = (int32_t)dist * CV_CW / 2 + off / 2;           // far rock, crystals: half speed
  for (uint8_t k = 0; k < 12; k++) {                           // stalactites and stalagmites far behind
    uint32_t h = (uint32_t)(k + 3) * 2654435761u;
    int16_t x = (int16_t)(((int32_t)((h >> 8) % 640) - far % 640 + 640) % 640) - 100;
    int16_t w = (int16_t)(10 + (h >> 4) % 14), len = (int16_t)(40 + (h >> 16) % 60);
    bool up = k & 1;
    int16_t y = up ? CV_BOT : CV_TOP;
    int16_t tip = up ? y - len : y + len;
    gfx.triangle(x - w, y, x + w, y, x, tip, rgb(102, 42, 102));
    gfx.triangle(x, y, x + w, y, x, tip, rgb(51, 0, 51));
    gfx.line(x - w / 2, y, x, tip, rgb(153, 85, 153));
  }
  for (uint8_t k = 0; k < 24; k++) {                           // glowing crystals
    uint32_t h = (uint32_t)(k + 5) * 2654435761u;
    int16_t x = (int16_t)(((int32_t)((h >> 8) % 640) - far % 640 + 640) % 640) - 160;
    int16_t y = (int16_t)(CV_TOP + 10 + (h >> 20) % (CV_BOT - CV_TOP - 20));
    col c = k % 3 ? rgb(102, 85, 204) : rgb(51, 170, 204);
    gfx.triangle(x, y - 5, x - 2, y, x + 2, y, colLight(c));
    gfx.triangle(x - 2, y, x + 2, y, x, y + 5, colDark(c));
    gfx.pixel(x, y - 2, C::WHITE);
  }
  for (uint8_t i = 0; i < CV_N; i++) {
    int16_t x = (int16_t)(i * CV_CW - off);
    uint16_t a = (uint16_t)(dist + i);                         // the column's own number: stable texture
    int16_t t = cv->top[i], b = cv->bot[i];
    cvRock(x, a, CV_TOP, t - 2, t);                            // roof, lit at its edge
    gfx.fill(x, t - 2, CV_CW, 2, rgb(204, 127, 51));
    gfx.hline(x, t, CV_CW, rgb(255, 212, 102));
    gfx.hline(x, t - 3, CV_CW, rgb(204, 127, 51));
    if ((cvHash(a, 7) & 7) == 0) gfx.vline(x + 1, t + 1, 1 + cvHash(a, 9) % 2, rgb(255, 212, 102));   // a drip
    gfx.hline(x, b, CV_CW, rgb(102, 204, 51));                 // moss on the floor
    gfx.hline(x, b + 1, CV_CW, rgb(51, 153, 51));
    gfx.fill(x, b + 2, CV_CW, 2, rgb(204, 127, 51));
    cvRock(x, a, b + 4, CV_BOT + 1, b);
    if (cv->rock[i]) {                                         // a boulder, slice by slice
      int16_t ry = cv->rock[i];
      bool l = i > 0 && cv->rock[i - 1] == ry, r = i + 1 < CV_N && cv->rock[i + 1] == ry;
      bool l2 = i > 1 && cv->rock[i - 2] == ry;
      int16_t cut = (l && r) ? (l2 ? 0 : 1) : 2;               // rounder at its ends
      gfx.fill(x, ry + cut, CV_CW, CV_ROCK - 2 * cut, rgb(102, 102, 127));
      gfx.fill(x, ry + cut, CV_CW, 3, rgb(153, 153, 170));
      gfx.hline(x, ry + cut, CV_CW, rgb(204, 212, 230));
      gfx.fill(x, ry + CV_ROCK - cut - 3, CV_CW, 3, rgb(51, 42, 102));
      if (cvHash(a, (uint32_t)ry) & 1) gfx.pixel(x + 1, ry + 6, rgb(153, 153, 170));
      if (!r) gfx.vline(x + CV_CW - 1, ry + cut, CV_ROCK - 2 * cut, rgb(51, 42, 102));
    }
  }
}

/* The helicopter, 26 x 11 with its rotors drawn on top: the body fills
   the 20 x 10 box the walls are checked against, the tail sticks out
   behind it (as it always did).                                       */
static const char *const CV_HELI[11] = {
  "..........................",
  "................kkk.......",
  "kk.............kkykk......",
  "kTk.........kkkyyyyykkk...",
  "kTTk......kkyYYyyyyykccck.",
  ".kTTkkkkkkyYyyyyyyyykccwck",
  "..kooooooooyyyyyyyyykcwcck",
  "...kkkkkkkkoyyyyyyyyykkkkk",
  "...........kkooooooooook..",
  "............kk.k...k.kk...",
  "..........kkkkkkkkkkkkkkkk" };
static const char CV_HKEYS[] = "kyYocwT";
static const col  CV_HPAL[7] = { rgb(51, 42, 0), rgb(255, 212, 0), rgb(255, 255, 153), rgb(204, 127, 0),
                                 C::CYAN, C::WHITE, C::RED };

static void cvCopter(int16_t y, uint32_t now) {
  int16_t x = CV_X - 6;
  gfx.sprite(x, y, CV_HELI, 11, CV_HKEYS, CV_HPAL, 1);
  static const uint8_t BL[4] = { 26, 16, 6, 16 };              // the main rotor turns: blade seen from the side
  uint8_t f = (uint8_t)((now / 35) % 4), w = BL[f];
  int16_t hub = x + 17;
  gfx.hline(hub - w / 2, y, w, f == 0 ? GREY4 : GREY3);
  gfx.hline(hub - 2, y, 4, GREY2);
  gfx.pixel(hub, y + 1, GREY2);
  int16_t tr = (int16_t)((now / 35) % 2);                      // tail rotor
  gfx.vline(x + 1, y + 1 + tr * 2, 4, GREY4);
  gfx.hline(x - 1 + tr * 2, y + 3, 4 - tr * 2, GREY3);
}

void caveRun() {
  cv = (CvState *)malloc(sizeof(CvState));
  if (!cv) return;
  bool again = true;
  while (again) {
    again = false;
    memset(cv, 0, sizeof(CvState));
    cv->mid = ((CV_TOP + CV_BOT) / 2) * 16;
    uint16_t dist = 0;
    for (uint8_t i = 0; i < CV_N; i++) cvColumn(i, 0, i < (CV_X + CV_CPW) / CV_CW + 12);   // straight around the copter
    int32_t y = ((CV_TOP + CV_BOT) / 2 - CV_CPH / 2) * 256, vy = 0;   // the copter's top, 1/256 px
    uint8_t rockIn = 60;                                        // columns until the next rock
    uint32_t now = gameMillis(), next = now, crashAt = 0, smokeAt = 0;
    bool started = false, crashed = false;
    fxReset();
    btnClear();
    while (poll()) {
      now = gameMillis();
      bool up = btnHeld(B_UP);
      if (!started && up) { started = true; next = now; }
      if (!started) next = now;
      if ((int32_t)(now - next) > 40) next = now - 40;          // a slow frame: catch up at most 40 ms
      while (started && !crashed && (int32_t)(now - next) >= 0) {
        next += CV_TICK;
        vy += up ? -120 : 120;
        if (vy > 1120) vy = 1120;
        if (vy < -1120) vy = -1120;
        y += vy;
        memmove(cv->top, cv->top + 1, CV_N - 1);                // scroll one column
        memmove(cv->bot, cv->bot + 1, CV_N - 1);
        memmove(cv->rock, cv->rock + 1, CV_N - 1);
        dist++;
        cvColumn(CV_N - 1, dist);
        if (--rockIn == 0) {                                    // a rock in the opening
          rockIn = (uint8_t)(40 + random(40) - (dist > 2000 ? 20 : dist / 100));
          uint8_t t = cv->top[CV_N - 1], b = cv->bot[CV_N - 1];
          int16_t room = b - t - CV_ROCK - 16;
          uint8_t r = (uint8_t)(t + 8 + random(room > 0 ? room : 1));
          for (uint8_t k = 0; k < 4; k++) cv->rock[CV_N - 4 + k] = r;
        }
        int16_t cy = (int16_t)(y / 256);
        if (dist % 2 == 0)                                      // exhaust smoke, left behind with the cave
          fxEmit(CV_X - 4, (int16_t)(cy + 6), (int16_t)-fxRange(120, 150), (int16_t)fxRange(-15, 5), (uint16_t)fxRange(450, 650),
                 GREY3, rgb(51, 0, 51), 2, -3, FX_GROW);
        bool hit = false;
        for (uint8_t c = CV_X / CV_CW; c <= (CV_X + CV_CPW - 1) / CV_CW; c++) {
          if (cy <= cv->top[c] || cy + CV_CPH >= cv->bot[c]) hit = true;
          if (cv->rock[c] && cy + CV_CPH > cv->rock[c] && cy < cv->rock[c] + CV_ROCK) hit = true;
        }
        if (hit) {                                              // a fireball, wreckage, a shock wave
          crashed = true; crashAt = now; sfx(150, 400);
          int16_t ex = CV_X + CV_CPW / 2, ey = (int16_t)(cy + CV_CPH / 2);
          fxExplode(ex, ey, 3);
          fxBurst(ex, ey, 6, rgb(150, 140, 120), rgb(40, 30, 30), 140, 700, 60, 3);
          fxShake(6, 500); fxFlash(C::WHITE, 200, 120);
        }
        else if (dist % 100 == 0) sfx(1300, 20);
      }

      // ---- draw ----
      int16_t off = 0, cy = (int16_t)(y / 256);
      if (started && !crashed) {                               // smooth between two steps
        int32_t f = CV_TICK - (int32_t)(next - now);
        if (f < 0) f = 0;
        if (f > CV_TICK) f = CV_TICK;
        off = (int16_t)(f * CV_CW / CV_TICK);
        cy = (int16_t)((y + vy * f / CV_TICK) / 256);
      } else if (!started) cy = (int16_t)(y / 256 + (int32_t)(sinf(now / 200.0f) * 3));
      fxUpdate();
      cvDraw(dist, off);
      if (!crashed) cvCopter(cy, now);
      else if (now - smokeAt > 60) {                            // the wreck smoulders
        smokeAt = now;
        fxEmit((int16_t)(CV_X + 6 + fxRand(10)), (int16_t)(cy + 5), (int16_t)fxRange(-10, 10), -30, 600, GREY2, rgb(51, 0, 51), 3, -4, FX_GROW);
      }
      fxDraw(0, 0, CV_TOP, SCR_H);
      fxShakeApply(CV_TOP, SCR_H);
      fxFlashApply(0, CV_TOP, SCR_W, SCR_H - CV_TOP);
      statusBar("CAVE", (uint16_t)(dist / 10), curHigh, C::BROWN);   // last: the roof may reach into it
      if (!started) {
        gfx.rbox(70, 196, 180, 30, 8, C::BLACK);
        gfx.rframe(70, 196, 180, 30, 8, C::ORANGE);
        gfx.center(202, "hold UP to fly", C::WHITE);
      }
      gfx.send();
      if (crashed && now - crashAt > 700) break;
    }
    if (!crashed) break;
    again = gameOver((uint16_t)(dist / 10));
  }
  free(cv);
  cv = NULL;
}

static const char *const IC_CAVE[12] = {
  "nnnnnnnnnnnn", "nnnnnn..nnnn", "nnn.......nn", "n...........", "..sssss.....", "....s.......",
  "oo.yyyyy....", "...yyyccy...", "....yyyy....", "....s..s..ss", "nn......nnnn", "nnnnnnnnnnnn" };
