// MiniArcade: Match 3. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  MATCH 3   (swap two neighbours, three or more in a row vanish)
// =========================================================
/* OK picks a stone, an arrow swaps it with that neighbour. A swap only
   counts when it makes a row of three or more; the stones above fall
   down and new ones come in from the top - chains score more. 30 moves;
   a row of four gives one move back, five or more two.           */
#define M3_W    10
#define M3_H    6
#define M3_C    8               // cell size
#define M3_T    6               // kinds of stones

static int8_t m3[M3_H][M3_W];   // -1 = empty (only while things fall)
static uint8_t m3R, m3C;        // cursor
static bool    m3Held;          // a stone is picked

static uint8_t m3New(uint8_t r, uint8_t c) {      // a stone that makes no row yet
  for (uint8_t t = 0; t < 20; t++) {
    uint8_t k = random(M3_T);
    if (c >= 2 && m3[r][c - 1] == k && m3[r][c - 2] == k) continue;
    if (r >= 2 && m3[r - 1][c] == k && m3[r - 2][c] == k) continue;
    return k;
  }
  return random(M3_T);
}

// marks every stone that is part of a row of 3+; returns the count, *best = longest row
uint8_t m3Find(bool mark[M3_H][M3_W], uint8_t *best) {
  uint8_t n = 0;
  *best = 0;
  memset(mark, 0, sizeof(bool) * M3_H * M3_W);
  for (uint8_t r = 0; r < M3_H; r++)                     // rows
    for (uint8_t c = 0; c < M3_W; ) {
      uint8_t e = c;
      while (e < M3_W && m3[r][e] >= 0 && m3[r][e] == m3[r][c]) e++;
      if (m3[r][c] >= 0 && e - c >= 3) { for (uint8_t k = c; k < e; k++) mark[r][k] = true; if (e - c > *best) *best = e - c; }
      c = e > c ? e : c + 1;
    }
  for (uint8_t c = 0; c < M3_W; c++)                     // columns
    for (uint8_t r = 0; r < M3_H; ) {
      uint8_t e = r;
      while (e < M3_H && m3[e][c] >= 0 && m3[e][c] == m3[r][c]) e++;
      if (m3[r][c] >= 0 && e - r >= 3) { for (uint8_t k = r; k < e; k++) mark[k][c] = true; if (e - r > *best) *best = e - r; }
      r = e > r ? e : r + 1;
    }
  for (uint8_t r = 0; r < M3_H; r++) for (uint8_t c = 0; c < M3_W; c++) if (mark[r][c]) n++;
  return n;
}

static void m3Swap(uint8_t r1, uint8_t c1, uint8_t r2, uint8_t c2) {
  int8_t t = m3[r1][c1]; m3[r1][c1] = m3[r2][c2]; m3[r2][c2] = t;
}

// is there any swap that makes a row?
bool m3CanMove() {
  bool mark[M3_H][M3_W];
  uint8_t best;
  for (uint8_t r = 0; r < M3_H; r++)
    for (uint8_t c = 0; c < M3_W; c++) {
      if (c + 1 < M3_W) { m3Swap(r, c, r, c + 1); bool ok = m3Find(mark, &best) > 0; m3Swap(r, c, r, c + 1); if (ok) return true; }
      if (r + 1 < M3_H) { m3Swap(r, c, r + 1, c); bool ok = m3Find(mark, &best) > 0; m3Swap(r, c, r + 1, c); if (ok) return true; }
    }
  return false;
}

void m3Fill() {                                          // a fresh board with a move and no rows
  do {
    for (uint8_t r = 0; r < M3_H; r++) for (uint8_t c = 0; c < M3_W; c++) m3[r][c] = m3New(r, c);
  } while (!m3CanMove());
}

static void m3Stone(int16_t x, int16_t y, int8_t k) {    // 6 x 6 inside an 8 x 8 cell
  switch (k) {
  case 0: oled.drawBox(x + 2, y + 1, 4, 6); oled.drawBox(x + 1, y + 2, 6, 4); break;          // ball
  case 1: oled.drawBox(x + 1, y + 1, 6, 6); break;                                           // block
  case 2: for (uint8_t i = 0; i < 4; i++) { oled.drawHLine(x + 4 - i, y + 1 + i, i * 2); oled.drawHLine(x + 1 + i, y + 4 + i, 6 - i * 2); } break;  // diamond
  case 3: for (uint8_t i = 0; i < 6; i++) oled.drawHLine(x + 4 - (i + 1) / 2, y + 1 + i, (i + 1) / 2 * 2 + (i & 1 ? 0 : 1)); break;               // triangle
  case 4: oled.drawLine(x + 1, y + 1, x + 6, y + 6); oled.drawLine(x + 6, y + 1, x + 1, y + 6);
          oled.drawLine(x + 2, y + 1, x + 6, y + 5); oled.drawLine(x + 5, y + 1, x + 1, y + 5); break;   // cross
  case 5: oled.drawFrame(x + 1, y + 1, 6, 6); oled.drawPixel(x + 3, y + 3); oled.drawPixel(x + 4, y + 4); break;   // ring
  default: break;
  }
}

