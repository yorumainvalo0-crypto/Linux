// MiniArcade Color: Minigolf, 9 holes seen from above. Part of
// console.cpp, which includes it. LEFT/RIGHT turn the aim, UP/DOWN set
// the strength, OK hits the ball. Walls bounce it back, sand slows it
// down, water costs a stroke and puts it back where it was hit. A hole
// gives (par + 3 - strokes) x 20 points and 50 more for a hole in one;
// after par + 4 strokes the hole is over. The courses and the physics
// are the OLED console's (tiles of 4 units, 1/256 unit steps of 20 ms),
// drawn 2.5 times as big.

#define MG_W    32               // tiles
#define MG_H    12
#define MG_T    4                // tile size in course units
#define MG_TS   10               // ... on the screen
#define MG_Y    62               // top of the course on the screen
#define MG_STEP 20               // ms per physics step

static const char MG_HOLES[9][MG_H][MG_W + 1] = {
  { "################################",
    "#..............................#",
    "#..............................#",
    "#..............................#",
    "#..............................#",
    "#..S.......................O...#",
    "#..............................#",
    "#..............................#",
    "#..............................#",
    "#..............................#",
    "#..............................#",
    "################################" },
  { "################################",
    "#..............#...............#",
    "#..............#...............#",
    "#..............#...............#",
    "#..S...........#...........O...#",
    "#..............#...............#",
    "#..............#...............#",
    "#..............#...............#",
    "#..............................#",
    "#..............................#",
    "#..............................#",
    "################################" },
  { "################################",
    "#....................#.........#",
    "#..S.................#.........#",
    "#....................#.........#",
    "#....................#.........#",
    "################.....#.........#",
    "################.....#.........#",
    "################...............#",
    "################...........O...#",
    "################...............#",
    "################...............#",
    "################################" },
  { "################################",
    "#..............................#",
    "#..........ssssss..............#",
    "#..........ssssss..............#",
    "#..S.......ssssss..........O...#",
    "#..........ssssss..............#",
    "#..........ssssss..............#",
    "#..............................#",
    "#..............................#",
    "#..........#########...........#",
    "#..............................#",
    "################################" },
  { "################################",
    "#.............ww...............#",
    "#.............ww...............#",
    "#.............ww...............#",
    "#..S..........ww...........O...#",
    "#.............ww...............#",
    "#.............ww...............#",
    "#..............................#",
    "#.............ww...............#",
    "#.............ww...............#",
    "#.............ww...............#",
    "################################" },
  { "################################",
    "#..S.....#.........#...........#",
    "#........#.........#...........#",
    "#........#....#....#....#......#",
    "#........#....#....#....#......#",
    "#.............#.........#......#",
    "#.............#.........#......#",
    "#.............#.........#...O..#",
    "######........#.........#......#",
    "#.............#.........#......#",
    "#.............#.........#......#",
    "################################" },
  { "################################",
    "################################",
    "################################",
    "#.........sss.........sss......#",
    "#..S......sss.........sss...O..#",
    "#.........sss.........sss......#",
    "#..............#######.........#",
    "################################",
    "################################",
    "################################",
    "################################",
    "################################" },
  { "################################",
    "#..............................#",
    "#..........wwwwwwwwwww.........#",
    "#..........w.........w.........#",
    "#..S.......w....O....w.........#",
    "#..........w.........w.........#",
    "#..........w.........w.........#",
    "#..........www.....www.........#",
    "#..............................#",
    "#..............................#",
    "#..............................#",
    "################################" },
  { "################################",
    "#..S...........................#",
    "#.......####.......####........#",
    "#.......####.......####........#",
    "#..............................#",
    "#...ss.........ww.........ss...#",
    "#...ss.........ww.........ss...#",
    "#..............................#",
    "#.......####.......####........#",
    "#.......####.......####....O...#",
    "#..............................#",
    "################################" } };
static const uint8_t MG_PAR[9] = { 2, 3, 3, 3, 3, 4, 3, 4, 4 };

