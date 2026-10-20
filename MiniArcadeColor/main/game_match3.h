// MiniArcade Color: Match 3. Part of console.cpp, which includes it.
// Swap two neighbouring gems; three or more of a kind in a row vanish.
// OK picks a gem, an arrow swaps it with that neighbour. A swap only
// counts when it makes a row; the gems above fall down and new ones drop
// in from the top - chains score more. 30 moves; a row of four gives one
// move back, five or more two. 10 x 6 board of 30 px cells; the swap
// glides, cleared gems shrink away, the others fall.

#define M3_W    10
#define M3_H     6
#define M3_C    30                     // cell size
#define M3_X    10                     // board, top left
#define M3_Y    32
#define M3_T     6                     // kinds of gems
#define M3_SWAP 190                    // ms a swap takes
#define M3_CLR  320                    // ms the cleared gems take to vanish

static int8_t   m3[M3_H][M3_W];        // -1 = empty (only while things fall)
static int16_t  m3Fall[M3_H][M3_W];    // px a gem started above its place
static uint8_t  m3R, m3C;              // cursor
static bool     m3Held;                // a gem is picked
static int16_t  m3CurX, m3CurY;        // the cursor as drawn (glides)
// what the drawing animates: a swap (cells a, b and how far, 0..256), the
// gems being cleared (how far gone, 0..256) and the fall (ms since it began)
static uint8_t  m3SwA = 255, m3SwB = 255;
static uint16_t m3SwF, m3ClrF;
static const bool (*m3Mark)[M3_W] = NULL;
static uint32_t m3FallT;

static uint8_t m3New(uint8_t r, uint8_t c) {      // a gem that makes no row yet
  for (uint8_t t = 0; t < 20; t++) {
    uint8_t k = (uint8_t)random(M3_T);
    if (c >= 2 && m3[r][c - 1] == k && m3[r][c - 2] == k) continue;
    if (r >= 2 && m3[r - 1][c] == k && m3[r - 2][c] == k) continue;
    return k;
  }
  return (uint8_t)random(M3_T);
}

// marks every gem that is part of a row of 3+; returns the count, *best = longest row
static uint8_t m3Find(bool mark[M3_H][M3_W], uint8_t *best) {
  uint8_t n = 0;
  *best = 0;
  memset(mark, 0, sizeof(bool) * M3_H * M3_W);
  for (uint8_t r = 0; r < M3_H; r++)                     // rows
    for (uint8_t c = 0; c < M3_W; ) {
      uint8_t e = c;
      while (e < M3_W && m3[r][e] >= 0 && m3[r][e] == m3[r][c]) e++;
      if (m3[r][c] >= 0 && e - c >= 3) { for (uint8_t k = c; k < e; k++) mark[r][k] = true; if (e - c > *best) *best = (uint8_t)(e - c); }
      c = e > c ? e : (uint8_t)(c + 1);
    }
  for (uint8_t c = 0; c < M3_W; c++)                     // columns
    for (uint8_t r = 0; r < M3_H; ) {
      uint8_t e = r;
      while (e < M3_H && m3[e][c] >= 0 && m3[e][c] == m3[r][c]) e++;
      if (m3[r][c] >= 0 && e - r >= 3) { for (uint8_t k = r; k < e; k++) mark[k][c] = true; if (e - r > *best) *best = (uint8_t)(e - r); }
      r = e > r ? e : (uint8_t)(r + 1);
    }
  for (uint8_t r = 0; r < M3_H; r++) for (uint8_t c = 0; c < M3_W; c++) if (mark[r][c]) n++;
  return n;
}

static void m3Swap(uint8_t r1, uint8_t c1, uint8_t r2, uint8_t c2) {
  int8_t t = m3[r1][c1]; m3[r1][c1] = m3[r2][c2]; m3[r2][c2] = t;
}

