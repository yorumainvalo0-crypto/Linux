// MiniArcade: Sudoku. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  SUDOKU   (every row, column and 3x3 box holds 1..9 once)
// =========================================================
/* A new puzzle every time: a random full grid, then numbers are taken out
   one by one as long as the puzzle keeps exactly one solution. The grid
   uses the whole height (9 cells of 6 px), the right side shows the time,
   the mistakes and the number picker. A wrong number counts as a mistake
   and is not placed; the third mistake ends the game.               */
#define SU_C    7                      // cell pitch: 6 px + 1 px line

static uint8_t suGrid[81];             // 0 = empty
static uint8_t suSol[81];
static uint8_t suGiven[81];            // 1 = part of the puzzle
static uint8_t suCur, suPick;          // cursor and the number in the picker
static bool    suPicking;

// 3 x 5 digits, one row per 3 bits
static const uint8_t SU_FONT[10][5] = {
  {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,3,1,7},{5,5,7,1,1},
  {7,4,7,1,7},{7,4,7,5,7},{7,1,1,2,2},{7,5,7,5,7},{7,5,7,1,7} };

static void suDigit(int16_t x, int16_t y, uint8_t d) {
  for (uint8_t r = 0; r < 5; r++)
    for (uint8_t c = 0; c < 3; c++)
      if (SU_FONT[d][r] & (4 >> c)) oled.drawPixel(x + c, y + r);
}

static uint16_t suMask(const uint8_t *g, uint8_t i) {      // numbers already used around cell i
  uint8_t r = i / 9, c = i % 9, br = r / 3 * 3, bc = c / 3 * 3;
  uint16_t m = 0;
  for (uint8_t k = 0; k < 9; k++) {
    m |= 1 << g[r * 9 + k];
    m |= 1 << g[k * 9 + c];
    m |= 1 << g[(br + k / 3) * 9 + bc + k % 3];
  }
  return m;
}

/* Fills g; counts solutions up to "limit". shuffle: try numbers in a random
   order (to make a random full grid). Picks the cell with the fewest
   choices first, so even hard puzzles solve in a few milliseconds.  */
static uint8_t suSolve(uint8_t *g, uint8_t limit, bool shuffle) {
  int8_t best = -1;
  uint8_t bestN = 10;
  uint16_t bestM = 0;
  for (uint8_t i = 0; i < 81; i++) {
    if (g[i]) continue;
    uint16_t m = ~suMask(g, i) & 0x3FE;
    uint8_t n = 0;
    for (uint8_t d = 1; d <= 9; d++) if (m & (1 << d)) n++;
    if (n < bestN) { bestN = n; best = i; bestM = m; if (!n) return 0; }
  }
  if (best < 0) return 1;                                 // full: one solution
  uint8_t order[9], n = 0, found = 0;
  for (uint8_t d = 1; d <= 9; d++) if (bestM & (1 << d)) order[n++] = d;
  if (shuffle) for (uint8_t i = n; i > 1; i--) { uint8_t j = random(i), t = order[i - 1]; order[i - 1] = order[j]; order[j] = t; }
  for (uint8_t k = 0; k < n; k++) {
    g[best] = order[k];
    found += suSolve(g, limit - found, shuffle);
    if (found >= limit) { if (limit > 1) g[best] = 0; return found; }
  }
  g[best] = 0;
  return found;
}

// a new puzzle with about "keep" numbers given; returns how many are given
uint8_t suMake(uint8_t keep) {
  memset(suSol, 0, 81);
  suSolve(suSol, 1, true);                                // a random full grid
  memcpy(suGrid, suSol, 81);
  uint8_t order[81], left = 81;
  for (uint8_t i = 0; i < 81; i++) order[i] = i;
  for (uint8_t i = 81; i > 1; i--) { uint8_t j = random(i), t = order[i - 1]; order[i - 1] = order[j]; order[j] = t; }
  for (uint8_t k = 0; k < 81 && left > keep; k++) {
    uint8_t i = order[k], v = suGrid[i];
    suGrid[i] = 0;
    uint8_t tmp[81];
    memcpy(tmp, suGrid, 81);
    if (suSolve(tmp, 2, false) != 1) suGrid[i] = v;         // not unique any more: keep it
    else left--;
  }
  for (uint8_t i = 0; i < 81; i++) suGiven[i] = suGrid[i] ? 1 : 0;
  return left;
}

