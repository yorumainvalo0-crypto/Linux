// MiniArcade: Checkers. Part of MiniArcade.ino, which includes it -
#include "boardgames.h"
// not compiled on its own. The rules are in boardgames.h.

// =========================================================
//  CHECKERS   (against the CPU or two players on one console)
// =========================================================
/* The board fills the whole height (8 x 8 squares of 8 px), the right
   side shows whose turn it is. Move the cursor with the arrows, OK picks
   a piece and OK on a marked square moves it; a take is a must and goes
   on as long as the piece can take. Own men are solid discs, kings have
   a dark middle; the other side's pieces are rings.                   */
#define CK_CELL 8

static Checkers ck;
static uint8_t  ckCur = 52;            // cursor, 0..63 on the screen (row * 8 + column)
static int8_t   ckSel = -1;            // picked piece (0..31), -1 = none

// screen square <-> board square, the "me" side is always at the bottom
static int8_t ckFromScreen(uint8_t s, uint8_t me) {
  int8_t r = s / 8, c = s % 8;
  if (me) { r = 7 - r; c = 7 - c; }
  return Checkers::at(r, c);
}
static void ckPiece(int16_t x, int16_t y, uint8_t p, bool mine) {
  oled.setDrawColor(0); oled.drawBox(x + 1, y + 1, 7, 7); oled.setDrawColor(1);   // no dots of the square around it
  if (mine) {
    oled.drawBox(x + 2, y + 1, 4, 6); oled.drawBox(x + 1, y + 2, 6, 4);
    if (Checkers::king(p)) { oled.setDrawColor(0); oled.drawBox(x + 3, y + 3, 2, 2); oled.setDrawColor(1); }
  } else {
    oled.drawHLine(x + 2, y + 1, 4); oled.drawHLine(x + 2, y + 6, 4);
    oled.drawVLine(x + 1, y + 2, 4); oled.drawVLine(x + 6, y + 2, 4);
    if (Checkers::king(p)) oled.drawBox(x + 3, y + 3, 2, 2);
  }
}

/* The board as "me" sees it. cursor < 0: none; line1 / line2: right side. */
void ckDraw(const Checkers &b, uint8_t me, int8_t cursor, int8_t sel, const char *line1, const char *line2) {
  CkStep st[48];
  uint8_t n = (sel >= 0) ? b.gen(st) : 0;
  for (uint8_t s = 0; s < 64; s++) {
    int16_t x = (s % 8) * CK_CELL, y = (s / 8) * CK_CELL;
    int8_t i = ckFromScreen(s, me);
    if (i < 0) continue;                                        // light square: empty
    for (uint8_t a = 0; a < 4; a++)                             // dark squares: a dot pattern
      for (uint8_t d = 0; d < 4; d++) if (!((a + d) & 1)) oled.drawPixel(x + 1 + a * 2, y + 1 + d * 2);
    uint8_t p = b.sq[i];
    if (p) ckPiece(x, y, p, Checkers::owner(p) == me);
    for (uint8_t k = 0; k < n; k++)                             // where the picked piece may go
      if (st[k].from == sel && Checkers::next(sel, st[k].dir, st[k].jump ? 2 : 1) == i)
        oled.drawBox(x + 3, y + 3, 2, 2);
    if (i == sel && ((millis() / 200) & 1)) oled.drawFrame(x, y, CK_CELL, CK_CELL);
  }
  oled.drawFrame(0, 0, 64, 64);
  if (cursor >= 0) {                                            // cursor: corners of the square
    int16_t x = (cursor % 8) * CK_CELL, y = (cursor / 8) * CK_CELL;
    oled.drawHLine(x, y, 3); oled.drawVLine(x, y, 3);
    oled.drawHLine(x + 5, y, 3); oled.drawVLine(x + 7, y, 3);
    oled.drawHLine(x, y + 7, 3); oled.drawVLine(x, y + 5, 3);
    oled.drawHLine(x + 5, y + 7, 3); oled.drawVLine(x + 7, y + 5, 3);
  }
  oled.setFont(FONT_B);
  oled.drawStr(67, 12, "CHECKERS");
  oled.setFont(FONT);
  char buf[20];
  snprintf(buf, sizeof(buf), "%u : %u", b.count(me), b.count(me ^ 1));
  oled.drawStr(68, 28, buf);
  if (line1) oled.drawStr(68, 44, line1);
  if (line2) oled.drawStr(68, 54, line2);
}