// is there any swap that makes a row?
static bool m3CanMove() {
  bool mark[M3_H][M3_W];
  uint8_t best;
  for (uint8_t r = 0; r < M3_H; r++)
    for (uint8_t c = 0; c < M3_W; c++) {
      if (c + 1 < M3_W) { m3Swap(r, c, r, c + 1); bool ok = m3Find(mark, &best) > 0; m3Swap(r, c, r, c + 1); if (ok) return true; }
      if (r + 1 < M3_H) { m3Swap(r, c, r + 1, c); bool ok = m3Find(mark, &best) > 0; m3Swap(r, c, r + 1, c); if (ok) return true; }
    }
  return false;
}

static void m3Fill() {                                   // a fresh board with a move and no rows
  do {
    for (uint8_t r = 0; r < M3_H; r++) for (uint8_t c = 0; c < M3_W; c++) m3[r][c] = (int8_t)m3New(r, c);
  } while (!m3CanMove());
  for (uint8_t r = 0; r < M3_H; r++) for (uint8_t c = 0; c < M3_W; c++) m3Fall[r][c] = (int16_t)((M3_H - r + 1) * M3_C);
  m3FallT = gameMillis();                                // the new board drops in
}

// ---------------- drawing ----------------
// one gem around (cx, cy); s = 256 full size, less while it vanishes
static void m3Gem(int16_t cx, int16_t cy, int8_t k, uint16_t s) {
  int16_t r = (int16_t)(12 * s / 256);
  if (r < 1) return;
  switch (k) {
  case 0:                                                // red ball
    gfx.disc(cx, cy, r, C::DKRED);
    gfx.disc(cx - 1, cy - 1, r - 1, C::RED);
    gfx.disc(cx - r / 3, cy - r / 3, r / 3, rgb(255, 153, 153));
    break;
  case 1:                                                // blue block
    gfx.rbox(cx - r, cy - r, 2 * r, 2 * r, r / 3, C::DKBLUE);
    gfx.rbox(cx - r + 1, cy - r + 1, 2 * r - 3, 2 * r - 3, r / 3, C::BLUE);
    gfx.rbox(cx - r / 2, cy - r / 2, r, r / 2, 2, rgb(153, 204, 255));
    break;
  case 2:                                                // yellow diamond
    gfx.triangle(cx - r, cy, cx + r, cy, cx, cy - r - 1, C::YELLOW);
    gfx.triangle(cx - r, cy, cx + r, cy, cx, cy + r + 1, rgb(204, 153, 0));
    gfx.triangle(cx - r / 2, cy - 1, cx, cy - 1, cx, cy - r / 2 - 1, C::WHITE);
    break;
  case 3:                                                // green triangle
    gfx.triangle(cx, cy - r - 1, cx - r - 1, cy + r - 1, cx + r + 1, cy + r - 1, C::DKGRN);
    gfx.triangle(cx, cy - r + 2, cx - r + 2, cy + r - 3, cx + r - 2, cy + r - 3, C::GREEN);
    gfx.fill(cx - 1, cy - r / 3, 2, r / 2, C::LIME);
    break;
  case 4:                                                // purple cross
    gfx.rbox(cx - r, cy - r / 3 - 1, 2 * r + 1, 2 * (r / 3) + 3, 2, C::PURPLE);
    gfx.rbox(cx - r / 3 - 1, cy - r, 2 * (r / 3) + 3, 2 * r + 1, 2, C::PURPLE);
    gfx.fill(cx - r / 3, cy - r + 1, 2, r - 1, rgb(204, 153, 255));
    break;
  default:                                               // cyan ring
    gfx.disc(cx, cy, r, rgb(0, 128, 153));
    gfx.disc(cx, cy, r - 2, C::CYAN);
    gfx.disc(cx, cy, r / 2, rgb(0, 85, 102));
    gfx.fill(cx - r / 2 - 1, cy - r + 2, 3, 2, C::WHITE);
    break;
  }
}

static int16_t m3Px(uint8_t c) { return M3_X + c * M3_C + M3_C / 2; }
static int16_t m3Py(uint8_t r) { return M3_Y + r * M3_C + M3_C / 2; }

