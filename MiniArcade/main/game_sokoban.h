// MiniArcade: Sokoban. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  SOKOBAN   (push every box onto a goal)
// =========================================================
/* Levels: '#' wall, '.' goal, '$' box, '*' box on a goal, '@' player,
   '+' player on a goal, rows split by '|'. They were made by a generator
   that pulls the boxes backwards from the goals; the PC test solves
   every one of them, so none is impossible. OK takes a move back.     */
#define SK_W   21
#define SK_H    8
#define SK_C    6             // tile size in px
#define SK_UNDO 128

enum { SK_WALL = 1, SK_GOAL = 2, SK_BOX = 4 };

static const char *const SK_LEVELS[] = {
#include "sokoban_levels.h"
};
static const uint8_t SK_N = sizeof(SK_LEVELS) / sizeof(SK_LEVELS[0]);

static uint8_t  sk[SK_H][SK_W];
static uint8_t  skW, skH, skX, skY;              // size, player
static uint8_t  skUndo[SK_UNDO];                 // bits 0-1 direction, bit 2 = pushed a box
static uint16_t skUndoN, skMoves;
static uint8_t  skLast = 0;                      // level shown first next time

bool skLoad(uint8_t lvl) {
  memset(sk, 0, sizeof(sk));
  skW = skH = 0;
  uint8_t x = 0, y = 0;
  for (const char *p = SK_LEVELS[lvl]; ; p++) {
    if (*p == '|' || !*p) {
      if (x > skW) skW = x;
      y++; x = 0;
      if (!*p || y >= SK_H) break;
      continue;
    }
    if (x >= SK_W) continue;
    uint8_t t = 0;
    switch (*p) {
    case '#': t = SK_WALL; break;
    case '.': t = SK_GOAL; break;
    case '$': t = SK_BOX; break;
    case '*': t = SK_BOX | SK_GOAL; break;
    case '+': t = SK_GOAL; /* fall through */
    case '@': skX = x; skY = y; break;
    }
    sk[y][x++] = t;
  }
  skH = y;
  skUndoN = skMoves = 0;
  return skW && skH;
}

static const int8_t SK_DX[4] = { 0, 0, -1, 1 }, SK_DY[4] = { -1, 1, 0, 0 };   // up down left right

// one step; returns 0 = blocked, 1 = walked, 2 = pushed a box
uint8_t skStep(uint8_t d) {
  int8_t nx = skX + SK_DX[d], ny = skY + SK_DY[d];
  if (nx < 0 || ny < 0 || nx >= SK_W || ny >= SK_H || (sk[ny][nx] & SK_WALL)) return 0;
  uint8_t r = 1;
  if (sk[ny][nx] & SK_BOX) {
    int8_t bx = nx + SK_DX[d], by = ny + SK_DY[d];
    if (bx < 0 || by < 0 || bx >= SK_W || by >= SK_H || (sk[by][bx] & (SK_WALL | SK_BOX))) return 0;
    sk[ny][nx] &= ~SK_BOX;
    sk[by][bx] |= SK_BOX;
    r = 2;
  }
  skX = nx; skY = ny;
  skMoves++;
  if (skUndoN == SK_UNDO) { memmove(skUndo, skUndo + 1, SK_UNDO - 1); skUndoN--; }   // forget the oldest
  skUndo[skUndoN++] = d | (r == 2 ? 4 : 0);
  return r;
}

static bool skBack() {
  if (!skUndoN) return false;
  uint8_t u = skUndo[--skUndoN], d = u & 3;
  int8_t px = skX - SK_DX[d], py = skY - SK_DY[d];
  if (u & 4) {                                   // pull the box back
    int8_t bx = skX + SK_DX[d], by = skY + SK_DY[d];
    sk[by][bx] &= ~SK_BOX;
    sk[skY][skX] |= SK_BOX;
  }
  skX = px; skY = py;
  if (skMoves) skMoves--;
  return true;
}

bool skSolved() {
  for (uint8_t y = 0; y < SK_H; y++)
    for (uint8_t x = 0; x < SK_W; x++)
      if ((sk[y][x] & SK_BOX) && !(sk[y][x] & SK_GOAL)) return false;
  return true;
}

