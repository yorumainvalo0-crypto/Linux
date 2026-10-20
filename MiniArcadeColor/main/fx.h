// MiniArcade Color: small effects the games share - a particle pool,
// screen shake, a colour flash, rising score text and a few glow shapes.
// Part of console.cpp, which includes it before the games.
// Everything runs on gameMillis(): the same at any frame rate, and it
// stands still in the pause menu. One pool for all games: fxReset() when
// a round starts, fxUpdate() once per frame, fxDraw() after the scene.
// The pool is static and small (about 1 kB); a full pool hands out its
// oldest particles again, so a burst never fails and never overflows.

#define FX_N    48                     // particles
#define FX_TXT  4                      // rising texts

enum { FX_DRAG = 1, FX_GROW = 2, FX_RING = 4, FX_STREAK = 8 };

struct FxPart {
  int32_t  x, y;                       // 1/256 px
  int16_t  vx, vy;                     // 1/16 px per second
  uint16_t age, life;                  // ms; life 0 = free slot
  col      c0, c1;                     // colour at birth and at the end
  int8_t   grav;                       // 10 px/s^2 (negative rises: smoke)
  uint8_t  size, flags;
};
struct FxText { int16_t x, y; uint16_t age; col c; Font f; char s[8]; };

static FxPart   fxP[FX_N];
static FxText   fxT[FX_TXT];
static uint8_t  fxNext = 0;            // where the search for a free slot starts
static uint32_t fxLast = 0, fxSeed = 0x2545F491u;
static uint16_t fxShakeT = 0, fxShakeLen = 1, fxFlashT = 0, fxFlashLen = 1;
static uint8_t  fxShakeAmp = 0, fxFlashMax = 200;
static int8_t   fxSX = 0, fxSY = 0;    // the shake's offset this frame
static col      fxFlashC = 0;

// an own random generator: effects never change the games' random()
static uint16_t fxRand(uint16_t n) {
  fxSeed ^= fxSeed << 13; fxSeed ^= fxSeed >> 17; fxSeed ^= fxSeed << 5;
  return n ? (uint16_t)(fxSeed % n) : 0;
}
static int16_t fxRange(int16_t lo, int16_t hi) { return (int16_t)(lo + fxRand((uint16_t)(hi - lo + 1))); }

// sin / cos in 32 steps a turn, *64 (0 = right, 8 = down)
static int8_t fxSin(uint8_t a) {
  static const int8_t Q[9] = { 0, 12, 24, 36, 45, 53, 59, 63, 64 };
  a &= 31;
  int8_t v = Q[a <= 8 ? a : (a <= 16 ? 16 - a : (a <= 24 ? a - 16 : 32 - a))];
  return a > 16 ? (int8_t)-v : v;
}
static int8_t fxCos(uint8_t a) { return fxSin((uint8_t)(a + 8)); }

/* colLerp, but a grey stays grey: the cube has no neutral middle greys
   (they come out pink or green), so near-grey results take the nearest
   of the four extra greys.                                            */
static col fxLerp(col a, col b, uint8_t t) {
  uint8_t r0, g0, b0, r1, g1, b1;
  colToRgb(a, r0, g0, b0);
  colToRgb(b, r1, g1, b1);
  int16_t r = (int16_t)(r0 + (r1 - r0) * t / 255), g = (int16_t)(g0 + (g1 - g0) * t / 255), bl = (int16_t)(b0 + (b1 - b0) * t / 255);
  if (abs(r - g) < 28 && abs(g - bl) < 28 && abs(r - bl) < 28) {
    static const uint8_t L[6] = { 0, 32, 96, 160, 224, 255 };
    static const col     G[6] = { C::BLACK, GREY1, GREY2, GREY3, GREY4, C::WHITE };
    int16_t v = (int16_t)((r + g + bl) / 3), best = 0;
    for (uint8_t k = 1; k < 6; k++) if (abs(v - L[k]) < abs(v - L[best])) best = k;
    return G[best];
  }
  return rgb((uint8_t)r, (uint8_t)g, (uint8_t)bl);
}

