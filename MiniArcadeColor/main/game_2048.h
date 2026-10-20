// MiniArcade Color: 2048. Part of console.cpp, which includes it.
// 4 x 4 board of 46 px tiles on the left, the biggest tile and the moves
// on the right. An arrow slides every tile to that side; two equal tiles
// that meet merge into one (once per move) and score their new value.
// After each move a 2 (now and then a 4) comes in. The tiles glide to
// their places, merged ones pop, a new one grows. No move left = over.

#define G2_T     46                    // tile size
#define G2_G      5                    // gap between the tiles
#define G2_X      6                    // board, top left
#define G2_Y     29
#define G2_SLIDE 130                   // ms a slide takes
#define G2_POP   170                   // ms of the pop / grow afterwards

/* The board keeps exponents: 1 = 2, 2 = 4, ... 11 = 2048, 0 = empty. */
static uint8_t  g2[16];                // [row * 4 + col]
static uint8_t  g2Old[16];             // the board before the last move
static int8_t   g2Dest[16];            // where each of its tiles went
static uint16_t g2Merged;              // bit i: tile i was merged (pops)
static uint8_t  g2Fresh = 255;         // the tile that came after the move
static uint32_t g2AnimAt;              // when the last move began

// one line of 4 cells (idx, towards idx[0]); returns true if anything moved
static bool g2Line(uint8_t *b, const uint8_t idx[4], uint32_t *gain, int8_t *dest, uint16_t *merged) {
  uint8_t v[4], src[4], n = 0;
  for (uint8_t i = 0; i < 4; i++) if (b[idx[i]]) { v[n] = b[idx[i]]; src[n++] = idx[i]; }
  uint8_t out[4] = { 0, 0, 0, 0 }, k = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (i + 1 < n && v[i] == v[i + 1]) {          // merge a pair, then skip it
      out[k] = (uint8_t)(v[i] + 1);
      *gain += 1UL << (v[i] + 1);
      if (dest) { dest[src[i]] = (int8_t)idx[k]; dest[src[i + 1]] = (int8_t)idx[k]; }
      if (merged) *merged |= (uint16_t)(1u << idx[k]);
      i++;
    } else {
      out[k] = v[i];
      if (dest) dest[src[i]] = (int8_t)idx[k];
    }
    k++;
  }
  bool moved = false;
  for (uint8_t i = 0; i < 4; i++) {
    if (b[idx[i]] != out[i]) moved = true;
    b[idx[i]] = out[i];
  }
  return moved;
}

// dir: B_UP / B_DOWN / B_LEFT / B_RIGHT; dest / merged (may be NULL) for the animation
static bool g2Move(uint8_t *b, uint8_t dir, uint32_t *gain, int8_t *dest = NULL, uint16_t *merged = NULL) {
  bool moved = false;
  for (uint8_t l = 0; l < 4; l++) {
    uint8_t idx[4];
    for (uint8_t i = 0; i < 4; i++) {
      uint8_t r, c;
      switch (dir) {
      case B_LEFT:  r = l;     c = i;     break;
      case B_RIGHT: r = l;     c = 3 - i; break;
      case B_UP:    r = i;     c = l;     break;
      default:      r = 3 - i; c = l;     break;
      }
      idx[i] = (uint8_t)(r * 4 + c);
    }
    if (g2Line(b, idx, gain, dest, merged)) moved = true;
  }
  return moved;
}

