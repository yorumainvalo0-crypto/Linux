// MiniArcade Color: Tic Tac Toe against the CPU or two players on one
// console. Part of console.cpp, which includes it.
// The arrows move the cursor, OK sets the mark. The machine looks at every
// possible game (there are few enough), so on "hard" it never loses; the
// easier levels mix in random moves. Starting alternates between the
// rounds. A win is worth 100, a draw 20; a loss against the CPU ends it.
// (Prefix xo: Tetris has tt.)

#define XO_CELL 62
#define XO_X    14                     // the board, top left
#define XO_Y    38
#define XO_PX   214                    // the panel on the right

static uint8_t xo[9];                  // 0 empty, 1 = X (player one), 2 = O

static const uint8_t XO_LINES[8][3] = {
  {0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}
};

static int8_t xoLine(uint8_t who) {    // index of a completed line, or -1
  for (uint8_t i = 0; i < 8; i++)
    if (xo[XO_LINES[i][0]] == who && xo[XO_LINES[i][1]] == who && xo[XO_LINES[i][2]] == who)
      return i;
  return -1;
}

static bool xoFull() {
  for (uint8_t i = 0; i < 9; i++) if (!xo[i]) return false;
  return true;
}

// score from the view of "me": +10 win (earlier is better), -10 loss, 0 draw
static int8_t xoMinimax(uint8_t me, uint8_t turn, uint8_t depth) {
  if (xoLine(me) >= 0)     return 10 - depth;
  if (xoLine(3 - me) >= 0) return depth - 10;
  if (xoFull())            return 0;
  int8_t best = (turn == me) ? -100 : 100;
  for (uint8_t i = 0; i < 9; i++) {
    if (xo[i]) continue;
    xo[i] = turn;
    int8_t v = xoMinimax(me, 3 - turn, depth + 1);
    xo[i] = 0;
    if (turn == me ? v > best : v < best) best = v;
  }
  return best;
}

/* level 0 = easy, 1 = normal, 2 = hard; "me" is the side to move */
static uint8_t xoThink(uint8_t level, uint8_t me) {
  uint8_t fr[9], n = 0;
  for (uint8_t i = 0; i < 9; i++) if (!xo[i]) fr[n++] = i;
  if (n == 9) {                                 // empty board: every start draws
    static const uint8_t first[5] = { 0, 2, 4, 6, 8 };
    return first[random(5)];
  }
  for (uint8_t k = 0; k < n; k++) {             // a win in one is always taken
    xo[fr[k]] = me;
    bool win = xoLine(me) >= 0;
    xo[fr[k]] = 0;
    if (win) return fr[k];
  }
  uint8_t chance = (level == 0) ? 60 : (level == 1 ? 20 : 0);
  if (random(100) < chance) return fr[random(n)];
  int8_t bestV = -100;
  uint8_t best[9], m = 0;
  for (uint8_t k = 0; k < n; k++) {
    xo[fr[k]] = me;
    int8_t v = xoMinimax(me, 3 - me, 1);
    xo[fr[k]] = 0;
    if (v > bestV) { bestV = v; m = 0; }
    if (v == bestV) best[m++] = fr[k];
  }
  return best[random(m)];                       // equal moves: pick any
}

// ---------------- screen ----------------
static const col XO_COL[3] = { C::BLACK, rgb(255, 85, 85), rgb(51, 204, 255) };
static struct { uint8_t mode, tally[3], last; uint16_t score; uint32_t lastAt; bool two; } xoV;

// a thick line: a round brush along it
static void xoThick(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t r, col c) {
  int16_t dx = x1 - x0, dy = y1 - y0;
  int16_t n = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
  if (!n) { gfx.disc(x0, y0, r, c); return; }
  for (int16_t i = 0; i <= n; i += 2) gfx.disc(x0 + dx * i / n, y0 + dy * i / n, r, c);
  gfx.disc(x1, y1, r, c);
}

// a mark centred on (cx, cy), size s = half its width; bg = the cell behind it
static void xoMark(int16_t cx, int16_t cy, uint8_t who, int16_t s, col bg) {
  if (s < 2) return;
  col c = XO_COL[who];
  if (who == 1) {
    xoThick(cx - s + 2, cy - s + 4, cx + s + 2, cy + s + 4, s / 5 + 1, colDark(c, 2));   // shadow
    xoThick(cx + s + 2, cy - s + 4, cx - s + 2, cy + s + 4, s / 5 + 1, colDark(c, 2));
    xoThick(cx - s, cy - s, cx + s, cy + s, s / 5 + 1, c);
    xoThick(cx + s, cy - s, cx - s, cy + s, s / 5 + 1, c);
  } else {
    int16_t w = s / 3 + 1;
    gfx.disc(cx + 2, cy + 3, s + 2, colDark(c, 2));
    gfx.disc(cx, cy, s + 2, c);
    gfx.disc(cx, cy, s + 2 - w, bg);
    gfx.circle(cx, cy, s, colLight(c));
  }
}

