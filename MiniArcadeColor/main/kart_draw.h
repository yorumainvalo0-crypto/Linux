// MiniArcade Color: Kart GP - the picture: sky, the track as a floor in
// perspective, the karts and everything else on it, the effects and the
// HUD. Part of console.cpp (through game_kart.h).
/* The floor is "mode 7": every row below the horizon is a straight line
   through the world at the distance that row looks at (the camera 25 px
   above the track), sampled once per pixel in 16.16 fixed point - map
   tile, cell, then a texel of the material's 16 x 16 texture. Towards the
   horizon the rows take on the haze step by step (a colour table per
   step), and the farthest ones use one colour per material so that the
   pattern does not shimmer. The sky is a gradient, sun or moon, clouds and
   two layers of hills or mountains that turn with the camera at their own
   speed. Sprites (karts from 16 sides, boxes, coins, trees ...) are scaled
   to their distance with the nearest pixel, drawn far to near.        */

struct KgCam { int32_t x, y; uint16_t a; int32_t c, s, turn; };   // x, y in 1/256 px; turn: the angle, not wrapped (the near hills)

static void kgCamSet(KgCam &c, int32_t x, int32_t y, uint16_t a) {
  c.turn += kgAngDiff(a, c.a);
  c.x = x; c.y = y; c.a = a; c.c = kgCos(a); c.s = kgSin(a);
}

// the distance every floor row looks at (1/256 px): camera height x focal length / rows below the horizon
struct KgRows {
  int32_t d[KG_FLOOR];
  constexpr KgRows() : d() { for (int r = 0; r < KG_FLOOR; r++) d[r] = (int32_t)((int64_t)KG_CAMH * KG_F * 256 * 2 / (2 * r + 1)); }
};
static constexpr KgRows KG_ROWS{};
static const uint16_t KG_FOGD[KG_FOG - 1] = { 175, 230, 300, 400, 560, 800, 1250 };   // px: where each step of haze starts
#define KG_FARD 330                                             // px: from here one colour per material
#define KG_HALFD 80                                             // px: nearer than this a texel is over 2 px wide: every second pixel is sampled

#ifdef ARCADE_TRACE
static uint32_t kgCost = 0;                                     // work counted for the frame rate model (host tests)
#define KG_COST(n) (kgCost += (uint32_t)(n))
#else
#define KG_COST(n)
#endif

// ---------------- the floor ----------------
static void kgFloor(const KgTrack &T, const KgCam &c) {
  uint8_t *fb = gfx.buffer() + KG_HZ * SCR_W;
  const uint16_t *tidx = T.tidx;
  const uint8_t *tdat = T.tdat;
  uint8_t outA = T.outMat == KM_PIT ? KM_PIT : KM_OUTA, outB = T.outMat == KM_PIT ? KM_PIT : KM_OUTB;
  for (int16_t r = 0; r < KG_FLOOR; r++, fb += SCR_W) {
    int32_t d = KG_ROWS.d[r];
    int32_t dpx = d >> 8;
    int64_t du = (-(int64_t)d * c.s >> 6) / KG_F, dv = ((int64_t)d * c.c >> 6) / KG_F;
    int64_t u = ((int64_t)c.x << 8) + ((int64_t)d * c.c >> 6) - du * (SCR_W / 2) + du / 2;
    int64_t v = ((int64_t)c.y << 8) + ((int64_t)d * c.s >> 6) - dv * (SCR_W / 2) + dv / 2;
    uint32_t U = (uint32_t)(int32_t)u, V = (uint32_t)(int32_t)v, DU = (uint32_t)(int32_t)du, DV = (uint32_t)(int32_t)dv;
    uint8_t f = 0;
    while (f < KG_FOG - 1 && dpx >= KG_FOGD[f]) f++;
    uint32_t key = 0xFFFFFFFF;                                  // the tile of the last pixel
    const uint8_t *tp = tdat;
    if (dpx >= KG_FARD) {                                       // far: the material's colour in the haze
      const uint8_t *flat = T.flat[f];
      KG_COST(SCR_W * 22 + 80);
      for (int16_t x = 0; x < SCR_W; x++, U += DU, V += DV) {
        uint8_t m;
        if (((U | V) >> 26) == 0) {
          uint32_t k = (V >> 21) * KG_TILES + (U >> 21);
          if (k != key) { key = k; tp = tdat + tidx[k] * 32; }
          uint8_t b = tp[((V >> 16) & 0x1C) + ((U >> 19) & 3)];
          m = (U & 0x40000) ? b >> 4 : b & 15;
        } else m = ((U >> 21) ^ (V >> 21)) & 1 ? outB : outA;
        fb[x] = flat[m];
      }
      continue;
    }
    bool half = dpx < KG_HALFD;
    if (half) { DU *= 2; DV *= 2; }
    KG_COST((half ? SCR_W / 2 : SCR_W) * (f ? 30 : 27) + 80);
    const uint8_t *fog = T.fog[f];
    for (int16_t x = 0; x < SCR_W; x += half ? 2 : 1, U += DU, V += DV) {
      uint8_t m;
      if (((U | V) >> 26) == 0) {
        uint32_t k = (V >> 21) * KG_TILES + (U >> 21);
        if (k != key) { key = k; tp = tdat + tidx[k] * 32; }
        uint8_t b = tp[((V >> 16) & 0x1C) + ((U >> 19) & 3)];
        m = (U & 0x40000) ? b >> 4 : b & 15;
      } else m = ((U >> 21) ^ (V >> 21)) & 1 ? outB : outA;
      uint8_t t = T.tex[m][((V >> 12) & 0xF0) | ((U >> 16) & 15)];
      if (f) t = fog[t];
      fb[x] = t;
      if (half) fb[x + 1] = t;
    }
  }
}

