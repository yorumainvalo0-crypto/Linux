// MiniArcade: Minigolf. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  MINIGOLF   (9 holes, seen from above)
// =========================================================
/* LEFT/RIGHT turn the aim, UP/DOWN set the strength, OK hits the ball.
   Walls bounce it back, sand slows it down, water costs a stroke and puts
   it back where it was hit. A hole gives (par + 3 - strokes) x 20 points
   and 50 more for a hole in one; after par + 4 strokes the hole is over. */
#define MG_W    32               // tiles of 4 x 4 px
#define MG_H    12
#define MG_T    4

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
static int32_t mgBx, mgBy, mgVx, mgVy, mgHx, mgHy;     // ball and hole, 1/256 px
static uint8_t mgAng, mgPow;

char mgTile(int32_t x, int32_t y) {                     // x, y in 1/256 px
  int16_t tx = (x >> 8) / MG_T, ty = ((y >> 8) - TOP_H) / MG_T;
  if (tx < 0 || ty < 0 || tx >= MG_W || ty >= MG_H) return '#';
  return MG_HOLES[mgHole][ty][tx];
}

// the middle of the first tile with character ch, in 1/256 px
static void mgFind(char ch, int32_t *x, int32_t *y) {
  for (uint8_t r = 0; r < MG_H; r++)
    for (uint8_t c = 0; c < MG_W; c++)
      if (MG_HOLES[mgHole][r][c] == ch) { *x = (int32_t)(c * MG_T + 2) << 8; *y = (int32_t)(TOP_H + r * MG_T + 2) << 8; return; }
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
uint8_t mgStep(int32_t *x, int32_t *y, int32_t *vx, int32_t *vy, int32_t hx, int32_t hy) {
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

static void mgDraw(int32_t bx, int32_t by, uint8_t ang, uint8_t pow, bool aim, uint8_t strokes, uint16_t score) {
  char b[32];                                             // two lines in the yellow band
  snprintf(b, sizeof(b), "HOLE %u/9  PAR %u", mgHole + 1, MG_PAR[mgHole]);
  oled.drawStr(1, 7, b);
  snprintf(b, sizeof(b), "%u", score);
  rightStr(7, b);
  snprintf(b, sizeof(b), "shot %u", strokes);
  oled.drawStr(1, 15, b);
  oled.drawStr(SCR_W - 56, 15, "pow");                     // strength bar
  oled.drawFrame(SCR_W - 40, 10, 38, 5);
  oled.drawBox(SCR_W - 39, 11, pow * 36 / 10, 3);
  for (uint8_t r = 0; r < MG_H; r++)
    for (uint8_t c = 0; c < MG_W; c++) {
      int16_t x = c * MG_T, y = TOP_H + r * MG_T;
      switch (MG_HOLES[mgHole][r][c]) {
      case '#': {                                        // only the edge of a wall block
        bool open = false;
        for (int8_t dr = -1; dr <= 1; dr++) for (int8_t dc = -1; dc <= 1; dc++) {
          int8_t rr = r + dr, cc = c + dc;
          if (rr >= 0 && rr < MG_H && cc >= 0 && cc < MG_W && MG_HOLES[mgHole][rr][cc] != '#') open = true;
        }
        if (open) oled.drawBox(x, y, MG_T, MG_T);
        break;
      }
      case 's': oled.drawPixel(x + 1, y + 1); oled.drawPixel(x + 3, y + 3); break;
      case 'w': oled.drawHLine(x + ((r + c) & 1) * 2, y + 1, 2); oled.drawHLine(x + (((r + c) & 1) ^ 1) * 2, y + 3, 2); break;
      case 'O':
        oled.drawBox(x + 1, y, 2, 4); oled.drawBox(x, y + 1, 4, 2);
        oled.drawVLine(x + 3, y - 6, 6); oled.drawBox(x + 4, y - 6, 3, 2);   // flag
        break;
      default: break;
      }
    }
  int16_t px = bx >> 8, py = by >> 8;
  if (aim) for (uint8_t k = 3; k < 4 + pow * 3; k += 2)
    oled.drawPixel(px + mgCos(ang) * k / 256, py + mgSin(ang) * k / 256);
  oled.drawBox(px - 1, py, 3, 1); oled.drawBox(px, py - 1, 1, 3);   // the ball
}

void minigolfRun() {
  bool again = true;
  while (again) {
    again = false;
    uint16_t score = 0;
    uint8_t &ang = mgAng, &pow = mgPow;
    ang = 0; pow = 5;
    for (mgHole = 0; mgHole < 9; mgHole++) {
      int32_t &bx = mgBx, &by = mgBy, &hx = mgHx, &hy = mgHy, &vx = mgVx, &vy = mgVy, sx, sy;
      vx = vy = 0;
      mgFind('S', &bx, &by);
      mgFind('O', &hx, &hy);
      sx = bx; sy = by;
      uint8_t strokes = 0, limit = MG_PAR[mgHole] + 4;
      bool in = false;
      btnClear();
      while (!in && strokes < limit) {
        if (!poll()) return;
        bool rolling = vx || vy;
        if (!rolling) {
          if (btn(B_LEFT))  { ang = (ang + 31) & 31; sfx(600, 5); }
          if (btn(B_RIGHT)) { ang = (ang + 1) & 31;  sfx(600, 5); }
          if (btn(B_UP)   && pow < 10) { pow++; sfx(800, 8); }
          if (btn(B_DOWN) && pow > 1)  { pow--; sfx(500, 8); }
          if (btnTap(B_OK)) {
            int32_t sp = 130 * pow;                          // 1/256 px per step
            vx = mgCos(ang) * sp / 256; vy = mgSin(ang) * sp / 256;
            sx = bx; sy = by;
            strokes++;
            sfx(300 + pow * 60, 30);
          }
        }
        for (uint8_t k = 0; k < 2 && (vx || vy); k++) {    // two physics steps per frame
          uint8_t r = mgStep(&bx, &by, &vx, &vy, hx, hy);
          if (r == 1) { in = true; sfx(1500, 150); break; }
          if (r == 2) {                                        // splash: back, one more stroke
            sfx(150, 200);
            bx = sx; by = sy; vx = vy = 0;
            strokes++;
            break;
          }
        }
        mgDraw(bx, by, ang, pow, !(vx || vy) && !in, strokes, score);
        oled.sendBuffer();
      }
      uint16_t pts = 0;
      if (in && strokes < MG_PAR[mgHole] + 3) pts = (MG_PAR[mgHole] + 3 - strokes) * 20;
      if (in && strokes == 1) pts += 50;
      score += pts;
      const char *msg = !in ? "TOO MANY" : strokes == 1 ? "HOLE IN ONE!" :
                        strokes < MG_PAR[mgHole] ? "BIRDIE!" : strokes == MG_PAR[mgHole] ? "PAR" : "IN";
      uint32_t t0 = millis();
      btnClear();
      while (millis() - t0 < 1800) {
        if (!poll()) return;
        if (btnTap(B_OK) && millis() - t0 > 400) break;
        mgDraw(hx, hy, ang, pow, false, strokes, score);
        oled.setDrawColor(0); oled.drawBox(20, 30, 88, 18); oled.setDrawColor(1);
        oled.drawFrame(20, 30, 88, 18);
        char b[24];
        snprintf(b, sizeof(b), "%s +%u", msg, pts);
        centerStr(42, b);
        oled.sendBuffer();
      }
    }
    again = gameOver(score);
  }
}
