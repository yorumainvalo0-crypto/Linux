// MiniArcade: Tetris. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  TETRIS   (10 x 16 field, 3 px cells -> fits the blue area)
// =========================================================
#define T_W 10
#define T_H 16
#define T_CELL 3
#define T_X 1
#define T_Y TOP_H

// 4x4 bitmasks, bit 15 = top left; 7 pieces x 4 rotations
static const uint16_t T_PIECE[7][4] = {
  { 0x0F00, 0x2222, 0x00F0, 0x4444 },  // I
  { 0x44C0, 0x8E00, 0x6440, 0x0E20 },  // J
  { 0x4460, 0x0E80, 0xC440, 0x2E00 },  // L
  { 0xCC00, 0xCC00, 0xCC00, 0xCC00 },  // O
  { 0x06C0, 0x8C40, 0x06C0, 0x8C40 },  // S
  { 0x0E40, 0x4C40, 0x4E00, 0x4640 },  // T
  { 0x0C60, 0x4C80, 0x0C60, 0x4C80 }   // Z
};

static uint16_t tFld[T_H];             // one bit per column

bool tHit(uint8_t p, uint8_t r, int8_t px, int8_t py) {
  uint16_t m = T_PIECE[p][r];
  for (uint8_t i = 0; i < 16; i++) {
    if (!(m & (0x8000 >> i))) continue;
    int8_t x = px + (i & 3), y = py + (i >> 2);
    if (x < 0 || x >= T_W || y >= T_H) return true;
    if (y >= 0 && (tFld[y] & (1 << x))) return true;
  }
  return false;
}

void tLock(uint8_t p, uint8_t r, int8_t px, int8_t py) {
  uint16_t m = T_PIECE[p][r];
  for (uint8_t i = 0; i < 16; i++) {
    if (!(m & (0x8000 >> i))) continue;
    int8_t x = px + (i & 3), y = py + (i >> 2);
    if (y >= 0 && y < T_H && x >= 0 && x < T_W) tFld[y] |= (1 << x);
  }
}

uint8_t tClearLines() {
  uint8_t n = 0;
  for (int8_t y = T_H - 1; y >= 0; y--) {
    if (tFld[y] == 0x03FF) {
      for (int8_t k = y; k > 0; k--) tFld[k] = tFld[k - 1];
      tFld[0] = 0;
      y++;                             // re-check the same row
      n++;
    }
  }
  return n;
}

void tDraw(uint16_t m, int8_t px, int8_t py, uint8_t cell, uint8_t ox, uint8_t oy) {
  for (uint8_t i = 0; i < 16; i++) {
    if (!(m & (0x8000 >> i))) continue;
    int8_t x = px + (i & 3), y = py + (i >> 2);
    if (y >= 0) oled.drawBox(ox + x * cell, oy + y * cell, cell - 1, cell - 1);
  }
}

void tetrisRun() {
  bool again = true;
  while (again) {
    again = false;
    uint8_t  p = random(7), nextP = random(7), r = 0, level = 0;
    int8_t   px = 3, py = -1;
    uint16_t score = 0, lines = 0;
    uint32_t nextFall = gameMillis() + 500;
    memset(tFld, 0, sizeof(tFld));
    btnClear();

    while (poll()) {
      // ---- input ----
      if (btn(B_LEFT)  && !tHit(p, r, px - 1, py)) px--;
      if (btn(B_RIGHT) && !tHit(p, r, px + 1, py)) px++;
      if (btn(B_UP)) {                                   // rotate + wall kick
        uint8_t nr = (r + 1) & 3;
        if      (!tHit(p, nr, px, py))     r = nr;
        else if (!tHit(p, nr, px - 1, py)) { r = nr; px--; }
        else if (!tHit(p, nr, px + 1, py)) { r = nr; px++; }
      }
      bool drop = false;
      if (btn(B_OK)) {                                   // hard drop
        while (!tHit(p, r, px, py + 1)) py++;
        drop = true;
      }
      if (btnHeld(B_DOWN) && nextFall > gameMillis() + 40) nextFall = gameMillis() + 40;   // soft drop

      // ---- gravity ----
      uint32_t now = gameMillis();
      if (drop || now >= nextFall) {
        int16_t step = 500 - (int16_t)level * 40;       // signed: level 13+ went negative
        if (step < 100) step = 100;
        if (!drop && !tHit(p, r, px, py + 1)) {
          py++;
          nextFall = now + (btnHeld(B_DOWN) ? 40 : step);
        } else {
          tLock(p, r, px, py);
          uint8_t n = tClearLines();
          sfx(300, 20);
          if (n) {
            sfx(1000, 90);
            static const uint8_t pts[5] = { 0, 1, 3, 5, 8 };
            score += (uint16_t)pts[n] * 10 * (level + 1);
            lines += n;
            level = lines / 10;
          }
          p = nextP; nextP = random(7); r = 0; px = 3; py = -1;
          nextFall = now + step;
          if (tHit(p, r, px, py)) { again = gameOver(score); break; }
        }
      }

      // ---- draw ----
      statusBar("TETRIS", score, curHigh);
      oled.drawFrame(T_X - 1, T_Y, T_W * T_CELL + 2, T_H * T_CELL + 2);
      for (uint8_t y = 0; y < T_H; y++)
        for (uint8_t x = 0; x < T_W; x++)
          if (tFld[y] & (1 << x)) oled.drawBox(T_X + x * T_CELL, T_Y + 1 + y * T_CELL, 2, 2);
      tDraw(T_PIECE[p][r], px, py, T_CELL, T_X, T_Y + 1);

      char b[16];
      oled.drawStr(40, 24, "NEXT");
      tDraw(T_PIECE[nextP][0], 0, 0, 4, 40, 28);
      snprintf(b, sizeof(b), "LINES %u", lines); oled.drawStr(74, 24, b);
      snprintf(b, sizeof(b), "LEVEL %u", level + 1); oled.drawStr(74, 34, b);
      oled.drawStr(74, 50, "OK = drop");
      oled.sendBuffer();
    }
  }
}
