// MiniArcade Color: Tetris. Part of console.cpp, which includes it.
// 10 x 20 board of 10 px blocks in the middle, score and level on the
// left, the next piece on the right. LEFT/RIGHT move, UP turns, DOWN
// drops faster, OK (UP+DOWN or LEFT+RIGHT) drops at once. The pieces come
// from a shuffled bag of all seven, the shadow shows where one lands.

#define TT_W  10
#define TT_H  20
#define TT_C  10
#define TT_X  110
#define TT_Y  34

static uint8_t ttBoard[TT_H][TT_W];          // 0 = empty, 1..7 = piece colour
static const col TT_COL[8] = { 0, C::CYAN, C::YELLOW, C::PURPLE, C::GREEN, C::RED, C::BLUE, C::ORANGE };
// the seven pieces, 4 turns each, as 4 x 4 bit masks (bit 15 = top left)
static const uint16_t TT_SHAPE[7][4] = {
  { 0x0F00, 0x2222, 0x00F0, 0x4444 },   // I
  { 0x6600, 0x6600, 0x6600, 0x6600 },   // O
  { 0x4E00, 0x4640, 0x0E40, 0x4C40 },   // T
  { 0x6C00, 0x4620, 0x06C0, 0x8C40 },   // S
  { 0xC600, 0x2640, 0x0C60, 0x4C80 },   // Z
  { 0x8E00, 0x6440, 0x0E20, 0x44C0 },   // J
  { 0x2E00, 0x4460, 0x0E80, 0xC440 } }; // L

struct TtPiece { int8_t t, r, x, y; };

static bool ttFits(const TtPiece &p) {
  uint16_t m = TT_SHAPE[p.t][p.r];
  for (uint8_t i = 0; i < 16; i++)
    if (m & (0x8000 >> i)) {
      int8_t x = p.x + i % 4, y = p.y + i / 4;
      if (x < 0 || x >= TT_W || y >= TT_H) return false;
      if (y >= 0 && ttBoard[y][x]) return false;
    }
  return true;
}

static void ttBlock(int16_t px, int16_t py, col c, uint8_t s = TT_C) {
  gfx.fill(px, py, s, s, c);
  gfx.hline(px, py, s, colLight(c, 2));
  gfx.vline(px, py, s, colLight(c, 2));
  gfx.hline(px, py + s - 1, s, colDark(c, 2));
  gfx.vline(px + s - 1, py, s, colDark(c, 2));
}

static void ttDrawPiece(const TtPiece &p, int16_t ox, int16_t oy, uint8_t s, bool ghost) {
  uint16_t m = TT_SHAPE[p.t][p.r];
  for (uint8_t i = 0; i < 16; i++)
    if (m & (0x8000 >> i)) {
      int16_t px = ox + (i % 4) * s, py = oy + (i / 4) * s;
      if (ghost) gfx.frame(px + 1, py + 1, s - 2, s - 2, colDark(TT_COL[p.t + 1]));
      else ttBlock(px, py, TT_COL[p.t + 1], s);
    }
}

static uint8_t ttBag[7], ttBagN = 0;
static uint8_t ttNext() {
  if (!ttBagN) {
    for (uint8_t i = 0; i < 7; i++) ttBag[i] = i;
    for (uint8_t i = 6; i > 0; i--) { uint8_t j = (uint8_t)random(i + 1), t = ttBag[i]; ttBag[i] = ttBag[j]; ttBag[j] = t; }
    ttBagN = 7;
  }
  return ttBag[--ttBagN];
}

static void ttSide(const char *label, int16_t x, int16_t y, uint32_t v, col c) {
  gfx.text(x, y, label, C::LTGREY, F_SMALL);
  char b[12];
  snprintf(b, sizeof(b), "%lu", (unsigned long)v);
  gfx.text(x, y + 12, b, c, F_TEXT);
}

