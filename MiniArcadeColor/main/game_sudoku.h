// MiniArcade Color: Sudoku. Part of console.cpp, which includes it.
// Every row, column and 3 x 3 box holds 1..9 once. A new puzzle every
// time: a random full grid, then numbers are taken out one by one as long
// as the puzzle keeps exactly one solution. The grid fills the left side
// (22 px cells), the right side shows the time, the mistakes and a 3 x 3
// number pad. Arrows move the cursor, OK on an empty field opens the pad,
// the arrows pick a number, OK writes it. A wrong number counts as a
// mistake and is not placed; the third mistake ends the game.

#define SU_C   22                      // cell pitch
#define SU_X    6                      // grid, top left
#define SU_Y   30
#define SU_PX 214                      // the panel on the right

// everything on the heap while the game runs (the solver keeps its own
// stack there too: no recursion on the 8 kB task stack)
struct SuMem {
  uint8_t  grid[81], sol[81], given[81], tmp[81];   // grid: 0 = empty; given: part of the puzzle
  uint8_t  stCell[81];                               // solver: the cell of each level
  uint16_t stLeft[81];                               // ... and the numbers not yet tried there
};
static SuMem  *suM = NULL;
static uint8_t suCur, suPick;          // cursor and the number in the pad
static bool    suPicking;

static uint16_t suMask(const uint8_t *g, uint8_t i) {      // numbers already used around cell i
  uint8_t r = i / 9, c = i % 9, br = r / 3 * 3, bc = c / 3 * 3;
  uint16_t m = 0;
  for (uint8_t k = 0; k < 9; k++) {
    m |= (uint16_t)(1 << g[r * 9 + k]);
    m |= (uint16_t)(1 << g[k * 9 + c]);
    m |= (uint16_t)(1 << g[(br + k / 3) * 9 + bc + k % 3]);
  }
  return m;
}

/* Fills g; counts solutions up to "limit" (limit 1: g keeps the solution,
   more: g comes back as it was). shuffle: try the numbers in a random
   order (to make a random full grid). Always the cell with the fewest
   choices first. Depth first with an explicit stack in suM.          */
static uint8_t suSolve(uint8_t *g, uint8_t limit, bool shuffle) {
  uint8_t *stC = suM->stCell;
  uint16_t *stL = suM->stLeft;
  uint8_t d = 0, found = 0;
  bool descend = true;
  while (true) {
    if (descend) {
      int8_t best = -1;
      uint8_t bestN = 10;
      uint16_t bestM = 0;
      for (uint8_t i = 0; i < 81; i++) {
        if (g[i]) continue;
        uint16_t m = (uint16_t)(~suMask(g, i) & 0x3FE);
        uint8_t n = 0;
        for (uint8_t k = 1; k <= 9; k++) if (m & (1 << k)) n++;
        if (n < bestN) { bestN = n; best = (int8_t)i; bestM = m; if (!n) break; }
      }
      if (best < 0) {                                       // full: a solution
        if (++found >= limit) {
          if (limit > 1) for (uint8_t k = 0; k < d; k++) g[stC[k]] = 0;
          return found;
        }
      } else if (bestN) { stC[d] = (uint8_t)best; stL[d] = bestM; d++; }
    }
    if (!d) return found;                                   // every way tried
    uint8_t c = stC[d - 1];
    uint16_t m = stL[d - 1];
    if (!m) { g[c] = 0; d--; descend = false; continue; }   // back one level
    uint8_t dig = 0;
    if (shuffle) {
      uint8_t n = 0;
      for (uint8_t k = 1; k <= 9; k++) if (m & (1 << k)) n++;
      uint8_t r = (uint8_t)random(n);
      for (uint8_t k = 1; k <= 9; k++) if ((m & (1 << k)) && !r--) { dig = k; break; }
    } else while (!(m & (1 << dig))) dig++;
    stL[d - 1] = (uint16_t)(m & ~(1 << dig));
    g[c] = dig;
    descend = true;
  }
}

