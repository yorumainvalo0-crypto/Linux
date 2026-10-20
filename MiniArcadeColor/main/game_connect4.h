// MiniArcade Color: Connect Four ("4 wins") against the CPU or two players
// on one console. Part of console.cpp, which includes it.
// A blue board of 7 x 6 holes. LEFT/RIGHT move the disc above it, OK or
// DOWN drops it - it falls into place. The CPU searches with alpha-beta
// (depth 1 / 2 / 5 for easy / normal / hard, easy also blunders). A win is
// worth 100 and the next round starts on an empty board, until the CPU
// wins. Two players take turns, red begins.

#define C4_W    7
#define C4_H    6
#define C4_CELL 28
#define C4_R    11                     // hole radius
#define C4_X    62                     // the holes, top left
#define C4_Y    60
#define C4_SELY (C4_Y - 22)            // the disc waiting above the board
#define C4_SIDE 54                     // width of the side panels

static uint8_t c4[C4_H][C4_W];         // 0 empty, 1 player (red), 2 cpu / player two (yellow)

static int8_t c4Drop(uint8_t col, uint8_t who) {
  for (int8_t r = C4_H - 1; r >= 0; r--)
    if (!c4[r][col]) { c4[r][col] = who; return r; }
  return -1;
}

static bool c4Wins(uint8_t who) {
  for (int8_t r = 0; r < C4_H; r++)
    for (int8_t c = 0; c < C4_W; c++) {
      if (c4[r][c] != who) continue;
      if (c + 3 < C4_W && c4[r][c+1] == who && c4[r][c+2] == who && c4[r][c+3] == who) return true;
      if (r + 3 < C4_H && c4[r+1][c] == who && c4[r+2][c] == who && c4[r+3][c] == who) return true;
      if (r + 3 < C4_H && c + 3 < C4_W && c4[r+1][c+1] == who && c4[r+2][c+2] == who && c4[r+3][c+3] == who) return true;
      if (r + 3 < C4_H && c - 3 >= 0   && c4[r+1][c-1] == who && c4[r+2][c-2] == who && c4[r+3][c-3] == who) return true;
    }
  return false;
}

static bool c4Full() {
  for (uint8_t c = 0; c < C4_W; c++) if (!c4[0][c]) return false;
  return true;
}

/* ---- engine: negamax with alpha-beta pruning ----
   Search depth sets the difficulty. Equally good moves are picked at
   random, so the machine does not repeat the same game every time.      */
#define C4_INF 30000

static int c4Window(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t me) {
  uint8_t mine = 0, opp = 0;
  uint8_t v[4] = { a, b, c, d };
  for (uint8_t i = 0; i < 4; i++) {
    if (v[i] == me) mine++;
    else if (v[i]) opp++;
  }
  if (mine && opp) return 0;                 // blocked window, worth nothing
  if (mine == 3) return 80;
  if (mine == 2) return 10;
  if (opp  == 3) return -95;                 // block a little harder than build
  if (opp  == 2) return -12;
  return 0;
}

static int c4Eval(uint8_t me) {
  int v = 0;
  for (uint8_t r = 0; r < C4_H; r++)
    for (uint8_t c = 0; c < C4_W; c++) {
      if (c + 3 < C4_W) v += c4Window(c4[r][c], c4[r][c+1], c4[r][c+2], c4[r][c+3], me);
      if (r + 3 < C4_H) v += c4Window(c4[r][c], c4[r+1][c], c4[r+2][c], c4[r+3][c], me);
      if (r + 3 < C4_H && c + 3 < C4_W)
        v += c4Window(c4[r][c], c4[r+1][c+1], c4[r+2][c+2], c4[r+3][c+3], me);
      if (r + 3 < C4_H && c >= 3)
        v += c4Window(c4[r][c], c4[r+1][c-1], c4[r+2][c-2], c4[r+3][c-3], me);
    }
  for (uint8_t r = 0; r < C4_H; r++)         // the middle column is worth more
    if (c4[r][3] == me) v += 6;
    else if (c4[r][3]) v -= 6;
  return v;
}