static void fxReset() {
  memset(fxP, 0, sizeof(fxP));
  memset(fxT, 0, sizeof(fxT));
  fxNext = 0;
  fxShakeT = fxFlashT = 0;
  fxSX = fxSY = 0;
  fxLast = gameMillis();
}

static inline uint8_t fxCount() {                      // particles alive (the tests)
  uint8_t n = 0;
  for (uint8_t i = 0; i < FX_N; i++) if (fxP[i].life) n++;
  return n;
}

static int16_t fxClampV(int32_t v) { return (int16_t)(v > 32000 ? 32000 : (v < -32000 ? -32000 : v)); }

// one particle at (x, y) px, speed (vx, vy) px/s, living `life` ms
static void fxEmit(int16_t x, int16_t y, int16_t vx, int16_t vy, uint16_t life, col c0, col c1,
                   uint8_t size = 2, int8_t grav = 0, uint8_t flags = 0) {
  uint8_t k = fxNext;
  for (uint8_t i = 0; i < FX_N && fxP[k].life; i++) k = (uint8_t)((k + 1) % FX_N);
  if (fxP[k].life) k = fxNext;                           // all busy: the next in turn goes
  fxNext = (uint8_t)((k + 1) % FX_N);
  FxPart &p = fxP[k];
  p.x = (int32_t)x * 256; p.y = (int32_t)y * 256;
  p.vx = fxClampV((int32_t)vx * 16); p.vy = fxClampV((int32_t)vy * 16);
  p.age = 0; p.life = life ? life : 1;
  p.c0 = c0; p.c1 = c1; p.size = size; p.grav = grav; p.flags = flags;
}

// n particles flying apart from (x, y) at up to `speed` px/s
static void fxBurst(int16_t x, int16_t y, uint8_t n, col c0, col c1, int16_t speed,
                    uint16_t life = 500, int8_t grav = 0, uint8_t size = 2, uint8_t flags = FX_DRAG) {
  for (uint8_t k = 0; k < n; k++) {
    uint8_t a = (uint8_t)fxRand(32);
    int16_t s = (int16_t)(speed / 4 + fxRand((uint16_t)(speed * 3 / 4 + 1)));
    fxEmit(x, y, (int16_t)(fxCos(a) * s / 64), (int16_t)(fxSin(a) * s / 64),
           (uint16_t)(life / 2 + fxRand((uint16_t)(life / 2 + 1))), c0, c1, size, grav, flags);
  }
}

// a ring growing to radius r (shock wave)
static void fxRing(int16_t x, int16_t y, uint8_t r, col c0, col c1, uint16_t ms) {
  fxEmit(x, y, 0, 0, ms, c0, c1, r, 0, FX_RING);
}

// a ready-made explosion: a hot core, a fireball that swells, sparks and a shock ring; power 1..3
static void fxExplode(int16_t x, int16_t y, uint8_t power) {
  fxBurst(x, y, (uint8_t)(3 + power * 2), C::ORANGE, rgb(51, 0, 0), (int16_t)(30 + power * 20), (uint16_t)(300 + power * 70), -3,
          (uint8_t)(3 + power * 2), FX_DRAG | FX_GROW);
  fxBurst(x, y, (uint8_t)(2 + power), C::WHITE, C::YELLOW, (int16_t)(15 + power * 10), (uint16_t)(160 + power * 50), 0, (uint8_t)(4 + power * 2));
  fxBurst(x, y, (uint8_t)(5 + power * 4), C::YELLOW, C::RED, (int16_t)(110 + power * 50), (uint16_t)(260 + power * 80), 25, 2, FX_STREAK);
  fxRing(x, y, (uint8_t)(8 + power * 9), C::WHITE, C::ORANGE, (uint16_t)(200 + power * 70));
}