// a new puzzle with about "keep" numbers given; returns how many are given
static uint8_t suMake(uint8_t keep) {
  memset(suM->sol, 0, 81);
  suSolve(suM->sol, 1, true);                               // a random full grid
  memcpy(suM->grid, suM->sol, 81);
  uint8_t order[81], left = 81;
  for (uint8_t i = 0; i < 81; i++) order[i] = i;
  for (uint8_t i = 81; i > 1; i--) { uint8_t j = (uint8_t)random(i), t = order[i - 1]; order[i - 1] = order[j]; order[j] = t; }
  for (uint8_t k = 0; k < 81 && left > keep; k++) {
    uint8_t i = order[k], v = suM->grid[i];
    suM->grid[i] = 0;
    memcpy(suM->tmp, suM->grid, 81);
    if (suSolve(suM->tmp, 2, false) != 1) suM->grid[i] = v; // not unique any more: keep it
    else left--;
  }
  for (uint8_t i = 0; i < 81; i++) suM->given[i] = suM->grid[i] ? 1 : 0;
  return left;
}

// ---------------- drawing ----------------
static int16_t suCurX, suCurY;         // the cursor as drawn (glides)

static void suGlide(int16_t &v, int16_t target, uint32_t dt) {
  int16_t d = target - v;
  if (d > SU_C * 2 || d < -SU_C * 2) { v = target; return; }
  int16_t s = (int16_t)(d * (int32_t)dt / 50);
  if (!s && d) s = d > 0 ? 1 : -1;
  if ((d > 0 && s > d) || (d < 0 && s < d)) s = d;
  v += s;
}

static int16_t suCx(uint8_t i) { return SU_X + (i % 9) * SU_C; }
static int16_t suCy(uint8_t i) { return SU_Y + (i / 9) * SU_C; }