static void m3Draw(uint8_t cr, uint8_t cc, bool held, const bool (*flash)[M3_W], uint8_t moves, uint16_t combo) {
  for (uint8_t r = 0; r < M3_H; r++)
    for (uint8_t c = 0; c < M3_W; c++) {
      int16_t x = c * M3_C, y = TOP_H + r * M3_C;
      if (flash && flash[r][c]) { oled.drawFrame(x + 2, y + 2, 4, 4); continue; }
      m3Stone(x, y, m3[r][c]);
    }
  if (cr < M3_H) {
    int16_t x = cc * M3_C, y = TOP_H + cr * M3_C;
    if (held) { oled.drawFrame(x, y, M3_C, M3_C); oled.drawFrame(x - 1, y - 1, M3_C + 2, M3_C + 2); }
    else if ((millis() / 250) & 1) oled.drawFrame(x, y, M3_C, M3_C);
  }
  char b[16];
  int16_t tx = M3_W * M3_C + 4;
  oled.drawVLine(M3_W * M3_C + 1, TOP_H, SCR_H - TOP_H);
  oled.drawStr(tx, TOP_H + 9, "MOVES");
  snprintf(b, sizeof(b), "%u", moves);
  oled.setFont(FONT_B); oled.drawStr(tx, TOP_H + 24, b); oled.setFont(FONT);
  if (combo > 1) { snprintf(b, sizeof(b), "x%u", combo); oled.drawStr(tx, TOP_H + 38, b); }
  if (held) oled.drawStr(tx, SCR_H - 2, "swap?");
}

void match3Run() {
  bool again = true;
  while (again) {
    again = false;
    m3Fill();
    uint16_t score = 0;
    uint8_t  moves = 30, &cr = m3R, &cc = m3C;
    bool &held = m3Held;
    cr = 2; cc = 4; held = false;
    btnClear();
    while (true) {
      if (!poll()) return;
      int8_t dr = 0, dc = 0;
      if (btn(B_UP))    dr = -1;
      if (btn(B_DOWN))  dr = 1;
      if (btn(B_LEFT))  dc = -1;
      if (btn(B_RIGHT)) dc = 1;
      if (btnTap(B_OK)) { held = !held; sfx(held ? 900 : 500, 20); }
      if (dr || dc) {
        int8_t nr = cr + dr, nc = cc + dc;
        if (nr >= 0 && nr < M3_H && nc >= 0 && nc < M3_W) {
          if (!held) { cr = nr; cc = nc; sfx(700, 8); }
          else {
            held = false;
            m3Swap(cr, cc, nr, nc);
            bool mark[M3_H][M3_W];
            uint8_t best;
            if (!m3Find(mark, &best)) {                   // no row: swap back
              sfx(200, 120);
              for (uint8_t i = 0; i < 8 && poll(); i++) {
                statusBar("MATCH 3", score, curHigh);
                m3Draw(nr, nc, false, NULL, moves, 0);
                oled.sendBuffer();
              }
              m3Swap(cr, cc, nr, nc);
            } else {
              moves--;
              cr = nr; cc = nc;
              uint16_t combo = 0;
              uint8_t cnt;
              while ((cnt = m3Find(mark, &best)) > 0) {   // clear, fall, refill - as long as rows appear
                combo++;
                score += cnt * 10 * combo;
                if (best == 4) moves++;
                if (best >= 5) moves += 2;
                sfx(900 + combo * 150, 60);
                for (uint8_t i = 0; i < 10 && poll(); i++) {
                  statusBar("MATCH 3", score, curHigh);
                  m3Draw(255, 0, false, (i & 2) ? mark : NULL, moves, combo);
                  oled.sendBuffer();
                }
                for (uint8_t c = 0; c < M3_W; c++) {      // gravity
                  int8_t w = M3_H - 1;
                  for (int8_t r = M3_H - 1; r >= 0; r--) if (!mark[r][c]) m3[w--][c] = m3[r][c];
                  while (w >= 0) m3[w--][c] = random(M3_T);
                }
              }
              if (!m3CanMove()) {                          // stuck: a new board
                sfx(400, 200);
                m3Fill();
              }
            }
          }
        }
      }
      statusBar("MATCH 3", score, curHigh);
      m3Draw(cr, cc, held, NULL, moves, 0);
      oled.sendBuffer();
      if (!moves) {
        for (uint8_t i = 0; i < 40 && poll(); i++) {
          statusBar("MATCH 3", score, curHigh);
          m3Draw(255, 0, false, NULL, 0, 0);
          oled.setFont(FONT_B); oled.drawStr(M3_W * M3_C + 4, SCR_H - 2, "END"); oled.setFont(FONT);
          oled.sendBuffer();
        }
        again = gameOver(score);
        break;
      }
    }
  }
}
