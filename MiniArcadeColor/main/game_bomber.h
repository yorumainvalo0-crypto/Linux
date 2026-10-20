// MiniArcade Color: Bomberman against 1 to 3 CPU players. Part of
// console.cpp, which includes it. The rules and the CPU players are the
// OLED game's (RtBomb in rtgames.h); the 17 x 9 field is drawn with 18 px
// tiles: grass, stone pillars, bricks, power-ups, bombs and fire.
// Arrows walk, OK drops a bomb. Bricks burn and may leave a power-up:
// the bomb = one bomb more, the flame = longer fire. The last one
// standing wins. Points: 10 per brick, 20 per power-up, 100 per player
// caught in your fire and 200 for winning.

#include "rtgames.h"

#define BOMB_T  18                           // tile size
#define BOMB_X0 7
#define BOMB_Y0 (TOP_H + 2)
#define BOMB_HUD (BOMB_Y0 + RB_H * BOMB_T + 2)

static const char *const BOMB_FIG[16] = {
  "......oo......", ".....oooo.....", "....wwwwww....", "...wwwwwwww...", "..wwppppppww..", "..wwpkppkpww..",
  "..wwpkppkpww..", "...wwppppww...", "....wwwwww....", "...oooooooo...", "..woooooooow..", "..w.oooooo.w..",
  "....wwwwww....", "....ww..ww....", "...ooo..ooo...", "...ooo..ooo..." };
static const col BOMB_PCOL[RB_MAXP] = { C::BLUE, C::RED, C::GREEN, C::PURPLE };

static void bombFigure(int16_t x, int16_t y, uint8_t p, bool mirror) {
  const col pal[] = { BOMB_PCOL[p], C::WHITE, rgb(255, 190, 150), C::BLACK };
  gfx.sprite(x, y, BOMB_FIG, 16, "owpk", pal, 1, mirror);
}

static void bombTile(int16_t sx, int16_t sy, uint8_t t, uint8_t x, uint8_t y, uint32_t now) {
  switch (t) {
  case RB_WALL:                                            // stone pillar with a bevel
    gfx.fill(sx, sy, BOMB_T, BOMB_T, rgb(120, 120, 135));
    gfx.fill(sx, sy, BOMB_T, 2, rgb(200, 200, 215));
    gfx.fill(sx, sy, 2, BOMB_T, rgb(200, 200, 215));
    gfx.fill(sx, sy + BOMB_T - 2, BOMB_T, 2, rgb(55, 55, 70));
    gfx.fill(sx + BOMB_T - 2, sy, 2, BOMB_T, rgb(55, 55, 70));
    gfx.fill(sx + 5, sy + 5, BOMB_T - 10, BOMB_T - 10, rgb(100, 100, 115));
    break;
  case RB_BRICK:                                           // red bricks, three rows
    gfx.fill(sx, sy, BOMB_T, BOMB_T, rgb(180, 85, 50));
    for (int16_t r = 0; r < 3; r++) {
      gfx.hline(sx, sy + r * 6 + 5, BOMB_T, rgb(90, 40, 25));
      gfx.hline(sx, sy + r * 6, BOMB_T, rgb(225, 130, 90));
      int16_t off = (r & 1) ? 4 : 13;
      gfx.vline(sx + off, sy + r * 6, 5, rgb(90, 40, 25));
    }
    break;
  case RB_PBOMB:                                           // power-up: one more bomb
  case RB_PFIRE: {                                         // power-up: longer fire
    bool blink = (now / 200) % 2;
    gfx.rbox(sx + 1, sy + 1, BOMB_T - 2, BOMB_T - 2, 4, t == RB_PBOMB ? C::BLUE : C::ORANGE);
    gfx.rframe(sx + 1, sy + 1, BOMB_T - 2, BOMB_T - 2, 4, blink ? C::WHITE : C::YELLOW);
    if (t == RB_PBOMB) {
      gfx.disc(sx + 8, sy + 10, 4, C::BLACK);
      gfx.line(sx + 10, sy + 6, sx + 12, sy + 4, C::YELLOW);
    } else {
      gfx.triangle(sx + 9, sy + 3, sx + 4, sy + 14, sx + 14, sy + 14, C::RED);
      gfx.triangle(sx + 9, sy + 7, sx + 6, sy + 14, sx + 12, sy + 14, C::YELLOW);
    }
    break; }
  default:                                                 // grass, a chequer
    gfx.fill(sx, sy, BOMB_T, BOMB_T, (x + y) & 1 ? rgb(40, 135, 50) : rgb(55, 160, 60));
    break;
  }
}

