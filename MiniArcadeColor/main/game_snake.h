// MiniArcade Color: Snake. Part of console.cpp, which includes it.
// 22 x 14 fields of 14 px on a lawn inside a stone wall. Arrows steer; an
// apple is a point, the golden star (it comes now and then and goes again)
// five. The snake gets a little faster with every apple.

#define SN_C   14                      // field size
#define SN_W   22
#define SN_H   14
#define SN_X0  6                       // the lawn, top left
#define SN_Y0  (TOP_H + 9)
#define SN_MAX (SN_W * SN_H)

static uint8_t snX[SN_MAX], snY[SN_MAX];
static uint16_t snLen;

static bool snOnSnake(uint8_t x, uint8_t y, uint16_t from = 0) {
  for (uint16_t i = from; i < snLen; i++) if (snX[i] == x && snY[i] == y) return true;
  return false;
}

static void snPlace(uint8_t &fx, uint8_t &fy) {
  do { fx = (uint8_t)random(SN_W); fy = (uint8_t)random(SN_H); } while (snOnSnake(fx, fy));
}

// ---------------- drawing (online Snake uses it too, with 10 px fields) ----------------
static uint32_t snHash(uint32_t x, uint32_t y) {
  uint32_t h = x * 374761393u + y * 668265263u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}

// w x h fields of s px from (x0, y0): a soft chequer with tufts of grass, in the shade of the wall at the top and left
static void snLawn(int16_t x0, int16_t y0, uint8_t w, uint8_t h, uint8_t s) {
  for (uint8_t y = 0; y < h; y++)
    for (uint8_t x = 0; x < w; x++) {
      int16_t px = x0 + x * s, py = y0 + y * s;
      gfx.fill(px, py, s, s, (x + y) & 1 ? rgb(51, 128, 0) : rgb(51, 128, 51));
      uint32_t r = snHash(x, y);
      for (uint8_t k = 0; k < 2; k++, r >>= 10) {            // a little v of blades
        int16_t tx = px + (int16_t)(r % (s - 3)), ty = py + 1 + (int16_t)((r >> 5) % (s - 3));
        col c = k ? rgb(0, 85, 0) : rgb(102, 170, 51);
        gfx.pixel(tx, ty, c); gfx.pixel(tx + 2, ty, c); gfx.pixel(tx + 1, ty + 1, c);
      }
    }
  gfx.fill(x0, y0, w * s, 2, rgb(0, 85, 0));
  fxDitherFill(x0, y0 + 2, w * s, 2, rgb(0, 85, 0));
  gfx.fill(x0, y0, 2, h * s, rgb(0, 85, 0));
  fxDitherFill(x0 + 2, y0, 2, h * s, rgb(0, 85, 0));
}

// grey stones over the play area, leaving out the lawn (lx, ly, lw, lh)
static void snWall(int16_t lx, int16_t ly, int16_t lw, int16_t lh) {
  gfx.fill(0, TOP_H, SCR_W, SCR_H - TOP_H, GREY1);           // the mortar
  for (int16_t y = TOP_H, row = 0; y < SCR_H; y += 5, row++)
    for (int16_t x = -(row & 1) * 6; x < SCR_W; x += 12) {
      if (x >= lx && x + 11 <= lx + lw && y >= ly && y + 4 <= ly + lh) continue;
      uint32_t r = snHash((uint32_t)(x + 16), (uint32_t)y);
      col face = r & 3 ? GREY2 : GREY3;
      gfx.fill(x, y, 11, 4, face);
      gfx.hline(x, y, 11, face == GREY3 ? GREY4 : GREY3);    // lit from above
      if (r & 0x30) gfx.pixel(x + 2 + (int16_t)((r >> 8) % 7), y + 2, GREY1);
    }
  gfx.frame(lx - 1, ly - 1, lw + 2, lh + 2, C::BLACK);
}