static void suDraw(uint8_t cur, uint8_t pick, bool picking, uint8_t mistakes, uint32_t secs) {
  for (uint8_t k = 0; k <= 9; k++) {                      // lines: solid around the boxes
    int16_t p = k * SU_C;
    if (k % 3 == 0) { oled.drawVLine(p, 0, 64); oled.drawHLine(0, p > 63 ? 63 : p, 64); }
    else for (uint8_t q = 0; q < 64; q += 2) { oled.drawPixel(p, q); oled.drawPixel(q, p); }
  }
  for (uint8_t i = 0; i < 81; i++) {
    int16_t x = (i % 9) * SU_C + 1, y = (i / 9) * SU_C + 1;
    uint8_t d = suGrid[i];
    bool inv = i == cur || (picking && d && d == pick);
    if (inv) { oled.drawBox(x, y, 6, 6); oled.setDrawColor(0); }
    if (d) {
      suDigit(x + 1, y, d);
      if (!suGiven[i]) oled.drawHLine(x + 1, y + 5, 3);   // own numbers are underlined
    }
    oled.setDrawColor(1);
  }
  char b[16];
  oled.setFont(FONT_B);
  oled.drawStr(68, 12, "SUDOKU");
  oled.setFont(FONT);
  snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
  oled.drawStr(68, 25, b);
  for (uint8_t m = 0; m < 3; m++) oled.drawStr(98 + m * 9, 25, m < mistakes ? "X" : "-");
  uint8_t left = 9;                                       // how many of "pick" are still missing
  for (uint8_t i = 0; i < 81; i++) if (suGrid[i] == pick) left--;
  if (picking) {
    oled.drawFrame(68, 32, 58, 20);
    oled.setFont(FONT_B);
    snprintf(b, sizeof(b), "< %u >", pick);
    oled.drawStr(72, 46, b);
    oled.setFont(FONT);
    snprintf(b, sizeof(b), "%u left", left);
    oled.drawStr(68, 62, b);
  } else {
    oled.drawStr(68, 40, "OK = write");
    oled.drawStr(68, 50, "a number");
  }
}

void sudokuRun() {
  static const char *const MODES[3] = { "easy", "medium", "hard" };
  bool again = true;
  while (again) {
    again = false;
    uint8_t mode = chooseMode("SUDOKU", MODES, 3);
    if (mode == 255) return;
    static const uint8_t KEEP[3] = { 40, 32, 26 };
    suMake(KEEP[mode]);
    uint8_t &cur = suCur, &pick = suPick, mistakes = 0;
    bool &picking = suPicking;
    cur = 40; pick = 1; picking = false;
    uint32_t t0 = gameMillis();
    btnClear();
    while (true) {
      if (!poll()) return;
      uint32_t secs = (gameMillis() - t0) / 1000;
      if (!picking) {
        if (btn(B_UP))    { cur = cur >= 9 ? cur - 9 : cur + 72; sfx(700, 8); }
        if (btn(B_DOWN))  { cur = cur < 72 ? cur + 9 : cur - 72; sfx(700, 8); }
        if (btn(B_LEFT))  { cur = cur % 9 ? cur - 1 : cur + 8;   sfx(700, 8); }
        if (btn(B_RIGHT)) { cur = cur % 9 < 8 ? cur + 1 : cur - 8; sfx(700, 8); }
        if (btnTap(B_OK)) {
          if (suGrid[cur]) { pick = suGrid[cur]; sfx(400, 30); }   // a filled field: just show its number
          else { picking = true; sfx(900, 20); }
        }
      } else {
        if (btn(B_UP) || btn(B_RIGHT))  { pick = pick % 9 + 1; sfx(800, 8); }
        if (btn(B_DOWN) || btn(B_LEFT)) { pick = pick > 1 ? pick - 1 : 9; sfx(800, 8); }
        if (btnTap(B_OK)) {
          picking = false;
          if (suSol[cur] == pick) {
            suGrid[cur] = pick;
            sfx(1200, 40);
          } else {
            mistakes++;
            sfx(200, 250);
          }
        }
      }
      bool done = !memcmp(suGrid, suSol, 81);
      if (done || mistakes >= 3) {
        static const uint16_t BASE[3] = { 100, 200, 300 };
        int32_t sc = done ? BASE[mode] + (secs < 1200 ? (1200 - secs) / 6 : 0) - mistakes * 25 : 0;
        if (sc < 0) sc = 0;
        if (done) sfx(1600, 300);
        for (uint8_t i = 0; i < 50 && poll(); i++) {
          suDraw(255, 0, false, mistakes, secs);
          oled.setDrawColor(0); oled.drawBox(66, 30, 62, 34); oled.setDrawColor(1);
          oled.setFont(FONT_B);
          oled.drawStr(68, 46, done ? "SOLVED" : "3 X");
          oled.setFont(FONT);
          oled.sendBuffer();
        }
        again = gameOver((uint16_t)sc);
        break;
      }
      suDraw(cur, pick, picking, mistakes, secs);
      oled.sendBuffer();
    }
  }
}
