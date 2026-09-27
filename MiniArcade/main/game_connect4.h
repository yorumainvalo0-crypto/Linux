// MiniArcade: Connect Four. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  CONNECT FOUR   (against the ESP32)
// =========================================================
#define C4_W 7
#define C4_H 6
#define C4_CELL 7
#define C4_X 45          // leaves room for the hint text on the left
#define C4_Y (TOP_H + 1)

static uint8_t c4[C4_H][C4_W];         // 0 empty, 1 player, 2 cpu

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

static const char *c4Vs = NULL;          // "vs NAME" in a multiplayer game

static void c4Draw(uint8_t sel, const char *msg, uint16_t score) {
  if (c4Vs) {
    oled.setFont(FONT_B); oled.drawStr(2, 12, "4 WINS"); oled.setFont(FONT);
    rightStr(12, c4Vs);
    oled.drawHLine(0, TOP_H - 1, SCR_W);
  } else statusBar("4 WINS", score, curHigh);
  oled.drawFrame(C4_X - 2, C4_Y - 1, C4_W * C4_CELL + 4, C4_H * C4_CELL + 2);
  for (uint8_t r = 0; r < C4_H; r++)
    for (uint8_t c = 0; c < C4_W; c++) {
      int16_t x = C4_X + c * C4_CELL, y = C4_Y + r * C4_CELL;
      if (c4[r][c] == 1)      oled.drawBox(x + 1, y + 1, 5, 5);
      else if (c4[r][c] == 2) oled.drawFrame(x + 1, y + 1, 5, 5);
      else                    oled.drawPixel(x + 3, y + 3);
    }
  oled.drawBox(C4_X + sel * C4_CELL + 2, C4_Y - 4, 3, 2);
  if (msg) oled.drawStr(2, SCR_H - 1, msg);
  oled.sendBuffer();
}

void connect4Run() {
  static const char *const modes[4] = { "1P easy", "1P normal", "1P hard", "2 players" };
  uint8_t mode = chooseMode("4 WINS", modes, 4);
  if (mode == 255) return;
  bool twoPlayers = (mode == 3);

  bool again = true;
  while (again) {
    again = false;
    uint16_t score = 0;
    uint8_t sel = 3, turn = 1;                 // 1 = player one, 2 = cpu / player two
    memset(c4, 0, sizeof(c4));
    btnClear();

    while (poll()) {
      if (btn(B_LEFT)  && sel > 0)         { sel--; sfx(700, 15); }
      if (btn(B_RIGHT) && sel < C4_W - 1)  { sel++; sfx(700, 15); }

      if (btn(B_OK) || btn(B_DOWN)) {
        if (c4Drop(sel, turn) >= 0) {
          sfx(500, 40);
          if (c4Wins(turn)) {                  // somebody got four in a row
            sfx(1400, 250);
            const char *msg = twoPlayers ? (turn == 1 ? "P1 WINS!" : "P2 WINS!") : "YOU WIN!";
            if (turn == 1) score += 100;
            for (uint8_t i = 0; i < 45 && poll(); i++) c4Draw(sel, msg, score);
            if (!twoPlayers && turn == 2) { again = gameOver(score); break; }
            memset(c4, 0, sizeof(c4));
            turn = 1;
          } else if (c4Full()) {
            for (uint8_t i = 0; i < 30 && poll(); i++) c4Draw(sel, "DRAW", score);
            memset(c4, 0, sizeof(c4));
            turn = 1;
          } else if (twoPlayers) {
            turn = 3 - turn;                   // hand the device over
          } else {
            for (uint8_t i = 0; i < 8 && poll(); i++) c4Draw(sel, "thinking...", score);
            uint8_t c = c4Think(mode);
            c4Drop(c, 2);
            sfx(350, 40);
            if (c4Wins(2)) {                     // let the player see how it happened
              sfx(200, 250);
              for (uint8_t i = 0; i < 45 && poll(); i++) c4Draw(sel, "CPU WINS", score);
              again = gameOver(score);
              break;
            }
            if (c4Full()) {
              for (uint8_t i = 0; i < 30 && poll(); i++) c4Draw(sel, "DRAW", score);
              memset(c4, 0, sizeof(c4));
            }
          }
        }
      }
      c4Draw(sel, twoPlayers ? (turn == 1 ? "player 1" : "player 2") : "OK=drop", score);
    }
  }
}