// shake the picture by up to px pixels, dying away over ms
static void fxShake(uint8_t px, uint16_t ms) {
  if (fxShakeT && px < fxShakeAmp * fxShakeT / fxShakeLen) return;   // a stronger one runs
  fxShakeAmp = px; fxShakeT = fxShakeLen = ms ? ms : 1;
}

// tint the picture towards c, fading out over ms
static void fxFlash(col c, uint16_t ms, uint8_t strength = 200) { fxFlashC = c; fxFlashMax = strength; fxFlashT = fxFlashLen = ms ? ms : 1; }

// rising text ("+10"), gone after a second
static void fxText(int16_t x, int16_t y, const char *s, col c, Font f = F_SMALL) {
  uint8_t k = 0;                                         // a free one, else the oldest
  for (uint8_t i = 0; i < FX_TXT; i++) {
    if (!fxT[i].s[0]) { k = i; break; }
    if (fxT[i].age > fxT[k].age) k = i;
  }
  FxText &t = fxT[k];
  strncpy(t.s, s, sizeof(t.s) - 1); t.s[sizeof(t.s) - 1] = 0;
  t.x = (int16_t)(x - Gfx::width(t.s, f) / 2); t.y = y; t.age = 0; t.c = c; t.f = f;
}

static void fxUpdate() {
  uint32_t now = gameMillis();
  uint32_t d = now - fxLast;
  fxLast = now;
  if (d > 50) d = 50;                                    // a slow frame (or the first): at most 50 ms
  int32_t dt = (int32_t)d;
  for (uint8_t i = 0; i < FX_N; i++) {
    FxPart &p = fxP[i];
    if (!p.life) continue;
    if (p.age + dt >= p.life) { p.life = 0; continue; }
    p.age = (uint16_t)(p.age + dt);
    p.x += (int32_t)p.vx * dt * 2 / 125;                 // 1/16 px/s * ms -> 1/256 px
    p.y += (int32_t)p.vy * dt * 2 / 125;
    if (p.grav) p.vy = fxClampV(p.vy + (int32_t)p.grav * dt * 4 / 25);
    if (p.flags & FX_DRAG) { p.vx -= (int16_t)(p.vx * dt / 400); p.vy -= (int16_t)(p.vy * dt / 400); }
  }
  for (uint8_t i = 0; i < FX_TXT; i++)
    if (fxT[i].s[0] && (fxT[i].age += (uint16_t)dt) > 1000) fxT[i].s[0] = 0;
  if (fxShakeT) {
    fxShakeT = (uint16_t)(fxShakeT > dt ? fxShakeT - dt : 0);
    int16_t a = (int16_t)((fxShakeAmp * (int32_t)fxShakeT + fxShakeLen - 1) / fxShakeLen);
    fxSX = (int8_t)fxRange((int16_t)-a, a);
    fxSY = (int8_t)fxRange((int16_t)-a, a);
  } else fxSX = fxSY = 0;
  if (fxFlashT) fxFlashT = (uint16_t)(fxFlashT > dt ? fxFlashT - dt : 0);
}

// a scrolling game moves everything already flying with its world (dx, dy px)
static void fxScroll(int16_t dx, int16_t dy) {
  if (!dx && !dy) return;
  for (uint8_t i = 0; i < FX_N; i++) if (fxP[i].life) { fxP[i].x += (int32_t)dx * 256; fxP[i].y += (int32_t)dy * 256; }
  for (uint8_t i = 0; i < FX_TXT; i++) { fxT[i].x += dx; fxT[i].y += dy; }
}

static void fxFillClip(int16_t x, int16_t y, int16_t w, int16_t h, col c, int16_t top, int16_t bot) {
  if (y < top) { h -= top - y; y = top; }
  if (y + h > bot) h = bot - y;
  if (h > 0) gfx.fill(x, y, w, h, c);
}

