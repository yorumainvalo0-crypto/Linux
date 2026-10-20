// MiniArcade Color: Pac-Man. Part of console.cpp, which includes it.
// The rules, the maze and the three ghosts are the OLED game's (RtPac in
// rtgames.h); here the 32 x 12 maze is drawn with 10 px tiles: blue
// walls, dots, blinking power pellets, the ghosts red, pink and cyan.
// Arrows steer (the last one pressed counts at the next crossing). Eat
// all dots for the next level; three lives.

#include "rtgames.h"

#define PAC_T   10                        // tile size
#define PAC_Y0  73                        // top of the maze
#define PAC_X0  0

static const char *const PAC_GHOST[2][10] = {
  { "...xxxx...", ".xxxxxxxx.", "xxxxxxxxxx", "xxxxxxxxxx", "xxxxxxxxxx", "xxxxxxxxxx", "xxxxxxxxxx",
    "xxxxxxxxxx", "xx.xxxx.xx", "x...xx...x" },
  { "...xxxx...", ".xxxxxxxx.", "xxxxxxxxxx", "xxxxxxxxxx", "xxxxxxxxxx", "xxxxxxxxxx", "xxxxxxxxxx",
    "xxxxxxxxxx", "xxx.xx.xxx", ".x..xx..x." } };
static const col PAC_GCOL[RM_GH] = { C::RED, C::PINK, C::CYAN };

// a wall tile next to a corridor gets the bright border on that side
static bool pacOpen(int x, int y) { return y >= 0 && y < RM_H && !RtPac::wallAt(x, y); }

static void pacMaze() {
  for (int y = 0; y < RM_H; y++)
    for (int x = 0; x < RM_W; x++) {
      if (!RtPac::wallAt(x, y)) continue;
      int16_t sx = PAC_X0 + x * PAC_T, sy = PAC_Y0 + y * PAC_T;
      if (RtPac::doorAt(x, y)) { gfx.fill(sx, sy + 4, PAC_T, 3, C::PINK); continue; }
      bool edge = false;
      for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (pacOpen(x + dx, y + dy)) edge = true;
      gfx.fill(sx, sy, PAC_T, PAC_T, edge ? rgb(0, 0, 110) : rgb(0, 0, 50));
      if (pacOpen(x, y - 1)) gfx.fill(sx, sy, PAC_T, 2, C::BLUE);
      if (pacOpen(x, y + 1)) gfx.fill(sx, sy + PAC_T - 2, PAC_T, 2, C::BLUE);
      if (pacOpen(x - 1, y)) gfx.fill(sx, sy, 2, PAC_T, C::BLUE);
      if (pacOpen(x + 1, y)) gfx.fill(sx + PAC_T - 2, sy, 2, PAC_T, C::BLUE);
    }
}

// where an entity is on the screen (top left of its tile), smooth between tiles
static void pacPos(const RmEnt &e, uint8_t period, int16_t *x, int16_t *y) {
  int16_t off = e.dir ? (int16_t)(e.p * PAC_T / period) : 0;
  int16_t px = (int16_t)(e.x * PAC_T + RtPac::dx(e.dir) * off);
  if (px < 0) px += RM_W * PAC_T;
  if (px >= RM_W * PAC_T) px -= RM_W * PAC_T;
  *x = PAC_X0 + px;
  *y = (int16_t)(PAC_Y0 + e.y * PAC_T + RtPac::dy(e.dir) * off);
}

static void pacMan(int16_t x, int16_t y, uint8_t face, uint8_t open) {
  int16_t cx = x + 5, cy = y + 5;
  gfx.disc(cx, cy, 5, C::YELLOW);
  if (!open) return;
  int16_t w = open == 1 ? 3 : 6;                     // mouth, towards where he looks
  int16_t ex = cx + 7 * RtPac::dx(face), ey = cy + 7 * RtPac::dy(face);
  if (RtPac::dx(face)) gfx.triangle(cx, cy, ex, ey - w, ex, ey + w, C::BLACK);
  else gfx.triangle(cx, cy, ex - w, ey, ex + w, ey, C::BLACK);
}