static const uint8_t C4_ORDER[C4_W] = { 3, 2, 4, 1, 5, 0, 6 };

static int c4Neg(uint8_t depth, int alpha, int beta, uint8_t me) {
  uint8_t opp = 3 - me;
  int best = -C4_INF - 1;
  for (uint8_t i = 0; i < C4_W; i++) {
    uint8_t c = C4_ORDER[i];
    int8_t r = c4Drop(c, me);
    if (r < 0) continue;
    int v;
    if (c4Wins(me))     v = C4_INF - (20 - depth);     // win as early as possible
    else if (!depth)    v = c4Eval(me);
    else                v = -c4Neg(depth - 1, -beta, -alpha, opp);
    c4[r][c] = 0;
    if (v > best) best = v;
    if (best > alpha) alpha = best;
    if (alpha >= beta) break;                          // cut off
  }
  return (best == -C4_INF - 1) ? 0 : best;             // board full = draw
}

/* difficulty 0 = easy, 1 = normal, 2 = hard */
static uint8_t c4Think(uint8_t level) {
  uint8_t depth = (level == 0) ? 1 : (level == 1 ? 2 : 5);   // ~0.4 s per move at 5
  if (level == 0 && random(10) < 5) {                  // easy also blunders
    uint8_t pick[C4_W], n = 0;
    for (uint8_t c = 0; c < C4_W; c++) if (!c4[0][c]) pick[n++] = c;
    if (n) return pick[random(n)];
  }
  int bestV = -C4_INF - 1;
  uint8_t best[C4_W], n = 0;
  for (uint8_t i = 0; i < C4_W; i++) {
    uint8_t c = C4_ORDER[i];
    int8_t r = c4Drop(c, 2);
    if (r < 0) continue;
    int v;
    if (c4Wins(2)) v = C4_INF;
    else           v = -c4Neg(depth, -C4_INF, C4_INF, 1);
    c4[r][c] = 0;
    if (v > bestV) { bestV = v; n = 0; best[n++] = c; }
    else if (v == bestV && n < C4_W) best[n++] = c;    // collect equal moves
  }
  return n ? best[random(n)] : 0;
}

// ---------------- screen ----------------
static const col C4_DISC[3] = { C::BLACK, C::RED, C::YELLOW };

// what the screen shows besides the board
static struct { uint8_t mode, turn, tally[3]; uint16_t score; bool two; } c4V;

static void c4Disc(int16_t cx, int16_t cy, uint8_t who) {
  col b = C4_DISC[who];
  gfx.disc(cx, cy, C4_R, colDark(b, 2));
  gfx.disc(cx - 1, cy - 1, C4_R - 1, b);
  gfx.circle(cx - 1, cy - 1, C4_R - 4, colDark(b));
  gfx.disc(cx - 4, cy - 5, 2, colLight(b, 2));
}