static void bombFire(const RtBomb &g, int16_t sx, int16_t sy, uint8_t x, uint8_t y, uint32_t now) {
  bool l = x > 0 && g.fire[y][x - 1], r = x < RB_W - 1 && g.fire[y][x + 1];
  bool u = y > 0 && g.fire[y - 1][x], d = y < RB_H - 1 && g.fire[y + 1][x];
  col outer = (now / 60) % 2 ? C::RED : C::ORANGE;
  gfx.fill(sx + 2, sy + 2, BOMB_T - 4, BOMB_T - 4, outer);
  if (l || r) gfx.fill(l ? sx : sx + 3, sy + 3, BOMB_T - (l ? 0 : 3) - (r ? 0 : 3), BOMB_T - 6, outer);
  if (u || d) gfx.fill(sx + 3, u ? sy : sy + 3, BOMB_T - 6, BOMB_T - (u ? 0 : 3) - (d ? 0 : 3), outer);
  if (l || r) gfx.fill(l ? sx : sx + 5, sy + 6, BOMB_T - (l ? 0 : 5) - (r ? 0 : 5), BOMB_T - 12, C::YELLOW);
  if (u || d) gfx.fill(sx + 6, u ? sy : sy + 5, BOMB_T - 12, BOMB_T - (u ? 0 : 5) - (d ? 0 : 5), C::YELLOW);
  gfx.fill(sx + 6, sy + 6, BOMB_T - 12, BOMB_T - 12, C::WHITE);
}

/* the field, the players and the HUD below; msg over the field */
static void bombDraw(const RtBomb &g, const char *msg, uint32_t now) {
  static const char *const NAMES[RB_MAXP] = { "YOU", "CPU1", "CPU2", "CPU3" };
  gfx.fill(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(20, 40, 25));
  for (uint8_t y = 0; y < RB_H; y++)
    for (uint8_t x = 0; x < RB_W; x++) {
      int16_t sx = BOMB_X0 + x * BOMB_T, sy = BOMB_Y0 + y * BOMB_T;
      bombTile(sx, sy, g.tile[y][x], x, y, now);
      if (g.fire[y][x]) bombFire(g, sx, sy, x, y, now);
    }
  for (uint8_t i = 0; i < RB_BOMBS; i++) {                // bombs swell faster before they go off
    const RbBomb &b = g.bomb[i];
    if (!b.t) continue;
    int16_t cx = BOMB_X0 + b.x * BOMB_T + 9, cy = BOMB_Y0 + b.y * BOMB_T + 10;
    uint8_t beat = b.t < 40 ? 4 : 10;
    int16_t r = (b.t / beat) % 2 ? 7 : 6;
    gfx.disc(cx, cy, r, (b.t < 40 && ((b.t / 4) & 1)) ? C::DKRED : C::BLACK);
    gfx.disc(cx - 2, cy - 2, 1, C::GREY);
    gfx.line(cx + 3, cy - r + 1, cx + 5, cy - r - 2, C::BROWN);
    gfx.disc(cx + 6, cy - r - 3, 1, (now / 50) % 2 ? C::YELLOW : C::ORANGE);
  }
  for (uint8_t p = 0; p < g.n; p++) {                     // players, smooth between tiles
    if (!g.alive[p]) continue;
    int16_t off = (int16_t)(g.wait[p] * BOMB_T / RB_STEP);
    int16_t sx = BOMB_X0 + g.px[p] * BOMB_T - RtBomb::ddx(g.dir[p]) * off + 2;
    int16_t sy = BOMB_Y0 + g.py[p] * BOMB_T - RtBomb::ddy(g.dir[p]) * off + 1;
    bombFigure(sx, sy, p, g.dir[p] == 3);
  }
  // HUD: everybody, alive or out; your bombs and fire; the time
  char b[24];
  for (uint8_t p = 0; p < g.n; p++) {
    int16_t x = 4 + p * 79;
    gfx.rbox(x, BOMB_HUD, 76, 22, 5, g.alive[p] ? colDark(BOMB_PCOL[p], 2) : C::DKGREY);
    bombFigure(x + 3, BOMB_HUD + 3, p, false);
    gfx.text(x + 21, BOMB_HUD + 2, NAMES[p], g.alive[p] ? C::WHITE : C::GREY);
    if (!g.alive[p]) gfx.line(x + 2, BOMB_HUD + 20, x + 18, BOMB_HUD + 2, C::RED);
  }
  snprintf(b, sizeof(b), "BOMBS %u   FIRE %u", g.maxB[0], g.range[0]);
  gfx.text(8, BOMB_HUD + 26, b, C::LTGREY, F_SMALL);
  uint16_t left = g.ticks < RB_TIME ? (RB_TIME - g.ticks) / 50 : 0;
  snprintf(b, sizeof(b), "%u:%02u", left / 60, left % 60);
  gfx.right(SCR_W - 8, BOMB_HUD + 25, b, left < 30 ? C::RED : C::YELLOW, F_TEXT);
  if (msg) {
    int16_t w = Gfx::width(msg, F_BOLD) + 20;
    int16_t y = BOMB_Y0 + RB_H * BOMB_T / 2 - 18;
    gfx.rbox((SCR_W - w) / 2, y, w, 34, 8, C::BLACK);
    gfx.rframe((SCR_W - w) / 2, y, w, 34, 8, C::ORANGE);
    gfx.center(y + 5, msg, C::YELLOW, F_BOLD);
  }
}