// ---------------- the sky ----------------
static int32_t kgW(uint16_t a, uint16_t f, uint16_t ph) { return kgSin((uint16_t)(a * f + ph)); }   // Q14

// the far layer's height (px) at angle a
static int16_t kgFarH(uint8_t kind, uint16_t a, uint8_t H) {
  int32_t v;
  switch (kind) {
  case KF_FAR_PEAKS:   v = 9000 + kgW(a, 3, 1000) * 3 / 10 + kgW(a, 7, 9000) / 5 + kgW(a, 17, 300) / 10 + abs(kgW(a, 41, 0)) / 16; break;
  case KF_FAR_MESAS:   { v = 7000 + kgW(a, 4, 2000) * 6 / 10 + kgW(a, 9, 100) * 3 / 10; if (v > 12500) v = 12500 + (v - 12500) / 16; if (v < 3000) v = 3000; break; }
  case KF_FAR_ISLANDS: { v = kgW(a, 3, 5000) * 7 / 10 + kgW(a, 8, 0) * 4 / 10 - 5000; if (v < 0) v = 0; break; }
  case KF_FAR_ALPS:    v = 4000 + (16384 - abs(kgW(a, 5, 700))) * 4 / 10 + (16384 - abs(kgW(a, 13, 3000))) / 4 + abs(kgW(a, 37, 0)) / 12; break;
  case KF_FAR_HILLS:   v = 5000 + kgW(a, 2, 4000) * 3 / 10 + kgW(a, 5, 100) / 5; break;
  default: {                                                    // the volcano: one big cone, low hills round it
    int16_t d = kgAngDiff(a, 47000);
    int32_t cone = 16384 - abs(d) * 4;
    if (cone > 14800) cone = 14800;                             // the crater
    v = 3000 + kgW(a, 6, 0) / 6;
    if (cone > v) v = cone;
  }
  }
  return (int16_t)(v * H / 16384);
}

static int16_t kgNearH(uint8_t kind, uint16_t a, uint8_t H, uint8_t &tone) {
  tone = 0;
  switch (kind) {
  case KN_HILLS: {
    int32_t v = 8000 + kgW(a, 5, 3000) * 3 / 10 + kgW(a, 11, 0) / 5;
    uint16_t seg = (uint16_t)(a >> 9), in = a & 511;            // trees on top: little round bumps
    uint32_t h = kgHash(seg);
    if (h & 1) { int32_t r = 256 - abs((int32_t)in - 256); v += r * (6 + (h >> 4) % 8); tone = 1; }
    return (int16_t)(v * H / 16384);
  }
  case KN_DUNES: { int32_t v = 7000 + kgW(a, 6, 0) * 4 / 10 + abs(kgW(a, 13, 2000)) / 5; tone = kgW(a, 6, 4096) > 0; return (int16_t)(v * H / 16384); }
  case KN_SEA: return H;
  case KN_PINES: {
    uint16_t seg = (uint16_t)(a >> 9), in = a & 511;
    uint32_t h = kgHash(seg * 3 + 1);
    int32_t top = 6000 + (int32_t)(h % 10000), r = 256 - abs((int32_t)in - 256);
    return (int16_t)((4000 + top * r / 256) * H / 16384);
  }
  case KN_CITY: {
    uint16_t seg = (uint16_t)(a >> 10);
    uint32_t h = kgHash(seg * 7 + 3);
    tone = (uint8_t)(h >> 20) & 3;
    return (int16_t)((4000 + (h % 12000)) * H / 16384);
  }
  default: { int32_t v = 6000 + abs(kgW(a, 9, 0)) * 3 / 10 + abs(kgW(a, 23, 900)) / 5; return (int16_t)(v * H / 16384); }
  }
}

static int16_t kgSkyX(const KgCam &c, uint16_t a) {             // screen x of a direction in the sky
  return (int16_t)(SCR_W / 2 + (int32_t)kgAngDiff(a, c.a) * 5 / 256);
}

static void kgCloud(int16_t x, int16_t y, int16_t s, col c, col lo) {
  gfx.disc(x - s, y + 1, s * 2 / 3, lo); gfx.disc(x + s, y + 1, s * 2 / 3, lo);
  gfx.disc(x - s, y, s * 2 / 3, c); gfx.disc(x + s, y, s * 2 / 3, c); gfx.disc(x, y - s / 3, s, c);
  gfx.hline(x - s - s / 2, y + s / 2, 3 * s, lo);
}