static int16_t xoCx(uint8_t i) { return XO_X + (i % 3) * XO_CELL + XO_CELL / 2; }
static int16_t xoCy(uint8_t i) { return XO_Y + (i / 3) * XO_CELL + XO_CELL / 2; }

/* cur < 9: cursor; line >= 0: strike through it; turn: whose go (0 = none);
   note: under the players; msg: big banner over the board              */
static void xoDraw(uint8_t cur, int8_t line, uint8_t turn, const char *note, const char *msg) {
  statusBar("TIC TAC TOE", xoV.score, curHigh, C::PINK);
  gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(60, 20, 70), rgb(10, 0, 20));
  const col CELL = rgb(40, 20, 60), HI = rgb(90, 40, 110);
  gfx.rbox(XO_X - 6 + 4, XO_Y - 6 + 4, 3 * XO_CELL + 12, 3 * XO_CELL + 12, 10, C::BLACK);
  gfx.rbox(XO_X - 6, XO_Y - 6, 3 * XO_CELL + 12, 3 * XO_CELL + 12, 10, rgb(220, 200, 230));
  uint32_t now = gameMillis();
  for (uint8_t i = 0; i < 9; i++) {
    int16_t x = XO_X + (i % 3) * XO_CELL, y = XO_Y + (i / 3) * XO_CELL;
    bool on = i == cur;
    col bg = on ? HI : CELL;
    gfx.rbox(x + 2, y + 2, XO_CELL - 4, XO_CELL - 4, 6, bg);
    if (on) {                                                   // cursor: bright corners
      col k = XO_COL[turn ? turn : 1];
      int16_t e = XO_CELL - 4;
      gfx.fill(x + 2, y + 2, 12, 3, k);          gfx.fill(x + 2, y + 2, 3, 12, k);
      gfx.fill(x + e - 10, y + 2, 12, 3, k);     gfx.fill(x + e - 1, y + 2, 3, 12, k);
      gfx.fill(x + 2, y + e - 1, 12, 3, k);      gfx.fill(x + 2, y + e - 10, 3, 12, k);
      gfx.fill(x + e - 10, y + e - 1, 12, 3, k); gfx.fill(x + e - 1, y + e - 10, 3, 12, k);
      if (!xo[i] && turn) xoMark(xoCx(i), xoCy(i), turn, 10, bg);   // where it would go
    }
    if (xo[i]) {
      int16_t s = 18;
      if (i == xoV.last && now - xoV.lastAt < 200) s = (int16_t)(4 + (now - xoV.lastAt) * 14 / 200);   // grows in
      xoMark(xoCx(i), xoCy(i), xo[i], s, bg);
    }
  }
  if (line >= 0) {                                              // strike through the row
    const uint8_t *L = XO_LINES[line];
    int16_t x0 = xoCx(L[0]), y0 = xoCy(L[0]), x1 = xoCx(L[2]), y1 = xoCy(L[2]);
    int16_t ex = (x1 - x0) / 5, ey = (y1 - y0) / 5;
    xoThick(x0 - ex, y0 - ey, x1 + ex, y1 + ey, 4, C::YELLOW);
    xoThick(x0 - ex, y0 - ey, x1 + ex, y1 + ey, 1, C::WHITE);
  }
  // panel: both players with their rounds won
  for (uint8_t p = 1; p <= 2; p++) {
    int16_t y = 34 + (p - 1) * 70;
    bool act = turn == p && !msg;
    gfx.rbox(XO_PX, y, 98, 62, 8, act ? colDark(XO_COL[p], 2) : rgb(30, 10, 40));
    if (act) gfx.rframe(XO_PX, y, 98, 62, 8, XO_COL[p]);
    xoMark(XO_PX + 22, y + 31, p, 12, act ? colDark(XO_COL[p], 2) : rgb(30, 10, 40));
    const char *name = xoV.two ? (p == 1 ? "X" : "O") : (p == 1 ? "YOU" : "CPU");
    gfx.text(XO_PX + 46, y + 8, name, C::WHITE, F_TEXT);
    char b[8];
    snprintf(b, sizeof(b), "%u", xoV.tally[p]);
    gfx.text(XO_PX + 46, y + 32, b, C::YELLOW, F_TEXT);
    gfx.text(XO_PX + 46 + Gfx::width(b, F_TEXT) + 4, y + 38, "won", C::LTGREY, F_SMALL);
  }
  char b[16];
  snprintf(b, sizeof(b), "draws %u", xoV.tally[0]);
  gfx.text(XO_PX + 8, 180, b, C::LTGREY, F_SMALL);
  if (!xoV.two) {
    static const char *const LV[3] = { "level easy", "level normal", "level hard" };
    gfx.text(XO_PX + 8, 192, LV[xoV.mode], C::LTGREY, F_SMALL);
  }
  if (note) gfx.text(XO_PX + 8, 212, note, C::YELLOW, F_SMALL);
  if (msg) {
    int16_t w = Gfx::width(msg, F_BOLD) + 36, x = XO_X + (3 * XO_CELL - w) / 2;
    gfx.rbox(x + 4, 120, w, 42, 10, C::BLACK);
    gfx.rbox(x, 116, w, 42, 10, rgb(80, 0, 80));
    gfx.rframe(x, 116, w, 42, 10, C::PINK);
    gfx.text(x + 18, 125, msg, C::WHITE, F_BOLD);
  }
}