// all particles and texts; (ox, oy) shifts them (a camera), rows outside top..bot stay untouched
static void fxDraw(int16_t ox = 0, int16_t oy = 0, int16_t top = TOP_H, int16_t bot = SCR_H) {
  for (uint8_t i = 0; i < FX_N; i++) {
    const FxPart &p = fxP[i];
    if (!p.life) continue;
    int16_t x = (int16_t)(p.x >> 8) + ox, y = (int16_t)(p.y >> 8) + oy;
    uint8_t t = (uint8_t)((uint32_t)p.age * 255 / p.life);
    col c = fxLerp(p.c0, p.c1, t);
    if (p.flags & FX_RING) {
      int16_t r = (int16_t)(1 + (int32_t)p.size * p.age / p.life);
      if (y - r >= top && y + r < bot) { gfx.circle(x, y, r, c); if (r > 3) gfx.circle(x, y, r - 1, colDark(c)); }
      continue;
    }
    if (p.flags & FX_STREAK) {                           // a short line back along the way it came
      int16_t tx = (int16_t)(x - p.vx / 400), ty = (int16_t)(y - p.vy / 400);
      if (y >= top && y < bot && ty >= top && ty < bot) gfx.line(tx, ty, x, y, c);
      continue;
    }
    uint8_t s = p.size;
    if (p.flags & FX_GROW) s = (uint8_t)(p.size + (uint32_t)p.size * 2 * p.age / p.life);
    else if (s > 1 && t > 170) s--;                      // shrinks at the end
    if (s >= 4) {                                        // a puff
      int16_t r = s / 2;
      if (y - r >= top && y + r < bot) gfx.disc(x, y, r, c);
    } else fxFillClip((int16_t)(x - s / 2), (int16_t)(y - s / 2), s, s, c, top, bot);
  }
  for (uint8_t i = 0; i < FX_TXT; i++) {
    const FxText &t = fxT[i];
    if (!t.s[0]) continue;
    int16_t y = (int16_t)(t.y + oy - t.age * 28 / 1000);
    if (y < top || y + Gfx::height(t.f) > bot) continue;
    col c = t.age > 650 ? colDark(t.c, (uint8_t)((t.age - 650) / 120)) : t.c;
    gfx.textShadow(t.x + ox, y, t.s, c, C::BLACK, t.f);
  }
}

// moves rows y0..y1-1 of the picture by the shake's offset (call after drawing them)
static void fxShakeApply(int16_t y0 = TOP_H, int16_t y1 = SCR_H) {
  int8_t dx = fxSX, dy = fxSY;
  if (!dx && !dy) return;
  uint8_t *fb = gfx.buffer();
  if (dy > 0) for (int16_t y = y1 - 1; y >= y0 + dy; y--) memcpy(fb + y * SCR_W, fb + (y - dy) * SCR_W, SCR_W);
  if (dy < 0) for (int16_t y = y0; y < y1 + dy; y++) memcpy(fb + y * SCR_W, fb + (y - dy) * SCR_W, SCR_W);
  if (dx)
    for (int16_t y = y0; y < y1; y++) {
      uint8_t *r = fb + y * SCR_W;
      if (dx > 0) memmove(r + dx, r, SCR_W - dx);
      else memmove(r, r - dx, SCR_W + dx);
    }
}

// every pixel of the box blended towards c by t (0..255), through a table
static void fxTint(int16_t x, int16_t y, int16_t w, int16_t h, col c, uint8_t t) {
  static uint8_t lut[256];
  static col lc = 0;
  static uint8_t lt = 0;
  if (!t) return;
  if (lc != c || lt != t) { for (int i = 0; i < 256; i++) lut[i] = fxLerp((col)i, c, t); lc = c; lt = t; }
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > SCR_W) w = SCR_W - x;
  if (y + h > SCR_H) h = SCR_H - y;
  uint8_t *fb = gfx.buffer();
  for (int16_t j = 0; j < h; j++) {
    uint8_t *r = fb + (y + j) * SCR_W + x;
    for (int16_t i = 0; i < w; i++) r[i] = lut[r[i]];
  }
}