static void pacGhost(const RtPac &pm, uint8_t i, uint32_t t) {
  const RmEnt &g = pm.gh[i];
  int16_t x, y;
  pacPos(g, pm.ghostPeriod(g), &x, &y);
  for (int16_t k = 0; k < 2; k++) {                 // twice near the tunnel's ends
    int16_t gx = x - k * RM_W * PAC_T;
    if (k && gx + PAC_T <= 0) break;
    if (g.state != RG_EYES) {
      bool fright = g.state == RG_FRIGHT;
      if (fright && pm.fright < 90 && (t / 8) % 2) {
        col pal[] = { C::WHITE };
        gfx.sprite(gx, y, PAC_GHOST[(t / 6) % 2], 10, "x", pal, 1);
        continue;
      }
      col pal[] = { fright ? C::BLUE : PAC_GCOL[i] };
      gfx.sprite(gx, y, PAC_GHOST[(t / 6) % 2], 10, "x", pal, 1);
      if (fright) {                                  // a scared face
        gfx.fill(gx + 2, y + 3, 2, 2, C::PINK);
        gfx.fill(gx + 6, y + 3, 2, 2, C::PINK);
        for (int16_t m = 0; m < 6; m++) gfx.pixel(gx + 2 + m, y + 6 + (m & 1), C::PINK);
        continue;
      }
    }
    int16_t ox = RtPac::dx(g.dir), oy = RtPac::dy(g.dir);  // eyes look where it goes
    gfx.fill(gx + 1, y + 2, 3, 4, C::WHITE);
    gfx.fill(gx + 6, y + 2, 3, 4, C::WHITE);
    gfx.fill(gx + 2 + ox, y + 3 + oy, 2, 2, C::DKBLUE);
    gfx.fill(gx + 7 + ox, y + 3 + oy, 2, 2, C::DKBLUE);
  }
}

static void pacDraw(const RtPac &pm, uint8_t face, const char *msg, col msgCol) {
  gfx.fill(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(0, 0, 20));
  gfx.fill(0, PAC_Y0, SCR_W, RM_H * PAC_T, C::BLACK);
  pacMaze();
  for (uint8_t y = 0; y < RM_H; y++)
    for (uint8_t x = 0; x < RM_W; x++) {
      uint32_t bit = 1UL << x;
      int16_t sx = PAC_X0 + x * PAC_T, sy = PAC_Y0 + y * PAC_T;
      if (pm.dots[y] & bit) gfx.fill(sx + 4, sy + 4, 2, 2, rgb(255, 200, 160));
      if ((pm.power[y] & bit) && (pm.t / 10) % 2 == 0) gfx.disc(sx + 5, sy + 5, 3, rgb(255, 200, 160));
    }
  for (uint8_t g = 0; g < RM_GH; g++) pacGhost(pm, g, pm.t);
  const RmEnt &e = pm.pac[0];
  if (pm.alive(0) && (!pm.freeze || (pm.freeze / 6) % 2 == 0 || pm.t < RM_FREEZE)) {
    int16_t x, y;
    pacPos(e, pm.pacPeriod(), &x, &y);
    uint8_t open = e.dir ? (uint8_t)((pm.t / 3) % 3) : 1;
    pacMan(x, y, face, open == 2 ? 1 : open);
    if (x > SCR_W - PAC_T) pacMan(x - RM_W * PAC_T, y, face, open);
  }
  // above the maze: level and lives; below: dots left
  char b[20];
  snprintf(b, sizeof(b), "LEVEL %u", pm.level + 1);
  gfx.text(8, TOP_H + 14, b, C::WHITE, F_TEXT);
  gfx.text(SCR_W - 120, TOP_H + 18, "LIVES", C::LTGREY, F_SMALL);
  for (uint8_t i = 0; i < pm.lives[0]; i++) pacMan(SCR_W - 84 + i * 16, TOP_H + 16, 4, 1);
  snprintf(b, sizeof(b), "DOTS %u", pm.dotsLeft);
  gfx.text(8, PAC_Y0 + RM_H * PAC_T + 14, b, C::LTGREY, F_SMALL);
  if (pm.fright) {                                 // how long the ghosts stay blue
    gfx.text(SCR_W - 130, PAC_Y0 + RM_H * PAC_T + 14, "POWER", C::CYAN, F_SMALL);
    gfx.fill(SCR_W - 100, PAC_Y0 + RM_H * PAC_T + 15, (int16_t)(pm.fright * 90 / 350), 6, C::BLUE);
  }
  if (msg) {
    int16_t w = Gfx::width(msg, F_BOLD) + 16;
    gfx.rbox((SCR_W - w) / 2, PAC_Y0 + 62, w, 30, 6, C::BLACK);
    gfx.rframe((SCR_W - w) / 2, PAC_Y0 + 62, w, 30, 6, msgCol);
    gfx.center(PAC_Y0 + 65, msg, msgCol, F_BOLD);
  }
}