static void suDraw(uint8_t cur, bool showCur, uint8_t mistakes, uint32_t secs, uint8_t bad, uint8_t badDigit, uint32_t dt) {
  const uint8_t *g = suM->grid;
  gfx.fill(0, TOP_H, SCR_W, SCR_H - TOP_H, C::BLACK);
  gfx.fill(SU_X, SU_Y, 9 * SU_C, 9 * SU_C, rgb(10, 10, 40));
  uint8_t sel = suPicking ? suPick : (cur < 81 ? g[cur] : 0);       // this number is highlighted
  uint8_t cr = cur / 9, cc = cur % 9;
  uint32_t now = gameMillis();
  for (uint8_t i = 0; i < 81; i++) {
    int16_t x = suCx(i), y = suCy(i);
    uint8_t r = i / 9, c = i % 9;
    col bg = rgb(10, 10, 40);
    if (showCur && (r == cr || c == cc || (r / 3 == cr / 3 && c / 3 == cc / 3))) bg = rgb(0, 42, 102);
    if (sel && g[i] == sel) bg = rgb(102, 85, 0);
    if (i == bad && (now / 120) % 2) bg = C::DKRED;
    if (bg != rgb(10, 10, 40)) gfx.fill(x + 1, y + 1, SU_C - 1, SU_C - 1, bg);
    char b[2] = { 0, 0 };
    if (g[i]) {
      b[0] = (char)('0' + g[i]);
      gfx.text(x + 4, y + 1, b, suM->given[i] ? C::WHITE : rgb(102, 212, 255), F_BOLD);
    } else if (i == bad) {
      b[0] = (char)('0' + badDigit);
      gfx.text(x + 4, y + 1, b, C::RED, F_BOLD);
    }
  }
  for (uint8_t k = 0; k <= 9; k++) {                        // lines, thick around the boxes
    int16_t p = (int16_t)(k * SU_C);
    if (k % 3) {
      gfx.vline(SU_X + p, SU_Y, 9 * SU_C, rgb(51, 51, 102));
      gfx.hline(SU_X, SU_Y + p, 9 * SU_C, rgb(51, 51, 102));
    }
  }
  for (uint8_t k = 0; k <= 9; k += 3) {
    int16_t p = (int16_t)(k * SU_C);
    gfx.fill(SU_X + p - 1, SU_Y - 1, 3, 9 * SU_C + 3, C::LTGREY);
    gfx.fill(SU_X - 1, SU_Y + p - 1, 9 * SU_C + 3, 3, C::LTGREY);
  }
  if (showCur && cur < 81) {
    suGlide(suCurX, suCx(cur), dt);
    suGlide(suCurY, suCy(cur), dt);
    col fc = suPicking ? C::YELLOW : C::CYAN;
    gfx.frame(suCurX, suCurY, SU_C + 1, SU_C + 1, fc, 2);
  }

  // the panel: time, mistakes, number pad
  char b[16];
  gfx.text(SU_PX, 32, "TIME", C::LTGREY, F_SMALL);
  snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(secs / 60), (unsigned long)(secs % 60));
  gfx.text(SU_PX, 44, b, C::WHITE, F_BOLD);
  gfx.text(SU_PX, 74, "MISTAKES", C::LTGREY, F_SMALL);
  for (uint8_t m = 0; m < 3; m++) {
    int16_t x = SU_PX + m * 32;
    gfx.rbox(x, 86, 28, 24, 5, m < mistakes ? C::RED : rgb(30, 30, 60));
    gfx.text(x + 9, 90, "X", m < mistakes ? C::WHITE : rgb(60, 60, 100), F_TEXT);
  }
  for (uint8_t d = 1; d <= 9; d++) {
    int16_t x = SU_PX + ((d - 1) % 3) * 33, y = 120 + ((d - 1) / 3) * 30;
    uint8_t left = 9;                                       // how many of d are still missing
    for (uint8_t i = 0; i < 81; i++) if (g[i] == d) left--;
    bool on = d == sel;
    col bg = suPicking && on ? C::YELLOW : (on ? rgb(102, 85, 0) : (left ? rgb(30, 30, 80) : rgb(20, 20, 30)));
    gfx.rbox(x, y, 30, 27, 5, bg);
    if (suPicking && on) gfx.rframe(x - 1, y - 1, 32, 29, 6, C::WHITE);
    b[0] = (char)('0' + d); b[1] = 0;
    gfx.text(x + 4, y + 4, b, suPicking && on ? C::BLACK : (left ? C::WHITE : C::GREY), F_TEXT);
    snprintf(b, sizeof(b), "%u", left);
    gfx.text(x + 22, y + 16, b, suPicking && on ? C::DKBLUE : C::GREY, F_SMALL);
  }
  if (suPicking) {
    gfx.text(SU_PX, 214, "arrows: pick", C::LTGREY, F_SMALL);
    gfx.text(SU_PX, 226, "OK = place it", C::LTGREY, F_SMALL);
  } else {
    gfx.text(SU_PX, 214, "OK = write", C::LTGREY, F_SMALL);
    gfx.text(SU_PX, 226, "a number", C::LTGREY, F_SMALL);
  }
}