/* The keys for the player "me". Returns a step as (from | dir << 5) when
   one was made, -1 otherwise. ckCur / ckSel keep the cursor and pick. */
int16_t ckInput(const Checkers &b, uint8_t me) {
  int8_t r = ckCur / 8, c = ckCur % 8;
  if (btn(B_UP)    && r > 0) { r--; sfx(700, 8); }
  if (btn(B_DOWN)  && r < 7) { r++; sfx(700, 8); }
  if (btn(B_LEFT)  && c > 0) { c--; sfx(700, 8); }
  if (btn(B_RIGHT) && c < 7) { c++; sfx(700, 8); }
  ckCur = r * 8 + c;
  if (b.chain >= 0) ckSel = b.chain;                            // taking on: the piece stays picked
  if (!btnTap(B_OK)) return -1;
  int8_t i = ckFromScreen(ckCur, me);
  if (i < 0) { sfx(250, 40); return -1; }
  CkStep st[48];
  uint8_t n = b.gen(st);
  if (ckSel >= 0)                                               // a target of the picked piece?
    for (uint8_t k = 0; k < n; k++)
      if (st[k].from == ckSel && Checkers::next(ckSel, st[k].dir, st[k].jump ? 2 : 1) == i)
        return ckSel | (st[k].dir << 5);
  if (b.chain < 0) {
    if (i == ckSel) { ckSel = -1; sfx(500, 20); return -1; }    // put it back
    for (uint8_t k = 0; k < n; k++) if (st[k].from == i) { ckSel = i; sfx(900, 20); return -1; }
  }
  sfx(250, 40);                                                 // nothing to do there
  return -1;
}

void checkersRun() {
  static const char *const MODES[4] = { "1P easy", "1P normal", "1P hard", "2 players" };
  bool again = true;
  while (again) {
    again = false;
    uint8_t mode = chooseMode("CHECKERS", MODES, 4);
    if (mode == 255) return;
    bool twoP = mode == 3;
    uint32_t rnd = millis() | 1;
    ck.begin();
    ckCur = 52; ckSel = -1;
    btnClear();
    while (!ck.winner) {
      if (!poll()) return;
      bool cpu = !twoP && ck.turn == 1;
      if (!cpu) {
        int16_t m = ckInput(ck, 0);                             // two players: both from the bottom view
        if (m >= 0) {
          if (!ck.apply(m & 31, m >> 5)) ckSel = -1;            // the turn is over
          sfx(600, 30);
        }
        const char *who = twoP ? (ck.turn ? "top's go" : "bottom's go") : "your go";
        ckDraw(ck, 0, ckCur, ckSel, who, ck.chain >= 0 ? "take on!" : NULL);
        oled.sendBuffer();
      } else {                                                  // the CPU: shown thinking, then moves
        ckDraw(ck, 0, -1, -1, "thinking...", NULL);
        oled.sendBuffer();
        static const uint8_t DEPTH[3] = { 1, 3, 5 };
        CkStep s = ck.best(DEPTH[mode], rnd);
        if (mode == 0 && bgRand(rnd) % 3 == 0) {                // easy: now and then any move
          CkStep all[48];
          uint8_t n = ck.gen(all);
          s = all[bgRand(rnd) % n];
        }
        uint32_t t0 = millis();
        while (millis() - t0 < 350) { if (!poll()) return; ckDraw(ck, 0, -1, s.from, "thinking...", NULL); oled.sendBuffer(); }
        ck.apply(s.from, s.dir);
        sfx(400, 30);
      }
    }
    uint16_t score = 0;
    const char *msg = ck.winner == 3 ? "DRAW" : (twoP ? (ck.winner == 1 ? "BOTTOM WINS" : "TOP WINS") : (ck.winner == 1 ? "YOU WIN" : "CPU WINS"));
    if (!twoP) {
      static const uint16_t BASE[3] = { 60, 120, 200 };
      if (ck.winner == 1) score = BASE[mode] + 10 * ck.count(0);
      if (ck.winner == 3) score = BASE[mode] / 3;
    }
    sfx(ck.winner == 1 ? 1400 : 250, 250);
    for (uint8_t i = 0; i < 60 && poll(); i++) { ckDraw(ck, 0, -1, -1, msg, NULL); oled.sendBuffer(); }
    again = gameOver(score);
  }
}
