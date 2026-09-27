// MiniArcade: Minesweeper. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  MINESWEEPER   (21 x 8 field of 6 px cells)
// =========================================================
/* OK opens the cell under the cursor, a quick double tap on OK sets or
   takes away a flag. OK on an open number whose flags are all set opens
   the rest around it. The first cell opened is never a mine.           */
#define MS_W   21
#define MS_H    8
#define MS_C    6             // cell size in px
#define MS_X0   1
#define MS_DBL 280            // ms for a double tap

enum { MS_MINE = 0x10, MS_OPEN = 0x20, MS_FLAG = 0x40 };   // low 4 bits: mines around
static uint8_t ms[MS_H][MS_W];

// 3 x 5 digits 1..8, one row per nibble-ish: 5 rows of 3 bits
static const uint8_t MS_DIG[9][5] = {
  { 0, 0, 0, 0, 0 },
  { 2, 6, 2, 2, 7 }, { 6, 1, 2, 4, 7 }, { 6, 1, 2, 1, 6 }, { 5, 5, 7, 1, 1 },
  { 7, 4, 6, 1, 6 }, { 3, 4, 6, 5, 2 }, { 7, 1, 2, 2, 2 }, { 2, 5, 2, 5, 2 },
};

// places the mines, keeping the 3 x 3 around (sx, sy) free, and counts
void msPlace(uint8_t mines, uint8_t sx, uint8_t sy) {
  memset(ms, 0, sizeof(ms));
  while (mines) {
    uint8_t x = random(MS_W), y = random(MS_H);
    if (ms[y][x] & MS_MINE) continue;
    if (abs((int)x - sx) <= 1 && abs((int)y - sy) <= 1) continue;
    ms[y][x] |= MS_MINE;
    mines--;
  }
  for (uint8_t y = 0; y < MS_H; y++)
    for (uint8_t x = 0; x < MS_W; x++)
      for (int8_t dy = -1; dy <= 1; dy++)
        for (int8_t dx = -1; dx <= 1; dx++) {
          int8_t nx = x + dx, ny = y + dy;
          if ((dx || dy) && nx >= 0 && nx < MS_W && ny >= 0 && ny < MS_H && (ms[ny][nx] & MS_MINE)) ms[y][x]++;
        }
}

/* opens a cell; empty ones open their neighbours too (no recursion: a
   small stack of cells still to look at). Returns false on a mine.     */
bool msOpen(uint8_t sx, uint8_t sy) {
  if (ms[sy][sx] & (MS_OPEN | MS_FLAG)) return true;
  if (ms[sy][sx] & MS_MINE) { ms[sy][sx] |= MS_OPEN; return false; }
  static uint8_t stack[MS_W * MS_H];
  uint16_t n = 0;
  ms[sy][sx] |= MS_OPEN;
  stack[n++] = sy * MS_W + sx;
  while (n) {
    uint8_t c = stack[--n], x = c % MS_W, y = c / MS_W;
    if (ms[y][x] & 0x0F) continue;                   // a number: stop here
    for (int8_t dy = -1; dy <= 1; dy++)
      for (int8_t dx = -1; dx <= 1; dx++) {
        int8_t nx = x + dx, ny = y + dy;
        if (nx < 0 || nx >= MS_W || ny < 0 || ny >= MS_H) continue;
        if (ms[ny][nx] & (MS_OPEN | MS_FLAG | MS_MINE)) continue;
        ms[ny][nx] |= MS_OPEN;
        stack[n++] = ny * MS_W + nx;
      }
  }
  return true;
}

// OK on an open number: when its flags are all set, open the rest
static bool msChord(uint8_t x, uint8_t y) {
  uint8_t flags = 0;
  for (int8_t dy = -1; dy <= 1; dy++)
    for (int8_t dx = -1; dx <= 1; dx++) {
      int8_t nx = x + dx, ny = y + dy;
      if (nx >= 0 && nx < MS_W && ny >= 0 && ny < MS_H && (ms[ny][nx] & MS_FLAG)) flags++;
    }
  if (flags != (ms[y][x] & 0x0F)) return true;
  bool ok = true;
  for (int8_t dy = -1; dy <= 1; dy++)
    for (int8_t dx = -1; dx <= 1; dx++) {
      int8_t nx = x + dx, ny = y + dy;
      if (nx >= 0 && nx < MS_W && ny >= 0 && ny < MS_H && !msOpen(nx, ny)) ok = false;
    }
  return ok;
}

static bool msWon() {
  for (uint8_t y = 0; y < MS_H; y++)
    for (uint8_t x = 0; x < MS_W; x++)
      if (!(ms[y][x] & MS_MINE) && !(ms[y][x] & MS_OPEN)) return false;
  return true;
}

