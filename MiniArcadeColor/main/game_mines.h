// MiniArcade Color: Minesweeper. Part of console.cpp, which includes it.
// 21 x 8 field of 15 px cells in the classic grey look: raised closed
// cells, coloured numbers, a mine counter, a face and a timer on top.
// OK opens the cell under the cursor, a quick double tap on OK sets or
// takes away a flag. OK on an open number whose flags are all set opens
// the rest around it. The first cell opened is never a mine. Points: the
// faster, the more (1000 - seconds), times 1 / 2 / 3 for the difficulty.

#define MS_W   21
#define MS_H    8
#define MS_C   15                      // cell size in px
#define MS_X0   2                      // field, top left
#define MS_Y0  74
#define MS_DBL 280                     // ms for a double tap

enum { MS_MINE = 0x10, MS_OPEN = 0x20, MS_FLAG = 0x40 };   // low 4 bits: mines around

// the field and the flood fill's stack live on the heap while the game runs
static uint8_t (*ms)[MS_W] = NULL;     // [MS_H][MS_W]
static uint8_t *msStack = NULL;        // MS_W * MS_H cells still to look at
static uint8_t  msCx, msCy;            // the cursor
static int16_t  msCurX, msCurY;        // ... as drawn (glides to the cell)

static bool msAlloc() {
  uint8_t *m = (uint8_t *)malloc(MS_W * MS_H * 2);
  if (!m) return false;
  memset(m, 0, MS_W * MS_H * 2);
  ms = (uint8_t (*)[MS_W])m;
  msStack = m + MS_W * MS_H;
  return true;
}
static void msFree() { free(ms); ms = NULL; msStack = NULL; }

// places the mines, keeping the 3 x 3 around (sx, sy) free, and counts
static void msPlace(uint8_t mines, uint8_t sx, uint8_t sy) {
  memset(ms, 0, MS_W * MS_H);
  while (mines) {
    uint8_t x = (uint8_t)random(MS_W), y = (uint8_t)random(MS_H);
    if (ms[y][x] & MS_MINE) continue;
    if (abs((int)x - sx) <= 1 && abs((int)y - sy) <= 1) continue;
    ms[y][x] |= MS_MINE;
    mines--;
  }
  for (uint8_t y = 0; y < MS_H; y++)
    for (uint8_t x = 0; x < MS_W; x++)
      for (int8_t dy = -1; dy <= 1; dy++)
        for (int8_t dx = -1; dx <= 1; dx++) {
          int8_t nx = (int8_t)(x + dx), ny = (int8_t)(y + dy);
          if ((dx || dy) && nx >= 0 && nx < MS_W && ny >= 0 && ny < MS_H && (ms[ny][nx] & MS_MINE)) ms[y][x]++;
        }
}

/* opens a cell; empty ones open their neighbours too (no recursion: a
   stack of cells still to look at). Returns false on a mine.           */
static bool msOpen(uint8_t sx, uint8_t sy) {
  if (ms[sy][sx] & (MS_OPEN | MS_FLAG)) return true;
  if (ms[sy][sx] & MS_MINE) { ms[sy][sx] |= MS_OPEN; return false; }
  uint16_t n = 0;
  ms[sy][sx] |= MS_OPEN;
  msStack[n++] = (uint8_t)(sy * MS_W + sx);
  while (n) {
    uint8_t c = msStack[--n], x = c % MS_W, y = c / MS_W;
    if (ms[y][x] & 0x0F) continue;                   // a number: stop here
    for (int8_t dy = -1; dy <= 1; dy++)
      for (int8_t dx = -1; dx <= 1; dx++) {
        int8_t nx = (int8_t)(x + dx), ny = (int8_t)(y + dy);
        if (nx < 0 || nx >= MS_W || ny < 0 || ny >= MS_H) continue;
        if (ms[ny][nx] & (MS_OPEN | MS_FLAG | MS_MINE)) continue;
        ms[ny][nx] |= MS_OPEN;
        msStack[n++] = (uint8_t)(ny * MS_W + nx);
      }
  }
  return true;
}

// OK on an open number: when its flags are all set, open the rest
static bool msChord(uint8_t x, uint8_t y) {
  uint8_t flags = 0;
  for (int8_t dy = -1; dy <= 1; dy++)
    for (int8_t dx = -1; dx <= 1; dx++) {
      int8_t nx = (int8_t)(x + dx), ny = (int8_t)(y + dy);
      if (nx >= 0 && nx < MS_W && ny >= 0 && ny < MS_H && (ms[ny][nx] & MS_FLAG)) flags++;
    }
  if (flags != (ms[y][x] & 0x0F)) return true;
  bool ok = true;
  for (int8_t dy = -1; dy <= 1; dy++)
    for (int8_t dx = -1; dx <= 1; dx++) {
      int8_t nx = (int8_t)(x + dx), ny = (int8_t)(y + dy);
      if (nx >= 0 && nx < MS_W && ny >= 0 && ny < MS_H && !msOpen((uint8_t)nx, (uint8_t)ny)) ok = false;
    }
  return ok;
}