static uint8_t mgHole;
static int32_t mgBx, mgBy, mgVx, mgVy, mgHx, mgHy;     // ball and hole, 1/256 course units
static uint8_t mgAng, mgPow;
static uint8_t mgCard[9];                              // strokes per hole, 0 = not played yet
static uint16_t mgScore;

static char mgTile(int32_t x, int32_t y) {             // x, y in 1/256 units
  if (x < 0 || y < 0) return '#';
  int16_t tx = (int16_t)((x >> 8) / MG_T), ty = (int16_t)((y >> 8) / MG_T);
  if (tx >= MG_W || ty >= MG_H) return '#';
  return MG_HOLES[mgHole][ty][tx];
}

// the middle of the first tile with character ch, in 1/256 units
static void mgFind(char ch, int32_t *x, int32_t *y) {
  for (uint8_t r = 0; r < MG_H; r++)
    for (uint8_t c = 0; c < MG_W; c++)
      if (MG_HOLES[mgHole][r][c] == ch) { *x = (int32_t)(c * MG_T + 2) << 8; *y = (int32_t)(r * MG_T + 2) << 8; return; }
}

// sin of 32 directions * 256 (0 = right, counted clockwise)
static int16_t mgSin(uint8_t a) {
  static const int16_t Q[9] = { 0, 50, 98, 142, 181, 213, 237, 251, 256 };
  a &= 31;
  if (a <= 8)  return Q[a];
  if (a <= 16) return Q[16 - a];
  if (a <= 24) return -Q[a - 16];
  return -Q[32 - a];
}
static int16_t mgCos(uint8_t a) { return mgSin(a + 8); }

/* One step of 20 ms. Returns 1 = in the hole, 2 = in the water, 0 = else. */
static uint8_t mgStep(int32_t *x, int32_t *y, int32_t *vx, int32_t *vy, int32_t hx, int32_t hy) {
  int32_t nx = *x + *vx;
  if (mgTile(nx, *y) == '#') { *vx = -*vx * 3 / 4; nx = *x; }
  int32_t ny = *y + *vy;
  if (mgTile(nx, ny) == '#') { *vy = -*vy * 3 / 4; ny = *y; }
  *x = nx; *y = ny;
  char t = mgTile(*x, *y);
  if (t == 'w') return 2;
  int32_t f = t == 's' ? 6 : 40;                        // friction: sand brakes hard
  *vx -= *vx / f; *vy -= *vy / f;
  int32_t sp = abs(*vx) + abs(*vy);
  if (sp < 24) { *vx = *vy = 0; }
  int32_t dx = *x - hx, dy = *y - hy;
  if (abs(dx) < (2 << 8) && abs(dy) < (2 << 8) && sp < 450) { *vx = *vy = 0; return 1; }
  return 0;
}

// ---------------- screen ----------------
static int16_t mgSx(int32_t x) { return (int16_t)(x * 5 / 512); }            // course -> screen
static int16_t mgSy(int32_t y) { return (int16_t)(MG_Y + y * 5 / 512); }

static bool mgWall(int8_t r, int8_t c) {
  return r < 0 || c < 0 || r >= MG_H || c >= MG_W || MG_HOLES[mgHole][r][c] == '#';
}