static void kgSky(const KgTrack &T, const KgCam &c, uint32_t ms) {
  const KgTheme &th = KG_THEME[T.theme];
  for (int16_t y = 0; y < KG_HZ; y++) {
    col k = y < KG_HZ / 2 ? colLerp(th.sky[0], th.sky[1], (uint8_t)(y * 255 / (KG_HZ / 2))) : colLerp(th.sky[1], th.sky[2], (uint8_t)((y - KG_HZ / 2) * 255 / (KG_HZ - KG_HZ / 2)));
    memset(gfx.buffer() + y * SCR_W, k, SCR_W);
  }
  KG_COST(KG_HZ * 140);
  if (th.night)
    for (uint8_t i = 0; i < 60; i++) {
      uint32_t h = kgHash(i * 977u + 5);
      int16_t x = kgSkyX(c, (uint16_t)h), y = (int16_t)(4 + (h >> 16) % (KG_HZ - 30));
      if (x < 0 || x >= SCR_W) continue;
      bool tw = ((h >> 8) + ms / 400) % 7 == 0;
      gfx.pixel(x, y, tw ? C::YELLOW : ((h >> 4) & 1 ? C::WHITE : GREY3));
      if ((h & 15) == 0) { gfx.pixel(x - 1, y, GREY2); gfx.pixel(x + 1, y, GREY2); gfx.pixel(x, y - 1, GREY2); gfx.pixel(x, y + 1, GREY2); }
    }
  int16_t sx = kgSkyX(c, th.sunA), sy = KG_HZ - th.sunY;
  if (sx > -30 && sx < SCR_W + 30) {
    if (th.night) {                                             // the moon
      fxGlow(sx, sy, 13, colLerp(th.sky[0], th.sunGlow, 90), colLerp(th.sky[0], th.sunGlow, 160), 3);
      gfx.disc(sx, sy, 8, th.sun);
      gfx.disc(sx + 3, sy - 2, 2, GREY4); gfx.disc(sx - 3, sy + 3, 1, GREY4);
    } else {
      fxGlow(sx, sy, 22, colLerp(th.sky[1], th.sunGlow, 70), colLerp(th.sky[1], th.sunGlow, 200), 4);
      gfx.disc(sx, sy, 9, th.sunGlow);
      gfx.disc(sx, sy, 7, th.sun);
    }
  }
  if (th.clouds)
    for (uint8_t i = 0; i < 7; i++) {
      uint32_t h = kgHash(i * 31u + 11 + T.theme);
      int16_t x = kgSkyX(c, (uint16_t)h), y = (int16_t)(12 + (h >> 16) % 30), s = (int16_t)(6 + (h >> 24) % 7);
      if (x < -40 || x > SCR_W + 40) continue;
      kgCloud(x, y, s, C::WHITE, colLerp(C::WHITE, th.sky[1], 110));
    }
  uint8_t *fb = gfx.buffer();
  // far layer: lit or shaded by its slope, snow (or glow) on the high parts, hazy at the foot
  col farFog = colLerp(th.far[1], th.fog, 140), farFog2 = colLerp(th.far[0], th.fog, 120);
  for (int16_t x = 0; x < SCR_W; x++) {
    uint16_t a = (uint16_t)(c.a + (x - SCR_W / 2) * 51);
    int16_t h = kgFarH(th.farKind, a, th.farH), h2 = kgFarH(th.farKind, (uint16_t)(a + 160), th.farH);
    if (h <= 0) continue;
    col side = h2 >= h ? th.far[0] : th.far[1];
    int16_t top = KG_HZ - h;
    int16_t snow = th.farKind == KF_FAR_PEAKS || th.farKind == KF_FAR_ALPS ? (int16_t)(h - th.farH * 7 / 10) : 0;
    if (th.farKind == KF_FAR_VOLCANO && h > th.farH * 85 / 100) snow = 2;
    for (int16_t y = top; y < KG_HZ; y++) {
      col k = side;
      if (y - top < snow) k = (th.farKind == KF_FAR_VOLCANO) ? (((x + ms / 90) & 3) ? th.far[2] : C::YELLOW) : th.far[2];
      else if (y >= KG_HZ - 3) k = h2 >= h ? farFog2 : farFog;
      fb[y * SCR_W + x] = k;
    }
    KG_COST(h * 3 + 40);
  }
  if (th.farKind == KF_FAR_VOLCANO) {                           // smoke over the crater
    int16_t vx = kgSkyX(c, 47000);
    for (uint8_t i = 0; i < 6; i++) {
      int16_t ph = (int16_t)((ms / 40 + i * 30) % 180);
      gfx.disc(vx + ph / 6 + (int16_t)(i * 3), KG_HZ - th.farH - ph / 5, (int16_t)(4 + ph / 25), i & 1 ? GREY1 : rgb(70, 40, 40));
    }
  }
  // near layer
  for (int16_t x = 0; x < SCR_W; x++) {
    uint16_t a = (uint16_t)(c.turn * 5 / 4 + (x - SCR_W / 2) * 64);   // a little faster than the far layer
    uint8_t tone;
    int16_t h = kgNearH(th.nearKind, a, th.nearH, tone);
    if (h <= 0) continue;
    int16_t top = KG_HZ - h;
    KG_COST(h * 3 + 30);
    for (int16_t y = top; y < KG_HZ; y++) {
      col k = tone && th.nearKind != KN_CITY ? th.nearc[1] : th.nearc[0];
      switch (th.nearKind) {
      case KN_SEA:
        k = (y == top) ? th.nearc[2] : ((((x * 7 + y * 13 + ms / 120) % 23) == 0) ? th.nearc[2] : (y < top + 2 ? th.nearc[0] : th.nearc[1]));
        break;
      case KN_PINES:
        if (y - top < 2) k = th.nearc[2];
        break;
      case KN_CITY: {                                           // windows, some lit
        uint32_t hh = kgHash((uint32_t)((a >> 7) * 131 + (y / 3) * 7));
        k = tone == 1 ? th.nearc[1] : th.nearc[0];
        if (((a >> 6) & 3) == 1 && (y % 3) == 1 && y - top > 2 && (hh & 3) == 0) k = (hh & 4) ? th.nearc[2] : C::CYAN;
        break;
      }
      case KN_ROCKS:
        if (y == top) k = th.nearc[2];
        break;
      default:
        if (tone && y - top < 2 && ((x + y) & 1)) k = th.nearc[2];
        break;
      }
      fb[y * SCR_W + x] = k;
    }
  }
}