static void msDraw(uint8_t cx, uint8_t cy, bool showAll, int16_t left, uint32_t secs) {
  char b[32];
  oled.setFont(FONT_B);
  oled.drawStr(2, 12, "MINES");
  oled.setFont(FONT);
  snprintf(b, sizeof(b), "*%d  %lus", left, (unsigned long)secs);
  rightStr(12, b);
  oled.drawHLine(0, TOP_H - 1, SCR_W);
  for (uint8_t y = 0; y < MS_H; y++)
    for (uint8_t x = 0; x < MS_W; x++) {
      int16_t px = MS_X0 + x * MS_C, py = TOP_H + y * MS_C;
      uint8_t c = ms[y][x];
      bool open = c & MS_OPEN;
      if ((c & MS_MINE) && (open || showAll)) {       // a mine: a small star
        oled.drawBox(px + 1, py + 1, 3, 3);
        oled.drawPixel(px + 2, py); oled.drawPixel(px + 2, py + 4);
        oled.drawPixel(px, py + 2); oled.drawPixel(px + 4, py + 2);
      } else if (open) {
        uint8_t n = c & 0x0F;
        for (uint8_t r = 0; r < 5 && n; r++)
          for (uint8_t k = 0; k < 3; k++)
            if (MS_DIG[n][r] & (4 >> k)) oled.drawPixel(px + 1 + k, py + r);
      } else {
        oled.drawBox(px, py, 5, 5);                   // still closed
        if (c & MS_FLAG) {                            // flag: a notch cut out
          oled.setDrawColor(0);
          oled.drawVLine(px + 1, py + 1, 3);
          oled.drawPixel(px + 2, py + 1); oled.drawPixel(px + 3, py + 2);
          oled.setDrawColor(1);
        }
      }
    }
  if ((millis() / 250) % 3)                           // blinking cursor
    oled.drawFrame(MS_X0 + cx * MS_C - 1, TOP_H + cy * MS_C - 1, MS_C + 1, MS_C + 1);
  oled.sendBuffer();
}

void minesRun() {
  static const char *const modes[3] = { "easy    20 mines", "normal  28 mines", "hard    36 mines" };
  uint8_t diff = chooseMode("MINESWEEPER", modes, 3);
  if (diff == 255) return;
  const uint8_t mines = 20 + diff * 8;
  bool again = true;
  while (again) {
    again = false;
    memset(ms, 0, sizeof(ms));
    uint8_t  cx = MS_W / 2, cy = MS_H / 2;
    bool     placed = false;
    uint32_t t0 = 0, tapAt = 0;
    bool     tapWait = false;                         // one tap seen, maybe a second comes
    btnClear();

    while (poll()) {
      if (btn(B_LEFT))  cx = cx ? cx - 1 : MS_W - 1;
      if (btn(B_RIGHT)) cx = (cx + 1) % MS_W;
      if (btn(B_UP))    cy = cy ? cy - 1 : MS_H - 1;
      if (btn(B_DOWN))  cy = (cy + 1) % MS_H;

      bool open = false;
      if (btn(B_OK)) {
        if (tapWait) {                                // second tap: flag
          tapWait = false;
          if (!(ms[cy][cx] & MS_OPEN)) { ms[cy][cx] ^= MS_FLAG; sfx(900, 25); }
        } else { tapWait = true; tapAt = gameMillis(); }
      }
      if (tapWait && gameMillis() - tapAt > MS_DBL) { tapWait = false; open = true; }

      bool boom = false;
      if (open && !(ms[cy][cx] & MS_FLAG)) {
        if (!placed) { msPlace(mines, cx, cy); placed = true; t0 = gameMillis(); }
        if (ms[cy][cx] & MS_OPEN) boom = !msChord(cx, cy);
        else                      boom = !msOpen(cx, cy);
        sfx(boom ? 150 : 600, boom ? 400 : 15);
      }

      uint8_t flags = 0;
      for (uint8_t y = 0; y < MS_H; y++) for (uint8_t x = 0; x < MS_W; x++) if (ms[y][x] & MS_FLAG) flags++;
      uint32_t secs = placed ? (gameMillis() - t0) / 1000 : 0;
      if (boom || (placed && msWon())) {
        uint16_t score = 0;
        if (!boom) {                                  // faster = more points
          score = (uint16_t)((secs < 990 ? 1000 - secs : 10) * (diff + 1));
          sfx(1400, 250);
        }
        for (uint8_t i = 0; i < 90 && poll(); i++) msDraw(cx, cy, true, (int16_t)mines - flags, secs);
        again = gameOver(score);
        break;
      }
      msDraw(cx, cy, false, (int16_t)mines - flags, secs);
    }
  }
}