// the blue plate: drawn over the discs, round holes let them show through
static void c4Plate() {
  const col PL = rgb(0, 85, 204), PD = rgb(0, 42, 153), PH = rgb(51, 128, 255);
  int16_t x0 = C4_X - 6, y0 = C4_Y - 6, w = C4_W * C4_CELL + 12, h = C4_H * C4_CELL + 12;
  gfx.fill(x0 + 8, y0 + h, 14, 4, PD);                          // feet
  gfx.fill(x0 + w - 22, y0 + h, 14, 4, PD);
  gfx.fill(x0, y0, w, 6, PL);
  gfx.fill(x0, y0 + h - 6, w, 6, PL);
  gfx.fill(x0, y0, 6, h, PL);
  gfx.fill(x0 + w - 6, y0, 6, h, PL);
  for (int16_t j = 0; j < C4_CELL; j++) {
    int16_t dy = j - C4_CELL / 2, dx = -1;
    if (dy >= -C4_R && dy <= C4_R) { dx = 0; while ((dx + 1) * (dx + 1) + dy * dy <= C4_R * C4_R + C4_R) dx++; }
    for (uint8_t r = 0; r < C4_H; r++) {
      int16_t y = C4_Y + r * C4_CELL + j;
      for (uint8_t c = 0; c < C4_W; c++) {
        int16_t x = C4_X + c * C4_CELL, cx = x + C4_CELL / 2;
        if (dx < 0) gfx.hline(x, y, C4_CELL, PL);
        else {
          gfx.hline(x, y, cx - dx - x, PL);
          gfx.hline(cx + dx + 1, y, x + C4_CELL - cx - dx - 1, PL);
        }
      }
    }
  }
  for (uint8_t r = 0; r < C4_H; r++)                            // rims
    for (uint8_t c = 0; c < C4_W; c++)
      gfx.circle(C4_X + c * C4_CELL + C4_CELL / 2, C4_Y + r * C4_CELL + C4_CELL / 2, C4_R + 1, PD);
  gfx.hline(x0, y0, w, PH);
  gfx.vline(x0, y0, h, PH);
  gfx.hline(x0, y0 + h - 1, w, PD);
  gfx.vline(x0 + w - 1, y0, h, PD);
}

// the four discs in a row of "who" (for the highlight); false = none
static bool c4Line(uint8_t who, int8_t *r0, int8_t *c0, int8_t *dr, int8_t *dc) {
  static const int8_t D[4][2] = { { 0, 1 }, { 1, 0 }, { 1, 1 }, { 1, -1 } };
  for (int8_t r = 0; r < C4_H; r++)
    for (int8_t c = 0; c < C4_W; c++)
      for (uint8_t d = 0; d < 4; d++) {
        uint8_t k = 0;
        for (; k < 4; k++) {
          int8_t rr = r + D[d][0] * k, cc = c + D[d][1] * k;
          if (rr < 0 || rr >= C4_H || cc < 0 || cc >= C4_W || c4[rr][cc] != who) break;
        }
        if (k == 4) { *r0 = r; *c0 = c; *dr = D[d][0]; *dc = D[d][1]; return true; }
      }
  return false;
}

static void c4Center(int16_t x0, int16_t y, const char *s, col c, Font f) {
  gfx.text(x0 + (C4_SIDE - Gfx::width(s, f)) / 2, y, s, c, f);
}

// a side panel: who plays this colour, the rounds won, a note
static void c4Side(int16_t x0, uint8_t p, bool active, const char *note) {
  col base = C4_DISC[p];
  if (active) {
    gfx.rbox(x0 + 2, 34, C4_SIDE - 4, 160, 8, colDark(base, 3));
    gfx.rframe(x0 + 2, 34, C4_SIDE - 4, 160, 8, base);
  }
  c4Center(x0, 42, c4V.two ? (p == 1 ? "P1" : "P2") : (p == 1 ? "YOU" : "CPU"), C::WHITE, F_TEXT);
  gfx.disc(x0 + C4_SIDE / 2 + 2, 86, C4_R + 1, C::BLACK);
  c4Disc(x0 + C4_SIDE / 2, 84, p);
  char b[8];
  if (c4V.two || p == 1) {
    snprintf(b, sizeof(b), "%u", c4V.tally[p]);
    c4Center(x0, 112, b, C::WHITE, F_BOLD);
    c4Center(x0, 140, "wins", C::LTGREY, F_SMALL);
  } else {
    static const char *const LV[3] = { "easy", "normal", "hard" };
    c4Center(x0, 116, LV[c4V.mode], C::LTGREY, F_SMALL);
  }
  if (note) c4Center(x0, 172, note, C::YELLOW, F_SMALL);
}