static void mgCourse(uint32_t now) {
  for (int8_t r = 0; r < MG_H; r++)
    for (int8_t c = 0; c < MG_W; c++) {
      int16_t x = c * MG_TS, y = MG_Y + r * MG_TS;
      char t = MG_HOLES[mgHole][r][c];
      if (t == '#') {
        bool open = false;                                       // next to the lawn: a wooden rail
        for (int8_t dr = -1; dr <= 1; dr++) for (int8_t dc = -1; dc <= 1; dc++)
          if (!mgWall(r + dr, c + dc)) open = true;
        if (open) {
          gfx.fill(x, y, MG_TS, MG_TS, rgb(153, 102, 51));
          if (!mgWall(r - 1, c)) gfx.hline(x, y, MG_TS, rgb(230, 170, 102));
          if (!mgWall(r + 1, c)) gfx.hline(x, y + MG_TS - 1, MG_TS, rgb(80, 50, 20));
          if (!mgWall(r, c - 1)) gfx.vline(x, y, MG_TS, rgb(204, 140, 80));
          if (!mgWall(r, c + 1)) gfx.vline(x + MG_TS - 1, y, MG_TS, rgb(80, 50, 20));
          gfx.pixel(x + 3 + (r * 3 + c) % 5, y + 4, rgb(110, 70, 30));
        } else {                                                 // the rough beyond
          gfx.fill(x, y, MG_TS, MG_TS, rgb(0, 85, 25));
          gfx.pixel(x + (r * 7 + c * 3) % 9, y + (r + c * 5) % 9, rgb(0, 127, 25));
          gfx.pixel(x + (r * 5 + c * 7) % 9, y + (r * 3 + c) % 9, rgb(0, 60, 0));
        }
      } else if (t == 's') {
        gfx.fill(x, y, MG_TS, MG_TS, C::SAND);
        gfx.pixel(x + (r * 3 + c * 7) % 9, y + (r * 5 + c) % 9, rgb(204, 160, 100));
        gfx.pixel(x + (r * 7 + c * 3 + 4) % 9, y + (r + c * 5 + 4) % 9, rgb(204, 160, 100));
      } else if (t == 'w') {
        gfx.fill(x, y, MG_TS, MG_TS, rgb(0, 100, 210));
        uint8_t ph = (uint8_t)((now / 300 + c + r * 2) % 5);    // slow ripples
        if (ph < 3) gfx.hline(x + ph * 2, y + 3 + ph, 4, rgb(102, 170, 255));
      } else {
        gfx.fill(x, y, MG_TS, MG_TS, (c / 2) & 1 ? rgb(51, 170, 51) : rgb(0, 170, 51));   // mown stripes
        if (t == 'S') { gfx.fill(x + 2, y + 2, 6, 6, rgb(153, 220, 102)); gfx.frame(x + 2, y + 2, 6, 6, C::WHITE); }
      }
    }
}

static void mgFlag(int16_t hx, int16_t hy, uint32_t now) {
  gfx.disc(hx, hy, 5, rgb(0, 80, 0));
  gfx.disc(hx, hy, 4, C::BLACK);
  gfx.vline(hx + 1, hy - 26, 25, C::WHITE);
  bool wave = (now / 400) % 2;
  gfx.triangle(hx + 2, hy - 26, hx + 2, hy - 16, hx + (wave ? 15 : 13), hy - 22 + (wave ? 1 : 0), C::RED);
}

static void mgBall(int16_t x, int16_t y, int16_t r) {
  gfx.disc(x + 1, y + 2, r, rgb(0, 80, 0));
  gfx.disc(x, y, r, C::WHITE);
  gfx.pixel(x + 1, y + 1, C::LTGREY);
}

static void mgInfo(uint8_t strokes) {
  char b[16];
  snprintf(b, sizeof(b), "HOLE %u/9", mgHole + 1);
  gfx.text(6, 33, b, C::WHITE, F_TEXT);
  snprintf(b, sizeof(b), "PAR %u", MG_PAR[mgHole]);
  gfx.text(100, 33, b, C::LIME, F_TEXT);
  snprintf(b, sizeof(b), "SHOT %u", strokes);
  gfx.text(164, 33, b, C::YELLOW, F_TEXT);
  gfx.text(238, 30, "POWER", C::LTGREY, F_SMALL);
  for (uint8_t i = 0; i < 10; i++) {                            // green -> yellow -> red
    col c = i < 4 ? C::GREEN : (i < 7 ? C::YELLOW : C::RED);
    gfx.fill(238 + i * 8, 42, 7, 12, i < mgPow ? c : rgb(0, 50, 20));
  }
}