// ---------------- sprites ----------------
enum { KB_MIRROR = 1, KB_GHOST = 2, KB_FLASH = 4 };

/* A sprite scaled to sc/256 screen px per sprite px, its foot (ox, oy) at
   (ax, ay). Rows above `top` are left alone (the horizon).            */
static void kgBlit(const KgSpr &s, int16_t ax, int16_t ay, int32_t sc, const col *pal, uint8_t fl = 0, int16_t top = 0) {
  if (sc < 24) return;
  if (sc > 2400) sc = 2400;
  int16_t w = (int16_t)(s.w * sc >> 8), h = (int16_t)(s.h * sc >> 8);
  if (w <= 0 || h <= 0) return;
  int16_t x0 = (int16_t)(ax - (s.ox * sc >> 8)), y0 = (int16_t)(ay - (s.oy * sc >> 8));
  if (x0 >= SCR_W || x0 + w <= 0 || y0 >= SCR_H || y0 + h <= top) return;
  int32_t inv = (256 << 8) / sc;
  uint8_t rb = (uint8_t)((s.w + 1) / 2);
  uint8_t *fb = gfx.buffer();
  int16_t i0 = x0 < 0 ? (int16_t)-x0 : 0, i1 = x0 + w > SCR_W ? (int16_t)(SCR_W - x0) : w;
  KG_COST((int32_t)w * h * 9 + 40);
  for (int16_t j = y0 < top ? (int16_t)(top - y0) : 0; j < h && y0 + j < SCR_H; j++) {
    int32_t sy = (j * inv + inv / 2) >> 8;
    if (sy >= s.h) sy = s.h - 1;
    const uint8_t *row = s.d + sy * rb;
    uint8_t *o = fb + (y0 + j) * SCR_W + x0;
    int32_t sxq = i0 * inv + inv / 2;
    for (int16_t i = i0; i < i1; i++, sxq += inv) {
      int32_t sx = sxq >> 8;
      if (fl & KB_MIRROR) sx = s.w - 1 - sx;
      uint8_t v = (uint8_t)((row[sx >> 1] >> ((sx & 1) ? 0 : 4)) & 15);
      if (!v) continue;
      if ((fl & KB_GHOST) && ((i + j) & 1)) continue;
      o[i] = (fl & KB_FLASH) ? C::WHITE : pal[v];
    }
  }
}

// a sprite at its natural size (menus, HUD)
static void kgSpr(const KgSpr &s, int16_t ax, int16_t ay, const col *pal, uint8_t scale = 1, uint8_t fl = 0) {
  kgBlit(s, ax, ay, 256 * scale, pal, fl);
}

static void kgShadow(const uint8_t *dark, int16_t cx, int16_t cy, int16_t rw) {
  if (rw < 2) return;
  int16_t rh = rw / 3 + 1;
  uint8_t *fb = gfx.buffer();
  for (int16_t dy = -rh; dy <= rh; dy++) {
    int16_t y = cy + dy;
    if (y < KG_HZ || y >= SCR_H) continue;
    int32_t span = rw * (int32_t)kgSqrt((uint32_t)((rh * rh - dy * dy) * 256)) / (rh * 16);
    for (int16_t x = cx - span; x <= cx + span; x++) if ((uint16_t)x < SCR_W) fb[y * SCR_W + x] = dark[fb[y * SCR_W + x]];
  }
}

static void kgKartPal(uint8_t drv, col *p, bool ghost = false) {
  const KgDriver &D = KG_DRV[drv];
  col q[16] = { 0, D.body[0], D.body[1], D.body[2], D.acc[0], D.acc[1], D.suit[0], D.suit[1], D.helm[0], D.helm[1],
                rgb(30, 40, 90), rgb(210, 235, 255), rgb(40, 40, 46), rgb(100, 100, 112), rgb(175, 175, 185), rgb(16, 14, 20) };
  if (ghost) {
    static const col G[4] = { rgb(230, 245, 255), rgb(150, 210, 255), rgb(80, 150, 230), rgb(40, 80, 160) };
    for (uint8_t i = 1; i < 16; i++) { uint8_t r, g, b; colToRgb(q[i], r, g, b); uint16_t l = (uint16_t)(r * 3 + g * 5 + b * 2) / 10; q[i] = G[l > 190 ? 0 : l > 120 ? 1 : l > 60 ? 2 : 3]; }
  }
  memcpy(p, q, sizeof(q));
}

// a world point (1/256 px, height h px) on the screen; false = behind the camera
static bool kgProj(const KgCam &c, int32_t wx, int32_t wy, int32_t hpx256, int16_t &sx, int16_t &sy, int32_t &z) {
  int32_t dx = (wx - c.x) >> 4, dy = (wy - c.y) >> 4;          // 1/16 px
  z = (dx * c.c + dy * c.s) >> 10;                              // 1/256 px
  if (z < 3 * 256) return false;
  int32_t xs = (-dx * c.s + dy * c.c) >> 10;
  sx = (int16_t)(SCR_W / 2 + (int64_t)xs * KG_F / z);
  sy = (int16_t)(KG_HZ + ((int64_t)(KG_CAMH * 256 - hpx256) * KG_F) / z);
  return true;
}