// the running flash over a box (the play field), strongest at its start
static void fxFlashApply(int16_t x = 0, int16_t y = TOP_H, int16_t w = SCR_W, int16_t h = SCR_H - TOP_H) {
  if (!fxFlashT) return;
  uint8_t t = (uint8_t)(((uint32_t)fxFlashT * fxFlashMax / fxFlashLen + 8) & 0xF0);   // in steps: the table is reused
  fxTint(x, y, w, h, fxFlashC, t);
}

// ---------------- shapes ----------------
// a soft glow: discs from `outer` at radius r to `inner` in the middle
static void fxGlow(int16_t cx, int16_t cy, int16_t r, col outer, col inner, uint8_t steps = 4) {
  for (uint8_t k = 0; k < steps; k++)
    gfx.disc(cx, cy, (int16_t)(r - r * k / steps), fxLerp(outer, inner, (uint8_t)(255 * k / (steps > 1 ? steps - 1 : 1))));
}

// every other pixel of a disc / box: looks half see-through
static void fxDitherDisc(int16_t cx, int16_t cy, int16_t r, col c, uint8_t phase = 0) {
  for (int16_t dy = -r; dy <= r; dy++) {
    int16_t dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r) dx++;
    for (int16_t x = (int16_t)(cx - dx + ((cx - dx + cy + dy + phase) & 1)); x <= cx + dx; x += 2) gfx.pixel(x, cy + dy, c);
  }
}
static void fxDitherFill(int16_t x, int16_t y, int16_t w, int16_t h, col c, uint8_t phase = 0) {
  for (int16_t j = 0; j < h; j++)
    for (int16_t i = (int16_t)((x + y + j + phase) & 1); i < w; i += 2) gfx.pixel(x + i, y + j, c);
}

// a four-pointed twinkle, r = arm length
static void fxStar(int16_t x, int16_t y, int16_t r, col c, col core = C::WHITE) {
  gfx.hline(x - r, y, 2 * r + 1, c);
  gfx.vline(x, y - r, 2 * r + 1, c);
  if (r > 2) { gfx.pixel(x - 1, y - 1, c); gfx.pixel(x + 1, y - 1, c); gfx.pixel(x - 1, y + 1, c); gfx.pixel(x + 1, y + 1, c); }
  gfx.pixel(x, y, core);
}

/* A text sprite (see Gfx::sprite) turned around (cx, cy): (ox, oy) is the
   sprite's own pivot, s64 / c64 the sine and cosine *64 (clockwise).
   Every screen pixel near it looks up its source pixel (no gaps); the
   pivot should lie near the middle.                                  */
static void fxSpriteTurn(int16_t cx, int16_t cy, const char *const *rows, uint8_t h, int16_t ox, int16_t oy,
                         const char *keys, const col *pal, int16_t s64, int16_t c64) {
  int16_t w = (int16_t)strlen(rows[0]);
  int16_t R = (int16_t)((w > h ? w : h) * 3 / 4 + 1);
  for (int16_t dy = -R; dy <= R; dy++)
    for (int16_t dx = -R; dx <= R; dx++) {
      int32_t u = ((int32_t)dx * c64 + (int32_t)dy * s64 + 32) >> 6, v = ((int32_t)dy * c64 - (int32_t)dx * s64 + 32) >> 6;
      u += ox; v += oy;
      if (u < 0 || u >= w || v < 0 || v >= h) continue;
      char ch = rows[v][u];
      if (ch == '.' || ch == ' ') continue;
      const char *k = strchr(keys, ch);
      if (k) gfx.pixel(cx + dx, cy + dy, pal[k - keys]);
    }
}
