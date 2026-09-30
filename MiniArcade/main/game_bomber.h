// MiniArcade: Bomberman. Part of MiniArcade.ino, which includes it -
// not compiled on its own. The rules and the CPU players are in rtgames.h.

// =========================================================
//  BOMBERMAN   (against 1 to 3 CPU players)
// =========================================================
/* Arrows walk, OK drops a bomb. Bricks burn and may leave a power-up:
   B = one bomb more, F = longer fire. The last one standing wins.
   Points: 10 per brick, 20 per power-up, 100 per player caught in your
   fire and 200 for winning.                                          */
#include "rtgames.h"
#define RB_T  5                              // tile size
#define RB_Y0 (TOP_H + 1)

static RtBomb rb;

static void rbFigure(int16_t x, int16_t y, bool solid) {
  if (solid) {
    oled.drawBox(x + 1, y, 3, 2);            // head
    oled.drawBox(x, y + 2, 5, 1);            // arms
    oled.drawBox(x + 1, y + 3, 3, 1);
    oled.drawPixel(x + 1, y + 4); oled.drawPixel(x + 3, y + 4);
  } else {
    oled.drawFrame(x + 1, y, 3, 3);
    oled.drawPixel(x, y + 3); oled.drawPixel(x + 4, y + 3);
    oled.drawPixel(x + 1, y + 4); oled.drawPixel(x + 3, y + 4);
  }
}

/* The field and a panel on the right. me = the player shown solid;
   names[p] are shown in the panel.                                   */
void rbDraw(const RtBomb &g, uint8_t me, const char *const *names, const char *msg) {
  for (uint8_t y = 0; y < RB_H; y++)
    for (uint8_t x = 0; x < RB_W; x++) {
      int16_t sx = x * RB_T, sy = RB_Y0 + y * RB_T;
      if (g.fire[y][x]) {                                   // flames
        oled.drawBox(sx + 1, sy + 1, 3, 3);
        oled.drawHLine(sx, sy + 2, 5); oled.drawVLine(sx + 2, sy, 5);
        continue;
      }
      switch (g.tile[y][x]) {
      case RB_WALL:  oled.drawBox(sx, sy, RB_T, RB_T); break;
      case RB_BRICK: oled.drawFrame(sx, sy, RB_T, RB_T); oled.drawPixel(sx + 2, sy + 2); break;
      case RB_PBOMB: oled.drawBox(sx + 1, sy + 1, 3, 3); oled.drawPixel(sx + 3, sy); break;
      case RB_PFIRE: oled.drawHLine(sx, sy + 2, 5); oled.drawVLine(sx + 2, sy, 5); oled.drawPixel(sx, sy); oled.drawPixel(sx + 4, sy + 4); break;
      default: break;
      }
    }
  for (uint8_t i = 0; i < RB_BOMBS; i++) {                  // bombs blink faster before they go off
    const RbBomb &b = g.bomb[i];
    if (!b.t || (b.t < 40 && (b.t / 4) & 1)) continue;
    int16_t sx = b.x * RB_T, sy = RB_Y0 + b.y * RB_T;
    oled.drawBox(sx + 1, sy + 1, 3, 4); oled.drawBox(sx, sy + 2, 5, 2); oled.drawPixel(sx + 3, sy);
  }
  for (uint8_t p = 0; p < g.n; p++)
    if (g.alive[p]) rbFigure(g.px[p] * RB_T, RB_Y0 + g.py[p] * RB_T, p == me);
  // the panel: everybody, alive or out, and your bombs / fire
  int16_t tx = RB_W * RB_T + 3;
  char b[20];
  for (uint8_t k = 0; k < g.n; k++) {
    uint8_t p = (me + k) % g.n;
    snprintf(b, sizeof(b), "%-5.5s%s", names[p], g.alive[p] ? "" : " x");
    oled.drawStr(tx, RB_Y0 + 6 + k * 8, b);
  }
  snprintf(b, sizeof(b), "B%u F%u", g.maxB[me], g.range[me]);
  oled.drawStr(tx, SCR_H - 10, b);
  uint16_t left = g.ticks < RB_TIME ? (RB_TIME - g.ticks) / 50 : 0;
  snprintf(b, sizeof(b), "%u:%02u", left / 60, left % 60);
  oled.drawStr(tx, SCR_H - 1, b);
  if (msg) {
    oled.setFont(FONT_B);
    uint8_t w = oled.getStrWidth(msg) + 8;
    int16_t x0 = (RB_W * RB_T - w) / 2;
    oled.setDrawColor(0); oled.drawBox(x0, 32, w, 15); oled.setDrawColor(1);
    oled.drawFrame(x0, 32, w, 15);
    oled.drawStr(x0 + 4, 44, msg);
    oled.setFont(FONT);
  }
}

// the direction held right now (1 up, 2 down, 3 left, 4 right)
uint8_t rbHeld() {
  if (btnHeld(B_UP)) return 1;
  if (btnHeld(B_DOWN)) return 2;
  if (btnHeld(B_LEFT)) return 3;
  if (btnHeld(B_RIGHT)) return 4;
  return 0;
}

void bomberRun() {
  static const char *const MODES[3] = { "vs 1 CPU", "vs 2 CPUs", "vs 3 CPUs" };
  static const char *const NAMES[4] = { "YOU", "CPU1", "CPU2", "CPU3" };
  bool again = true;
  while (again) {
    again = false;
    uint8_t mode = chooseMode("BOMBERMAN", MODES, 3);
    if (mode == 255) return;
    rb.begin(millis() * 2654435761u, mode + 2);
    bool bombKey = false;
    uint32_t next = gameMillis();
    btnClear();
    while (!rb.winner && rb.alive[0]) {
      if (!poll()) return;
      if (btnTap(B_OK)) bombKey = true;                     // latched until the next tick
      uint32_t now = gameMillis();
      for (uint8_t k = 0; k < 3 && (int32_t)(now - next) >= 0; k++) {
        uint8_t in[RB_MAXP] = { 0, 0, 0, 0 };
        in[0] = rbHeld() | (bombKey ? 8 : 0);
        bombKey = false;
        for (uint8_t p = 1; p < rb.n; p++) in[p] = rb.ai(p);
        uint8_t ev = rb.step(in);
        if (ev & RB_EV_BOOM)  sfx(90 + random(40), 120);
        if (ev & RB_EV_BOMB)  sfx(500, 20);
        if (ev & RB_EV_POWER) sfx(1300, 60);
        if (ev & RB_EV_DEAD)  sfx(200, 250);
        next += RT_TICK_MS;
      }
      if ((int32_t)(now - next) > 100) next = now;
      statusBar("BOMBER", rb.score[0], curHigh);
      rbDraw(rb, 0, NAMES, NULL);
      oled.sendBuffer();
    }
    bool won = rb.winner == 1;
    uint16_t score = rb.score[0] + (won ? 200 : 0);
    const char *msg = won ? "YOU WIN" : (rb.winner == RB_DRAW && rb.alive[0] ? "TIME UP" : "CAUGHT");
    sfx(won ? 1400 : 200, 300);
    for (uint8_t i = 0; i < 60 && poll(); i++) {
      statusBar("BOMBER", score, curHigh);
      rbDraw(rb, 0, NAMES, msg);
      oled.sendBuffer();
    }
    again = gameOver(score);
  }
}