static bool g2CanMove(const uint8_t *b) {
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

// the classic tile colours, a little brighter for the panel; cool colours from 512 on
static col g2Col(uint8_t e) {
  static const col C2[12] = {
    rgb(204, 170, 153),                                  // empty slot
    rgb(255, 255, 204), rgb(255, 212, 153), rgb(255, 170, 102), rgb(255, 128, 51),
    rgb(255, 85, 51),   rgb(255, 42, 0),    rgb(255, 212, 102), rgb(255, 212, 0),
    rgb(153, 212, 51),  rgb(51, 170, 255),  rgb(204, 85, 255) };
  return e < 12 ? C2[e] : rgb(51, 42, 51);
}

// a tile around its centre (cx, cy), s = size (bigger while it pops)
static void g2Tile(int16_t cx, int16_t cy, int16_t s, uint8_t e) {
  col c = g2Col(e);
  int16_t x = cx - s / 2, y = cy - s / 2;
  gfx.rbox(x, y + 2, s, s, 6, colDark(c, 2));               // a little depth
  gfx.rbox(x, y, s, s, 6, c);
  gfx.hline(x + 6, y + 2, s - 12, colLight(c));
  if (e >= 11) gfx.rframe(x - 1, y - 1, s + 2, s + 2, 7, C::WHITE);
  if (s < G2_T * 2 / 3) return;                             // still growing: no number yet
  char b[12];
  g2Num(b, e);
  uint8_t n = (uint8_t)strlen(b);
  Font f = n <= 2 ? F_BIG : (n == 3 ? F_BOLD : F_TEXT);
  int16_t top = f == F_BIG ? 3 : (f == F_BOLD ? 2 : 0);     // the digits' rows inside the font cell
  int16_t h   = f == F_BIG ? 27 : (f == F_BOLD ? 18 : 14);
  int16_t tx = cx - Gfx::width(b, f) / 2 + 1, ty = cy - top - h / 2;
  col tc = e <= 2 ? rgb(102, 85, 51) : (e >= 12 ? C::YELLOW : C::WHITE);
  if (tc == C::WHITE) gfx.text(tx + 1, ty + 1, b, colDark(c, 2), f);
  gfx.text(tx, ty, b, tc, f);
}

static int16_t g2Px(uint8_t i) { return G2_X + G2_G + (i % 4) * (G2_T + G2_G) + G2_T / 2; }
static int16_t g2Py(uint8_t i) { return G2_Y + G2_G + (i / 4) * (G2_T + G2_G) + G2_T / 2; }

static void g2Draw(uint32_t now, uint16_t moves, const char *msg) {
  gfx.fill(0, TOP_H, SCR_W, SCR_H - TOP_H, GREY1);
  const int16_t BW = 4 * G2_T + 5 * G2_G;
  gfx.rbox(G2_X, G2_Y, BW, BW, 8, rgb(153, 127, 102));
  for (uint8_t i = 0; i < 16; i++) gfx.rbox(g2Px(i) - G2_T / 2, g2Py(i) - G2_T / 2, G2_T, G2_T, 6, g2Col(0));
  uint32_t t = now - g2AnimAt;
  uint8_t top = 0;
  if (t < G2_SLIDE) {                                       // the old tiles glide to their places
    uint32_t f = t * 256 / G2_SLIDE;
    f = 256 - (256 - f) * (256 - f) / 256;                  // ease out
    for (uint8_t i = 0; i < 16; i++) {
      if (!g2Old[i] || g2Dest[i] < 0) continue;
      uint8_t d = (uint8_t)g2Dest[i];
      int16_t x = (int16_t)(g2Px(i) + (int32_t)(g2Px(d) - g2Px(i)) * (int32_t)f / 256);
      int16_t y = (int16_t)(g2Py(i) + (int32_t)(g2Py(d) - g2Py(i)) * (int32_t)f / 256);
      g2Tile(x, y, G2_T, g2Old[i]);
    }
  } else {
    uint32_t tp = t - G2_SLIDE;
    for (uint8_t i = 0; i < 16; i++) {
      if (!g2[i]) continue;
      int16_t s = G2_T;
      if (tp < G2_POP) {
        if (i == g2Fresh) s = (int16_t)(G2_T * (40 + 60 * tp / G2_POP) / 100);
        else if (g2Merged & (1u << i)) s = (int16_t)(G2_T + 8 * (tp < G2_POP / 2 ? tp : G2_POP - tp) / (G2_POP / 2));
      }
      g2Tile(g2Px(i), g2Py(i), s, g2[i]);
    }
  }
  for (uint8_t i = 0; i < 16; i++) if (g2[i] > top) top = g2[i];
  // right: the biggest tile, the moves
  const int16_t PX = G2_X + BW + 8, PW = SCR_W - PX - 6;
  gfx.rbox(PX, G2_Y, PW, 112, 8, rgb(51, 42, 51));
  gfx.text(PX + (PW - Gfx::width("BEST TILE", F_SMALL)) / 2, G2_Y + 8, "BEST TILE", C::LTGREY, F_SMALL);
  if (top) g2Tile(PX + PW / 2, G2_Y + 22 + G2_T / 2 + 4, G2_T, top);
  char b[16];
  gfx.rbox(PX, G2_Y + 120, PW, 54, 8, rgb(51, 42, 51));
  gfx.text(PX + (PW - Gfx::width("MOVES", F_SMALL)) / 2, G2_Y + 128, "MOVES", C::LTGREY, F_SMALL);
  snprintf(b, sizeof(b), "%u", moves);
  gfx.text(PX + (PW - Gfx::width(b, F_BOLD)) / 2, G2_Y + 142, b, C::WHITE, F_BOLD);
  gfx.text(PX + 2, SCR_H - 30, "arrows", C::GREY, F_SMALL);
  gfx.text(PX + 2, SCR_H - 18, "slide tiles", C::GREY, F_SMALL);
  if (msg) {
    gfx.dim(G2_X, G2_Y, BW, BW);
    int16_t w = Gfx::width(msg, F_BOLD) + 24;
    int16_t x = G2_X + (BW - w) / 2, y = G2_Y + BW / 2 - 20;
    gfx.rbox(x, y, w, 40, 8, rgb(255, 170, 0));
    gfx.rframe(x, y, w, 40, 8, C::WHITE);
    gfx.text(x + 12, y + 8, msg, C::WHITE, F_BOLD);
  }
}

void g2048Run() {
  static const uint8_t DIRS[4] = { B_UP, B_DOWN, B_LEFT, B_RIGHT };
  bool again = true;
  while (again) {
    again = false;
    memset(g2, 0, sizeof(g2));
    g2Spawn(); g2Spawn();
    memset(g2Old, 0, sizeof(g2Old));
    g2Merged = 0;
    g2Fresh = 255;
    g2AnimAt = gameMillis() - 1000;              // nothing to animate
    uint32_t score = 0, won = 0;                 // won: time the 2048 tile came
    uint16_t moves = 0;
    uint8_t  best = 0;
    for (uint8_t i = 0; i < 16; i++) if (g2[i] > best) best = g2[i];
    btnClear();
    while (poll()) {
      uint32_t now = gameMillis();
      for (uint8_t d = 0; d < 4; d++) {
        if (!btn(DIRS[d])) continue;
        uint8_t before[16];
        int8_t dest[16];
        memcpy(before, g2, 16);
        memset(dest, -1, sizeof(dest));
        uint32_t gain = 0;
        uint16_t merged = 0;
        if (!g2Move(g2, DIRS[d], &gain, dest, &merged)) { sfx(250, 20); continue; }
        memcpy(g2Old, before, 16);
        memcpy(g2Dest, dest, 16);
        g2Merged = merged;
        g2AnimAt = now;
        score += gain;
        moves++;
        sfx(gain ? 900 + (gain > 4000 ? 1000 : gain / 4) : 600, gain ? 40 : 15);
        memcpy(before, g2, 16);
        g2Spawn();
        for (uint8_t i = 0; i < 16; i++) if (g2[i] != before[i]) g2Fresh = i;
        for (uint8_t i = 0; i < 16; i++) if (g2[i] > best) {
          best = g2[i];
          if (best == 11) { won = now; sfx(1600, 300); }
        }
        break;                                   // one move per frame
      }
      uint16_t sc = score > 65535 ? 65535 : (uint16_t)score;
      if (!g2CanMove(g2)) {
        uint32_t t0 = gameMillis();
        bool left = false;
        while (gameMillis() - t0 < 1500) {       // a moment to see the board
          if (!poll()) { left = true; break; }
          uint32_t t = gameMillis();
          statusBar("2048", sc, curHigh, C::ORANGE);
          g2Draw(t, moves, t - t0 > G2_SLIDE + G2_POP ? "NO MOVES" : NULL);
          gfx.send();
        }
        if (left) return;
        again = gameOver(sc);
        break;
      }
      statusBar("2048", sc, curHigh, C::ORANGE);
      g2Draw(now, moves, won && now - won < 2500 && now - won > G2_SLIDE ? "2048!" : NULL);
      gfx.send();
    }
  }
}

static const char *const IC_2048[12] = {
  ".oooo..yyyy.", "....o..y..y.", ".oooo..y..y.", ".o.....y..y.", ".oooo..yyyy.", "............",
  ".r..r..wwww.", ".r..r..w..w.", ".rrrr..wwww.", "....r..w..w.", "....r..wwww.", "............" };