static void mgScorecard() {
  const col PARC = rgb(0, 102, 0);
  gfx.rbox(2, 186, SCR_W - 4, 52, 6, rgb(240, 240, 220));
  static const char *const ROW[3] = { "HOLE", "PAR", "YOU" };
  for (uint8_t k = 0; k < 3; k++) gfx.text(8, 190 + k * 15, ROW[k], C::DKGREY, F_SMALL);
  uint8_t sum = 0, par = 0;
  char b[8];
  for (uint8_t h = 0; h < 9; h++) {
    int16_t x = 40 + h * 28;
    if (h == mgHole) gfx.fill(x, 188, 27, 48, rgb(255, 230, 102));
    gfx.vline(x - 1, 189, 46, C::LTGREY);
    snprintf(b, sizeof(b), "%u", h + 1);
    gfx.text(x + 11, 190, b, C::DKGREY, F_SMALL);
    snprintf(b, sizeof(b), "%u", MG_PAR[h]);
    gfx.text(x + 11, 205, b, PARC, F_SMALL);
    if (mgCard[h]) {
      snprintf(b, sizeof(b), "%u", mgCard[h]);
      col c = mgCard[h] < MG_PAR[h] ? rgb(0, 128, 0) : (mgCard[h] == MG_PAR[h] ? C::BLACK : C::RED);
      gfx.text(x + 11 - (mgCard[h] > 9 ? 3 : 0), 220, b, c, F_SMALL);
      sum += mgCard[h]; par += MG_PAR[h];
    }
  }
  gfx.vline(40 + 9 * 28 - 1, 189, 46, C::LTGREY);
  gfx.text(296, 190, "SUM", C::DKGREY, F_SMALL);
  snprintf(b, sizeof(b), "%u", par);
  if (par) gfx.text(296, 205, b, PARC, F_SMALL);
  snprintf(b, sizeof(b), "%u", sum);
  if (sum) gfx.text(296, 220, b, C::BLACK, F_SMALL);
}

/* The whole screen. aim: the aiming line; ballR 0 = in the hole; splashAt:
   rings on the water at (sx, sy) (0 = none); msg: banner over the course */
static void mgDraw(int32_t bx, int32_t by, bool aim, uint8_t strokes, int16_t ballR,
                   int16_t sx, int16_t sy, uint32_t splashAt, const char *msg) {
  uint32_t now = gameMillis();
  statusBar("MINIGOLF", mgScore, curHigh, C::GREEN);
  gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(0, 70, 20), rgb(0, 25, 5));
  mgInfo(strokes);
  mgCourse(now);
  mgFlag(mgSx(mgHx), mgSy(mgHy), now);
  if (splashAt && now - splashAt < 700) {
    int16_t r = (int16_t)(2 + (now - splashAt) / 50);
    gfx.circle(sx, sy, r, C::WHITE);
    gfx.circle(sx, sy, r + 4, rgb(153, 204, 255));
  }
  int16_t px = mgSx(bx), py = mgSy(by);
  if (aim) {                                                    // dots as long as the strength
    int16_t c = mgCos(mgAng), s = mgSin(mgAng);
    int16_t len = 4 + mgPow * 3;
    for (int16_t k = 3; k < len; k += 2)
      gfx.disc(px + c * k * 5 / 512, py + s * k * 5 / 512, 1, k > len - 6 ? C::YELLOW : C::WHITE);
    int16_t ex = px + c * len * 5 / 512, ey = py + s * len * 5 / 512;
    gfx.triangle(ex + c * 5 / 256, ey + s * 5 / 256,
                 ex + mgCos(mgAng + 12) * 5 / 256, ey + mgSin(mgAng + 12) * 5 / 256,
                 ex + mgCos(mgAng + 20) * 5 / 256, ey + mgSin(mgAng + 20) * 5 / 256, C::YELLOW);
  }
  if (ballR > 0) mgBall(px, py, ballR);
  mgScorecard();
  if (msg) {
    int16_t w = Gfx::width(msg, F_BOLD) + 36, x = (SCR_W - w) / 2;
    gfx.rbox(x + 4, 104, w, 42, 10, C::BLACK);
    gfx.rbox(x, 100, w, 42, 10, rgb(0, 90, 30));
    gfx.rframe(x, 100, w, 42, 10, C::YELLOW);
    gfx.center(109, msg, C::WHITE, F_BOLD);
  }
}

