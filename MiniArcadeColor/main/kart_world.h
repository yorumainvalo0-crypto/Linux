// MiniArcade Color: Kart GP - the world: a track built from its centre
// line, the karts' physics, the CPU drivers and the items. No drawing in
// here. Part of console.cpp (through game_kart.h).
/* The map is built when a track is loaded: the centre line is walked
   pixel by pixel and every point across it (2 px apart) gets the
   material its distance from the middle asks for - road, the white
   edge, kerbs in red and white along the way, sand or snow, a wall, the
   sea or lava - the nearer stamp wins where two parts of the track come
   close. Only the 32 x 32 px tiles near the track are kept (8 x 8 cells
   of 4 px, half a byte each); the rest of the world is one of two plain
   tiles. About 15 kB for a track, on the heap while the game runs.
   Positions are in 1/256 px, speeds in 1/256 px per 10 ms step.      */

#define KG_PTMAX 600                   // centre line points (4800 px)

struct KgPt { int16_t x, y; uint16_t a; int8_t line, curv; };   // x, y in 1/4 px; a = heading; line = racing line (px)
struct KgDecor { int16_t x, y; uint8_t kind, flip; };
struct KgBox { int16_t x, y; uint16_t off; };                    // off: steps until it is back
struct KgCoin { int16_t x, y; uint16_t off; };

struct KgTrack {
  uint8_t  id, theme, w, outMat;
  uint16_t n;                          // centre line points
  uint32_t len;                        // px
  KgPt     pt[KG_PTMAX];
  uint16_t tidx[KG_TILES * KG_TILES];  // tile of every 32 px square (0 / 1 = plain)
  uint8_t *tdat;                       // the tiles: 8 rows of 4 bytes
  uint16_t ntiles;
  uint8_t  tex[KM_N][256];             // 16 x 16 texels of every material (colours)
  uint8_t  fog[KG_FOG][256];           // colour -> the same in the haze
  uint8_t  flat[KG_FOG][KM_N];         // far away: one colour per material
  uint8_t  dark[256];                  // colour -> in a shadow
  KgDecor  dec[KG_DECOR];
  uint8_t  ndec, nbox, ncoin;
  KgBox    box[KG_BOXES];
  KgCoin   coin[KG_COINS];
  int16_t  mmX, mmY, mmS;              // the mini map: offset and scale (1/256)
};

static inline uint8_t kgMat(const KgTrack &T, int32_t x, int32_t y) {           // x, y in px
  if ((uint32_t)x >= KG_WORLD || (uint32_t)y >= KG_WORLD) return T.outMat;
  uint16_t t = T.tidx[(y >> 5) * KG_TILES + (x >> 5)];
  uint8_t b = T.tdat[t * 32 + ((y >> 2) & 7) * 4 + ((x >> 3) & 3)];
  return (x & 4) ? b >> 4 : b & 15;
}

// ---------------- building a track ----------------
static uint32_t kgHash(uint32_t a) { a ^= a >> 16; a *= 0x7feb352dU; a ^= a >> 15; a *= 0x846ca68bU; a ^= a >> 16; return a; }

// the closed Catmull-Rom curve through the points; f = 0..1 inside segment i
static void kgCurve(const KgTrackDef &d, int i, float f, float &x, float &y) {
  int n = d.n;
  const uint8_t *p0 = d.p[(i + n - 1) % n], *p1 = d.p[i % n], *p2 = d.p[(i + 1) % n], *p3 = d.p[(i + 2) % n];
  float t2 = f * f, t3 = t2 * f;
  x = 2.f * (0.5f * (2 * p1[0] + (-p0[0] + p2[0]) * f + (2 * p0[0] - 5 * p1[0] + 4 * p2[0] - p3[0]) * t2 + (-p0[0] + 3 * p1[0] - 3 * p2[0] + p3[0]) * t3));
  y = 2.f * (0.5f * (2 * p1[1] + (-p0[1] + p2[1]) * f + (2 * p0[1] - 5 * p1[1] + 4 * p2[1] - p3[1]) * t2 + (-p0[1] + 3 * p1[1] - 3 * p2[1] + p3[1]) * t3));
  x *= 2.f; y *= 2.f;                                         // the points are in 4 px
}

// centre line points every 8 px of the curve, in two walks: the length first
static void kgCentre(KgTrack &T, const KgTrackDef &d) {
  const int SUB = 48;
  float len = 0, px, py;
  kgCurve(d, 0, 0, px, py);
  for (int i = 0; i < d.n; i++)
    for (int s = 1; s <= SUB; s++) {
      float x, y;
      kgCurve(d, i, (float)s / SUB, x, y);
      len += sqrtf((x - px) * (x - px) + (y - py) * (y - py));
      px = x; py = y;
    }
  int n = (int)(len / KG_PTS + 0.5f);
  if (n > KG_PTMAX) n = KG_PTMAX;
  float step = len / n, at = 0, next = 0;
  int k = 0;
  kgCurve(d, 0, 0, px, py);
  for (int i = 0; i < d.n && k < n; i++)
    for (int s = 1; s <= SUB && k < n; s++) {
      float x, y;
      kgCurve(d, i, (float)s / SUB, x, y);
      float seg = sqrtf((x - px) * (x - px) + (y - py) * (y - py));
      while (k < n && at + seg >= next) {
        float f = seg > 0 ? (next - at) / seg : 0;
        T.pt[k].x = (int16_t)((px + (x - px) * f) * 4);
        T.pt[k].y = (int16_t)((py + (y - py) * f) * 4);
        k++;
        next += step;
      }
      at += seg; px = x; py = y;
    }
  T.n = (uint16_t)k;
  T.len = (uint32_t)len;
  for (int i = 0; i < k; i++) {
    const KgPt &a = T.pt[(i + k - 1) % k], &b = T.pt[(i + 1) % k];
    T.pt[i].a = kgAtan2(b.y - a.y, b.x - a.x);
  }
  // how much it bends (heading change over 32 px, /128), smoothed
  int16_t raw[KG_PTMAX];
  for (int i = 0; i < k; i++) raw[i] = kgAngDiff(T.pt[(i + 2) % k].a, T.pt[(i + k - 2) % k].a);
  for (int i = 0; i < k; i++) {
    int32_t s = 0;
    for (int j = -2; j <= 2; j++) s += raw[(i + j + k) % k];
    s /= 5 * 128;
    T.pt[i].curv = (int8_t)(s > 127 ? 127 : s < -127 ? -127 : s);
  }
  // the racing line: towards the inside of a bend, smoothed so that it comes in from the outside
  int16_t line[KG_PTMAX];
  int lim = d.w - 9;
  for (int i = 0; i < k; i++) { int v = T.pt[i].curv * 3; line[i] = (int16_t)(v > lim ? lim : v < -lim ? -lim : v); }
  for (int pass = 0; pass < 3; pass++) {
    for (int i = 0; i < k; i++) {
      int32_t s = 0;
      for (int j = -6; j <= 6; j++) s += line[(i + j + k) % k];
      raw[i] = (int16_t)(s / 13);
    }
    memcpy(line, raw, sizeof(int16_t) * k);
  }
  for (int i = 0; i < k; i++) T.pt[i].line = (int8_t)(line[i] > lim ? lim : line[i] < -lim ? -lim : line[i]);
}