// ---------------- what is on the track, far to near ----------------
enum { KO_DECOR, KO_BOX, KO_COIN, KO_PEEL, KO_ROCKET, KO_KART };

static const KgSpr &kgDecorSpr(uint8_t kind, uint8_t theme, const col *&pal) {
  switch (kind) {
  case KD_TREE:   pal = KP_TREE; return KG_TREE;
  case KD_BUSH:   pal = KP_BUSH; return KG_BUSH;
  case KD_PINE:   pal = KP_PINE; return KG_PINE;
  case KD_PALM:   pal = KP_PALM; return KG_PALM;
  case KD_CACTUS: pal = KP_CACTUS; return KG_CACTUS;
  case KD_LAMP:   pal = KP_LAMP; return KG_LAMP;
  case KD_CHEV:   pal = KP_CHEV; return KG_CHEV;
  case KD_DEAD:   pal = KP_DEAD; return KG_DEAD;
  default:        pal = theme == KW_VOLCANO ? KP_ROCK_V : theme == KW_ICE ? KP_ROCK_I : KP_ROCK; return KG_ROCK;
  }
}

// effects at a kart: sparks of a drift, the flame of a boost, dust, the shield
static void kgKartFx(const KgRace &R, const KgKart &k, const KgCam &c, int16_t sx, int16_t sy, int32_t z, uint32_t ms, bool mine) {
  int32_t ca = kgCos(k.a), sa = kgSin(k.a);
  int32_t scale = (int32_t)KG_F * 256 / (z >> 4);                 // screen px per world px, 1/16
  uint32_t h = kgHash(ms / 33 + (uint32_t)(&k - R.k) * 77);
  if (k.boost) {                                                  // flame from the exhausts
    int16_t fx, fy; int32_t fz;
    if (kgProj(c, k.x - (ca * 5 >> 6), k.y - (sa * 5 >> 6), 400 + k.z, fx, fy, fz)) {
      int16_t r = (int16_t)(scale * 13 / 160 + (h & 1));
      if (r < 1) r = 1;
      gfx.disc(fx, fy, r + 1, C::RED);
      gfx.disc(fx, fy - (int16_t)(h & 1), r, C::ORANGE);
      gfx.disc(fx, fy, r * 2 / 3, C::YELLOW);
      if (r > 2) gfx.disc(fx, fy, r / 3, C::WHITE);
    }
  }
  if (k.drift && (k.charge > 20 || (h & 1))) {                    // sparks from the rear wheels
    col sc = k.charge >= KG_DRIFT2 ? C::ORANGE : k.charge >= KG_DRIFT1 ? C::CYAN : C::YELLOW;
    col sc2 = k.charge >= KG_DRIFT2 ? C::YELLOW : C::WHITE;
    for (int8_t side = -1; side <= 1; side += 2) {
      int16_t wx, wy; int32_t wz;
      if (!kgProj(c, k.x - (ca * 3 >> 6) - (sa * side * 3 >> 6), k.y - (sa * 3 >> 6) + (ca * side * 3 >> 6), 0, wx, wy, wz)) continue;
      int16_t len = (int16_t)(scale / 26 + 3);
      if (k.charge >= KG_DRIFT1) {                                 // a glow at the wheel, the colour of the charge
        int16_t r = (int16_t)(scale / 48 + 2 + (h & 1));
        gfx.disc(wx, wy - 1, r + 1, sc);
        gfx.disc(wx, wy - 1, r - 1 > 0 ? r - 1 : 1, sc2);
      }
      for (uint8_t i = 0; i < 6; i++) {
        uint32_t q = kgHash(h + i * 13 + (uint32_t)side * 101);
        int16_t dx = (int16_t)((int32_t)(q % (len * 2 + 1)) - len - k.drift * len / 2), dy = (int16_t)(-(int32_t)((q >> 8) % (len + 1)));
        gfx.line(wx, wy, wx + dx, wy + dy, i & 1 ? sc2 : sc);
      }
    }
  }
  int32_t v2 = (k.vx >> 4) * (k.vx >> 4) + (k.vy >> 4) * (k.vy >> 4);
  if (k.surf <= KM_OUTB || k.surf == KM_RUN || k.surf == KM_RUN2) {
    if (v2 > 40 && !k.fall && !mine) {                            // dust (the player's comes from the particles)
      col dc = KG_THEME[R.T->theme].mat[k.surf][1];
      int16_t wx, wy; int32_t wz;
      if (kgProj(c, k.x - (ca * 6 >> 6), k.y - (sa * 6 >> 6), 200, wx, wy, wz))
        fxDitherDisc(wx + (int16_t)(h % 5) - 2, wy, (int16_t)(scale / 64 + 1), dc, (uint8_t)h);
    }
  }
  if (k.shield) {                                                // a bubble: rings, a glint turning round
    int16_t r = (int16_t)(scale * 6 / 16);
    if (r > 3 && (k.shield > 120 || (ms / 80) % 2)) {
      int16_t cy = (int16_t)(sy - r / 2);
      gfx.circle(sx, cy, r, C::CYAN);
      gfx.circle(sx, cy, r - 1, rgb(0, 140, 200));
      uint16_t a = (uint16_t)(ms * 90);
      for (int8_t i = 0; i < 3; i++) {
        uint16_t b = (uint16_t)(a + i * 1400);
        gfx.pixel((int16_t)(sx + (kgCos(b) * (r - 2) >> 14)), (int16_t)(cy + (kgSin(b) * (r - 2) >> 14)), C::WHITE);
      }
      gfx.disc((int16_t)(sx - r / 2), (int16_t)(cy - r / 2), (int16_t)(r / 6 + 1), rgb(200, 255, 255));
    }
  }
}