void pacmanRun() {
  RtPac *pm = (RtPac *)malloc(sizeof(RtPac));
  if (!pm) return;
  bool again = true;
  while (again) {
    again = false;
    pm->begin(esp_random(), 1);
    uint8_t  latch = 0, face = 3, lastLives = pm->lives[0], lastLevel = 0;
    uint32_t next = gameMillis();
    bool over = false;
    btnClear();
    while (poll()) {
      if (btn(B_UP))    latch = 1;
      if (btn(B_DOWN))  latch = 2;
      if (btn(B_LEFT))  latch = 3;
      if (btn(B_RIGHT)) latch = 4;
      uint32_t now = gameMillis();
      for (uint8_t k = 0; k < 4 && (int32_t)(now - next) >= 0; k++) {   // fixed ticks, as many as time passed
        uint8_t in[2] = { latch, 0 };
        uint8_t ev = pm->step(in);
        next += RT_TICK_MS;
        if (ev & RT_EV_EAT) sfx(pm->t % 2 ? 880 : 660, 12);
        if (ev & RT_EV_HIT) sfx(1300, 60);
      }
      if ((int32_t)(now - next) > 100) next = now;
      if (pm->pac[0].dir) face = pm->pac[0].dir;
      if (pm->lives[0] < lastLives) { lastLives = pm->lives[0]; sfx(200, 400); }
      if (pm->level != lastLevel)   { lastLevel = pm->level; sfx(1500, 200); }
      uint16_t score = pm->score[0];
      if (pm->winner) {
        over = true;
        for (uint8_t i = 0; i < 60 && poll(); i++) {
          pacDraw(*pm, face, "GAME OVER", C::RED);
          statusBar("PAC-MAN", score, curHigh, C::YELLOW);
          gfx.send();
        }
        again = gameOver(score);
        break;
      }
      pacDraw(*pm, face, pm->freeze && pm->t < RM_FREEZE ? "READY!" : NULL, C::YELLOW);
      statusBar("PAC-MAN", score, curHigh, C::YELLOW);
      gfx.send();
    }
    if (!over) break;                                      // left from the pause menu
  }
  free(pm);
}

// the icon in the library (12 x 12, letters as ICON_KEYS in console.cpp)
static const char *const IC_PACMAN[12] = {
  "............", "..yyy...rrr.", ".yyyyy.rrrrr", "yyyy...rwrwr", "yyy....rrrrr", "yyyy...rrrrr",
  ".yyyyy.rrrrr", "..yyy..r.r.r", "............", ".w..w..w..w.", "............", "bbbbbbbbbbbb" };