void minigolfRun() {
  bool again = true;
  while (again) {
    again = false;
    mgScore = 0;
    memset(mgCard, 0, sizeof(mgCard));
    mgAng = 0; mgPow = 5;
    for (mgHole = 0; mgHole < 9; mgHole++) {
      int32_t &bx = mgBx, &by = mgBy, &vx = mgVx, &vy = mgVy, sx, sy;
      vx = vy = 0;
      mgFind('S', &bx, &by);
      mgFind('O', &mgHx, &mgHy);
      sx = bx; sy = by;
      uint8_t strokes = 0, limit = MG_PAR[mgHole] + 4;
      bool in = false;
      uint32_t last = gameMillis(), acc = 0, splashAt = 0;
      int16_t spx = 0, spy = 0;
      btnClear();
      while (!in && strokes < limit) {
        if (!poll()) return;
        uint32_t now = gameMillis(), dt = now - last;
        if (dt > 40) dt = 40;                                    // a slow frame does not make it jump
        last = now;
        if (!(vx || vy)) {
          acc = 0;
          if (btn(B_LEFT))  { mgAng = (mgAng + 31) & 31; sfx(600, 5); }
          if (btn(B_RIGHT)) { mgAng = (mgAng + 1) & 31;  sfx(600, 5); }
          if (btn(B_UP)   && mgPow < 10) { mgPow++; sfx(800, 8); }
          if (btn(B_DOWN) && mgPow > 1)  { mgPow--; sfx(500, 8); }
          if (btnTap(B_OK)) {
            int32_t sp = 130 * mgPow;                            // 1/256 units per step
            vx = mgCos(mgAng) * sp / 256; vy = mgSin(mgAng) * sp / 256;
            sx = bx; sy = by;
            strokes++;
            sfx(300 + mgPow * 60, 30);
          }
        } else {
          acc += dt;
          while (acc >= MG_STEP && (vx || vy)) {                 // real time: one step per 20 ms
            acc -= MG_STEP;
            uint8_t r = mgStep(&bx, &by, &vx, &vy, mgHx, mgHy);
            if (r == 1) { in = true; sfx(1500, 150); break; }
            if (r == 2) {                                        // splash: back, one more stroke
              sfx(150, 200);
              spx = mgSx(bx); spy = mgSy(by); splashAt = now;
              bx = sx; by = sy; vx = vy = 0;
              strokes++;
              break;
            }
          }
        }
        mgDraw(bx, by, !(vx || vy) && !in, strokes, 3, spx, spy, splashAt, NULL);
        gfx.send();
      }
      uint16_t pts = 0;
      if (in && strokes < MG_PAR[mgHole] + 3) pts = (MG_PAR[mgHole] + 3 - strokes) * 20;
      if (in && strokes == 1) pts += 50;
      mgScore += pts;
      mgCard[mgHole] = strokes;
      const char *what = !in ? "TOO MANY" : strokes == 1 ? "HOLE IN ONE!" :
                         strokes < MG_PAR[mgHole] ? "BIRDIE!" : strokes == MG_PAR[mgHole] ? "PAR" : "IN";
      char b[24];
      snprintf(b, sizeof(b), "%s +%u", what, pts);
      uint32_t t0 = gameMillis();
      btnClear();
      while (gameMillis() - t0 < 2000) {
        if (!poll()) return;
        if (btnTap(B_OK) && gameMillis() - t0 > 400) break;
        uint32_t t = gameMillis() - t0;
        int16_t r = in ? (int16_t)(t < 300 ? 3 - t / 100 : 0) : 3;   // the ball drops in
        mgDraw(in ? mgHx : bx, in ? mgHy : by, false, strokes, r, 0, 0, 0, b);
        gfx.send();
      }
    }
    mgHole = 8;
    again = gameOver(mgScore);
  }
}

static const char *const IC_MINIGOLF[12] = {
  "......wrrr..", "......wrrrr.", "......wrr...", "......w.....", "......w.....", ".ggggkwkggg.",
  "gggggkkkgggg", "gggggggggggg", "ggwwgggggggg", "ggwwgggggggg", ".gggggggggg.", "............" };