static void kgDrawKart(const KgRace &R, const KgKart &k, const KgCam &c, int16_t sx, int16_t sy, int32_t z, uint32_t ms) {
  bool mine = &k == &R.k[R.me];
  bool ghost = (k.flags & KK_GHOST) != 0;
  int32_t sc = (int32_t)((int64_t)KG_F * 65536 / KG_SPS / z);   // screen px per sprite px, 1/256
  if (k.fall) sc = sc * k.fall / 60;
  uint16_t view = kgAtan2(k.y - c.y, k.x - c.x);
  uint8_t fr = (uint8_t)(((uint16_t)(k.a - view) + 2048) >> 12) & 15;
  int16_t lift = (int16_t)((int64_t)k.z * KG_F / z);
  if (k.inv && (ms / 70) % 2) return;
  col pal[16];
  kgKartPal(k.drv, pal, ghost);
  if (!ghost) kgShadow(R.T->dark, sx, sy, (int16_t)(KG_F * 4 * 256 / z));
  if (k.hop) lift += (int16_t)(k.hop > 7 ? 14 - k.hop : k.hop) * (int16_t)(KG_F * 256 / z) / 12;
  kgBlit(KG_KART[fr], sx, (int16_t)(sy - lift), sc, pal, ghost ? KB_GHOST : 0, KG_HZ - 40);
  if (!ghost) kgKartFx(R, k, c, sx, (int16_t)(sy - lift), z, ms, mine);
}

static void kgObjects(KgRace &R, const KgCam &c, uint32_t ms, const char *tagName = NULL) {
  KgTrack &T = *R.T;
  KgObj *o = R.obj;
  uint8_t n = 0;
  auto add = [&](int32_t wx, int32_t wy, int32_t hh, uint8_t kind, uint8_t i) {
    if (n >= KG_OBJS) return;
    int16_t sx, sy; int32_t z;
    if (!kgProj(c, wx, wy, hh, sx, sy, z)) return;
    if (sx < -120 || sx > SCR_W + 120 || z > 900 * 256) return;
    o[n++] = { z, sx, sy, kind, i };
  };
  for (uint8_t i = 0; i < T.ndec; i++) add((int32_t)T.dec[i].x << 8, (int32_t)T.dec[i].y << 8, 0, KO_DECOR, i);
  for (uint8_t i = 0; i < T.nbox; i++) if (!T.box[i].off || T.box[i].off < 30) add((int32_t)T.box[i].x << 8, (int32_t)T.box[i].y << 8, 0, KO_BOX, i);
  for (uint8_t i = 0; i < T.ncoin; i++) if (!T.coin[i].off) add((int32_t)T.coin[i].x << 8, (int32_t)T.coin[i].y << 8, 0, KO_COIN, i);
  for (uint8_t i = 0; i < KG_PEELS; i++) if (R.peel[i].on) add((int32_t)R.peel[i].x << 8, (int32_t)R.peel[i].y << 8, 0, KO_PEEL, i);
  for (uint8_t i = 0; i < KG_ROCKETS; i++) if (R.rk[i].on) add(R.rk[i].x, R.rk[i].y, 0, KO_ROCKET, i);
  for (uint8_t i = 0; i <= R.nk; i++) {
    const KgKart &k = R.k[i];
    if (i == R.nk && !(k.flags & KK_GHOST)) continue;
    if (k.flags & KK_GONE) continue;
    add(k.x, k.y, 0, KO_KART, i);
  }
  for (uint8_t a = 1; a < n; a++)                                 // far first
    for (uint8_t b = a; b > 0 && o[b - 1].z < o[b].z; b--) { KgObj t = o[b]; o[b] = o[b - 1]; o[b - 1] = t; }
  for (uint8_t q = 0; q < n; q++) {
    const KgObj &ob = o[q];
    int32_t sc2 = (int32_t)((int64_t)KG_F * 65536 / ob.z / 2);    // billboards: 2 px a world px
    switch (ob.kind) {
    case KO_DECOR: {
      const col *pal;
      const KgSpr &s = kgDecorSpr(T.dec[ob.i].kind, T.theme, pal);
      kgShadow(T.dark, ob.sx, ob.sy, (int16_t)(s.w * sc2 / 1024));
      kgBlit(s, ob.sx, ob.sy, sc2, pal, T.dec[ob.i].flip ? KB_MIRROR : 0, 0);
      if (T.dec[ob.i].kind == KD_LAMP && KG_THEME[T.theme].night) {
        int16_t hy = (int16_t)(ob.sy - (KG_LAMP.oy - 6) * sc2 / 256), hx = (int16_t)(ob.sx + 4 * sc2 / 256);
        fxDitherDisc(hx, hy, (int16_t)(sc2 / 64 + 2), C::YELLOW, (uint8_t)(ms / 200));
      }
      break;
    }
    case KO_BOX: {
      col pal[16] = { 0 };
      for (uint8_t i = 0; i < 4; i++) {
        static const col RB[8] = { rgb(255, 60, 60), rgb(255, 160, 0), rgb(255, 240, 40), rgb(60, 220, 60), rgb(40, 200, 255), rgb(80, 100, 255), rgb(200, 80, 255), rgb(255, 80, 200) };
        pal[1 + i] = RB[(ms / 90 + i * 2) % 8];
      }
      pal[5] = C::WHITE; pal[6] = C::WHITE; pal[7] = rgb(170, 170, 200);
      int16_t bob = (int16_t)((kgSin((uint16_t)(ms * 40 + ob.i * 9000)) * sc2) >> 20);
      kgShadow(T.dark, ob.sx, ob.sy, (int16_t)(sc2 / 40));
      int32_t s4 = sc2 / 2;
      if (T.box[ob.i].off) s4 = s4 * (30 - T.box[ob.i].off) / 30;
      kgBlit(KG_BOX[(ms / 80 + ob.i) % 6], ob.sx, (int16_t)(ob.sy - 4 * sc2 / 256 + bob), s4, pal, 0, 0);
      break;
    }
    case KO_COIN:
      kgBlit(KG_COIN[(ms / 70 + ob.i) % 6], ob.sx, (int16_t)(ob.sy - 2 * sc2 / 256), sc2 / 2, KP_COIN, 0, 0);
      break;
    case KO_PEEL:
      kgBlit(KG_PEEL, ob.sx, ob.sy, sc2 / 2, KP_PEEL, 0, 0);
      break;
    case KO_ROCKET: {
      const KgRocket &r = R.rk[ob.i];
      uint16_t view = kgAtan2(r.y - c.y, r.x - c.x);
      uint8_t fr = (uint8_t)(((uint16_t)(r.a - view) + 4096) >> 13) & 7;
      int32_t ca = kgCos(r.a), sa = kgSin(r.a);
      int16_t fx, fy; int32_t fz;
      if (kgProj(c, r.x - (ca * 3 >> 6), r.y - (sa * 3 >> 6), 400, fx, fy, fz)) {
        int16_t rr = (int16_t)(sc2 / 128 + 1 + ((ms / 40) & 1));
        gfx.disc(fx, fy, rr + 1, C::ORANGE); gfx.disc(fx, fy, rr, C::YELLOW);
      }
      kgShadow(T.dark, ob.sx, ob.sy, (int16_t)(sc2 / 64));
      kgBlit(KG_ROCKET[fr], ob.sx, (int16_t)(ob.sy - sc2 / 256), sc2 / 2, KP_ROCKET, 0, 0);
      break;
    }
    case KO_KART: {
      const KgKart &k = R.k[ob.i];
      kgDrawKart(R, k, c, ob.sx, ob.sy, ob.z, ms);
      if (tagName && (k.flags & KK_REMOTE) && ob.z < 400 * 256) {
        int16_t tw = Gfx::width(tagName, F_SMALL);
        int16_t ty = (int16_t)(ob.sy - (int32_t)KG_F * 9 * 256 / ob.z - 10);
        if (ty > 2) { gfx.fill(ob.sx - tw / 2 - 2, ty - 1, tw + 4, 11, C::BLACK); gfx.text(ob.sx - tw / 2, ty, tagName, C::YELLOW, F_SMALL); }
      }
      break;
    }
    }
  }
}

