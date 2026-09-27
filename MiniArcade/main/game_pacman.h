// MiniArcade: Pac-Man. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  PAC-MAN   (the rules are in rtgames.h: the same game runs online)
// =========================================================
#include "rtgames.h"

static RtPac pm;

// maze walls as thin lines: a line between two wall tiles, but only where
// it borders a corridor (so solid blocks stay hollow)
static bool pmOpen(int x, int y) { return y >= 0 && y < RM_H && !RtPac::wallAt(x, y); }

static void pmMaze() {
  for (uint8_t y = 0; y < RM_H; y++)
    for (uint8_t x = 0; x < RM_W; x++) {
      if (!RtPac::wallAt(x, y)) continue;
      int16_t px = x * 4 + 1, py = TOP_H + y * 4 + 1;
      if (RtPac::doorAt(x, y)) { oled.drawPixel(px, py + 1); oled.drawPixel(px + 2, py + 1); continue; }
      bool edge = false;
      for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (pmOpen(x + dx, y + dy)) edge = true;
      if (edge) oled.drawPixel(px, py);
      if (x + 1 < RM_W && RtPac::wallAt(x + 1, y) && !RtPac::doorAt(x + 1, y) &&
          (pmOpen(x, y - 1) || pmOpen(x + 1, y - 1) || pmOpen(x, y + 1) || pmOpen(x + 1, y + 1)))
        oled.drawHLine(px, py, 5);
      if (y + 1 < RM_H && RtPac::wallAt(x, y + 1) &&
          (pmOpen(x - 1, y) || pmOpen(x - 1, y + 1) || pmOpen(x + 1, y) || pmOpen(x + 1, y + 1)))
        oled.drawVLine(px, py, 5);
    }
}

static void pmPac(const RmEnt &e, bool solid, uint32_t t) {
  int px, py;
  pm.pixel(e, pm.pacPeriod(), &px, &py);
  py += TOP_H;
  if (solid) oled.drawBox(px, py, 3, 3); else oled.drawFrame(px, py, 3, 3);
  if (e.dir && (t / 6) % 2) {                     // mouth
    oled.setDrawColor(0);
    oled.drawPixel(px + 1, py + 1);
    oled.drawPixel(px + 1 + RtPac::dx(e.dir), py + 1 + RtPac::dy(e.dir));
    oled.setDrawColor(1);
  }
}

static void pmGhost(const RmEnt &g, uint32_t t) {
  int px, py;
  pm.pixel(g, pm.ghostPeriod(g), &px, &py);
  py += TOP_H;
  if (g.state == RG_EYES) { oled.drawPixel(px, py); oled.drawPixel(px + 2, py); return; }
  if (g.state == RG_FRIGHT && pm.fright < 90 && (t / 8) % 2) return;   // blinks: nearly over
  oled.drawPixel(px + 1, py);
  if (g.state == RG_FRIGHT) { oled.drawPixel(px, py + 1); oled.drawPixel(px + 2, py + 1); }
  else oled.drawHLine(px, py + 1, 3);
  oled.drawPixel(px, py + 2); oled.drawPixel(px + 2, py + 2);
}

/* the whole picture; me = our Pac-Man (online), bar drawn by the caller */
void pmDraw(uint8_t me, const char *msg) {
  pmMaze();
  for (uint8_t y = 0; y < RM_H; y++)
    for (uint8_t x = 0; x < RM_W; x++) {
      uint32_t bit = 1UL << x;
      if (pm.dots[y] & bit) oled.drawPixel(x * 4 + 1, TOP_H + y * 4 + 1);
      if ((pm.power[y] & bit) && (pm.t / 10) % 2 == 0) {        // power pellet: a small cross
        oled.drawHLine(x * 4, TOP_H + y * 4 + 1, 3);
        oled.drawVLine(x * 4 + 1, TOP_H + y * 4, 3);
      }
    }
  for (uint8_t g = 0; g < RM_GH; g++) pmGhost(pm.gh[g], pm.t);
  for (uint8_t p = 0; p < pm.np; p++)
    if (pm.alive(p) && (!pm.freeze || (pm.freeze / 6) % 2 == 0 || pm.t < RM_FREEZE)) pmPac(pm.pac[p], p == me, pm.t);
  if (msg) {
    oled.setFont(FONT_B);
    uint8_t w = oled.getStrWidth(msg) + 8;
    oled.setDrawColor(0);
    oled.drawBox((SCR_W - w) / 2, 31, w, 15);
    oled.setDrawColor(1);
    oled.drawFrame((SCR_W - w) / 2, 31, w, 15);
    centerStr(43, msg);
    oled.setFont(FONT);
  }
  oled.sendBuffer();
}

void pacmanRun() {
  bool again = true;
  while (again) {
    again = false;
    pm.begin(esp_random(), 1);
    uint8_t  latch = 0, lastLives = pm.lives[0], lastLevel = 0;
    uint32_t next = gameMillis();
    btnClear();

    while (poll()) {
      if (btn(B_UP))    latch = 1;
      if (btn(B_DOWN))  latch = 2;
      if (btn(B_LEFT))  latch = 3;
      if (btn(B_RIGHT)) latch = 4;
      uint32_t now = gameMillis();
      for (uint8_t k = 0; k < 3 && (int32_t)(now - next) >= 0; k++) {
        uint8_t in[2] = { latch, 0 };
        uint8_t ev = pm.step(in);
        next += RT_TICK_MS;
        if (ev & RT_EV_EAT) sfx(pm.t % 2 ? 880 : 660, 12);
        if (ev & RT_EV_HIT) sfx(1300, 60);
      }
      if ((int32_t)(now - next) > 100) next = now;
      if (pm.lives[0] < lastLives) { lastLives = pm.lives[0]; sfx(200, 400); }
      if (pm.level != lastLevel)   { lastLevel = pm.level; sfx(1500, 200); }
      uint16_t score = pm.score[0];
      if (pm.winner) {
        for (uint8_t i = 0; i < 60 && poll(); i++) {
          statusBar("PAC-MAN", score, curHigh);
          pmDraw(0, "GAME OVER");
        }
        again = gameOver(score);
        break;
      }
      // bar: title, score, lives as small dots, level
      char b[16];
      oled.setFont(FONT_B);
      oled.drawStr(2, 12, "PAC-MAN");
      oled.setFont(FONT);
      snprintf(b, sizeof(b), "%u", score);
      oled.drawStr(58, 12, b);
      snprintf(b, sizeof(b), "L%u", pm.level + 1);
      rightStr(12, b);
      for (uint8_t i = 1; i < pm.lives[0]; i++) oled.drawBox(SCR_W - 18 - i * 5, 8, 3, 3);
      oled.drawHLine(0, TOP_H - 1, SCR_W);
      pmDraw(0, pm.freeze && pm.t < RM_FREEZE ? "READY!" : NULL);
    }
  }
}