static void c4Banner(const char *msg, col c) {
  int16_t w = Gfx::width(msg, F_BOLD) + 36, x = (SCR_W - w) / 2;
  gfx.rbox(x + 4, 124, w, 42, 10, C::BLACK);
  gfx.rbox(x, 120, w, 42, 10, colDark(c, 2));
  gfx.rframe(x, 120, w, 42, 10, c);
  gfx.center(129, msg, C::WHITE, F_BOLD);
}

/* sel < 0: no disc waiting above; fallCol >= 0: a disc of fallWho at
   height fallY on its way down; winWho: highlight that player's four  */
static void c4Draw(int8_t sel, int8_t fallCol, int16_t fallY, uint8_t fallWho, uint8_t winWho,
                   const char *msg, const char *note) {
  statusBar("4 WINS", c4V.score, curHigh, C::BLUE);
  gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(20, 30, 80), rgb(0, 0, 20));
  for (uint8_t r = 0; r < C4_H; r++)
    for (uint8_t c = 0; c < C4_W; c++)
      if (c4[r][c]) c4Disc(C4_X + c * C4_CELL + C4_CELL / 2, C4_Y + r * C4_CELL + C4_CELL / 2, c4[r][c]);
  if (fallCol >= 0) c4Disc(C4_X + fallCol * C4_CELL + C4_CELL / 2, fallY, fallWho);
  c4Plate();
  int8_t r0, c0, dr, dc;
  if (winWho && c4Line(winWho, &r0, &c0, &dr, &dc) && (gameMillis() / 250) % 2 == 0)
    for (uint8_t k = 0; k < 4; k++) {
      int16_t cx = C4_X + (c0 + dc * k) * C4_CELL + C4_CELL / 2, cy = C4_Y + (r0 + dr * k) * C4_CELL + C4_CELL / 2;
      gfx.circle(cx - 1, cy - 1, C4_R - 1, C::WHITE);
      gfx.circle(cx - 1, cy - 1, C4_R - 2, C::WHITE);
    }
  if (sel >= 0) {
    int16_t cx = C4_X + sel * C4_CELL + C4_CELL / 2;
    c4Disc(cx, C4_SELY, c4V.turn);
  }
  bool live = !msg;
  c4Side(0, 1, live && c4V.turn == 1, c4V.turn == 1 ? note : NULL);
  c4Side(SCR_W - C4_SIDE, 2, live && c4V.turn == 2, c4V.turn == 2 ? note : NULL);
  c4Center(0, 206, "LEFT/RIGHT", C::GREY, F_SMALL);
  c4Center(0, 218, "move", C::GREY, F_SMALL);
  c4Center(SCR_W - C4_SIDE, 206, "OK=drop", C::GREY, F_SMALL);
  c4Center(SCR_W - C4_SIDE, 218, "or DOWN", C::GREY, F_SMALL);
  if (msg) c4Banner(msg, winWho ? C4_DISC[winWho] : C::LTGREY);
}

// shows the board for a while (OK ends a message early); false = left the game
static bool c4Hold(uint16_t ms, uint8_t winWho, const char *msg, const char *note) {
  uint32_t t0 = gameMillis();
  btnClear();
  while (gameMillis() - t0 < ms) {
    if (!poll()) return false;
    if (msg && btn(B_OK) && gameMillis() - t0 > 500) break;
    c4Draw(-1, -1, 0, 0, winWho, msg, note);
    gfx.send();
  }
  return true;
}

/* A disc of "who" falls down column col. Returns its row, -1 when the
   column is full, -2 when the game was left meanwhile.               */