// ---------------- the HUD ----------------
static const char *kgOrd(uint8_t p) { return p == 1 ? "st" : p == 2 ? "nd" : p == 3 ? "rd" : "th"; }
static void kgTime(char *b, size_t n, uint32_t ms) { snprintf(b, n, "%u'%02u\"%02u", (unsigned)(ms / 60000), (unsigned)(ms / 1000 % 60), (unsigned)(ms / 10 % 100)); }

static void kgItemIcon(uint8_t it, int16_t cx, int16_t cy, uint8_t scale = 1) {
  switch (it) {
  case KI_TURBO:  kgSpr(KG_ITURBO, cx, (int16_t)(cy + 10 * scale), KP_TURBO, scale); break;
  case KI_PEEL:   kgSpr(KG_PEEL, cx, (int16_t)(cy + 8 * scale), KP_PEEL, (uint8_t)(scale + 1)); break;
  case KI_ROCKET: kgSpr(KG_IROCKET, cx, (int16_t)(cy + 6 * scale), KP_ROCKETI, scale); break;
  case KI_SHIELD: kgSpr(KG_ISHIELD, cx, (int16_t)(cy + 10 * scale), KP_SHIELD, scale); break;
  }
}

static void kgMiniMap(const KgRace &R, int16_t x0, int16_t y0) {
  const KgTrack &T = *R.T;
  gfx.dim(x0 - 4, y0 - 4, 72, 72);
  auto mm = [&](int32_t wx4, int32_t wy4, int16_t &x, int16_t &y) {
    x = (int16_t)(x0 + 32 + ((wx4 / 4 - T.mmX) * T.mmS >> 8));
    y = (int16_t)(y0 + 32 + ((wy4 / 4 - T.mmY) * T.mmS >> 8));
  };
  int16_t px, py;
  mm(T.pt[0].x, T.pt[0].y, px, py);
  for (uint16_t i = 3; i <= T.n; i += 3) {
    int16_t x, y;
    const KgPt &p = T.pt[i % T.n];
    mm(p.x, p.y, x, y);
    gfx.line(px, py, x, y, GREY3); gfx.line(px + 1, py, x + 1, y, GREY3); gfx.line(px, py + 1, x, y + 1, GREY4);
    px = x; py = y;
  }
  mm(T.pt[0].x, T.pt[0].y, px, py);
  gfx.fill(px - 2, py, 5, 2, C::WHITE);
  for (int8_t i = (int8_t)R.nk; i >= 0; i--) {
    const KgKart &k = R.k[i];
    if (i == R.nk && !(k.flags & KK_GHOST)) continue;
    if (k.flags & KK_GONE) continue;
    int16_t x, y;
    mm(k.x >> 6, k.y >> 6, x, y);
    if (i == R.me) { gfx.disc(x, y, 3, C::WHITE); gfx.disc(x, y, 2, KG_DRV[k.drv].body[1]); }
    else gfx.fill(x - 1, y - 1, 3, 3, (k.flags & KK_GHOST) ? rgb(150, 210, 255) : KG_DRV[k.drv].body[1]);
  }
}