static void suGame() {
  static const char *const MODES[3] = { "easy", "medium", "hard" };
  static const uint8_t  KEEP[3] = { 40, 32, 26 };
  static const uint16_t BASE[3] = { 100, 200, 300 };
  bool again = true;
  while (again) {
    again = false;
    uint8_t mode = chooseMode("SUDOKU", MODES, 3, C::CYAN);
    if (mode == 255) return;
    gfx.gradient(0, 0, SCR_W, SCR_H, rgb(0, 25, 70), C::BLACK);
    gfx.center(108, "making a puzzle ...", C::WHITE);
    gfx.send();
    suMake(KEEP[mode]);
    uint8_t mistakes = 0, bad = 255, badDigit = 0;
    suCur = 40; suPick = 1; suPicking = false;
    suCurX = suCx(suCur); suCurY = suCy(suCur);
    uint32_t t0 = gameMillis(), badAt = 0, last = t0;
    btnClear();
    while (true) {
      if (!poll()) return;
      uint32_t now = gameMillis(), dt = now - last;
      last = now;
      uint32_t secs = (now - t0) / 1000;
      if (bad != 255 && now - badAt > 700) bad = 255;
      if (!suPicking) {
        if (btn(B_UP))    { suCur = suCur >= 9 ? suCur - 9 : suCur + 72;        sfx(700, 8); }
        if (btn(B_DOWN))  { suCur = suCur < 72 ? suCur + 9 : suCur - 72;        sfx(700, 8); }
        if (btn(B_LEFT))  { suCur = suCur % 9 ? suCur - 1 : suCur + 8;          sfx(700, 8); }
        if (btn(B_RIGHT)) { suCur = suCur % 9 < 8 ? suCur + 1 : suCur - 8;      sfx(700, 8); }
        if (btnTap(B_OK)) {
          if (suM->grid[suCur]) { suPick = suM->grid[suCur]; sfx(400, 30); }   // a filled field: just show its number
          else { suPicking = true; sfx(900, 20); }
        }
      } else {
        if (btn(B_UP) || btn(B_RIGHT))  { suPick = suPick % 9 + 1;          sfx(800, 8); }
        if (btn(B_DOWN) || btn(B_LEFT)) { suPick = suPick > 1 ? suPick - 1 : 9; sfx(800, 8); }
        if (btnTap(B_OK)) {
          suPicking = false;
          if (suM->sol[suCur] == suPick) {
            suM->grid[suCur] = suPick;
            sfx(1200, 40);
          } else {
            mistakes++;
            bad = suCur; badDigit = suPick; badAt = now;
            sfx(200, 250);
          }
        }
      }
      bool done = !memcmp(suM->grid, suM->sol, 81);
      int32_t sc = BASE[mode] + (secs < 1200 ? (1200 - secs) / 6 : 0) - mistakes * 25;
      if (sc < 0) sc = 0;
      if (done || mistakes >= 3) {
        if (!done) sc = 0;
        if (done) sfx(1600, 300);
        uint32_t e0 = gameMillis();
        while (gameMillis() - e0 < 1600) {
          if (!poll()) return;
          statusBar("SUDOKU", (uint16_t)sc, curHigh, C::CYAN);
          suDraw(255, false, mistakes, secs, 255, 0, 0);
          const char *msg = done ? "SOLVED" : "3 MISTAKES";
          int16_t w = Gfx::width(msg, F_BOLD) + 28, x = SU_X + (9 * SU_C - w) / 2;
          gfx.rbox(x + 3, 111, w, 42, 8, C::BLACK);
          gfx.rbox(x, 108, w, 42, 8, done ? C::DKGRN : C::DKRED);
          gfx.rframe(x, 108, w, 42, 8, C::WHITE);
          gfx.text(x + 14, 117, msg, C::WHITE, F_BOLD);
          gfx.send();
        }
        again = gameOver((uint16_t)sc);
        break;
      }
      statusBar("SUDOKU", (uint16_t)sc, curHigh, C::CYAN);
      suDraw(suCur, true, mistakes, secs, bad, badDigit, dt);
      gfx.send();
    }
  }
}

void sudokuRun() {
  suM = (SuMem *)malloc(sizeof(SuMem));
  if (!suM) return;
  suGame();
  free(suM);
  suM = NULL;
}

static const char *const IC_SUDOKU[12] = {
  "wwwwwwwwwwww", "w...w...w..w", "w.b.w...wr.w", "w...w.b.w..w", "wwwwwwwwwwww", "w...w...w..w",
  "wr..w.w.w.bw", "w...w...w..w", "wwwwwwwwwwww", "w.w.w...w..w", "w...wb..w.rw", "wwwwwwwwwwww" };
