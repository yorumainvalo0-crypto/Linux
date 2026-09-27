// MiniArcade: Tic Tac Toe. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  TIC TAC TOE   (against the ESP32 or two players)
// =========================================================
/* The machine looks at every possible game (there are few enough), so on
   "hard" it never loses. The easier levels mix in random moves. Starting
   alternates between the rounds. A win is worth 100, a draw 20.         */
#define TT_CELL 15
#define TT_X    4
#define TT_Y    (TOP_H + 1)

static uint8_t tt[9];                   // 0 empty, 1 = X (player one), 2 = O

static const uint8_t TT_LINES[8][3] = {
  {0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6}
};

static int8_t ttLine(uint8_t who) {     // index of a completed line, or -1
  for (uint8_t i = 0; i < 8; i++)
    if (tt[TT_LINES[i][0]] == who && tt[TT_LINES[i][1]] == who && tt[TT_LINES[i][2]] == who)
      return i;
  return -1;
}

static bool ttFull() {
  for (uint8_t i = 0; i < 9; i++) if (!tt[i]) return false;
  return true;
}

// score from the view of "me": +10 win (earlier is better), -10 loss, 0 draw
static int8_t ttMinimax(uint8_t me, uint8_t turn, uint8_t depth) {
  if (ttLine(me) >= 0)     return 10 - depth;
  if (ttLine(3 - me) >= 0) return depth - 10;
  if (ttFull())            return 0;
  int8_t best = (turn == me) ? -100 : 100;
  for (uint8_t i = 0; i < 9; i++) {
    if (tt[i]) continue;
    tt[i] = turn;
    int8_t v = ttMinimax(me, 3 - turn, depth + 1);
    tt[i] = 0;
    if (turn == me ? v > best : v < best) best = v;
  }
  return best;
}

/* level 0 = easy, 1 = normal, 2 = hard; "me" is the side to move */
static uint8_t ttThink(uint8_t level, uint8_t me) {
  uint8_t free[9], n = 0;
  for (uint8_t i = 0; i < 9; i++) if (!tt[i]) free[n++] = i;
  if (n == 9) {                                 // empty board: every start draws
    static const uint8_t first[5] = { 0, 2, 4, 6, 8 };
    return first[random(5)];
  }
  for (uint8_t k = 0; k < n; k++) {             // a win in one is always taken
    tt[free[k]] = me;
    bool win = ttLine(me) >= 0;
    tt[free[k]] = 0;
    if (win) return free[k];
  }
  uint8_t chance = (level == 0) ? 60 : (level == 1 ? 20 : 0);
  if (random(100) < chance) return free[random(n)];
  int8_t bestV = -100;
  uint8_t best[9], m = 0;
  for (uint8_t k = 0; k < n; k++) {
    tt[free[k]] = me;
    int8_t v = ttMinimax(me, 3 - me, 1);
    tt[free[k]] = 0;
    if (v > bestV) { bestV = v; m = 0; }
    if (v == bestV) best[m++] = free[k];
  }
  return best[random(m)];                       // equal moves: pick any
}

static const char *ttVs = NULL;          // "vs NAME" in a multiplayer game