// sets a mark, with the little grow animation
static void xoSet(uint8_t i, uint8_t who) { xo[i] = who; xoV.last = i; xoV.lastAt = gameMillis(); }

void tictactoeRun() {
  static const char *const modes[4] = { "1P easy", "1P normal", "1P hard", "2 players" };
  uint8_t mode = chooseMode("TIC TAC TOE", modes, 4, C::PINK);
  if (mode == 255) return;
  xoV.two = mode == 3;
  xoV.mode = mode < 3 ? mode : 0;

  bool again = true;
  while (again) {
    again = false;
    xoV.score = 0;
    memset(xoV.tally, 0, sizeof(xoV.tally));
    uint8_t starter = 1, cur = 4;
    bool over = false;
    while (!over) {                              // one round after the other
      memset(xo, 0, sizeof(xo));
      xoV.last = 9;
      uint8_t turn = starter;
      int8_t  line = -1;
      const char *msg = NULL;
      btnClear();
      while (true) {
        if (!xoV.two && turn == 2) {             // the machine moves after a short pause
          uint32_t t0 = gameMillis();
          while (gameMillis() - t0 < 650) {
            if (!poll()) return;
            xoDraw(9, -1, 2, "thinking...", NULL);
            gfx.send();
          }
          xoSet(xoThink(mode, 2), 2);
          sfx(350, 40);
          btnClear();
        } else {
          if (!poll()) return;                   // left from the pause menu
          if (btn(B_LEFT)  && cur % 3)     { cur--;    sfx(700, 15); }
          if (btn(B_RIGHT) && cur % 3 < 2) { cur++;    sfx(700, 15); }
          if (btn(B_UP)    && cur > 2)     { cur -= 3; sfx(700, 15); }
          if (btn(B_DOWN)  && cur < 6)     { cur += 3; sfx(700, 15); }
          const char *who = xoV.two ? (turn == 1 ? "player X" : "player O") : "you: X";
          bool ok = btn(B_OK);
          if (ok && xo[cur]) sfx(300, 60);       // taken already
          if (!ok || xo[cur]) { xoDraw(cur, -1, turn, who, NULL); gfx.send(); continue; }
          xoSet(cur, turn);
          sfx(500, 40);
        }
        if ((line = xoLine(turn)) >= 0) {
          if (xoV.two) msg = (turn == 1) ? "X WINS" : "O WINS";
          else         msg = (turn == 1) ? "YOU WIN" : "CPU WINS";
          if (turn == 1 || xoV.two) xoV.score += 100;
          xoV.tally[turn]++;
          sfx(turn == 1 || xoV.two ? 1400 : 200, 250);
          break;
        }
        if (xoFull()) { msg = "DRAW"; xoV.score += 20; xoV.tally[0]++; break; }
        turn = 3 - turn;
      }
      uint32_t t0 = gameMillis();
      btnClear();
      while (gameMillis() - t0 < 2200) {         // the result, OK goes on earlier
        if (!poll()) return;
        if (btn(B_OK) && gameMillis() - t0 > 500) break;
        xoDraw(9, line, 0, NULL, msg);
        gfx.send();
      }
      if (!xoV.two && line >= 0 && xo[XO_LINES[line][0]] == 2) {   // lost against the machine
        again = gameOver(xoV.score);
        over = true;
      }
      starter = 3 - starter;                    // the other side begins next round
    }
  }
}

static const char *const IC_TICTACTOE[12] = {
  "r.rw...wccc.", ".r.w...wc.c.", "r.rw...wccc.", "wwwwwwwwwww.", "cccwr.rw....", "c.cw.r.w....",
  "cccwr.rw....", "wwwwwwwwwww.", "...wcccwr.r.", "...wc.cw.r..", "...wcccwr.r.", "............" };