static void m3Glide(int16_t &v, int16_t target, uint32_t dt) {
  int16_t d = target - v;
  int16_t s = (int16_t)(d * (int32_t)dt / 50);
  if (!s && d) s = d > 0 ? 1 : -1;
  if ((d > 0 && s > d) || (d < 0 && s < d)) s = d;
  v += s;
}

// how far the gems have fallen after t ms (px): calm, a little acceleration
static int16_t m3Dropped(uint32_t t) {
  if (t > 1000) return 400;
  uint32_t d = t * t * 3 / 2000 + t / 12;
  return d > 400 ? 400 : (int16_t)d;
}

static void m3Draw(bool cursor, uint8_t moves, uint16_t combo, uint16_t score, const char *msg, uint32_t dt) {
  gfx.fill(0, 0, SCR_W, SCR_H, rgb(51, 0, 51));
  gfx.rbox(M3_X - 4, M3_Y - 4, M3_W * M3_C + 8, M3_H * M3_C + 8, 8, rgb(30, 10, 40));
  for (uint8_t r = 0; r < M3_H; r++)
    for (uint8_t c = 0; c < M3_W; c++)
      gfx.rbox(M3_X + c * M3_C + 1, M3_Y + r * M3_C + 1, M3_C - 2, M3_C - 2, 5, (r + c) & 1 ? rgb(51, 25, 70) : rgb(42, 20, 60));
  int16_t dropped = m3Dropped(gameMillis() - m3FallT);
  for (uint8_t r = 0; r < M3_H; r++)
    for (uint8_t c = 0; c < M3_W; c++) {
      if (m3[r][c] < 0) continue;
      int16_t x = m3Px(c), y = m3Py(r);
      uint8_t i = (uint8_t)(r * M3_W + c);
      if (m3SwA != 255 && (i == m3SwA || i == m3SwB)) {  // gliding to the other cell
        uint8_t o = i == m3SwA ? m3SwB : m3SwA;
        x = (int16_t)(x + (int32_t)(m3Px(o % M3_W) - x) * m3SwF / 256);
        y = (int16_t)(y + (int32_t)(m3Py(o / M3_W) - y) * m3SwF / 256);
      }
      int16_t up = m3Fall[r][c] - dropped;
      if (up > 0) y -= up;
      uint16_t s = 256;
      if (m3Mark && m3Mark[r][c]) {
        s = (uint16_t)(256 - m3ClrF);
        if (m3ClrF < 64) gfx.disc(x, y, 14, C::WHITE);  // a flash first
      }
      if (y > M3_Y - M3_C) m3Gem(x, y, m3[r][c], s);
    }
  gfx.fill(0, TOP_H, SCR_W, M3_Y - 4 - TOP_H, rgb(51, 0, 51));   // gems still above the board
  statusBar("MATCH 3", score, curHigh, C::PINK);
  if (cursor) {
    m3Glide(m3CurX, M3_X + m3C * M3_C, dt);
    m3Glide(m3CurY, M3_Y + m3R * M3_C, dt);
    uint32_t now = gameMillis();
    if (m3Held) {
      col hc = (now / 150) % 2 ? C::YELLOW : C::ORANGE;
      gfx.rframe(m3CurX - 1, m3CurY - 1, M3_C + 2, M3_C + 2, 6, hc);
      gfx.rframe(m3CurX, m3CurY, M3_C, M3_C, 5, hc);
      gfx.rframe(m3CurX + 1, m3CurY + 1, M3_C - 2, M3_C - 2, 4, hc);
      int16_t cx = m3CurX + M3_C / 2, cy = m3CurY + M3_C / 2, a = M3_C / 2 + 4;   // the four ways it can go
      if (m3R > 0)        gfx.triangle(cx, cy - a - 4, cx - 4, cy - a, cx + 4, cy - a, C::WHITE);
      if (m3R < M3_H - 1) gfx.triangle(cx, cy + a + 4, cx - 4, cy + a, cx + 4, cy + a, C::WHITE);
      if (m3C > 0)        gfx.triangle(cx - a - 4, cy, cx - a, cy - 4, cx - a, cy + 4, C::WHITE);
      if (m3C < M3_W - 1) gfx.triangle(cx + a + 4, cy, cx + a, cy - 4, cx + a, cy + 4, C::WHITE);
    } else {
      col cc = (now / 300) % 2 ? C::WHITE : C::CYAN;
      gfx.rframe(m3CurX, m3CurY, M3_C, M3_C, 5, cc);
      gfx.rframe(m3CurX + 1, m3CurY + 1, M3_C - 2, M3_C - 2, 4, cc);
    }
  }
  // below: moves, chain, hint
  const int16_t IY = M3_Y + M3_H * M3_C + 8;
  char b[16];
  gfx.text(M3_X, IY + 4, "MOVES", C::LTGREY, F_SMALL);
  snprintf(b, sizeof(b), "%u", moves);
  gfx.text(M3_X + 32, IY - 2, b, moves <= 5 ? C::RED : C::WHITE, F_BOLD);
  if (combo > 1) {
    snprintf(b, sizeof(b), "x%u", combo);
    gfx.text(120, IY - 2, b, C::YELLOW, F_BOLD);
  }
  if (msg) gfx.right(SCR_W - 10, IY + 2, msg, C::ORANGE, F_TEXT);
  else if (m3Held) gfx.right(SCR_W - 10, IY + 4, "arrow = swap, OK = drop", C::YELLOW, F_SMALL);
  else {
    snprintf(b, sizeof(b), "%s = pick", okKeys());
    gfx.right(SCR_W - 10, IY + 4, b, C::GREY, F_SMALL);
  }
}