// a shiny apple in the field at (px, py), s px
static void snApple(int16_t px, int16_t py, uint8_t s = SN_C) {
  int16_t cx = px + s / 2, cy = py + s / 2, r = s / 2 - 2;
  gfx.hline(cx - r + 1, cy + r + 1, 2 * r - 1, rgb(0, 85, 0));   // its shadow
  gfx.disc(cx, cy, r, rgb(153, 0, 0));
  gfx.disc(cx - 1, cy - 1, r - 1, C::RED);
  gfx.pixel(cx, cy - r, rgb(102, 0, 0));                     // the dip at the stem
  if (r > 3) { gfx.fill(cx - 3, cy - 3, 2, 2, rgb(255, 153, 153)); gfx.pixel(cx - 3, cy - 3, C::WHITE); }
  else gfx.pixel(cx - 1, cy - 1, C::WHITE);
  gfx.vline(cx, cy - r - 2, 2, rgb(102, 42, 0));
  gfx.hline(cx + 1, cy - r - 1, 2, rgb(51, 170, 0));         // the leaf
  gfx.hline(cx + 2, cy - r - 2, 2, C::LIME);
}

static void snStar(int16_t px, int16_t py, uint32_t t) {
  int16_t cx = px + SN_C / 2, cy = py + SN_C / 2;
  fxDitherDisc(cx, cy, 10, rgb(255, 212, 0), (uint8_t)(t / 160));   // a shimmering glow
  col c = (t / 120) % 2 ? C::YELLOW : C::ORANGE;
  gfx.triangle(cx, cy - 7, cx - 6, cy + 5, cx + 6, cy + 5, rgb(204, 85, 0));
  gfx.triangle(cx, cy + 7, cx - 7, cy - 3, cx + 7, cy - 3, rgb(204, 85, 0));
  gfx.triangle(cx, cy - 6, cx - 5, cy + 4, cx + 5, cy + 4, c);
  gfx.triangle(cx, cy + 6, cx - 6, cy - 2, cx + 6, cy - 2, c);
  if ((t / 90) % 8 == 0) fxStar(cx - 2, cy - 2, 3, C::WHITE);
}

// a snake's colours: outline, body, band, light (the back), shade (the flanks), spots
enum { SNP_K, SNP_B, SNP_B2, SNP_L, SNP_S, SNP_SPOT };
static const col SN_GREEN[6]  = { rgb(0, 42, 0), rgb(102, 212, 0), rgb(51, 170, 0), rgb(204, 255, 102), rgb(0, 128, 0), rgb(0, 85, 0) };
static const col SN_ORANGE[6] = { rgb(102, 0, 0), rgb(255, 170, 0), rgb(255, 128, 0), rgb(255, 255, 153), rgb(204, 85, 0), rgb(153, 42, 0) };
static const col SN_RED[6]    = { rgb(51, 0, 0), rgb(255, 51, 51), rgb(204, 0, 0), rgb(255, 170, 153), rgb(153, 0, 0), rgb(102, 0, 0) };

static int16_t snRad(uint16_t k, int16_t R) { return k <= 1 ? R * 5 / 6 : R; }   // k fields from the tail

// the tube of a field from `skip` px off its centre to its edge towards (dx, dy), w px either side (0 = a line)
static void snHalf(int16_t cx, int16_t cy, int8_t dx, int8_t dy, int16_t w, uint8_t s, col c, int16_t skip = 0) {
  if (w < 0) return;
  int16_t a = s / 2, b = s - s / 2;                          // centre to the left edge / past the right one
  if (dx) gfx.fill(dx > 0 ? cx + skip : cx - a, cy - w, (dx > 0 ? b : a + 1) - skip, 2 * w + 1, c);
  else    gfx.fill(cx - w, dy > 0 ? cy + skip : cy - a, 2 * w + 1, (dy > 0 ? b : a + 1) - skip, c);
}

/* A snake of n fields (xs, ys; [0] = the head) on fields of s px from
   (x0, y0): a round tube with a light back, darker flanks, bands and
   spots, the last fields thinner down to a pointed tail, a head with eyes
   looking ahead and a flicking tongue. dead: crossed-out eyes.         */