// what lies at distance l (px, + = right) from the middle, s px along the lap
static uint8_t kgBand(const KgTrack &T, const KgTrackDef &d, const KgTheme &th, int16_t l, int32_t s, int8_t curv) {
  int16_t al = l < 0 ? -l : l, w = T.w;
  int32_t sl = (s % (int32_t)T.len + (int32_t)T.len) % (int32_t)T.len;
  bool bend = curv > 11 || curv < -11, outside = (curv > 0) == (l < 0);
  if (al < w) {
    if (sl < 6 || sl >= (int32_t)T.len - 2) return KM_FINISH;
    for (uint8_t b = 0; b < 3; b++)
      if (d.boost[b]) {                                      // a pad: chevrons pointing the way
        int32_t bs = (int32_t)d.boost[b] * (int32_t)T.len / 256, dl = abs(l - d.boostLat[b]);
        if (sl >= bs && sl < bs + 20 && dl < 10) return (((sl - bs) + 40 - dl) / 4) & 1 ? KM_BOOST : KM_LINE;
      }
    if (al >= w - 4) return KM_EDGE;
    if (th.centreLine && al < 2 && (sl / 14) % 2 == 0) return KM_LINE;
    if (bend && al < w - 8 && (kgHash((uint32_t)(sl / 24) * 7 + T.id) & 3) == 0 && ((l > 0) == (curv > 0))) return KM_ROAD2;
    return KM_ROAD;
  }
  int16_t o = al - w;
  bool bendsOnly = th.kerb == 0;
  uint8_t kerb = bendsOnly ? (bend ? 8 : 0) : th.kerb;
  if (o < kerb) return ((sl + 2000) / 8) & 1 ? KM_KERBA : KM_KERBB;
  o -= kerb;
  uint8_t run = (T.theme == KW_MEADOW && !(bend && outside)) ? 0 : th.run;   // the meadow: sand only where a bend throws one out
  if (o < run) return (kgHash((uint32_t)((sl / 40) * 3 + (l < 0))) & 1) ? KM_RUN : KM_RUN2;
  o -= run;
  if (o < th.wall) return KM_WALL;
  o -= th.wall;
  if (th.outside == KM_PIT && o < 4) return KM_SHORE;
  return 255;                                                  // the world beyond: as the tile is
}

// walks the centre line pixel by pixel and calls f(x, y, lateral, s, curv) for points across it
template <typename F> static void kgSweep(const KgTrack &T, int16_t reach, F f) {
  for (uint16_t i = 0; i < T.n; i++) {
    const KgPt &p = T.pt[i], &q = T.pt[(i + 1) % T.n];
    int16_t da = kgAngDiff(q.a, p.a);
    for (int j = 0; j < KG_PTS; j++) {
      int32_t x4 = p.x + (q.x - p.x) * j / KG_PTS, y4 = p.y + (q.y - p.y) * j / KG_PTS;
      uint16_t a = (uint16_t)(p.a + da * j / KG_PTS);
      int32_t nx = -kgSin(a), ny = kgCos(a);                    // to the right, Q14
      for (int16_t l = -reach; l <= reach; l += 2) {
        int32_t x = (x4 * 4096 + nx * l + (1 << 13)) >> 14, y = (y4 * 4096 + ny * l + (1 << 13)) >> 14;
        f(x, y, l, (int32_t)i * KG_PTS + j, p.curv);
      }
    }
  }
}

static void kgCell(KgTrack &T, int32_t x, int32_t y, uint8_t m) {
  if ((uint32_t)x >= KG_WORLD || (uint32_t)y >= KG_WORLD) return;
  uint16_t t = T.tidx[(y >> 5) * KG_TILES + (x >> 5)];
  if (t < 2) return;
  uint8_t &b = T.tdat[t * 32 + ((y >> 2) & 7) * 4 + ((x >> 3) & 3)];
  uint8_t cur = (x & 4) ? b >> 4 : b & 15;
  if (KM_RANK[m] >= KM_RANK[cur]) return;
  b = (x & 4) ? (uint8_t)((b & 0x0F) | m << 4) : (uint8_t)((b & 0xF0) | m);
}

// ---------------- textures ----------------
// the shade (0..3) of texel (u, v) of a pattern; t = time for the moving ones
static uint8_t kgTexel(uint8_t pat, uint8_t m, uint8_t u, uint8_t v, uint8_t t) {
  uint32_t h = kgHash((uint32_t)(u + v * 16 + m * 256));
  switch (pat) {
  case KT_SOLID:   return (h & 15) == 0 ? 1 : 0;
  case KT_NOISE:   return (h & 7) == 0 ? 1 : ((h >> 3) & 31) == 0 ? 2 : ((h >> 8) & 63) == 0 ? 3 : 0;
  case KT_GRASS:   { uint8_t s = 0; if (((u + v * 5) & 7) == 0 && (h & 48) == 0) s = 2; if ((h >> 6 & 15) == 0) s = 1; if ((h >> 10 & 127) == 0) s = 3; return s; }
  case KT_SAND:    return (h & 7) == 0 ? 1 : ((h >> 3) & 7) == 0 ? 2 : ((h >> 6) & 63) == 0 ? 3 : 0;
  case KT_STRIPE:  return v < 2 ? 2 : v > 13 ? 1 : ((h & 31) == 0 ? 3 : 0);
  case KT_BRICK:   { uint8_t row = v / 4, off = (row & 1) * 4; return (v % 4 == 3 || (u + off) % 8 == 7) ? 3 : ((h & 7) == 0 ? 1 : 0); }
  case KT_BLOCKS:  return (u == 15 || v == 15) ? 3 : (u == 0 || v == 0) ? 2 : ((h & 15) == 0 ? 1 : 0);
  case KT_WAVE:    { int w = (u * 2 + v * 3 + t) & 31; int w2 = (u * 3 - v * 2 + t * 2 + 64) & 31; return (w < 3 && (h & 1)) ? 2 : (w2 < 2) ? 1 : ((h & 63) == 0 ? 3 : 0); }
  case KT_CHEVRON: return (t / 6) & 1 ? ((h & 7) ? 2 : 0) : ((h & 7) ? 0 : 1);      // a pad flashing
  case KT_CHECK:   return ((u >> 2) + (v >> 2)) & 1 ? 1 : 0;
  case KT_LAVA:    { int w = (u * 3 + v * 5 + t) & 31, w2 = (u * 5 - v * 3 + 64 + t / 2) & 31; return w < 3 ? 2 : (w2 < 4 ? 1 : ((h & 15) == 0 ? 3 : 0)); }
  case KT_ICE:     return ((u + v * 2) & 15) == 0 || ((u * 3 + v) & 31) == 5 ? 2 : (h & 15) == 0 ? 1 : ((h >> 4) & 31) == 0 ? 3 : 0;
  case KT_MARKS:   return (u == 4 || u == 5 || u == 11 || u == 12) && (h & 3) ? 3 : ((h >> 2) & 7) == 0 ? 1 : 0;
  }
  return 0;
}