static bool msWon() {
  for (uint8_t y = 0; y < MS_H; y++)
    for (uint8_t x = 0; x < MS_W; x++)
      if (!(ms[y][x] & MS_MINE) && !(ms[y][x] & MS_OPEN)) return false;
  return true;
}

// ---------------- drawing ----------------
static void msMine(int16_t px, int16_t py) {
  int16_t cx = px + 7, cy = py + 7;
  gfx.hline(cx - 6, cy, 13, C::BLACK);
  gfx.vline(cx, cy - 6, 13, C::BLACK);
  gfx.line(cx - 4, cy - 4, cx + 4, cy + 4, C::BLACK);
  gfx.line(cx - 4, cy + 4, cx + 4, cy - 4, C::BLACK);
  gfx.disc(cx, cy, 4, C::BLACK);
  gfx.fill(cx - 2, cy - 2, 2, 2, C::WHITE);
}

static void msFlag(int16_t px, int16_t py) {
  gfx.vline(px + 8, py + 3, 8, C::BLACK);
  gfx.fill(px + 5, py + 10, 7, 2, C::BLACK);
  gfx.triangle(px + 8, py + 2, px + 8, py + 8, px + 3, py + 5, C::RED);
}

static void msCell(uint8_t x, uint8_t y, bool showAll) {
  static const col NUM[9] = { 0, rgb(0, 0, 255), rgb(0, 128, 0), rgb(255, 0, 0), rgb(0, 0, 128),
                              rgb(128, 0, 0), rgb(0, 128, 128), C::BLACK, GREY2 };
  int16_t px = MS_X0 + x * MS_C, py = MS_Y0 + y * MS_C;
  uint8_t c = ms[y][x];
  bool open = c & MS_OPEN;
  if (open || (showAll && (c & MS_MINE) && !(c & MS_FLAG))) {
    gfx.fill(px, py, MS_C, MS_C, (c & MS_MINE) && open ? C::RED : GREY4);   // the mine that went off: red
    gfx.hline(px, py, MS_C, GREY3);
    gfx.vline(px, py, MS_C, GREY3);
    if (c & MS_MINE) msMine(px, py);
    else if (c & 0x0F) {
      char b[2] = { (char)('0' + (c & 0x0F)), 0 };
      gfx.text(px + 2, py + 1, b, NUM[c & 0x0F], F_TEXT);
    }
    return;
  }
  gfx.fill(px, py, MS_C, MS_C, GREY3);                      // closed: raised
  gfx.hline(px, py, MS_C - 1, C::WHITE);
  gfx.hline(px, py + 1, MS_C - 2, GREY4);
  gfx.vline(px, py, MS_C - 1, C::WHITE);
  gfx.vline(px + 1, py, MS_C - 2, GREY4);
  gfx.hline(px + 1, py + MS_C - 1, MS_C - 1, GREY2);
  gfx.hline(px + 2, py + MS_C - 2, MS_C - 2, C::GREY);
  gfx.vline(px + MS_C - 1, py + 1, MS_C - 1, GREY2);
  gfx.vline(px + MS_C - 2, py + 2, MS_C - 2, C::GREY);
  if (c & MS_FLAG) {
    msFlag(px, py);
    if (showAll && !(c & MS_MINE)) {                        // a flag on a free cell: crossed out
      gfx.line(px + 2, py + 2, px + 12, py + 12, C::DKRED);
      gfx.line(px + 12, py + 2, px + 2, py + 12, C::DKRED);
    }
  }
}

// three red digits in a black box, like the old counters
static void msLed(int16_t x, int16_t y, int16_t v) {
  char b[8];
  if (v < 0) v = 0;
  if (v > 999) v = 999;
  snprintf(b, sizeof(b), "%03d", v);
  gfx.fill(x, y, 52, 30, C::BLACK);
  gfx.frame(x, y, 52, 30, GREY2);
  gfx.text(x + 4, y + 4, "888", rgb(64, 0, 0), F_BOLD);    // the unlit segments
  gfx.text(x + 4, y + 4, b, C::RED, F_BOLD);
}