static void snSnake(const uint8_t *xs, const uint8_t *ys, uint16_t n, int16_t x0, int16_t y0, uint8_t s,
                    const col *pal, bool dead, uint32_t now) {
  if (!n) return;
  static const uint8_t OFF[4] = { 0, 1, 2, 9 };              // outline, flanks, body, the light back: each narrower
  int16_t R = s >= 12 ? s / 2 - 2 : s / 2 - 1;               // half the width: some grass between two rows
  for (uint8_t p = 0; p < 4; p++)
    for (int16_t i = (int16_t)n - 1; i >= 0; i--) {
      int16_t cx = x0 + xs[i] * s + s / 2, cy = y0 + ys[i] * s + s / 2, r = snRad((uint16_t)(n - 1 - i), R);
      col c = p == 0 ? pal[SNP_K] : (p == 1 ? pal[SNP_S] : (p == 3 ? pal[SNP_L] : pal[(i & 2) ? SNP_B2 : SNP_B]));
      if (i == n - 1 && n > 1) {                             // the tail: a point
        if (p == 3) continue;
        int8_t dx = (int8_t)(xs[i - 1] - xs[i]), dy = (int8_t)(ys[i - 1] - ys[i]);
        int16_t w = r - OFF[p], e = dx + dy > 0 ? s - s / 2 - 1 : -(s / 2), t = (dx + dy > 0 ? -(s / 2) : s - s / 2 - 1) + (dx + dy) * OFF[p] * 2;
        if (w <= 0) continue;
        if (dx) gfx.triangle(cx + e, cy - w, cx + e, cy + w, cx + t, cy, c);
        else    gfx.triangle(cx - w, cy + e, cx + w, cy + e, cx, cy + t, c);
        continue;
      }
      int16_t w = p == 3 ? 0 : r - OFF[p];
      if (i > 0 && p < 3) gfx.rbox(cx - w, cy - w, 2 * w + 1, 2 * w + 1, w / 2 + 1, c);
      for (int8_t j = -1; j <= 1; j += 2) {                  // towards both neighbours
        int16_t k = i + j;
        if (k < 0 || k >= n || (p == 3 && k == n - 1)) continue;
        int16_t rk = snRad((uint16_t)(n - 1 - k), R);
        snHalf(cx, cy, (int8_t)(xs[k] - xs[i]), (int8_t)(ys[k] - ys[i]), p == 3 ? 0 : (rk < r ? rk : r) - OFF[p], s, c);
      }
      if (p == 3 && i > 0 && (i & 1) && s >= 12 && i < n - 2) {   // a spot on the back
        gfx.fill(cx - 1, cy, 3, 1, pal[SNP_SPOT]); gfx.fill(cx, cy - 1, 1, 3, pal[SNP_SPOT]);
      }
    }
  // the head, a little wider than the body and pushed forward
  int16_t cx = x0 + xs[0] * s + s / 2, cy = y0 + ys[0] * s + s / 2;
  int8_t dx = 1, dy = 0;
  if (n > 1) { dx = (int8_t)(xs[0] - xs[1]); dy = (int8_t)(ys[0] - ys[1]); }
  int16_t hx = cx + dx, hy = cy + dy, hw = dx ? R : R + 2, hh = dx ? R + 2 : R;
  for (uint8_t p = 0; p < 3; p++) {
    int16_t o = p, m = (hw < hh ? hw : hh) - o;
    gfx.rbox(hx - hw + o, hy - hh + o, 2 * (hw - o) + 1, 2 * (hh - o) + 1, m * 2 / 3, p == 0 ? pal[SNP_K] : (p == 1 ? pal[SNP_S] : pal[SNP_B]));
  }
  if (n > 1)                                                 // the neck runs into it, without a seam
    for (uint8_t p = 1; p < 4; p++)
      snHalf(cx, cy, (int8_t)-dx, (int8_t)-dy, p == 3 ? 0 : snRad((uint16_t)(n - 2), R) - OFF[p], s,
             p == 1 ? pal[SNP_S] : (p == 2 ? pal[SNP_B] : pal[SNP_L]), p == 3 ? 0 : R - 3);
  int16_t f = s >= 12 ? 1 : 0, q = (dx ? hh : hw) - (s >= 12 ? 3 : 2);
  if (s >= 12)                                               // a dark chevron between the eyes
    for (int8_t side = -1; side <= 1; side += 2)
      gfx.line(hx - dx * 3, hy - dy * 3, hx - dx * 1 - dy * 2 * side, hy - dy * 1 + dx * 2 * side, pal[SNP_SPOT]);
  for (int8_t side = -1; side <= 1; side += 2) {
    int16_t ex = hx + dx * f - dy * q * side, ey = hy + dy * f + dx * q * side;
    if (s >= 12) {
      gfx.disc(ex, ey, 2, C::WHITE);
      if (dead) { gfx.line(ex - 1, ey - 1, ex + 1, ey + 1, C::BLACK); gfx.line(ex - 1, ey + 1, ex + 1, ey - 1, C::BLACK); }
      else if (dx) gfx.vline(ex + dx, ey - 1, 3, C::BLACK);  // slit pupils, looking ahead
      else gfx.hline(ex - 1, ey + dy, 3, C::BLACK);
      gfx.pixel(hx + dx * (hw - 1) - dy * 2 * side, hy + dy * (hh - 1) + dx * 2 * side, pal[SNP_K]);   // nostrils
    } else {
      gfx.pixel(ex, ey, dead ? C::BLACK : C::WHITE);
      gfx.pixel(ex + dx, ey + dy, C::BLACK);
    }
  }
  static const uint8_t TONGUE[4] = { 2, 4, 5, 3 };            // flicks out now and then
  uint8_t ph = (uint8_t)((now / 70) % 14);
  if (!dead && ph < 4) {
    int16_t L = TONGUE[ph] * s / 14 + 1, tx = hx + dx * (hw + 1), ty = hy + dy * (hh + 1);
    gfx.line(tx, ty, tx + dx * (L - 1), ty + dy * (L - 1), C::RED);
    if (L >= 4) {
      int16_t fx = tx + dx * L, fy = ty + dy * L;
      gfx.pixel(fx - dy, fy - dx, C::RED); gfx.pixel(fx + dy, fy + dx, C::RED);
      gfx.pixel(fx + dx - dy, fy + dy - dx, rgb(255, 102, 153)); gfx.pixel(fx + dx + dy, fy + dy + dx, rgb(255, 102, 153));
    }
  }
}