static void kgTexMake(KgTrack &T, uint8_t m, uint8_t t) {
  const KgTheme &th = KG_THEME[T.theme];
  for (uint8_t v = 0; v < 16; v++)
    for (uint8_t u = 0; u < 16; u++) T.tex[m][v * 16 + u] = th.mat[m][kgTexel(th.pat[m], m, u, v, t)];
}

// the moving materials: water, lava, surf, boost pads
static void kgTexAnim(KgTrack &T, uint32_t ms) {
  const KgTheme &th = KG_THEME[T.theme];
  uint8_t t = (uint8_t)(ms / 60);
  static const uint8_t ANIM[5] = { KM_OUTA, KM_OUTB, KM_PIT, KM_SHORE, KM_BOOST };
  for (uint8_t i = 0; i < 5; i++) {
    uint8_t m = ANIM[i], p = th.pat[m];
    if (p == KT_WAVE || p == KT_LAVA || p == KT_CHEVRON) kgTexMake(T, m, m == KM_BOOST ? (uint8_t)(ms / 25) : t);
  }
}

static void kgLook(KgTrack &T) {
  const KgTheme &th = KG_THEME[T.theme];
  for (int c = 0; c < 256; c++) T.dark[c] = fxLerp((col)c, C::BLACK, 110);
  for (uint8_t m = 0; m < KM_N; m++) kgTexMake(T, m, 0);
  for (uint8_t f = 0; f < KG_FOG; f++) {
    uint8_t s = (uint8_t)(f * 255 / KG_FOG);
    for (int c = 0; c < 256; c++) T.fog[f][c] = f ? fxLerp((col)c, th.fog, s) : (uint8_t)c;
    for (uint8_t m = 0; m < KM_N; m++) {                       // far: the material's middle tone
      uint8_t r0, g0, b0, r1, g1, b1;
      colToRgb(th.mat[m][0], r0, g0, b0); colToRgb(th.mat[m][1], r1, g1, b1);
      T.flat[f][m] = fxLerp(rgb((uint8_t)((r0 * 3 + r1) / 4), (uint8_t)((g0 * 3 + g1) / 4), (uint8_t)((b0 * 3 + b1) / 4)), th.fog, (uint8_t)(48 + f * 200 / KG_FOG));
    }
  }
}

// ---------------- the whole track ----------------
static void kgPtAt(const KgTrack &T, uint16_t i, int16_t lat, int32_t &x, int32_t &y) {   // px
  const KgPt &p = T.pt[i % T.n];
  x = (p.x + ((-kgSin(p.a) * lat * 4) >> 14)) / 4;
  y = (p.y + ((kgCos(p.a) * lat * 4) >> 14)) / 4;
}

static bool kgBuild(KgTrack &T, uint8_t id) {
  const KgTrackDef &d = KG_TRACK[id];
  const KgTheme &th = KG_THEME[d.theme];
  free(T.tdat);
  T.tdat = NULL;
  T.id = id; T.theme = d.theme; T.w = d.w;
  T.outMat = th.outside;
  kgCentre(T, d);
  int16_t reach = (int16_t)(d.w + (th.kerb ? th.kerb : 8) + th.run + th.wall + 6);
  // which tiles the track touches: they get cells of their own
  memset(T.tidx, 0, sizeof(T.tidx));
  kgSweep(T, reach + 4, [&](int32_t x, int32_t y, int16_t, int32_t, int8_t) {
    if ((uint32_t)x < KG_WORLD && (uint32_t)y < KG_WORLD) T.tidx[(y >> 5) * KG_TILES + (x >> 5)] = 0xFFFF;
  });
  uint16_t nt = 2;
  for (uint16_t i = 0; i < KG_TILES * KG_TILES; i++) {
    uint8_t chk = (uint8_t)(((i % KG_TILES) ^ (i / KG_TILES)) & 1);
    T.tidx[i] = T.tidx[i] == 0xFFFF ? nt++ : chk;
  }
  T.ntiles = nt;
  T.tdat = (uint8_t *)malloc((size_t)nt * 32);
  if (!T.tdat) return false;
  uint8_t outA = th.outside == KM_PIT ? KM_PIT : KM_OUTA, outB = th.outside == KM_PIT ? KM_PIT : KM_OUTB;
  memset(T.tdat, outA | outA << 4, 32);
  memset(T.tdat + 32, outB | outB << 4, 32);
  for (uint16_t i = 0; i < KG_TILES * KG_TILES; i++)
    if (T.tidx[i] >= 2) memcpy(T.tdat + T.tidx[i] * 32, T.tdat + ((((i % KG_TILES) ^ (i / KG_TILES)) & 1) ? 32 : 0), 32);
  kgSweep(T, reach, [&](int32_t x, int32_t y, int16_t l, int32_t s, int8_t c) {
    uint8_t m = kgBand(T, d, th, l, s, c);
    if (m != 255) kgCell(T, x, y, m);
  });
  // gaps the sweep left on the outside of tight bends
  for (uint16_t i = 0; i < KG_TILES * KG_TILES; i++) {
    if (T.tidx[i] < 2) continue;
    int32_t bx = (i % KG_TILES) * KG_TILE, by = (i / KG_TILES) * KG_TILE;
    for (int32_t y = by; y < by + KG_TILE; y += KG_CELL)
      for (int32_t x = bx; x < bx + KG_TILE; x += KG_CELL) {
        uint8_t m = kgMat(T, x, y);
        if (KM_RANK[m] < 7) continue;
        uint8_t l = kgMat(T, x - KG_CELL, y), r = kgMat(T, x + KG_CELL, y), u = kgMat(T, x, y - KG_CELL), w = kgMat(T, x, y + KG_CELL);
        if (l == r && KM_RANK[l] < KM_RANK[m]) kgCell(T, x, y, l);
        else if (u == w && KM_RANK[u] < KM_RANK[m]) kgCell(T, x, y, u);
      }
  }
  kgLook(T);
  // item boxes: rows of four across the road; coins: lines of five on the racing line
  T.nbox = T.ncoin = 0;
  for (uint8_t r = 0; r < 3; r++) {
    if (!d.boxes[r]) continue;
    uint16_t i = (uint16_t)(d.boxes[r] * T.n / 256);
    for (int8_t c = 0; c < 4 && T.nbox < KG_BOXES; c++) {
      int32_t x, y;
      kgPtAt(T, i, (int16_t)((c * 2 - 3) * d.w / 5), x, y);
      T.box[T.nbox++] = { (int16_t)x, (int16_t)y, 0 };
    }
  }
  for (uint8_t r = 0; r < 4; r++) {
    if (!d.coins[r]) continue;
    uint16_t i = (uint16_t)(d.coins[r] * T.n / 256);
    for (uint8_t c = 0; c < 5 && T.ncoin < KG_COINS; c++) {
      int32_t x, y;
      uint16_t j = (uint16_t)((i + c * 2) % T.n);
      kgPtAt(T, j, T.pt[j].line, x, y);
      T.coin[T.ncoin++] = { (int16_t)x, (int16_t)y, 0 };
    }
  }
  // things by the track: on both sides now and then, where nothing else is
  T.ndec = 0;
  int16_t band0 = (int16_t)(d.w + (th.kerb ? th.kerb : 8) + th.run + th.wall);
  for (uint16_t i = 3; i < T.n && T.ndec < KG_DECOR - 2; i += 4) {
    uint32_t h = kgHash(i * 131u + id * 7u);
    for (int8_t side = -1; side <= 1; side += 2) {
      if ((h >> (side > 0 ? 1 : 2)) % 3 == 0 || T.ndec >= KG_DECOR - 2) continue;
      int16_t lat;
      if (th.decorBand == 1) lat = (int16_t)(d.w + (th.kerb ? th.kerb : 8) + 6 + (h >> 8) % (th.run > 14 ? th.run - 12 : 2));
      else if (th.decorBand == 2) lat = (int16_t)(band0 + 6);
      else lat = (int16_t)(band0 + 10 + (h >> 8) % 34);
      int32_t x, y;
      kgPtAt(T, i, (int16_t)(lat * side), x, y);
      bool ok = true;
      for (int16_t dy = -10; dy <= 10 && ok; dy += 10)
        for (int16_t dx = -10; dx <= 10 && ok; dx += 10) {
          uint8_t m = kgMat(T, x + dx, y + dy);
          if (KM_RANK[m] < 5 || (th.decorBand != 1 && m == KM_PIT)) ok = false;
        }
      uint8_t m0 = kgMat(T, x, y);
      if (th.decorBand == 1 && m0 != KM_RUN && m0 != KM_RUN2) ok = false;
      if (!ok) continue;
      uint8_t kind = th.decor[(h >> 12) % 4 == 0 ? 1 : 0];
      T.dec[T.ndec++] = { (int16_t)x, (int16_t)y, kind, (uint8_t)((h >> 16) & 1) };
    }
  }
  // chevron boards on the outside of the sharpest bends
  for (uint16_t i = 0; i < T.n && T.ndec < KG_DECOR; i += 2) {
    int8_t c = T.pt[i].curv;
    if (c < 30 && c > -30) continue;
    bool peak = true;
    for (int j = -6; j <= 6; j++) if (abs(T.pt[(i + j + T.n) % T.n].curv) > abs(c)) peak = false;
    if (!peak) continue;
    int32_t x, y;
    int16_t lat = (int16_t)((c > 0 ? -1 : 1) * (d.w + (th.kerb ? th.kerb : 8) + 10));
    kgPtAt(T, (uint16_t)(i + 2), lat, x, y);
    if (KM_RANK[kgMat(T, x, y)] >= 5 && kgMat(T, x, y) != KM_PIT) T.dec[T.ndec++] = { (int16_t)x, (int16_t)y, KD_CHEV, 0 };
    i += 10;
  }
  // the mini map: the track fitted into 64 x 64 px
  int16_t x0 = 4096, y0 = 4096, x1 = 0, y1 = 0;
  for (uint16_t i = 0; i < T.n; i++) {
    if (T.pt[i].x < x0) x0 = T.pt[i].x;
    if (T.pt[i].x > x1) x1 = T.pt[i].x;
    if (T.pt[i].y < y0) y0 = T.pt[i].y;
    if (T.pt[i].y > y1) y1 = T.pt[i].y;
  }
  int16_t span = (int16_t)((x1 - x0 > y1 - y0 ? x1 - x0 : y1 - y0) / 4 + 1);
  T.mmS = (int16_t)(64 * 256 / span);
  T.mmX = (int16_t)((x0 + x1) / 8); T.mmY = (int16_t)((y0 + y1) / 8);
  return true;
}