// runs the drawing for ms milliseconds, *f going 0..256 (swap / clear); false = the player left
static bool m3Anim(uint32_t ms, uint16_t *f, bool back, uint8_t moves, uint16_t combo, uint16_t score) {
  uint32_t t0 = gameMillis();
  while (true) {
    if (!poll()) return false;
    uint32_t t = gameMillis() - t0;
    if (t >= ms) break;
    uint32_t v = t * 256 / ms;
    if (back) v = v < 128 ? v * 2 : (256 - v) * 2;       // there and back again
    if (f) *f = (uint16_t)v;
    m3Draw(false, moves, combo, score, NULL, 0);
    gfx.send();
  }
  if (f) *f = back ? 0 : 256;
  return true;
}

// lets the falling gems land; false = the player left
static bool m3Land(uint8_t moves, uint16_t combo, uint16_t score) {
  int16_t most = 0;
  for (uint8_t r = 0; r < M3_H; r++) for (uint8_t c = 0; c < M3_W; c++) if (m3Fall[r][c] > most) most = m3Fall[r][c];
  while (m3Dropped(gameMillis() - m3FallT) < most) {
    if (!poll()) return false;
    m3Draw(false, moves, combo, score, NULL, 0);
    gfx.send();
  }
  return true;
}

static void m3Game() {
  bool again = true;
  while (again) {
    again = false;
    m3SwA = m3SwB = 255;
    m3Mark = NULL;
    m3Fill();
    uint16_t score = 0;
    uint8_t  moves = 30;
    m3R = 2; m3C = 4; m3Held = false;
    m3CurX = M3_X + m3C * M3_C; m3CurY = M3_Y + m3R * M3_C;
    uint32_t last = gameMillis();
    btnClear();
    while (true) {
      if (!poll()) return;
      uint32_t now = gameMillis(), dt = now - last;
      last = now;
      int8_t dr = 0, dc = 0;
      if (btn(B_UP))    dr = -1;
      if (btn(B_DOWN))  dr = 1;
      if (btn(B_LEFT))  dc = -1;
      if (btn(B_RIGHT)) dc = 1;
      if (btnTap(B_OK)) { m3Held = !m3Held; sfx(m3Held ? 900 : 500, 20); }
      if (dr || dc) {
        int8_t nr = (int8_t)(m3R + dr), nc = (int8_t)(m3C + dc);
        if (dr && dc) nc = (int8_t)m3C;                  // one way at a time
        if (nr >= 0 && nr < M3_H && nc >= 0 && nc < M3_W) {
          if (!m3Held) { m3R = (uint8_t)nr; m3C = (uint8_t)nc; sfx(700, 8); }
          else {
            m3Held = false;
            bool mark[M3_H][M3_W];
            uint8_t best;
            m3Swap(m3R, m3C, (uint8_t)nr, (uint8_t)nc);
            bool ok = m3Find(mark, &best) > 0;
            m3Swap(m3R, m3C, (uint8_t)nr, (uint8_t)nc);
            m3SwA = (uint8_t)(m3R * M3_W + m3C); m3SwB = (uint8_t)(nr * M3_W + nc);
            if (!ok) {                                   // no row: there and back
              sfx(200, 120);
              if (!m3Anim(M3_SWAP * 2, &m3SwF, true, moves, 0, score)) return;
              m3SwA = m3SwB = 255;
            } else {
              sfx(800, 20);
              if (!m3Anim(M3_SWAP, &m3SwF, false, moves, 0, score)) return;
              m3SwA = m3SwB = 255;
              m3Swap(m3R, m3C, (uint8_t)nr, (uint8_t)nc);
              moves--;
              m3R = (uint8_t)nr; m3C = (uint8_t)nc;
              m3CurX = M3_X + m3C * M3_C; m3CurY = M3_Y + m3R * M3_C;
              uint16_t combo = 0;
              uint8_t cnt;
              while ((cnt = m3Find(mark, &best)) > 0) {  // clear, fall, refill - as long as rows appear
                combo++;
                score += cnt * 10 * combo;
                if (best == 4) moves++;
                if (best >= 5) moves += 2;
                sfx(900 + combo * 150, 60);
                m3Mark = mark;
                bool stay = m3Anim(M3_CLR, &m3ClrF, false, moves, combo, score);
                m3Mark = NULL;
                if (!stay) return;
                for (uint8_t c = 0; c < M3_W; c++) {     // gravity: how far each gem falls
                  int8_t w = M3_H - 1;
                  for (int8_t r = M3_H - 1; r >= 0; r--)
                    if (!mark[r][c]) { m3[w][c] = m3[r][c]; m3Fall[w][c] = (int16_t)((w - r) * M3_C); w--; }
                  int16_t fresh = (int16_t)((w + 1) * M3_C);
                  while (w >= 0) { m3[w][c] = (int8_t)random(M3_T); m3Fall[w][c] = fresh; w--; }
                }
                m3FallT = gameMillis();
                if (!m3Land(moves, combo, score)) return;
              }
              if (!m3CanMove()) {                        // stuck: a new board
                sfx(400, 200);
                m3Fill();
                if (!m3Land(moves, 0, score)) return;
              }
            }
          }
        }
      }
      m3Draw(true, moves, 0, score, NULL, dt);
      gfx.send();
      if (!moves) {
        uint32_t t0 = gameMillis();
        while (gameMillis() - t0 < 1200) {
          if (!poll()) return;
          m3Draw(false, 0, 0, score, "END", 0);
          gfx.send();
        }
        again = gameOver(score);
        break;
      }
    }
  }
}

void match3Run() {
  m3Game();
  m3Mark = NULL;
}

static const char *const IC_MATCH3[12] = {
  "............", ".rr..bb..yy.", "rrrrbbbbyyyy", "rrrrbbbbyyyy", ".rr..bb..yy.", "............",
  "..g...p...cc", ".ggg.ppp.cwc", "ggggg.p..ccc", "............", ".rr..rr..rr.", ".rr..rr..rr." };