static void ttDraw(uint8_t cur, int8_t line, const char *msg, const char *who, uint16_t score) {
  if (ttVs) {
    oled.setFont(FONT_B); oled.drawStr(2, 12, "TIC TAC"); oled.setFont(FONT);
    rightStr(12, ttVs);
    oled.drawHLine(0, TOP_H - 1, SCR_W);
  } else statusBar("TIC TAC", score, curHigh);  // short: room for "BEST 1000"
  for (uint8_t i = 1; i < 3; i++) {             // the grid
    oled.drawVLine(TT_X + i * TT_CELL, TT_Y, 3 * TT_CELL);
    oled.drawHLine(TT_X, TT_Y + i * TT_CELL, 3 * TT_CELL);
  }
  for (uint8_t i = 0; i < 9; i++) {
    int16_t x = TT_X + (i % 3) * TT_CELL, y = TT_Y + (i / 3) * TT_CELL;
    if (tt[i] == 1) {                           // X
      oled.drawLine(x + 4, y + 4, x + 11, y + 11);
      oled.drawLine(x + 11, y + 4, x + 4, y + 11);
      oled.drawLine(x + 5, y + 4, x + 11, y + 10);
      oled.drawLine(x + 10, y + 4, x + 4, y + 10);
    } else if (tt[i] == 2) {                    // O as a small octagon
      oled.drawHLine(x + 6, y + 3, 4);  oled.drawHLine(x + 6, y + 12, 4);
      oled.drawVLine(x + 3, y + 6, 4);  oled.drawVLine(x + 12, y + 6, 4);
      oled.drawLine(x + 4, y + 5, x + 5, y + 4);   oled.drawLine(x + 10, y + 4, x + 11, y + 5);
      oled.drawLine(x + 4, y + 10, x + 5, y + 11); oled.drawLine(x + 10, y + 11, x + 11, y + 10);
    }
  }
  if (cur < 9) {                                // cursor: corner marks
    int16_t x = TT_X + (cur % 3) * TT_CELL + 1, y = TT_Y + (cur / 3) * TT_CELL + 1;
    uint8_t e = TT_CELL - 3;
    oled.drawHLine(x, y, 3);         oled.drawVLine(x, y, 3);
    oled.drawHLine(x + e - 2, y, 3); oled.drawVLine(x + e, y, 3);
    oled.drawHLine(x, y + e, 3);     oled.drawVLine(x, y + e - 2, 3);
    oled.drawHLine(x + e - 2, y + e, 3); oled.drawVLine(x + e, y + e - 2, 3);
  }
  if (line >= 0) {                              // strike through the winning row
    const uint8_t *L = TT_LINES[line];
    int16_t c = TT_CELL / 2;
    int16_t x0 = TT_X + (L[0] % 3) * TT_CELL + c, y0 = TT_Y + (L[0] / 3) * TT_CELL + c;
    int16_t x1 = TT_X + (L[2] % 3) * TT_CELL + c, y1 = TT_Y + (L[2] / 3) * TT_CELL + c;
    oled.drawLine(x0, y0, x1, y1);
    if (y0 == y1) oled.drawLine(x0, y0 + 1, x1, y1 + 1);   // two pixels thick
    else          oled.drawLine(x0 + 1, y0, x1 + 1, y1);
  }
  if (who) oled.drawStr(58, 30, who);
  if (msg) {
    oled.setFont(FONT_B);
    oled.drawStr(58, 48, msg);
    oled.setFont(FONT);
  }
  oled.sendBuffer();
}

void tictactoeRun() {
  static const char *const modes[4] = { "1P easy", "1P normal", "1P hard", "2 players" };
  uint8_t mode = chooseMode("TIC TAC TOE", modes, 4);
  if (mode == 255) return;
  bool twoPlayers = (mode == 3);

  bool again = true;
  while (again) {
    again = false;
    uint16_t score = 0;
    uint8_t  starter = 1, cur = 4;
    bool     over = false;
    while (!over) {                              // one round after the other
      memset(tt, 0, sizeof(tt));
      uint8_t turn = starter;
      int8_t  line = -1;
      const char *msg = NULL;
      btnClear();
      while (true) {
        if (!twoPlayers && turn == 2) {         // the machine moves
          for (uint8_t i = 0; i < 6 && poll(); i++) ttDraw(9, -1, NULL, "thinking...", score);
          tt[ttThink(mode, 2)] = 2;
          sfx(350, 40);
        } else {
          if (!poll()) return;                  // hold OK = back to the library
          if (btn(B_LEFT)  && cur % 3)  { cur--;    sfx(700, 15); }
          if (btn(B_RIGHT) && cur % 3 < 2) { cur++; sfx(700, 15); }
          if (btn(B_UP)    && cur > 2)  { cur -= 3; sfx(700, 15); }
          if (btn(B_DOWN)  && cur < 6)  { cur += 3; sfx(700, 15); }
          const char *who = twoPlayers ? (turn == 1 ? "player X" : "player O") : "you: X";
          bool ok = btn(B_OK);
          if (ok && tt[cur]) sfx(300, 60);        // taken already
          if (!ok || tt[cur]) { ttDraw(cur, -1, NULL, who, score); continue; }
          tt[cur] = turn;
          sfx(500, 40);
        }
        if ((line = ttLine(turn)) >= 0) {
          if (twoPlayers) msg = (turn == 1) ? "X WINS" : "O WINS";
          else            msg = (turn == 1) ? "YOU WIN" : "CPU WINS";
          if (turn == 1 || twoPlayers) score += 100;
          sfx(turn == 1 ? 1400 : 200, 250);
          break;
        }
        if (ttFull()) { msg = "DRAW"; score += 20; break; }
        turn = 3 - turn;
      }
      for (uint8_t i = 0; i < 70 && poll(); i++) ttDraw(9, line, msg, NULL, score);
      if (!twoPlayers && line >= 0 && tt[TT_LINES[line][0]] == 2) {   // lost against the machine
        again = gameOver(score);
        over = true;
      }
      starter = 3 - starter;                    // the other side begins next round
    }
  }
}