// ---------------- karts ----------------
enum { KK_CPU = 1, KK_REMOTE = 2, KK_GHOST = 4, KK_DONE = 8, KK_GONE = 16 };
enum { KI_NONE, KI_TURBO, KI_PEEL, KI_ROCKET, KI_SHIELD, KI_N };
#define KG_ROUL     110                // steps the item roulette turns
#define KG_DRIFT1   110                // drift charge for a blue mini turbo ...
#define KG_DRIFT2   230                // ... and an orange one

struct KgIn { int16_t steer; bool gas, brake, drift, use; };

struct KgKart {
  int32_t  x, y, vx, vy;               // 1/256 px, 1/256 px per step
  uint16_t a;                          // heading
  uint16_t idx;                        // nearest centre line point
  int32_t  prog;                       // centre line points driven since the start line
  int16_t  z, vz;                      // height (1/256 px), for hops and tumbles
  uint8_t  drv, flags;
  uint8_t  item, roul, coins, place;
  int8_t   drift;
  uint16_t charge;
  uint16_t boost, spin, tumble, shield, inv, fall, hop;
  uint8_t  surf;
  uint32_t finish;                     // race ms at the line, 0 = still racing
  uint32_t lapStart, lastLap, bestLap;
  uint8_t  lap;                        // laps done
  uint8_t  skill;                      // CPU: 0..255
  int8_t   lineOff;
  uint16_t aiItem, stuck, wrong;
  int16_t  steer;                      // as steered last (sprite, sparks)
  bool     useHeld;
};

struct KgPeel { int16_t x, y; uint8_t id, owner; bool on; };
struct KgRocket { int32_t x, y; uint16_t a, idx, life; uint8_t id, owner, target; bool on; };
struct KgObj { int32_t z; int16_t sx, sy; uint8_t kind, i; };   // a thing to draw, its distance
#define KG_OBJS 112

enum { KS_INTRO, KS_COUNT, KS_RACE, KS_OVER };
// what happened in a race, counted (the results, the host tests)
struct KgStats { uint16_t boxes, coins, used[KI_N], rocketHits, peelHits, blocked, falls, minis, drifts, walls, bumps; };
// what happened in a step (for the sound and the effects of the player's kart)
enum { KE_BOX = 1, KE_COIN = 2, KE_SPIN = 4, KE_HIT = 8, KE_BUMP = 16, KE_WALL = 32, KE_TURBO = 64, KE_MINI = 128,
       KE_LAP = 256, KE_FALL = 512, KE_FINISH = 1024, KE_SHIELD = 2048, KE_FIRE = 4096, KE_GOT = 8192, KE_BOOST = 16384 };
// a networked thing the player did (kart_online.h sends it)
enum { KN_BOX = 1, KN_PEEL, KN_ROCKET, KN_PEELHIT, KN_ROCKETHIT, KN_FINISH };

struct KgRace {
  KgTrack *T;
  KgKart   k[KG_KARTS + 1];            // the last one: the time trial's ghost
  uint8_t  nk, me;
  int32_t  vmax;
  uint8_t  cc;                         // 0 50cc, 1 100cc, 2 150cc
  uint8_t  phase;
  uint32_t t;                          // race ms since GO
  uint32_t seed;
  uint16_t ev;                         // KE_ bits of the player's kart this frame
  uint8_t  order[KG_KARTS];            // kart of every place
  KgPeel   peel[KG_PEELS];
  KgRocket rk[KG_ROCKETS];
  uint8_t  nextId;
  bool     items;                      // items on (off in the time trial: three turbos instead)
  bool     online;
  void   (*net)(uint8_t type, uint8_t a, uint8_t b, uint8_t c);
  int16_t  hitX, hitY;                 // where the last hit was (effects)
  uint8_t  hitKart;
  KgStats  st;
  KgObj    obj[KG_OBJS];               // the drawing's list (kart_draw.h)
};