// the face: 0 playing, 1 waiting for the second tap, 2 boom, 3 won
static void msFace(int16_t cx, int16_t cy, uint8_t mood) {
  gfx.fill(cx - 16, cy - 16, 33, 33, GREY3);
  gfx.frame(cx - 16, cy - 16, 33, 33, C::WHITE);
  gfx.disc(cx, cy, 12, C::YELLOW);
  gfx.circle(cx, cy, 12, C::BLACK);
  if (mood == 2) {                                          // X eyes, sad mouth
    for (int8_t s = -1; s <= 1; s += 2) {
      gfx.line(cx + s * 5 - 2, cy - 6, cx + s * 5 + 2, cy - 2, C::BLACK);
      gfx.line(cx + s * 5 - 2, cy - 2, cx + s * 5 + 2, cy - 6, C::BLACK);
    }
    gfx.hline(cx - 4, cy + 5, 9, C::BLACK);
    gfx.pixel(cx - 5, cy + 6, C::BLACK); gfx.pixel(cx + 5, cy + 6, C::BLACK);
    return;
  }
  if (mood == 3) {                                          // sunglasses
    gfx.fill(cx - 9, cy - 5, 8, 4, C::BLACK);
    gfx.fill(cx + 2, cy - 5, 8, 4, C::BLACK);
    gfx.hline(cx - 10, cy - 5, 21, C::BLACK);
  } else {
    gfx.fill(cx - 5, cy - 5, 2, 3, C::BLACK);
    gfx.fill(cx + 4, cy - 5, 2, 3, C::BLACK);
  }
  if (mood == 1) gfx.circle(cx, cy + 5, 2, C::BLACK);       // "oh"
  else {
    gfx.hline(cx - 3, cy + 6, 7, C::BLACK);
    gfx.pixel(cx - 4, cy + 5, C::BLACK); gfx.pixel(cx + 4, cy + 5, C::BLACK);
    gfx.pixel(cx - 5, cy + 4, C::BLACK); gfx.pixel(cx + 5, cy + 4, C::BLACK);
  }
}

// the drawn cursor follows the cell; a wrap to the other side jumps
static void msGlide(int16_t &v, int16_t target, uint32_t dt) {
  int16_t d = target - v;
  if (d > MS_C * 2 || d < -MS_C * 2) { v = target; return; }
  int16_t s = (int16_t)(d * (int32_t)dt / 50);
  if (!s && d) s = d > 0 ? 1 : -1;
  if ((d > 0 && s > d) || (d < 0 && s < d)) s = d;
  v += s;
}

static void msDraw(uint8_t cx, uint8_t cy, bool showAll, int16_t left, uint32_t secs, uint8_t mood,
                   uint8_t diff, uint16_t score, const char *msg, uint32_t dt) {
  statusBar("MINESWEEPER", score, curHigh, C::BLUE);
  gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(0, 42, 102), C::NAVY);
  // counter, face, timer
  gfx.fill(MS_X0, 30, MS_W * MS_C, 38, GREY3);
  gfx.hline(MS_X0, 30, MS_W * MS_C, C::WHITE);
  gfx.hline(MS_X0, 67, MS_W * MS_C, GREY2);
  msLed(MS_X0 + 6, 34, left);
  msFace(SCR_W / 2, 49, mood);
  msLed(MS_X0 + MS_W * MS_C - 58, 34, (int16_t)secs);
  // the field
  gfx.frame(MS_X0 - 2, MS_Y0 - 2, MS_W * MS_C + 4, MS_H * MS_C + 4, GREY2, 2);
  for (uint8_t y = 0; y < MS_H; y++)
    for (uint8_t x = 0; x < MS_W; x++) msCell(x, y, showAll);
  if (cx < MS_W) {
    msGlide(msCurX, MS_X0 + cx * MS_C, dt);
    msGlide(msCurY, MS_Y0 + cy * MS_C, dt);
    col cc = (gameMillis() / 300) % 2 ? C::YELLOW : C::ORANGE;
    gfx.frame(msCurX - 1, msCurY - 1, MS_C + 2, MS_C + 2, cc, 2);
  }
  // hints
  static const char *const DIFF[3] = { "easy", "normal", "hard" };
  char b[48];
  snprintf(b, sizeof(b), "%s - %u mines", DIFF[diff], 20 + diff * 8);
  gfx.text(8, 202, b, C::LTGREY, F_SMALL);
  gfx.text(8, 216, "OK open   OK OK flag", C::GREY, F_SMALL);
  gfx.right(SCR_W - 8, 216, "OK on a number: open around", C::GREY, F_SMALL);
  if (msg) {
    int16_t w = Gfx::width(msg, F_BOLD) + 28;
    int16_t x = (SCR_W - w) / 2, y = MS_Y0 + MS_H * MS_C / 2 - 21;
    gfx.rbox(x + 3, y + 3, w, 42, 8, C::BLACK);
    gfx.rbox(x, y, w, 42, 8, mood == 2 ? C::DKRED : C::DKGRN);
    gfx.rframe(x, y, w, 42, 8, C::WHITE);
    gfx.text(x + 14, y + 9, msg, C::WHITE, F_BOLD);
  }
}