static int8_t c4Fall(uint8_t col, uint8_t who) {
  int8_t row = -1;
  for (int8_t r = C4_H - 1; r >= 0 && row < 0; r--) if (!c4[r][col]) row = r;
  if (row < 0) return -1;
  int16_t y0 = C4_SELY, y1 = C4_Y + row * C4_CELL + C4_CELL / 2;
  uint32_t t0 = gameMillis(), landAt = 0;
  bool landed = false;
  while (true) {
    if (!poll()) return -2;
    uint32_t t = gameMillis() - t0;
    int32_t y = y0 + (int32_t)t * t * 7 / 10000;                // falls faster and faster: ~0.5 s
    if (y >= y1) {
      if (!landed) { landed = true; landAt = t; sfx(500, 40); }
      uint32_t b = t - landAt;
      if (b >= 160) break;
      int32_t k = b < 80 ? b : 160 - b;                         // a small bounce
      y = y1 - k * 6 / 80;
    }
    c4Draw(-1, (int8_t)col, (int16_t)y, who, 0, NULL, NULL);
    gfx.send();
  }
  c4[row][col] = who;
  return row;
}

void connect4Run() {
  static const char *const modes[4] = { "1P easy", "1P normal", "1P hard", "2 players" };
  uint8_t mode = chooseMode("4 WINS", modes, 4, C::BLUE);
  if (mode == 255) return;
  c4V.two = mode == 3;
  c4V.mode = mode < 3 ? mode : 0;

  bool again = true;
  while (again) {
    again = false;
    uint8_t sel = 3;
    c4V.score = 0;
    c4V.turn = 1;                                               // 1 = player one, 2 = cpu / player two
    memset(c4V.tally, 0, sizeof(c4V.tally));
    memset(c4, 0, sizeof(c4));
    btnClear();

    while (true) {
      if (!poll()) return;
      if (btn(B_LEFT)  && sel > 0)        { sel--; sfx(700, 15); }
      if (btn(B_RIGHT) && sel < C4_W - 1) { sel++; sfx(700, 15); }
      bool drop = btn(B_OK);
      if (btn(B_DOWN)) drop = true;
      if (drop) {
        uint8_t turn = c4V.turn;
        int8_t r = c4Fall(sel, turn);
        if (r == -2) return;
        if (r == -1) sfx(200, 60);                              // column full
        else if (c4Wins(turn)) {                                // somebody got four in a row
          sfx(1400, 250);
          if (turn == 1) c4V.score += 100;
          c4V.tally[turn]++;
          if (!c4Hold(2200, turn, c4V.two ? (turn == 1 ? "P1 WINS!" : "P2 WINS!") : "YOU WIN!", NULL)) return;
          memset(c4, 0, sizeof(c4));
          c4V.turn = 1;
        } else if (c4Full()) {
          if (!c4Hold(1600, 0, "DRAW", NULL)) return;
          memset(c4, 0, sizeof(c4));
          c4V.turn = 1;
        } else if (c4V.two) {
          c4V.turn = 3 - turn;                                  // hand the console over
        } else {
          c4V.turn = 2;
          if (!c4Hold(600, 0, NULL, "thinking...")) return;      // a short visible pause
          uint8_t c = c4Think(mode);
          if (c4Fall(c, 2) == -2) return;
          sfx(350, 40);
          if (c4Wins(2)) {                                      // let the player see how it happened
            sfx(200, 250);
            if (!c4Hold(2500, 2, "CPU WINS", NULL)) return;
            again = gameOver(c4V.score);
            break;
          }
          if (c4Full()) {
            if (!c4Hold(1600, 0, "DRAW", NULL)) return;
            memset(c4, 0, sizeof(c4));
          }
          c4V.turn = 1;
        }
        btnClear();
      }
      c4Draw(sel, -1, 0, 0, 0, NULL, c4V.two ? "your go" : (c4V.turn == 1 ? "your go" : NULL));
      gfx.send();
    }
  }
}

static const char *const IC_CONNECT4[12] = {
  ".....rr.....", ".....rr.....", "bbbbbbbbbbbb", "bb..b..b..bb", "bb..b..b..bb", "bbbbbbbbbbbb",
  "bb..byybrrbb", "bb..byybrrbb", "bbbbbbbbbbbb", "bbrrbyybyybb", "bbrrbyybyybb", "bbbbbbbbbbbb" };