static uint32_t kgRand(KgRace &R) { R.seed ^= R.seed << 13; R.seed ^= R.seed >> 17; R.seed ^= R.seed << 5; return R.seed; }

// the nearest centre line point, looked for near the last one (all of them when far off)
static void kgTrackPos(const KgTrack &T, KgKart &k) {
  int32_t px = k.x >> 6, py = k.y >> 6;                          // 1/4 px
  int32_t best = 0x7FFFFFFF;
  uint16_t bi = k.idx;
  for (int j = -10; j <= 10; j++) {
    uint16_t i = (uint16_t)((k.idx + j + T.n) % T.n);
    int32_t dx = T.pt[i].x - px, dy = T.pt[i].y - py, d = dx * dx + dy * dy;
    if (d < best) { best = d; bi = i; }
  }
  if (best > 70 * 70 * 16)                                       // far off: all of them, but no jumps forward (no short cuts)
    for (uint16_t i = 0; i < T.n; i++) {
      int32_t dx = T.pt[i].x - px, dy = T.pt[i].y - py, d = dx * dx + dy * dy;
      int32_t dj = ((int32_t)i - k.idx + T.n + T.n / 2) % T.n - T.n / 2;
      if (d < best && dj < 12) { best = d; bi = i; }
    }
  int32_t dj = ((int32_t)bi - k.idx + T.n + T.n / 2) % T.n - T.n / 2;
  k.prog += dj;
  k.idx = bi;
}

static void kgPlace(KgRace &R, KgKart &k, uint16_t i, int16_t lat) {
  int32_t x, y;
  kgPtAt(*R.T, i, lat, x, y);
  k.x = x << 8; k.y = y << 8; k.vx = k.vy = 0;
  k.a = R.T->pt[i % R.T->n].a;
  k.idx = (uint16_t)(i % R.T->n);
  k.z = k.vz = 0;
}

// the start grid: two rows behind the line, the first place in front
static void kgGrid(KgRace &R, KgKart &k, uint8_t slot) {
  const KgTrack &T = *R.T;
  uint16_t back = (uint16_t)(2 + slot * 2);
  kgPlace(R, k, (uint16_t)(T.n - back), (int16_t)((slot & 1) ? T.w / 3 : -T.w / 3));
  k.prog = -(int32_t)back;
}

static void kgKartReset(KgKart &k, uint8_t drv, uint8_t flags) {
  memset(&k, 0, sizeof(k));
  k.drv = drv; k.flags = flags;
}

static int32_t kgVmax(const KgRace &R, const KgKart &k) {
  const KgDriver &D = KG_DRV[k.drv];
  int32_t v = R.vmax * (94 + D.speed * 2) / 100;
  v = v * (100 + k.coins) / 100;                                 // a coin: 1 % faster, ten at most
  return v;
}

static void kgHitBy(KgRace &R, KgKart &k, bool rocket);

// falls into water or lava: back on the track a moment later
static void kgRespawn(KgRace &R, KgKart &k) {
  const KgTrack &T = *R.T;
  uint16_t i = (uint16_t)((k.idx + T.n - 2) % T.n);
  int32_t prog = k.prog - 2;
  kgPlace(R, k, i, 0);
  k.prog = prog;
  k.inv = 150; k.boost = k.spin = k.tumble = 0; k.drift = 0; k.charge = 0;
}

// one 10 ms step of a kart
static void kgKartStep(KgRace &R, KgKart &k, const KgIn &in) {
  const KgTrack &T = *R.T;
  const KgTheme &th = KG_THEME[T.theme];
  const KgDriver &D = KG_DRV[k.drv];
  bool me = &k == &R.k[R.me];
  if (k.fall) {                                                 // in the water: sink, then back
    if (--k.fall == 0) kgRespawn(R, k);
    k.vx -= k.vx / 8; k.vy -= k.vy / 8;
    k.x += k.vx; k.y += k.vy;
    k.z -= 24;
    return;
  }
  if (k.boost) k.boost--;
  if (k.inv) k.inv--;
  if (k.shield) k.shield--;
  if (k.roul && --k.roul == 0) {                                 // the roulette stops
    uint32_t r = kgRand(R) % 100;
    uint8_t pl = k.place ? k.place : 1, n = R.nk;
    if (R.online) n = 2;
    if (pl == 1) k.item = r < 50 ? KI_PEEL : r < 80 ? KI_SHIELD : KI_TURBO;
    else if (pl * 2 <= n + 1) k.item = r < 25 ? KI_PEEL : r < 55 ? KI_TURBO : r < 80 ? KI_ROCKET : KI_SHIELD;
    else k.item = r < 42 ? KI_TURBO : r < 84 ? KI_ROCKET : r < 92 ? KI_SHIELD : KI_PEEL;
    if (me) R.ev |= KE_GOT;
  }
  uint8_t m = kgMat(T, k.x >> 8, k.y >> 8);
  k.surf = m;
  if (m == KM_PIT && !k.z) {
    k.fall = 60; k.drift = 0;
    R.st.falls++;
    if (me) R.ev |= KE_FALL;
    return;
  }
  if (m == KM_BOOST && k.boost < 60) { k.boost = 60; if (me) R.ev |= KE_BOOST; }
  int32_t vmax = kgVmax(R, k);
  int32_t sp = th.speed[m];
  if (k.boost) { vmax = vmax * 4 / 3; if (sp < 100) sp = 100; }
  vmax = vmax * sp / 100;
  int32_t vfull = kgVmax(R, k);
  // turn
  int16_t steer = in.steer;
  k.steer = steer;
  bool stunned = k.spin || k.tumble;
  int32_t c = kgCos(k.a), s = kgSin(k.a);
  int32_t vf = (k.vx * c + k.vy * s) >> 14;
  if (k.spin) { k.spin--; k.a += 1100; }
  else if (k.tumble) { k.tumble--; k.a += 700; }
  else {
    int32_t av = vf < 0 ? -vf : vf;
    int32_t f = av * 256 / (vfull * 35 / 100 + 1);
    if (f > 256) f = 256;
    int32_t turnMax = 150 + D.handling * 12;
    int32_t turn = steer * turnMax / 256 * f / 256;
    // drifting: a hop, then the kart slides round the bend, charging a mini turbo
    if (in.drift && !k.drift && !k.hop && (steer > 90 || steer < -90) && vf > vfull / 2 && !k.z) {
      k.drift = steer > 0 ? 1 : -1; k.hop = 14; k.vz = 300; k.charge = 0;
      if (me) R.st.drifts++;
    }
    if (k.drift) {
      if (!in.drift || vf < vfull * 3 / 10) {
        if (k.charge >= KG_DRIFT1) { k.boost = k.charge >= KG_DRIFT2 ? 110 : 55; if (me) { R.ev |= KE_MINI; R.st.minis++; } }
        k.drift = 0; k.charge = 0;
      } else {
        int32_t into = steer * k.drift;                          // -256..256: steering with the drift or against it
        turn = k.drift * turnMax * (150 + into * 106 / 256) / 256 * f / 256;
        k.charge = (uint16_t)(k.charge + 1 + (into > 60 ? 1 : 0));
      }
    }
    if (vf < 0) turn = -turn;
    k.a = (uint16_t)(k.a + turn);
  }
  if (k.hop) k.hop--;
  if (k.z || k.vz) {                                             // in the air
    k.z += k.vz; k.vz -= k.tumble ? 22 : 40;
    if (k.z <= 0) { k.z = 0; k.vz = 0; }
  }
  // speeds along and across the new heading
  c = kgCos(k.a); s = kgSin(k.a);
  vf = (k.vx * c + k.vy * s) >> 14;
  int32_t vl = (-k.vx * s + k.vy * c) >> 14;
  int32_t acc = vfull * (2 + D.accel) / 520 + 1;
  if (stunned) vf -= vf / 24;
  else if (in.brake && !k.drift) {
    if (vf > 0) vf -= vfull / 50;
    else if (vf > -vfull / 3) vf -= acc / 2;
  } else if (in.gas || k.boost) {
    if (vf < vmax) { vf += acc + (vmax - vf) / 160; if (vf > vmax) vf = vmax; }
  } else vf -= vf / 100;
  if (vf > vmax) vf -= (vf - vmax) / 12 + 1;                       // off the road: slows down
  int32_t grip = (KM_RANK[m] <= 4) ? th.gripRoad : th.gripOff;
  if (k.drift) grip = grip / 5;
  if (stunned) grip = grip / 3;
  vl -= vl * grip / 256;
  k.vx = (vf * c - vl * s) >> 14;
  k.vy = (vf * s + vl * c) >> 14;
  // move; walls stop it and push it back
  int32_t nx = k.x + k.vx, ny = k.y + k.vy;
  auto solid = [&](int32_t x, int32_t y) { uint8_t mm = kgMat(T, x >> 8, y >> 8); return th.speed[mm] == 0 && mm == KM_WALL; };
  if (solid(nx, ny)) {
    bool xs = solid(nx, k.y), ys = solid(k.x, ny);
    if (!xs) { k.x = nx; k.vy = -k.vy / 3; k.vx = k.vx * 3 / 4; }
    else if (!ys) { k.y = ny; k.vx = -k.vx / 3; k.vy = k.vy * 3 / 4; }
    else { k.vx = -k.vx / 3; k.vy = -k.vy / 3; }
    k.drift = 0; k.charge = 0;
    if (me) { R.ev |= KE_WALL; R.st.walls++; }
  } else { k.x = nx; k.y = ny; }
}