static void kgHud(const KgRace &R, uint32_t ms, bool timeTrial) {
  const KgKart &k = R.k[R.me];
  char b[32];
  // place, big, top left
  if (!timeTrial) {
    uint8_t p = k.place ? k.place : R.nk;
    static const col PC[4] = { rgb(255, 215, 0), rgb(220, 225, 235), rgb(230, 150, 80), C::WHITE };
    snprintf(b, sizeof(b), "%u", p);
    gfx.textShadow(8, 4, b, PC[p < 4 ? p - 1 : 3], C::BLACK, F_BIG);
    gfx.textShadow(8 + Gfx::width(b, F_BIG) + 1, 6, kgOrd(p), PC[p < 4 ? p - 1 : 3], C::BLACK, F_BOLD);
  }
  snprintf(b, sizeof(b), "LAP %u/%u", k.lap + 1 > KG_LAPS ? KG_LAPS : k.lap + 1, KG_LAPS);
  gfx.textShadow(timeTrial ? 8 : 10, timeTrial ? 8 : 44, b, C::WHITE, C::BLACK, F_TEXT);
  // the race clock
  kgTime(b, sizeof(b), k.finish ? k.finish : R.t);
  gfx.textShadow((SCR_W - Gfx::width(b, F_TEXT)) / 2, 5, b, C::WHITE, C::BLACK, F_TEXT);
  if (timeTrial && k.bestLap) {
    char t[20];
    kgTime(t, sizeof(t), k.bestLap);
    snprintf(b, sizeof(b), "BEST %s", t);
    gfx.textShadow((SCR_W - Gfx::width(b, F_SMALL)) / 2, 24, b, C::YELLOW, C::BLACK, F_SMALL);
  }
  // the item, top right: a roulette while it turns
  int16_t ix = SCR_W - 42, iy = 4;
  gfx.rbox(ix + 2, iy + 2, 36, 36, 7, C::BLACK);
  gfx.rbox(ix, iy, 36, 36, 7, rgb(30, 30, 60));
  gfx.rframe(ix, iy, 36, 36, 7, k.roul ? C::YELLOW : C::WHITE);
  gfx.rframe(ix + 1, iy + 1, 34, 34, 6, GREY2);
  uint8_t show = k.roul ? (uint8_t)(1 + (ms / 70) % 4) : k.item;
  if (show) kgItemIcon(show, ix + 18, iy + 18);
  // coins, bottom left
  kgBlit(KG_COIN[(ms / 90) % 6], 16, SCR_H - 6, 256, KP_COIN, 0);
  snprintf(b, sizeof(b), "x%u", k.coins);
  gfx.textShadow(28, SCR_H - 22, b, k.coins >= 10 ? C::YELLOW : C::WHITE, C::BLACK, F_TEXT);
  // the order: everybody's helmet colour, ours framed
  if (!timeTrial)
    for (uint8_t p = 0; p < R.nk; p++) {
      const KgKart &q = R.k[R.order[p]];
      int16_t y = (int16_t)(72 + p * 15), x = 6;
      if (q.flags & KK_GONE) continue;
      gfx.fill(x, y, 22, 13, R.order[p] == R.me ? C::WHITE : C::BLACK);
      gfx.fill(x + 1, y + 1, 20, 11, KG_DRV[q.drv].body[1]);
      gfx.disc(x + 15, y + 6, 3, KG_DRV[q.drv].helm[0]);
      snprintf(b, sizeof(b), "%u", p + 1);
      gfx.text(x + 3, y + 2, b, C::BLACK, F_SMALL);
    }
  kgMiniMap(R, SCR_W - 70, SCR_H - 70);
  if (k.wrong > 120 && !k.finish) {
    if ((ms / 300) % 2) { gfx.rbox(90, 104, 140, 30, 8, C::DKRED); gfx.center(110, "WRONG WAY!", C::WHITE, F_BOLD); }
  }
}

// lines rushing past at the edges while a boost runs
static void kgSpeedLines(uint32_t ms) {
  for (uint8_t i = 0; i < 10; i++) {
    uint32_t h = kgHash(ms / 40 + i * 31);
    int16_t y = (int16_t)(KG_HZ + 10 + h % (SCR_H - KG_HZ - 20));
    int16_t len = (int16_t)(20 + (h >> 8) % 40);
    bool left = (h >> 16) & 1;
    int16_t x = left ? (int16_t)((h >> 20) % 30) : (int16_t)(SCR_W - 1 - (h >> 20) % 30);
    int16_t ey = (int16_t)(y + (y - 150) / 5);
    gfx.line(x, y, left ? x + len : x - len, ey, (h >> 24) & 1 ? C::WHITE : GREY4);
  }
}

// the whole picture of a race
static void kgFrame(KgRace &R, const KgCam &c, uint32_t ms, const char *tag = NULL) {
  kgTexAnim(*R.T, ms);
  kgSky(*R.T, c, ms);
  kgFloor(*R.T, c);
  kgObjects(R, c, ms, tag);
}