static void msGame(uint8_t diff) {
  const uint8_t mines = (uint8_t)(20 + diff * 8);
  bool again = true;
  while (again) {
    again = false;
    memset(ms, 0, MS_W * MS_H);
    uint8_t &cx = msCx, &cy = msCy;                // (static: the tests look at them)
    cx = MS_W / 2; cy = MS_H / 2;
    msCurX = MS_X0 + cx * MS_C; msCurY = MS_Y0 + cy * MS_C;
    bool     placed = false;
    uint32_t t0 = 0, tapAt = 0, last = gameMillis();
    bool     tapWait = false;                     // one tap seen, maybe a second comes
    btnClear();

    while (poll()) {
      uint32_t now = gameMillis(), dt = now - last;
      last = now;
      if (btn(B_LEFT))  { cx = cx ? cx - 1 : MS_W - 1; sfx(700, 5); }
      if (btn(B_RIGHT)) { cx = (cx + 1) % MS_W;        sfx(700, 5); }
      if (btn(B_UP))    { cy = cy ? cy - 1 : MS_H - 1; sfx(700, 5); }
      if (btn(B_DOWN))  { cy = (cy + 1) % MS_H;        sfx(700, 5); }

      bool open = false;
      if (btn(B_OK)) {
        if (tapWait) {                            // second tap: flag
          tapWait = false;
          if (!(ms[cy][cx] & MS_OPEN)) { ms[cy][cx] ^= MS_FLAG; sfx(900, 25); }
        } else { tapWait = true; tapAt = now; }
      }
      if (tapWait && now - tapAt > MS_DBL) { tapWait = false; open = true; }

      bool boom = false;
      if (open && !(ms[cy][cx] & MS_FLAG)) {
        if (!placed) { msPlace(mines, cx, cy); placed = true; t0 = now; }
        if (ms[cy][cx] & MS_OPEN) boom = !msChord(cx, cy);
        else                      boom = !msOpen(cx, cy);
        sfx(boom ? 150 : 600, boom ? 400 : 15);
      }

      uint8_t flags = 0;
      for (uint8_t y = 0; y < MS_H; y++) for (uint8_t x = 0; x < MS_W; x++) if (ms[y][x] & MS_FLAG) flags++;
      uint32_t secs = placed ? (now - t0) / 1000 : 0;
      uint16_t score = (uint16_t)((secs < 990 ? 1000 - secs : 10) * (diff + 1));   // faster = more points
      if (boom || (placed && msWon())) {
        if (boom) score = 0;
        else sfx(1400, 250);
        uint32_t e0 = gameMillis(), l2 = e0;
        while (gameMillis() - e0 < 1800) {        // the whole field for a moment
          if (!poll()) return;
          uint32_t t = gameMillis();
          msDraw(255, 0, true, (int16_t)mines - flags, secs, boom ? 2 : 3, diff, score,
                 t - e0 > 400 ? (boom ? "BOOM!" : "CLEARED!") : NULL, t - l2);
          l2 = t;
          gfx.send();
        }
        again = gameOver(score);
        break;
      }
      msDraw(cx, cy, false, (int16_t)mines - flags, secs, tapWait ? 1 : 0, diff, placed ? score : (uint16_t)(1000 * (diff + 1)), NULL, dt);
      gfx.send();
    }
  }
}

void minesRun() {
  static const char *const modes[3] = { "easy    20 mines", "normal  28 mines", "hard    36 mines" };
  uint8_t diff = chooseMode("MINESWEEPER", modes, 3, C::BLUE);
  if (diff == 255) return;
  if (!msAlloc()) return;
  msGame(diff);
  msFree();
}

static const char *const IC_MINES[12] = {
  "wwwwwwwwwwww", "wssssssssrrk", "wsssskssrrsk", "wskssksskksk", "wssskkkssssk", "wsskwkkksssk",
  "wkkkkkkkkksk", "wsskkkkksssk", "wssskkkssssk", "wskssksskssk", "wssssksssssk", "wkkkkkkkkkkk" };