// two karts touch: pushed apart, the lighter one more
static void kgCollide(KgRace &R) {
  for (uint8_t i = 0; i < R.nk; i++)
    for (uint8_t j = i + 1; j < R.nk; j++) {
      KgKart &a = R.k[i], &b = R.k[j];
      if (a.fall || b.fall || (a.flags & KK_GONE) || (b.flags & KK_GONE)) continue;
      if ((a.flags & KK_REMOTE) && (b.flags & KK_REMOTE)) continue;
      int32_t dx = (b.x - a.x) >> 4, dy = (b.y - a.y) >> 4;      // 1/16 px
      int32_t d2 = dx * dx + dy * dy, r = 2 * KG_R * 16;
      if (d2 >= r * r || d2 == 0) continue;
      int32_t d = (int32_t)kgSqrt((uint32_t)d2);
      int32_t push = (r - d) * 16;                               // 1/256 px
      int32_t wa = KG_DRV[a.drv].weight + 2, wb = KG_DRV[b.drv].weight + 2;
      if (a.flags & KK_REMOTE) wa = 1000;                        // the other console moves its own kart
      if (b.flags & KK_REMOTE) wb = 1000;
      int32_t ux = dx * 256 / d, uy = dy * 256 / d;              // 1/256
      int32_t pa = push * wb / (wa + wb), pb = push * wa / (wa + wb);
      a.x -= ux * pa / 256; a.y -= uy * pa / 256;
      b.x += ux * pb / 256; b.y += uy * pb / 256;
      int32_t rel = ((b.vx - a.vx) * ux + (b.vy - a.vy) * uy) / 256;
      if (rel < 0) {                                             // coming together: bounce
        int32_t ia = -rel * wb * 3 / (2 * (wa + wb)), ib = -rel * wa * 3 / (2 * (wa + wb));
        if (!(a.flags & KK_REMOTE)) { a.vx -= ux * ia / 256; a.vy -= uy * ia / 256; }
        if (!(b.flags & KK_REMOTE)) { b.vx += ux * ib / 256; b.vy += uy * ib / 256; }
        if (i == R.me || j == R.me) { R.ev |= KE_BUMP; R.st.bumps++; }
      }
    }
}

static void kgHitBy(KgRace &R, KgKart &k, bool rocket) {
  bool me = &k == &R.k[R.me];
  if (k.inv || k.fall) return;
  if (k.shield) { k.shield = 0; R.st.blocked++; if (me) R.ev |= KE_SHIELD; return; }
  if (rocket) { k.tumble = 100; k.vz = 520; R.st.rocketHits++; } else { k.spin = 64; R.st.peelHits++; }
  k.drift = 0; k.charge = 0; k.boost = 0;
  k.coins = k.coins > (rocket ? 3 : 2) ? (uint8_t)(k.coins - (rocket ? 3 : 2)) : 0;
  R.hitX = (int16_t)(k.x >> 8); R.hitY = (int16_t)(k.y >> 8); R.hitKart = (uint8_t)(&k - R.k);
  if (me) R.ev |= rocket ? KE_HIT : KE_SPIN;
}

// uses the item of kart i
static void kgUse(KgRace &R, uint8_t i) {
  KgKart &k = R.k[i];
  bool me = i == R.me;
  uint8_t it = k.item;
  if (!it || k.roul) return;
  k.item = KI_NONE;
  R.st.used[it]++;
  int32_t c = kgCos(k.a), s = kgSin(k.a);
  if (it == KI_TURBO) { k.boost = 120; if (me) R.ev |= KE_TURBO; }
  else if (it == KI_SHIELD) { k.shield = 600; }
  else if (it == KI_PEEL) {
    for (uint8_t p = 0; p < KG_PEELS; p++)
      if (!R.peel[p].on) {
        KgPeel &q = R.peel[p];
        q.x = (int16_t)((k.x - (c * 9 >> 6)) >> 8); q.y = (int16_t)((k.y - (s * 9 >> 6)) >> 8);
        q.on = true; q.owner = i; q.id = R.nextId++;
        if (me && R.net) R.net(KN_PEEL, (uint8_t)(q.x / 4), (uint8_t)(q.y / 4), q.id);
        break;
      }
  } else if (it == KI_ROCKET) {
    for (uint8_t p = 0; p < KG_ROCKETS; p++)
      if (!R.rk[p].on) {
        KgRocket &q = R.rk[p];
        q.x = k.x + (c * 8 >> 6); q.y = k.y + (s * 8 >> 6);
        q.a = k.a; q.idx = k.idx; q.life = 600; q.on = true; q.owner = i; q.id = R.nextId++;
        q.target = 255;
        if (k.place > 1) q.target = R.order[k.place - 2];      // the one just ahead
        if (me && R.net) R.net(KN_ROCKET, q.id, q.target == 255 ? 255 : (uint8_t)(q.target != i), 0);
        if (me) R.ev |= KE_FIRE;
        break;
      }
  }
}