// the middle of a field on screen
static int16_t snPx(uint8_t x) { return (int16_t)(SN_X0 + x * SN_C + SN_C / 2); }
static int16_t snPy(uint8_t y) { return (int16_t)(SN_Y0 + y * SN_C + SN_C / 2); }

void snakeRun() {
  static const int8_t DX[4] = { 0, 0, -1, 1 }, DY[4] = { -1, 1, 0, 0 };   // as B_UP..B_RIGHT
  bool again = true;
  while (again) {
    again = false;
    snLen = 4;
    for (uint16_t i = 0; i < snLen; i++) { snX[i] = (uint8_t)(10 - i); snY[i] = SN_H / 2; }
    uint8_t dir = B_RIGHT, queue[2], nq = 0;
    uint8_t fx, fy, sx = 0, sy = 0;
    snPlace(fx, fy);
    bool star = false;
    uint32_t starUntil = 0, nextStar = gameMillis() + 8000, deadAt = 0;
    uint16_t score = 0, step = 180, popped = 0;                // ms per field
    bool started = false, dead = false, over = false;
    uint32_t next = gameMillis();
    fxReset();
    btnClear();
    while (poll()) {
      for (uint8_t k = 0; k < 4; k++)
        if (btn(k)) {
          uint8_t last = nq ? queue[nq - 1] : dir;
          bool opposite = (k ^ 1) == last;                     // UP/DOWN and LEFT/RIGHT pairs
          if (k != last && !opposite && nq < 2) queue[nq++] = k;
          started = true;
        }
      uint32_t now = gameMillis();
      if (!started) next = now;
      while (started && !dead && (int32_t)(now - next) >= 0) {
        next += step;
        if (nq) { dir = queue[0]; queue[0] = queue[1]; nq--; }
        int16_t nx = snX[0] + DX[dir], ny = snY[0] + DY[dir];
        bool grow = nx == fx && ny == fy;
        bool gold = star && nx == sx && ny == sy;
        bool tailLeaves = snX[snLen - 1] == nx && snY[snLen - 1] == ny && !grow;   // that field gets free
        if (nx < 0 || ny < 0 || nx >= SN_W || ny >= SN_H || (snOnSnake((uint8_t)nx, (uint8_t)ny) && !tailLeaves)) {
          dead = true;
          deadAt = now;
          sfx(150, 400);
          int16_t bx = (int16_t)(snPx(snX[0]) + DX[dir] * SN_C / 2), by = (int16_t)(snPy(snY[0]) + DY[dir] * SN_C / 2);
          fxBurst(bx, by, 14, C::WHITE, C::LIME, 150, 450, 0, 2, FX_STREAK);
          fxBurst(bx, by, 6, rgb(204, 255, 102), rgb(0, 85, 0), 70, 500, 0, 4, FX_DRAG | FX_GROW);
          fxShake(6, 450); fxFlash(C::RED, 260, 110);
          break;
        }
        if (grow && snLen < SN_MAX) snLen++;
        for (uint16_t i = snLen - 1; i > 0; i--) { snX[i] = snX[i - 1]; snY[i] = snY[i - 1]; }
        snX[0] = (uint8_t)nx; snY[0] = (uint8_t)ny;
        if (grow) {
          score++; sfx(1200, 30);
          int16_t ax = snPx((uint8_t)nx), ay = snPy((uint8_t)ny);
          fxBurst(ax, ay, 10, C::RED, rgb(102, 0, 0), 100, 450, 30, 2);
          fxBurst(ax, ay - 4, 3, C::LIME, C::DKGRN, 60, 450, 20, 2);
          fxRing(ax, ay, 11, C::WHITE, C::RED, 220);
          fxText(ax, ay - 16, "+1", C::YELLOW);
          snPlace(fx, fy); if (step > 85) step -= 3;
        }
        if (gold) {
          score += 5; star = false; sfx(1800, 80); nextStar = now + 10000;
          int16_t ax = snPx((uint8_t)nx), ay = snPy((uint8_t)ny);
          fxBurst(ax, ay, 16, C::WHITE, C::ORANGE, 160, 600, 20, 2);
          fxRing(ax, ay, 20, C::YELLOW, C::ORANGE, 300);
          fxText(ax, ay - 20, "+5", C::YELLOW, F_TEXT);
          fxFlash(C::YELLOW, 160, 60);
        }
      }
      if (!star && now >= nextStar && started) { do snPlace(sx, sy); while (sx == fx && sy == fy); star = true; starUntil = now + 6000; }
      if (star && now >= starUntil) { star = false; nextStar = now + 9000; }

      // a crash: the snake flashes, then bursts field by field from the tail
      uint16_t shown = snLen;
      const col *pal = SN_GREEN;
      if (dead) {
        uint32_t t = now - deadAt;
        uint16_t per = (uint16_t)(500 / snLen);
        if (per > 50) per = 50;
        if (per < 8) per = 8;
        uint16_t gone = t < 300 ? 0 : (uint16_t)((t - 300) / per);
        if (gone > snLen) gone = snLen;
        for (; popped < gone; popped++) {
          uint16_t i = (uint16_t)(snLen - 1 - popped);
          int16_t x = snPx(snX[i]), y = snPy(snY[i]);
          fxBurst(x, y, i ? 5 : 12, i ? rgb(255, 170, 153) : C::WHITE, rgb(102, 0, 0), i ? 90 : 140, 500, 40, i ? 3 : 2);
          if (i % 3 == 0) fxRing(x, y, i ? 8 : 16, C::WHITE, C::RED, 200);
        }
        shown = (uint16_t)(snLen - gone);
        pal = t < 300 && (t / 100) % 2 == 0 ? SN_GREEN : SN_RED;
        if (t > 300 + (uint32_t)per * snLen + 450) over = true;
      }

      fxUpdate();
      statusBar("SNAKE", score, curHigh, C::GREEN);
      snWall(SN_X0, SN_Y0, SN_W * SN_C, SN_H * SN_C);
      snLawn(SN_X0, SN_Y0, SN_W, SN_H, SN_C);
      snApple(SN_X0 + fx * SN_C, SN_Y0 + fy * SN_C);
      if (now % 1800 < 110) fxStar(snPx(fx) - 3, snPy(fy) - 3, 2, C::WHITE);   // a glint on the apple
      if (star && (starUntil - now > 1500 || (now / 100) % 2)) snStar(SN_X0 + sx * SN_C, SN_Y0 + sy * SN_C, now);
      snSnake(snX, snY, shown, SN_X0, SN_Y0, SN_C, pal, dead, now);
      fxDraw();
      fxShakeApply();
      fxFlashApply(SN_X0, SN_Y0, SN_W * SN_C, SN_H * SN_C);
      if (!started) {
        gfx.rbox(64, 112, 192, 32, 8, C::BLACK);
        gfx.rframe(64, 112, 192, 32, 8, C::LIME);
        gfx.center(119, "press an arrow", C::WHITE);
      }
      gfx.send();
      if (over) break;
    }
    if (!over) return;                                         // left from the pause menu
    again = gameOver(score);
  }
}
