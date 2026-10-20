// MiniArcade Color: Cave. Part of console.cpp, which includes it.
// Hold UP to climb, let go to sink. The cave scrolls from right to left;
// its middle wanders up and down and it gets narrower the further you
// fly, and now and then a rock hangs in the way. The score is the
// distance (one point per 10 columns).

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
#define CV_PUFFS  10

struct CvPuff { int16_t x, y; uint8_t life; };
struct CvState {
  uint8_t top[CV_N], bot[CV_N];        // walls per column: last row of the roof, first of the floor
  uint8_t rock[CV_N];                  // 0 = none, else the rock's top row
  CvPuff  puff[CV_PUFFS];
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

// the cave at scroll offset off (0..CV_CW-1 px), dist = columns flown so far
static void cvDraw(uint16_t dist, int16_t off) {
  gfx.fill(0, CV_TOP, SCR_W, CV_BOT - CV_TOP + 1, rgb(51, 0, 51));
  int32_t far = (int32_t)dist * CV_CW / 2 + off / 2;           // glowing crystals far behind, half speed
  for (uint8_t k = 0; k < 24; k++) {
    uint32_t h = (uint32_t)(k + 5) * 2654435761u;
    int16_t x = (int16_t)(((int32_t)((h >> 8) % 640) - far % 640 + 640) % 640) - 160;
    int16_t y = (int16_t)(CV_TOP + 10 + (h >> 20) % (CV_BOT - CV_TOP - 20));
    col c = k % 3 ? rgb(90, 60, 160) : rgb(60, 160, 180);
    gfx.triangle(x, y - 4, x - 2, y, x + 2, y, c);
    gfx.triangle(x - 2, y, x + 2, y, x, y + 4, colDark(c));
  }
  for (uint8_t i = 0; i < CV_N; i++) {
    int16_t x = (int16_t)(i * CV_CW - off);
    uint16_t a = (uint16_t)(dist + i);                         // the column's own number: stable texture
    uint8_t h = (uint8_t)((a * 151u) >> 2);
    col rock = (h & 3) ? rgb(102, 42, 0) : rgb(80, 42, 30);
    int16_t t = cv->top[i], b = cv->bot[i];
    gfx.fill(x, CV_TOP, CV_CW, t - CV_TOP - 3, rock);
    gfx.fill(x, t - 9, CV_CW, 6, rgb(153, 85, 0));
    gfx.fill(x, t - 3, CV_CW, 3, rgb(204, 128, 51));
    gfx.hline(x, t, CV_CW, rgb(255, 200, 102));
    gfx.hline(x, b, CV_CW, rgb(102, 204, 51));                 // moss on the floor
    gfx.fill(x, b + 1, CV_CW, 3, rgb(204, 128, 51));
    gfx.fill(x, b + 4, CV_CW, 6, rgb(153, 85, 0));
    gfx.fill(x, b + 10, CV_CW, CV_BOT - b - 9, rock);
    if (cv->rock[i]) {                                         // a rock, slice by slice
      int16_t ry = cv->rock[i];
      bool l = i > 0 && cv->rock[i - 1] == ry, r = i + 1 < CV_N && cv->rock[i + 1] == ry;
      int16_t cut = (l && r) ? 0 : 2;                          // rounder at its ends
      gfx.fill(x, ry + cut, CV_CW, CV_ROCK - 2 * cut, rgb(120, 120, 135));
      gfx.hline(x, ry + cut + 1, CV_CW, C::LTGREY);
      gfx.hline(x, ry + CV_ROCK - cut - 2, CV_CW, C::DKGREY);
    }
  }
}

static void cvCopter(int16_t y, uint32_t now) {
  int16_t x = CV_X;
  int16_t r = (int16_t)((now / 40) % 4) * 3;                   // the rotor turns
  gfx.hline(x + 11 - 8 - r / 2, y, 16 + r, C::LTGREY);
  gfx.vline(x + 11, y, 3, C::GREY);
  gfx.fill(x - 4, y + 5, 10, 3, C::ORANGE);                    // tail and its rotor
  gfx.vline(x - 4, y + 2 + (int16_t)((now / 40) % 2) * 2, 5, C::LTGREY);
  gfx.rbox(x + 4, y + 2, 16, 8, 4, C::YELLOW);
  gfx.hline(x + 7, y + 3, 9, rgb(255, 250, 180));
  gfx.fill(x + 14, y + 4, 5, 3, C::CYAN);                      // window
  gfx.hline(x + 6, y + 10, 12, C::LTGREY);                     // skid
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
    uint32_t now = gameMillis(), next = now, crashAt = 0;
    bool started = false, crashed = false;
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
        for (uint8_t k = 0; k < CV_PUFFS; k++)                  // exhaust drifts away
          if (cv->puff[k].life) { cv->puff[k].x -= CV_CW + 1; cv->puff[k].life--; }
        if (dist % 3 == 0)
          for (uint8_t k = 0; k < CV_PUFFS; k++)
            if (!cv->puff[k].life) { cv->puff[k] = { (int16_t)(CV_X - 6), (int16_t)(y / 256 + 6), 14 }; break; }
        int16_t cy = (int16_t)(y / 256);
        bool hit = false;
        for (uint8_t c = CV_X / CV_CW; c <= (CV_X + CV_CPW - 1) / CV_CW; c++) {
          if (cy <= cv->top[c] || cy + CV_CPH >= cv->bot[c]) hit = true;
          if (cv->rock[c] && cy + CV_CPH > cv->rock[c] && cy < cv->rock[c] + CV_ROCK) hit = true;
        }
        if (hit) { crashed = true; crashAt = now; sfx(150, 400); }
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
      cvDraw(dist, off);
      for (uint8_t k = 0; k < CV_PUFFS; k++) {
        const CvPuff &p = cv->puff[k];
        if (p.life && !crashed) gfx.disc(p.x - off, p.y, (int16_t)(1 + (14 - p.life) / 5), p.life > 7 ? C::GREY : C::DKGREY);
      }
      if (!crashed) cvCopter(cy, now);
      else {                                                    // the crash: a flash and sparks
        uint32_t age = now - crashAt;
        int16_t cx = CV_X + CV_CPW / 2, ccy = cy + CV_CPH / 2;
        if (age < 300) gfx.disc(cx, ccy, (int16_t)(6 + age / 15), age < 120 ? C::WHITE : C::ORANGE);
        gfx.circle(cx, ccy, (int16_t)(8 + age / 8), C::YELLOW);
        for (uint8_t k = 0; k < 12; k++) {
          uint32_t h = (uint32_t)(k + 1) * 2654435761u;
          int16_t dx = (int16_t)((int32_t)((h >> 8) % 61) - 30), dy = (int16_t)((int32_t)((h >> 16) % 61) - 36);
          gfx.fill(cx + dx * (int16_t)age / 300, ccy + dy * (int16_t)age / 300 + (int16_t)(age * age / 20000), 3, 3, k % 2 ? C::ORANGE : C::YELLOW);
        }
      }
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