// rockets fly along the track, then straight at their target
static void kgRockets(KgRace &R) {
  const KgTrack &T = *R.T;
  int32_t v = R.vmax * 8 / 5;
  for (uint8_t p = 0; p < KG_ROCKETS; p++) {
    KgRocket &q = R.rk[p];
    if (!q.on) continue;
    if (!q.life--) { q.on = false; continue; }
    KgKart *tg = q.target < R.nk ? &R.k[q.target] : NULL;
    int32_t tx, ty;
    bool home = false;
    if (tg && !tg->fall && !(tg->flags & KK_GONE)) {
      int32_t dx = (tg->x - q.x) >> 8, dy = (tg->y - q.y) >> 8;
      if (dx * dx + dy * dy < 70 * 70) home = true;
    }
    if (home) { tx = tg->x; ty = tg->y; }
    else {
      int32_t x, y;
      kgPtAt(T, (uint16_t)(q.idx + 3), 0, x, y);
      tx = x << 8; ty = y << 8;
    }
    uint16_t want = kgAtan2(ty - q.y, tx - q.x);
    int16_t d = kgAngDiff(want, q.a);
    int16_t lim = home ? 1400 : 900;
    q.a = (uint16_t)(q.a + (d > lim ? lim : d < -lim ? -lim : d));
    q.x += (kgCos(q.a) * v) >> 14; q.y += (kgSin(q.a) * v) >> 14;
    KgKart dummy;
    dummy.x = q.x; dummy.y = q.y; dummy.idx = q.idx; dummy.prog = 0;
    kgTrackPos(T, dummy);
    q.idx = dummy.idx;
    uint8_t m = kgMat(T, q.x >> 8, q.y >> 8);
    if (m == KM_WALL) { q.on = false; R.hitX = (int16_t)(q.x >> 8); R.hitY = (int16_t)(q.y >> 8); R.hitKart = 255; continue; }
    for (uint8_t i = 0; i < R.nk; i++) {
      KgKart &k = R.k[i];
      if ((i == q.owner && q.life > 560) || k.fall || (k.flags & KK_GONE)) continue;
      int32_t dx = (k.x - q.x) >> 8, dy = (k.y - q.y) >> 8;
      if (dx * dx + dy * dy > 36) continue;
      q.on = false;
      if (k.flags & KK_REMOTE) { R.hitX = (int16_t)(k.x >> 8); R.hitY = (int16_t)(k.y >> 8); R.hitKart = i; break; }   // its console says what happens
      kgHitBy(R, k, true);
      if (i == R.me && R.net) R.net(KN_ROCKETHIT, q.id, q.owner == R.me ? 1 : 0, 0);
      break;
    }
  }
}

static void kgPickups(KgRace &R) {
  KgTrack &T = *R.T;
  for (uint8_t b = 0; b < T.nbox; b++) if (T.box[b].off) T.box[b].off--;
  for (uint8_t c = 0; c < T.ncoin; c++) if (T.coin[c].off) T.coin[c].off--;
  for (uint8_t i = 0; i < R.nk; i++) {
    KgKart &k = R.k[i];
    if (k.fall || (k.flags & (KK_REMOTE | KK_GONE))) continue;
    int32_t x = k.x >> 8, y = k.y >> 8;
    if (R.items)
      for (uint8_t b = 0; b < T.nbox; b++) {
        KgBox &q = T.box[b];
        if (q.off || abs(q.x - x) > 7 || abs(q.y - y) > 7) continue;
        q.off = 160;
        if (!k.item && !k.roul) { k.roul = KG_ROUL; if (i == R.me) R.st.boxes++; }
        if (i == R.me) { R.ev |= KE_BOX; if (R.net) R.net(KN_BOX, b, 0, 0); }
      }
    for (uint8_t c = 0; c < T.ncoin; c++) {
      KgCoin &q = T.coin[c];
      if (q.off || abs(q.x - x) > 6 || abs(q.y - y) > 6) continue;
      q.off = 400;
      if (k.coins < 10) k.coins++;
      if (i == R.me) { R.ev |= KE_COIN; R.st.coins++; }
    }
    for (uint8_t p = 0; p < KG_PEELS; p++) {
      KgPeel &q = R.peel[p];
      if (!q.on || abs(q.x - x) > 5 || abs(q.y - y) > 5 || k.tumble || k.spin || k.z > 512) continue;
      q.on = false;
      kgHitBy(R, k, false);
      if (i == R.me && R.net) R.net(KN_PEELHIT, q.id, q.owner == R.me ? 1 : 0, 0);
    }
  }
}

// laps, the finish, the places
static void kgProgress(KgRace &R) {
  const KgTrack &T = *R.T;
  for (uint8_t i = 0; i < R.nk; i++) {
    KgKart &k = R.k[i];
    if (k.flags & (KK_REMOTE | KK_GONE)) continue;
    int32_t before = k.prog;
    if (!k.fall) kgTrackPos(T, k);
    k.wrong = (uint16_t)(k.prog < before ? k.wrong + 1 : (k.prog > before ? 0 : k.wrong));
    while (k.prog >= (int32_t)(k.lap + 1) * T.n && !k.finish) {
      k.lap++;
      k.lastLap = R.t - k.lapStart;
      if (!k.bestLap || k.lastLap < k.bestLap) k.bestLap = k.lastLap;
      k.lapStart = R.t;
      if (i == R.me) R.ev |= KE_LAP;
      if (k.lap >= KG_LAPS) {
        k.finish = R.t ? R.t : 1; k.flags |= KK_DONE;
        if (i == R.me) { R.ev |= KE_FINISH; if (R.net) R.net(KN_FINISH, (uint8_t)(R.t / 10), (uint8_t)(R.t / 2560), (uint8_t)(R.t / 655360)); }
      }
    }
  }
  // places: finished ones by their time, the others by how far they got
  uint8_t o[KG_KARTS];
  for (uint8_t i = 0; i < R.nk; i++) o[i] = i;
  for (uint8_t a = 1; a < R.nk; a++)
    for (uint8_t b = a; b > 0; b--) {
      const KgKart &p = R.k[o[b - 1]], &q = R.k[o[b]];
      bool swap;
      if ((p.flags & KK_GONE) != (q.flags & KK_GONE)) swap = (p.flags & KK_GONE) != 0;
      else if (p.finish && q.finish) swap = q.finish < p.finish;
      else if (p.finish || q.finish) swap = q.finish != 0;
      else swap = q.prog > p.prog;
      if (!swap) break;
      uint8_t t = o[b]; o[b] = o[b - 1]; o[b - 1] = t;
    }
  for (uint8_t p = 0; p < R.nk; p++) { R.order[p] = o[p]; R.k[o[p]].place = (uint8_t)(p + 1); }
}