static void skDrawMap() {
  int16_t ox = (SCR_W - skW * SK_C) / 2, oy = TOP_H + (SCR_H - TOP_H - skH * SK_C) / 2;
  for (uint8_t y = 0; y < skH; y++)
    for (uint8_t x = 0; x < skW; x++) {
      int16_t px = ox + x * SK_C, py = oy + y * SK_C;
      uint8_t t = sk[y][x];
      if (t & SK_WALL) {                          // wall: grey (every other pixel)
        for (uint8_t r = 0; r < SK_C; r++)
          for (uint8_t c = (r & 1); c < SK_C; c += 2) oled.drawPixel(px + c, py + r);
      } else if (t & SK_BOX) {
        oled.drawFrame(px, py, SK_C, SK_C);
        if (t & SK_GOAL) oled.drawBox(px + 2, py + 2, 2, 2);         // done: filled centre
        else { oled.drawPixel(px + 2, py + 2); oled.drawPixel(px + 3, py + 3); }
      } else if (t & SK_GOAL) {
        oled.drawFrame(px + 2, py + 2, 2, 2);
      }
      if (x == skX && y == skY) {                 // player: a round blob
        oled.drawHLine(px + 2, py, 2);
        oled.drawBox(px + 1, py + 1, 4, 3);
        oled.drawPixel(px + 1, py + 5); oled.drawPixel(px + 4, py + 5);
        oled.drawHLine(px + 2, py + 4, 2);
      }
    }
}

static void skBar(uint8_t lvl, const char *right) {
  char b[24];
  oled.setFont(FONT_B);
  snprintf(b, sizeof(b), "SOKOBAN %u", lvl + 1);
  oled.drawStr(2, 12, b);
  oled.setFont(FONT);
  rightStr(12, right);
  oled.drawHLine(0, TOP_H - 1, SCR_W);
}

// the level list: LEFT / RIGHT through the unlocked ones, OK plays
static uint8_t skChoose() {
  uint8_t open = curHigh < SK_N ? curHigh : SK_N - 1;       // curHigh = levels solved
  uint8_t lvl = skLast > open ? open : skLast;
  skLoad(lvl);
  btnClear();
  while (poll()) {
    if (btn(B_LEFT)  && lvl)        { lvl--; skLoad(lvl); sfx(700, 15); }
    if (btn(B_RIGHT) && lvl < open) { lvl++; skLoad(lvl); sfx(700, 15); }
    if (btn(B_OK)) { sfx(1200, 50); return lvl; }
    char b[16];
    snprintf(b, sizeof(b), "<%u/%u> OK", lvl + 1, SK_N);
    skBar(lvl, b);
    skDrawMap();
    oled.sendBuffer();
  }
  return 255;
}

void sokobanRun() {
  uint8_t lvl = skChoose();
  while (lvl != 255) {
    skLast = lvl;
    skLoad(lvl);
    btnClear();
    bool done = false;
    while (!done) {
      if (!poll()) return;
      static const uint8_t KEYS[4] = { B_UP, B_DOWN, B_LEFT, B_RIGHT };
      for (uint8_t d = 0; d < 4; d++)
        if (btn(KEYS[d])) {
          uint8_t r = skStep(d);
          sfx(r == 2 ? 450 : (r ? 700 : 200), r ? 12 : 30);
        }
      if (btn(B_OK) && skBack()) sfx(500, 20);
      char b[16];
      snprintf(b, sizeof(b), "%u moves", skMoves);
      skBar(lvl, b);
      skDrawMap();
      oled.sendBuffer();
      done = skSolved();
    }
    bool record = lvl + 1 > curHigh;              // best = levels solved
    if (record) { curHigh = lvl + 1; saveHigh(curGame, curHigh); }
    sfx(1400, 250);
    for (uint8_t i = 0; i < 150; i++) {
      if (!poll()) return;
      skBar(lvl, lvl + 1 == SK_N ? "ALL DONE!" : "SOLVED!");
      skDrawMap();
      oled.sendBuffer();
      if (i > 30 && btn(B_OK)) break;
    }
    awardCheck(record);
    if (lvl + 1 < SK_N) lvl++;                    // on to the next one
    else lvl = skChoose();
  }
}