// the direction held right now (1 up, 2 down, 3 left, 4 right)
static uint8_t bombHeld() {
  if (btnHeld(B_UP)) return 1;
  if (btnHeld(B_DOWN)) return 2;
  if (btnHeld(B_LEFT)) return 3;
  if (btnHeld(B_RIGHT)) return 4;
  return 0;
}

void bomberRun() {
  static const char *const MODES[3] = { "vs 1 CPU", "vs 2 CPUs", "vs 3 CPUs" };
  RtBomb *rb = (RtBomb *)malloc(sizeof(RtBomb));
  if (!rb) return;
  bool again = true;
  while (again) {
    again = false;
    uint8_t mode = chooseMode("BOMBERMAN", MODES, 3, C::ORANGE);
    if (mode == 255) break;
    rb->begin(millis() * 2654435761u, mode + 2);
    bool bombKey = false, quit = false;
    uint32_t next = gameMillis();
    btnClear();
    while (!rb->winner && rb->alive[0]) {
      if (!poll()) { quit = true; break; }
      if (btnTap(B_OK)) bombKey = true;                     // kept until the next tick
      uint32_t now = gameMillis();
      for (uint8_t k = 0; k < 4 && (int32_t)(now - next) >= 0; k++) {   // fixed ticks, as many as time passed
        uint8_t in[RB_MAXP] = { 0, 0, 0, 0 };
        in[0] = bombHeld() | (bombKey ? 8 : 0);
        bombKey = false;
        for (uint8_t p = 1; p < rb->n; p++) in[p] = rb->ai(p);
        uint8_t ev = rb->step(in);
        if (ev & RB_EV_BOOM)  sfx((uint16_t)(90 + random(40)), 120);
        if (ev & RB_EV_BOMB)  sfx(500, 20);
        if (ev & RB_EV_POWER) sfx(1300, 60);
        if (ev & RB_EV_DEAD)  sfx(200, 250);
        next += RT_TICK_MS;
      }
      if ((int32_t)(now - next) > 100) next = now;
      bombDraw(*rb, NULL, now);
      statusBar("BOMBER", rb->score[0], curHigh, C::ORANGE);
      gfx.send();
    }
    if (quit) break;
    bool won = rb->winner == 1;
    uint16_t score = rb->score[0] + (won ? 200 : 0);
    const char *msg = won ? "YOU WIN" : (rb->winner == RB_DRAW && rb->alive[0] ? "TIME UP" : "CAUGHT");
    sfx(won ? 1400 : 200, 300);
    for (uint8_t i = 0; i < 60 && poll(); i++) {
      bombDraw(*rb, msg, gameMillis());
      statusBar("BOMBER", score, curHigh, C::ORANGE);
      gfx.send();
    }
    again = gameOver(score);
  }
  free(rb);
}

// the icon in the library (12 x 12, letters as ICON_KEYS in console.cpp)
static const char *const IC_BOMBER[12] = {
  "........y...", ".......oyo..", "......n.y...", "......n.....", "...kkkkk....", "..kkkkkkk...",
  ".kwkkkkkkk..", ".kwkkkkkkk..", ".kkkkkkkkk..", ".kkkkkkkkk..", "..kkkkkkk...", "...kkkkk...." };