// ---------------- the CPU drivers ----------------
static void kgAI(KgRace &R, uint8_t i, KgIn &in) {
  KgKart &k = R.k[i];
  const KgTrack &T = *R.T;
  memset(&in, 0, sizeof(in));
  in.gas = true;
  int32_t v = (k.vx * kgCos(k.a) + k.vy * kgSin(k.a)) >> 14;
  uint16_t look = (uint16_t)(3 + (v > 0 ? v : 0) / 70);
  uint16_t ti = (uint16_t)((k.idx + look) % T.n);
  int16_t lim = (int16_t)(T.w - 6);
  int16_t lat = (int16_t)(T.pt[ti].line + k.lineOff);
  // a peel or a slower kart right ahead: go round it
  for (uint8_t p = 0; p < KG_PEELS; p++) {
    const KgPeel &q = R.peel[p];
    if (!q.on) continue;
    int32_t dx = q.x - (k.x >> 8), dy = q.y - (k.y >> 8);
    int32_t ahead = (dx * kgCos(k.a) + dy * kgSin(k.a)) >> 14;
    if (ahead < 4 || ahead > 50) continue;
    int32_t side = (-dx * kgSin(k.a) + dy * kgCos(k.a)) >> 14;
    if (side > -9 && side < 9) lat = (int16_t)(lat + (side > 0 ? -12 : 12));
  }
  lat = lat > lim ? lim : lat < -lim ? -lim : lat;
  int32_t tx, ty;
  kgPtAt(T, ti, lat, tx, ty);
  uint16_t want = kgAtan2((ty << 8) - k.y, (tx << 8) - k.x);
  int16_t d = kgAngDiff(want, k.a);
  int32_t st = (int32_t)d * (180 + k.skill / 4) / 1024;
  if (k.flags & KK_CPU) {                                       // drift through the long bends
    int32_t bend = 0;
    for (uint16_t j = 2; j < 10; j++) bend += T.pt[(k.idx + j) % T.n].curv;
    int32_t vf = kgVmax(R, k);
    if (!k.drift && (bend > 180 || bend < -180) && v > vf * 7 / 10 && k.skill > 90) in.drift = true;
    if (k.drift && (bend * k.drift > 75 || k.charge < KG_DRIFT1)) in.drift = true;
    if (k.drift && k.charge >= KG_DRIFT2) in.drift = false;
  }
  in.steer = (int16_t)(st > 256 ? 256 : st < -256 ? -256 : st);
  if ((d > 9000 || d < -9000) && v > R.vmax / 2) in.brake = true;
  // the sharpest bend coming: as fast as the steering can take it (a drift a little faster)
  int32_t cmax = 0;
  for (uint16_t j = 1; j < look + 7; j++) { int32_t c = T.pt[(k.idx + j) % T.n].curv; c = c < 0 ? -c : c; if (c > cmax) cmax = c; }
  if (cmax > 35) {
    int32_t turnMax = 150 + KG_DRV[k.drv].handling * 12;
    int32_t lim = turnMax * 64 * (k.drift ? 125 : 112) / 100 / cmax;
    if (v > lim) in.gas = false;
    if (v > lim * 115 / 100 && !k.drift) in.brake = true;
  }
  // stuck against something: back off a moment
  if (v < 20 && R.phase == KS_RACE) k.stuck++; else if (k.stuck) k.stuck--;
  if (k.stuck > 150) {                                          // backwards, turned the other way
    in.brake = true; in.gas = false; in.drift = false;
    in.steer = (int16_t)(d > 0 ? -256 : 256);
    if (k.stuck > 280) k.stuck = 0;
    else k.stuck += 2;
  }
  // items, after a moment's thought
  if (k.item && (k.flags & KK_CPU)) {
    if (!k.aiItem) k.aiItem = (uint16_t)(60 + kgRand(R) % 240);
    else if (--k.aiItem == 0) {
      bool go = true;
      if (k.item == KI_ROCKET && k.place == 1) go = false;
      if (k.item == KI_PEEL) {                                  // best with somebody close behind
        go = false;
        for (uint8_t j = 0; j < R.nk; j++)
          if (j != i && R.k[j].place == k.place + 1 && abs((int32_t)(R.k[j].prog - k.prog)) < 8) go = true;
        if (!go && kgRand(R) % 4 == 0) go = true;
      }
      if (go) in.use = true; else k.aiItem = 40;
    }
  }
}

// a kart driven by the CPU for a moment (the player after the finish, the tests)
static void kgAutopilot(KgRace &R, uint8_t i, KgIn &in) {
  uint8_t sk = R.k[i].skill;
  R.k[i].skill = 200;
  kgAI(R, i, in);
  R.k[i].skill = sk;
  in.use = false;
}

// one step of the whole race; in = the player's input
static void kgRaceStep(KgRace &R, const KgIn &mine) {
  if (R.phase == KS_INTRO || R.phase == KS_COUNT) return;
  for (uint8_t i = 0; i < R.nk; i++) {
    KgKart &k = R.k[i];
    if (k.flags & (KK_REMOTE | KK_GONE)) continue;
    KgIn in;
    if (k.flags & KK_CPU) {
      kgAI(R, i, in);
      int32_t keep = R.vmax;                                    // CPU top speed: its skill, a little rubber band
      int32_t f = 90 + R.cc * 2 + k.skill / 32;
      if (!R.online && R.me < R.nk) {
        int32_t gap = k.prog - R.k[R.me].prog;
        if (gap > (int32_t)R.T->n / 5) f -= 4;
        else if (gap < -(int32_t)R.T->n / 5) f += 4;
      }
      R.vmax = keep * f / 100;
      kgKartStep(R, k, in);
      R.vmax = keep;
    } else {
      if (k.finish) kgAutopilot(R, i, in); else in = mine;
      kgKartStep(R, k, in);
    }
    bool use = in.use && !k.useHeld;
    k.useHeld = in.use;
    if (use && !k.finish) kgUse(R, i);
  }
  kgCollide(R);
  kgRockets(R);
  kgPickups(R);
  kgProgress(R);
  R.t += KG_STEP;
}

static void kgRaceInit(KgRace &R, KgTrack *T, uint8_t cc, uint32_t seed) {
  KgTrack *keep = T;
  memset(&R, 0, sizeof(R));
  R.T = keep;
  R.cc = cc;
  R.vmax = KG_VMAX * (85 + cc * 15) / 100;
  R.seed = seed | 1;
  R.items = true;
  for (uint8_t b = 0; b < T->nbox; b++) T->box[b].off = 0;
  for (uint8_t c = 0; c < T->ncoin; c++) T->coin[c].off = 0;
}
