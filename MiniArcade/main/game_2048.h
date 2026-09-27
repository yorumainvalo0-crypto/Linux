// MiniArcade: 2048. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  2048   (slide the tiles, equal ones merge)
// =========================================================
/* The board keeps exponents: 1 = 2, 2 = 4, ... 11 = 2048. A move slides
   every row towards one side; two equal neighbours merge once per move. */
#define G2_TW 24              // tile 24 x 12 px, 4 x 4 = 96 x 48
#define G2_TH 12

static uint8_t g2[16];        // [row * 4 + col], 0 = empty

// one line of 4 towards index 0; returns true if anything moved
static bool g2Line(uint8_t *c[4], uint32_t *gain) {
  uint8_t v[4] = { 0, 0, 0, 0 }, n = 0;
  bool moved = false;
  for (uint8_t i = 0; i < 4; i++) if (*c[i]) v[n++] = *c[i];
  uint8_t out[4] = { 0, 0, 0, 0 }, k = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (i + 1 < n && v[i] == v[i + 1]) {         // merge a pair, then skip it
      out[k++] = v[i] + 1;
      *gain += 1UL << (v[i] + 1);
      i++;
    } else out[k++] = v[i];
  }
  for (uint8_t i = 0; i < 4; i++) {
    if (*c[i] != out[i]) moved = true;
    *c[i] = out[i];
  }
  return moved;
}

// dir: B_UP / B_DOWN / B_LEFT / B_RIGHT
bool g2Move(uint8_t *b, uint8_t dir, uint32_t *gain) {
  bool moved = false;
  for (uint8_t l = 0; l < 4; l++) {
    uint8_t *c[4];
    for (uint8_t i = 0; i < 4; i++) {
      uint8_t r, col;
      switch (dir) {
      case B_LEFT:  r = l;     col = i;     break;
      case B_RIGHT: r = l;     col = 3 - i; break;
      case B_UP:    r = i;     col = l;     break;
      default:      r = 3 - i; col = l;     break;
      }
      c[i] = &b[r * 4 + col];
    }
    if (g2Line(c, gain)) moved = true;
  }
  return moved;
}

bool g2CanMove(const uint8_t *b) {
  for (uint8_t i = 0; i < 16; i++) {
    if (!b[i]) return true;
    if (i % 4 < 3 && b[i] == b[i + 1]) return true;
    if (i < 12 && b[i] == b[i + 4]) return true;
  }
  return false;
}

static void g2Spawn() {
  uint8_t free_[16], n = 0;
  for (uint8_t i = 0; i < 16; i++) if (!g2[i]) free_[n++] = i;
  if (n) g2[free_[random(n)]] = random(10) ? 1 : 2;   // a 4 now and then
}

static void g2Num(char *b, uint8_t e) {         // tile text, at most 4 characters
  e &= 31;
  uint32_t v = 1UL << e;
  if (v < 10000) snprintf(b, 12, "%lu", (unsigned long)v);
  else           snprintf(b, 12, "%luk", (unsigned long)(v / 1024));
}

static void g2Draw(uint8_t fresh, const char *msg) {
  char b[12];
  uint8_t top = 0;
  for (uint8_t i = 0; i < 16; i++) {
    int16_t x = (i % 4) * G2_TW, y = TOP_H + (i / 4) * G2_TH;
    if (g2[i] > top) top = g2[i];
    if (!g2[i]) { oled.drawPixel(x + G2_TW / 2, y + G2_TH / 2); continue; }
    g2Num(b, g2[i]);
    bool big = g2[i] >= 6;                        // 64 and up: filled tile
    if (big) oled.drawBox(x + 1, y + 1, G2_TW - 2, G2_TH - 2);
    else     oled.drawFrame(x + 1, y + 1, G2_TW - 2, G2_TH - 2);
    if (i == fresh && !big) oled.drawFrame(x + 2, y + 2, G2_TW - 4, G2_TH - 4);
    oled.setDrawColor(big ? 0 : 1);
    oled.drawStr(x + (G2_TW - oled.getStrWidth(b)) / 2 + 1, y + 9, b);
    oled.setDrawColor(1);
  }
  oled.drawVLine(4 * G2_TW + 1, TOP_H, SCR_H - TOP_H);
  oled.drawStr(4 * G2_TW + 5, TOP_H + 10, "MAX");
  if (top) { g2Num(b, top); oled.drawStr(4 * G2_TW + 5, TOP_H + 20, b); }
  if (msg) {
    oled.setFont(FONT_B);
    uint8_t w = oled.getStrWidth(msg) + 8;
    oled.setDrawColor(0);
    oled.drawBox((4 * G2_TW - w) / 2, 32, w, 15);
    oled.setDrawColor(1);
    oled.drawFrame((4 * G2_TW - w) / 2, 32, w, 15);
    oled.drawStr((4 * G2_TW - w) / 2 + 4, 44, msg);
    oled.setFont(FONT);
  }
  oled.sendBuffer();
}

void g2048Run() {
  bool again = true;
  while (again) {
    again = false;
    memset(g2, 0, sizeof(g2));
    g2Spawn(); g2Spawn();
    uint32_t score = 0;
    uint8_t  best = 0, fresh = 255;              // fresh: the tile that just came
    uint32_t won = 0;                            // time the 2048 tile appeared
    btnClear();

    while (poll()) {
      static const uint8_t DIRS[4] = { B_UP, B_DOWN, B_LEFT, B_RIGHT };
      for (uint8_t d = 0; d < 4; d++) {
        if (!btn(DIRS[d])) continue;
        uint32_t gain = 0;
        if (!g2Move(g2, DIRS[d], &gain)) { sfx(250, 20); continue; }
        score += gain;
        sfx(gain ? 900 + (gain > 4000 ? 1000 : gain / 4) : 600, gain ? 40 : 15);
        uint8_t before[16];
        memcpy(before, g2, 16);
        g2Spawn();
        for (uint8_t i = 0; i < 16; i++) if (g2[i] != before[i]) fresh = i;
        for (uint8_t i = 0; i < 16; i++) if (g2[i] > best) {
          best = g2[i];
          if (best == 11) { won = gameMillis(); sfx(1600, 300); }
        }
      }
      uint16_t sc = score > 65535 ? 65535 : (uint16_t)score;
      if (!g2CanMove(g2)) {
        for (uint8_t i = 0; i < 40 && poll(); i++) {  // a moment to see the board
          statusBar("2048", sc, curHigh);
          g2Draw(fresh, "NO MOVES");
        }
        again = gameOver(sc);
        break;
      }
      statusBar("2048", sc, curHigh);
      g2Draw(fresh, won && gameMillis() - won < 2500 ? "2048!" : NULL);
    }
  }
}