void tetrisRun() {
  bool again = true;
  while (again) {
    again = false;
    memset(ttBoard, 0, sizeof(ttBoard));
    ttBagN = 0;
    TtPiece cur = { (int8_t)ttNext(), 0, 3, -1 };
    uint8_t nextT = ttNext();
    uint32_t score = 0;
    uint16_t lines = 0;
    uint8_t level = 1;
    uint32_t fallAt = gameMillis();
    uint8_t clearRows[4], nClear = 0;
    uint32_t clearUntil = 0;
    bool over = false;
    btnClear();
    while (poll()) {
      uint32_t now = gameMillis();
      if (nClear) {                                          // rows flash, then vanish
        if (now >= clearUntil) {
          for (uint8_t k = 0; k < nClear; k++) {
            for (int8_t y = clearRows[k]; y > 0; y--) memcpy(ttBoard[y], ttBoard[y - 1], TT_W);
            memset(ttBoard[0], 0, TT_W);
          }
          nClear = 0;
          fallAt = now;
        }
      } else {
        TtPiece p = cur;
        if (btn(B_LEFT))  { p.x--; if (ttFits(p)) { cur = p; sfx(500, 8); } p = cur; }
        if (btn(B_RIGHT)) { p.x++; if (ttFits(p)) { cur = p; sfx(500, 8); } p = cur; }
        if (btn(B_UP)) {                                     // turn, with a small kick off the walls
          p.r = (p.r + 1) % 4;
          static const int8_t KICK[4] = { 0, -1, 1, 2 };
          for (uint8_t k = 0; k < 4; k++) { TtPiece q = p; q.x += KICK[k]; if (ttFits(q)) { cur = q; sfx(800, 10); break; } }
          p = cur;
        }
        bool hard = btnTap(B_OK);
        uint16_t gravity = level >= 15 ? 60 : (uint16_t)(800 - (level - 1) * 52);
        if (btnHeld(B_DOWN) && gravity > 45) gravity = 45;
        bool land = false;
        if (hard) {
          while (true) { TtPiece q = cur; q.y++; if (!ttFits(q)) break; cur = q; score += 2; }
          land = true;
          sfx(300, 40);
        } else if (now - fallAt >= gravity) {
          fallAt = now;
          TtPiece q = cur; q.y++;
          if (ttFits(q)) { cur = q; if (btnHeld(B_DOWN)) score++; }
          else land = true;
        }
        if (land) {
          uint16_t m = TT_SHAPE[cur.t][cur.r];
          for (uint8_t i = 0; i < 16; i++)
            if (m & (0x8000 >> i)) {
              int8_t y = cur.y + i / 4;
              if (y < 0) { over = true; continue; }
              ttBoard[y][cur.x + i % 4] = (uint8_t)(cur.t + 1);
            }
          for (int8_t y = 0; y < TT_H; y++) {
            bool full = true;
            for (uint8_t x = 0; x < TT_W; x++) if (!ttBoard[y][x]) full = false;
            if (full && nClear < 4) clearRows[nClear++] = (uint8_t)y;
          }
          if (nClear) {
            static const uint16_t PTS[5] = { 0, 40, 100, 300, 1200 };
            score += PTS[nClear] * level;
            lines += nClear;
            level = (uint8_t)(1 + lines / 10);
            clearUntil = now + 300;
            sfx(nClear == 4 ? 1800 : 1300, 120);
          }
          cur = { (int8_t)nextT, 0, 3, -1 };
          nextT = ttNext();
          if (!ttFits(cur)) over = true;
        }
      }

      // ---- draw ----
      uint16_t shown = score > 65535 ? 65535 : (uint16_t)score;
      statusBar("TETRIS", shown, curHigh, C::PURPLE);
      gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(40, 0, 60), C::BLACK);
      gfx.fill(TT_X - 3, TT_Y - 3, TT_W * TT_C + 6, TT_H * TT_C + 6, C::GREY);
      gfx.fill(TT_X, TT_Y, TT_W * TT_C, TT_H * TT_C, rgb(10, 10, 30));
      for (uint8_t x = 1; x < TT_W; x++) gfx.vline(TT_X + x * TT_C, TT_Y, TT_H * TT_C, rgb(25, 25, 50));
      for (uint8_t y = 0; y < TT_H; y++) {
        bool flash = false;
        for (uint8_t k = 0; k < nClear; k++) if (clearRows[k] == y) flash = true;
        for (uint8_t x = 0; x < TT_W; x++)
          if (ttBoard[y][x]) ttBlock(TT_X + x * TT_C, TT_Y + y * TT_C, flash && (now / 60) % 2 ? C::WHITE : TT_COL[ttBoard[y][x]]);
      }
      if (!nClear && !over) {
        TtPiece g = cur;
        while (true) { TtPiece q = g; q.y++; if (!ttFits(q)) break; g = q; }
        ttDrawPiece(g, TT_X + g.x * TT_C, TT_Y + g.y * TT_C, TT_C, true);
        // only the part inside the board
        uint16_t m = TT_SHAPE[cur.t][cur.r];
        for (uint8_t i = 0; i < 16; i++)
          if ((m & (0x8000 >> i)) && cur.y + i / 4 >= 0)
            ttBlock(TT_X + (cur.x + i % 4) * TT_C, TT_Y + (cur.y + i / 4) * TT_C, TT_COL[cur.t + 1]);
      }
      // left: score, lines, level
      gfx.rbox(8, 40, 92, 150, 8, rgb(30, 0, 50));
      ttSide("SCORE", 16, 50, score, C::YELLOW);
      ttSide("LINES", 16, 96, lines, C::CYAN);
      ttSide("LEVEL", 16, 142, level, C::LIME);
      // right: next
      gfx.rbox(224, 40, 88, 80, 8, rgb(30, 0, 50));
      gfx.text(236, 48, "NEXT", C::LTGREY, F_SMALL);
      TtPiece np = { (int8_t)nextT, 0, 0, 0 };
      ttDrawPiece(np, 240, 66, 12, false);
      gfx.text(224, 140, "UP turn", C::GREY, F_SMALL);
      gfx.text(224, 154, "DOWN faster", C::GREY, F_SMALL);
      char drop[20];
      snprintf(drop, sizeof(drop), "%s drop", okKeys());
      gfx.text(224, 168, drop, C::GREY, F_SMALL);
      gfx.send();
      if (over) { sfx(150, 500); delay(500); break; }
    }
    if (!over) return;
    again = gameOver(score > 65535 ? 65535 : (uint16_t)score);
  }
}
