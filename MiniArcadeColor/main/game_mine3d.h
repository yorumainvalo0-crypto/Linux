// MiniArcade Color: Mine 3D. Part of console.cpp, which includes it.
// A block world in first person: 32 x 32 x 16 blocks of grass hills,
// beaches, lakes, trees, caves and stone with coal, gold and crystal ore,
// new from a seed. The world wraps round at its edges. Free play: no
// time, every kind of block without end, the world is kept in flash (the
// seed and the blocks changed). Ore hunt: three minutes to dig ore,
// coal 1, gold 5, crystal 20 points, blocks only from what was dug.
// LEFT/RIGHT turn. UP/DOWN held walk forward / back (a step of one block
// is jumped), tapped look up / down (four steps). OK: the pick digs the
// block in the middle of the view (it goes into the bag), a block from
// the bag is put on the face you look at - put under your own feet, it
// lifts you. A double OK opens the bag: LEFT/RIGHT choose, OK takes it.
//
// Speed: one ray per pixel through the block grid (Amanatides & Woo) at
// 160 x 92, every pixel drawn 2 x 2, all in integers: positions in 1/256
// block, ray directions and lengths in 1/4096. Textures (16 x 16 tones)
// and their colours per light and fog step are made at the start on the
// heap and freed at the end, like the world. Sine table: Doom's.

#define VOX_W     32                     // world size x, z (wraps round)
#define VOX_H     16                     // height
#define VOX_N     (VOX_W * VOX_W * VOX_H)
#define VOX_RW    160                    // render size, drawn 2 x 2
#define VOX_RH    92
#define VOX_VY    TOP_H                  // view top
#define VOX_BAR   (VOX_VY + 2 * VOX_RH)  // 210: the bag below
#define VOX_TAN   3277                   // half the view width, Q12 (0.8 = 77 degrees)
#define VOX_VIEW  14                     // view distance in blocks
#define VOX_MAXT  (VOX_VIEW << 12)
#define VOX_FOG0  8                      // fog from here on, 8 steps up to VOX_VIEW
#define VOX_REACH (9 << 11)              // 4.5 blocks, Q12
#define VOX_SEA   5                      // water below this height
#define VOX_HW    77                     // the player: half width 0.3,
#define VOX_PH    461                    // height 1.8,
#define VOX_EYE   410                    // eyes at 1.6 (1/256 block)
#define VOX_JUMP  44                     // jump speed, gravity 3 (1/256 block per tick): 1.17 high
#define VOX_TICK  25                     // ms per game step
#define VOX_TIME  180                    // seconds per round
#define VOX_NT    13                     // textures
#define VOX_SLOTS 6                      // the bag: pick + five kinds of block
#define VOX_FSLOTS 10                    // free play: pick + nine kinds, no counting
#define VOX_EDITS 1000                   // free play: blocks changed that are kept
#define VOX_DBL   280                    // ms for a double OK
#define VOX_TAPMS 150                    // UP/DOWN let go before this: a tap (look)

enum : uint8_t { VB_AIR, VB_GRASS, VB_DIRT, VB_STONE, VB_SAND, VB_WOOD, VB_LEAF, VB_COAL, VB_GOLD, VB_GEM,
                 VB_ROCK, VB_WATER, VB_BLOOM };
enum : uint8_t { VT_GRASS, VT_GSIDE, VT_DIRT, VT_STONE, VT_SAND, VT_BARK, VT_RINGS, VT_LEAF, VT_COAL, VT_GOLD,
                 VT_GEM, VT_ROCK, VT_FLOWER };
// texture of the top, the sides and the bottom of every block
static const uint8_t VOX_FTEX[13][3] = {
  { 0, 0, 0 }, { VT_GRASS, VT_GSIDE, VT_DIRT }, { VT_DIRT, VT_DIRT, VT_DIRT }, { VT_STONE, VT_STONE, VT_STONE },
  { VT_SAND, VT_SAND, VT_SAND }, { VT_RINGS, VT_BARK, VT_RINGS }, { VT_LEAF, VT_LEAF, VT_LEAF },
  { VT_COAL, VT_COAL, VT_COAL }, { VT_GOLD, VT_GOLD, VT_GOLD }, { VT_GEM, VT_GEM, VT_GEM },
  { VT_ROCK, VT_ROCK, VT_ROCK }, { 0, 0, 0 }, { VT_FLOWER, VT_GSIDE, VT_DIRT } };
// colours: tones 0..9 from the first to the second, 10..15 from the third to the fourth
static const uint8_t VOX_RAMP[VOX_NT][4][3] = {
  { { 34, 96, 36 },   { 128, 200, 72 },  { 34, 96, 36 },   { 128, 200, 72 } },   // grass
  { { 82, 54, 32 },   { 168, 120, 78 },  { 40, 112, 40 },  { 124, 196, 70 } },   // grass side
  { { 82, 54, 32 },   { 168, 120, 78 },  { 150, 140, 128 }, { 210, 200, 186 } }, // dirt, pebbles
  { { 70, 72, 82 },   { 170, 170, 178 }, { 70, 72, 82 },   { 170, 170, 178 } },   // stone
  { { 178, 156, 98 }, { 248, 234, 178 }, { 178, 156, 98 }, { 248, 234, 178 } },  // sand
  { { 56, 36, 20 },   { 132, 94, 54 },   { 56, 36, 20 },   { 132, 94, 54 } },     // bark
  { { 124, 86, 44 },  { 214, 172, 108 }, { 60, 40, 22 },   { 112, 80, 46 } },     // cut log
  { { 18, 78, 44 },   { 98, 180, 70 },   { 18, 78, 44 },   { 98, 180, 70 } },     // leaves
  { { 70, 72, 82 },   { 170, 170, 178 }, { 16, 16, 20 },   { 74, 74, 84 } },      // coal
  { { 70, 72, 82 },   { 170, 170, 178 }, { 196, 132, 12 }, { 255, 240, 110 } },   // gold
  { { 70, 72, 82 },   { 170, 170, 178 }, { 20, 160, 196 }, { 220, 255, 255 } },  // crystal
  { { 16, 16, 22 },   { 112, 104, 120 }, { 16, 16, 22 },   { 112, 104, 120 } },   // bedrock
  { { 34, 96, 36 },   { 128, 200, 72 },  { 224, 44, 96 },  { 255, 230, 60 } } };  // grass with flowers
// light per face: top, side x, side z, bottom; the same in the shade; deep below
static const uint16_t VOX_LIT[8] = { 256, 214, 180, 150, 168, 144, 124, 104 };
static const uint8_t  VOX_FOGRGB[3] = { 186, 214, 240 }, VOX_ZENRGB[3] = { 64, 124, 222 };
static const uint8_t  VOX_SLOT[VOX_SLOTS] = { VB_AIR, VB_DIRT, VB_STONE, VB_SAND, VB_WOOD, VB_LEAF };   // AIR = the pick
static const uint8_t  VOX_FREE[VOX_FSLOTS] = { VB_AIR, VB_GRASS, VB_DIRT, VB_STONE, VB_SAND, VB_WOOD, VB_LEAF,
                                               VB_COAL, VB_GOLD, VB_GEM };
static const char *const VOX_NAME[13] = { "pick: dig", "grass", "dirt", "stone", "sand", "wood", "leaves", "coal",
                                          "gold", "crystal", "bedrock", "water", "flowers" };
static const char *const VOX_ORE[3] = { "coal", "gold", "crystal" };
static const uint8_t  VOX_PTS[3] = { 1, 5, 20 };
static const int16_t  VOX_PITCH[4] = { 72, 0, -80, -160 };          // look up, level, down, steep down
#define VOX_EDGE  rgb(255, 255, 255)                                 // frame of the block aimed at

struct VoxState {
  uint8_t  world[VOX_N];                      // block per cell, index (y * 32 + z) * 32 + x
  uint8_t  top[VOX_W * VOX_W];                // per column 1 + the highest block that is not air or water
  uint8_t  tex[VOX_NT][256];                  // tone 0..15, row by row (v * 16 + u)
  col      shade[VOX_NT][8][8][16];           // tone -> colour per light and fog step
  col      tint[256];                         // any colour seen through water
  uint32_t cloud[32];                         // one bit per 4 x 4 blocks, 26 blocks up
  int32_t  px, py, pz, vy;                    // the feet, 1/256 block
  int16_t  yaw, pitch;                        // 1024 per turn
  uint8_t  look, sel;                         // pitch step, slot in the hand
  bool     ground;
  uint16_t bag[VOX_SLOTS];                    // blocks per slot (0 = the pick)
  uint16_t score, dug, placed, ores[3];
  bool     free;                              // free play
  bool     dirty;                             // changed since kept
  uint32_t seed;
  uint16_t edN;                               // free play: the blocks changed, kept with the seed
  uint16_t edIdx[VOX_EDITS];
  uint8_t  edBlk[VOX_EDITS];
};
static VoxState *vox = NULL;
static uint32_t voxSteps = 0, voxRays = 0, voxFrames = 0, voxStepsMax = 0;   // the speed check (steps: simulator only)

static inline int voxIdx(int x, int y, int z) { return (y << 10) | ((z & 31) << 5) | (x & 31); }
static uint8_t voxGet(int x, int y, int z) {
  if (y < 0) return VB_ROCK;
  if (y >= VOX_H) return VB_AIR;
  return vox->world[voxIdx(x, y, z)];
}
static void voxColumn(int x, int z) {          // the height for the light
  x &= 31; z &= 31;
  int y = VOX_H;
  while (y > 0 && (vox->world[voxIdx(x, y - 1, z)] == VB_AIR || vox->world[voxIdx(x, y - 1, z)] == VB_WATER)) y--;
  vox->top[z * VOX_W + x] = (uint8_t)y;
}
static void voxSet(int x, int y, int z, uint8_t b) {
  if (y < 0 || y >= VOX_H) return;
  vox->world[voxIdx(x, y, z)] = b;
  voxColumn(x, z);
}
// a block changed by the player; free play remembers it for the flash
static bool voxEdit(int x, int y, int z, uint8_t b) {
  VoxState *s = vox;
  if (s->free) {
    uint16_t i = (uint16_t)voxIdx(x, y, z), k = 0;
    while (k < s->edN && s->edIdx[k] != i) k++;
    if (k == s->edN) {
      if (s->edN >= VOX_EDITS) return false;                    // full: nothing more changes
      s->edIdx[s->edN++] = i;
    }
    s->edBlk[k] = b;
    s->dirty = true;
  }
  voxSet(x, y, z, b);
  return true;
}
static uint8_t voxSlots() { return vox->free ? VOX_FSLOTS : VOX_SLOTS; }
static uint8_t voxSlotBlk(uint8_t k) { return vox->free ? VOX_FREE[k] : VOX_SLOT[k]; }
static bool voxHave(uint8_t k) { return vox->free || vox->bag[k]; }

static bool voxSolid(int x, int y, int z) { uint8_t b = voxGet(x, y, z); return b && b != VB_WATER; }

// ---------------- textures and colours ----------------
static uint8_t voxHash(uint32_t a) {
  a ^= a >> 15; a *= 0x2c1b3c6dU; a ^= a >> 12; a *= 0x297a2d39U; a ^= a >> 15;
  return (uint8_t)a;
}

// tone 0..15 of texel x, y (y = 0 at the top of a side) of texture t; leaves: 0 = a hole
static uint8_t voxTone(uint8_t t, int x, int y) {
  uint8_t n = voxHash((uint32_t)(t * 4099 + y * 16 + x)), m = voxHash((uint32_t)(t * 577 + (y >> 1) * 8 + (x >> 1) + 1000));
  int v = 2 + (n & 3) + (m & 3) + (n > 230);                     // grain 2..9
  switch (t) {
  case VT_GSIDE: {
    int edge = 3 + voxHash((uint32_t)(x * 13 + 7)) % 4;          // the grass hangs over the edge
    if (y < edge) return (uint8_t)(10 + (n & 3) + (y < edge - 1 ? 2 : 0));
    return (uint8_t)v; }
  case VT_DIRT: return (uint8_t)(n > 246 ? 13 : v);              // a pebble here and there
  case VT_SAND: return (uint8_t)(4 + (n & 3) + (m & 1) * 2);
  case VT_BARK: {                                                // furrows down the trunk
    int s = voxHash((uint32_t)(x * 5 + 77)) & 3;
    if ((x + (y / 5 + s) % 2) % 4 == 0) return (uint8_t)(1 + (n & 1));
    return (uint8_t)(4 + s + (n & 1) * 2); }
  case VT_RINGS: {                                               // a sawn log: bark round square rings
    if (x == 0 || y == 0 || x == 15 || y == 15) return (uint8_t)(10 + (n & 3));
    int d = abs(2 * x - 15) > abs(2 * y - 15) ? abs(2 * x - 15) : abs(2 * y - 15);
    return (uint8_t)(((d / 4) & 1 ? 3 : 7) + (n & 1) * 2); }
  case VT_LEAF: return (uint8_t)(n < 56 ? 0 : 2 + (n & 3) + (m & 3));
  case VT_ROCK: return (uint8_t)((n & 7) + (m & 1) * 2);
  case VT_COAL: case VT_GOLD: case VT_GEM:
    for (int k = 0; k < 4; k++) {                                // lumps of ore in the stone
      int cx = 2 + voxHash((uint32_t)(t * 31 + k * 7)) % 12, cy = 2 + voxHash((uint32_t)(t * 53 + k * 11 + 3)) % 12;
      int d2 = (x - cx) * (x - cx) + (y - cy) * (y - cy);
      if (d2 <= 2) return (uint8_t)(d2 == 0 && t != VT_COAL ? 15 : 10 + (n & 3));
    }
    return (uint8_t)(m < 30 ? 1 : (n > 240 ? 9 : v));
  case VT_STONE: return (uint8_t)(m < 30 ? 1 : (n > 240 ? 9 : v));
  case VT_FLOWER: {
    static const int8_t F[3][2] = { { 3, 4 }, { 11, 3 }, { 7, 11 } };
    for (int k = 0; k < 3; k++) {
      int d = abs(x - F[k][0]) + abs(y - F[k][1]);
      if (d == 0) return 15;                                     // the middle
      if (d == 1) return (uint8_t)(10 + (n & 1));                // petals
    }
    return (uint8_t)v; }
  default: return (uint8_t)v;                                    // grass
  }
}

// the nearest colour of the cube round rgb(); a grey only by a grey, the extra greys too
// (the cube alone turns dark greys green)
static int voxSat(int r, int g, int b) { return (r > g ? (r > b ? r : b) : (g > b ? g : b)) - (r < g ? (r < b ? r : b) : (g < b ? g : b)); }
static col voxNear(const uint8_t (*pal)[3], int r, int g, int b) {   // pal: the RGB of every colour
  int r0 = (r * 5 + 127) / 255, g0 = (g * 6 + 127) / 255, b0 = (b * 5 + 127) / 255;
  bool grey = voxSat(r, g, b) < 16;
  col best = rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
  int32_t bd = 0x7FFFFFFF;
  for (int k = 0; k < 31; k++) {
    int R = r0 + k / 9 % 3 - 1, G = g0 + k / 3 % 3 - 1, B = b0 + k % 3 - 1;
    col c = k < 27 ? (col)(R * 42 + G * 6 + B) : (col)(GREY1 + k - 27);
    if (k < 27 && (R < 0 || R > 5 || G < 0 || G > 6 || B < 0 || B > 5)) continue;
    int cr = pal[c][0], cg = pal[c][1], cb = pal[c][2];
    if (k >= 27 ? !grey : grey && voxSat(cr, cg, cb) > 20) continue;
    int32_t hr = (cr - cg) - (r - g), hb = (cg - cb) - (g - b);  // the hue counts too: dark dirt stays brown
    int32_t d = 2 * (cr - r) * (cr - r) + 4 * (cg - g) * (cg - g) + 3 * (cb - b) * (cb - b) + 2 * (hr * hr + hb * hb);
    if (d < bd) { bd = d; best = c; }
  }
  return best;
}

static void voxInit() {
  VoxState *s = vox;
  uint8_t pal[256][3];
  for (int c = 0; c < 256; c++) colToRgb((col)c, pal[c][0], pal[c][1], pal[c][2]);
  for (uint8_t t = 0; t < VOX_NT; t++) {
    for (int y = 0; y < 16; y++)
      for (int x = 0; x < 16; x++) s->tex[t][y * 16 + x] = voxTone(t, x, y);
    for (uint8_t k = 0; k < 16; k++) {
      const uint8_t *a = VOX_RAMP[t][k < 10 ? 0 : 2], *b = VOX_RAMP[t][k < 10 ? 1 : 3];
      int f = k < 10 ? k * 256 / 9 : (k - 10) * 256 / 5;
      int base[3];
      for (int c = 0; c < 3; c++) base[c] = a[c] + (((int)b[c] - a[c]) * f >> 8);
      for (uint8_t l = 0; l < 8; l++)
        for (uint8_t g = 0; g < 8; g++) {
          int o[3];
          int lf = k >= 10 && t >= VT_GOLD && t <= VT_GEM && VOX_LIT[l] < 230 ? 230 : VOX_LIT[l];   // ore glints in the dark
          for (int c = 0; c < 3; c++) {
            int lit = base[c] * lf >> 8;
            o[c] = lit + ((VOX_FOGRGB[c] - lit) * g * 30 >> 8);    // fog: an eighth more per step
            if (o[c] > 255) o[c] = 255;
          }
          s->shade[t][l][g][k] = voxNear(pal, o[0], o[1], o[2]);
        }
    }
  }
  for (int c = 0; c < 256; c++)
    s->tint[c] = voxNear(pal, (pal[c][0] + 24) >> 1, (pal[c][1] + 84) >> 1, (pal[c][2] + 180) >> 1);
}

// ---------------- the world ----------------
static uint32_t voxRnd;                         // its own random numbers: one seed, one world
static uint32_t voxR(uint32_t n) { voxRnd = voxRnd * 1664525u + 1013904223u; return (voxRnd >> 8) % n; }

// periodic value noise: a grid of g x g heights over the 32 blocks, smooth between them
static int voxNoise(const uint8_t *grid, int g, int x, int z) {
  int cell = VOX_W / g, gx = x / cell, gz = z / cell;
  int fx = (x % cell) * 256 / cell, fz = (z % cell) * 256 / cell;
  fx = fx * fx * (768 - 2 * fx) >> 16;
  fz = fz * fz * (768 - 2 * fz) >> 16;
  int x1 = (gx + 1) % g, z1 = (gz + 1) % g;
  int a = grid[gz * g + gx], b = grid[gz * g + x1], c = grid[z1 * g + gx], d = grid[z1 * g + x1];
  int top = a + ((b - a) * fx >> 8), bot = c + ((d - c) * fx >> 8);
  return top + ((bot - top) * fz >> 8);
}

static void voxGenerate(uint32_t seed) {
  VoxState *s = vox;
  voxRnd = seed;
  uint8_t g4[16], g8[64];
  for (uint8_t &v : g4) v = (uint8_t)voxR(256);
  for (uint8_t &v : g8) v = (uint8_t)voxR(256);
  memset(s->world, 0, sizeof(s->world));
  uint8_t height[VOX_W * VOX_W];
  for (int z = 0; z < VOX_W; z++)                                // hills: stone, dirt, grass; sand by the water
    for (int x = 0; x < VOX_W; x++) {
      int h = 2 + voxNoise(g4, 4, x, z) * 8 / 255 + voxNoise(g8, 8, x, z) * 3 / 255;
      if (h > 11) h = 11;
      height[z * VOX_W + x] = (uint8_t)h;
      bool beach = h <= VOX_SEA + 1;
      for (int y = 0; y < h; y++) {
        uint8_t b = VB_STONE;
        if (y == 0 || (y == 1 && voxR(2))) b = VB_ROCK;
        else if (y >= h - 3) b = beach ? (y >= h - 2 ? VB_SAND : VB_DIRT) : (y == h - 1 ? VB_GRASS : VB_DIRT);
        s->world[voxIdx(x, y, z)] = b;
      }
      for (int y = h; y < VOX_SEA; y++) s->world[voxIdx(x, y, z)] = VB_WATER;
    }
  static const uint8_t ORE[3][4] = { { VB_COAL, 70, 9, 6 }, { VB_GOLD, 16, 5, 4 }, { VB_GEM, 10, 3, 3 } };   // block, veins, top, size
  for (const auto &o : ORE)
    for (int v = 0; v < o[1]; v++) {
      int x = (int)voxR(32), z = (int)voxR(32), y = 1 + (int)voxR(o[2]), n = 1 + (int)voxR(o[3]);
      for (int k = 0; k < n; k++) {
        if (s->world[voxIdx(x, y, z)] == VB_STONE) s->world[voxIdx(x, y, z)] = o[0];
        int a = (int)voxR(6), d = a & 1 ? 1 : -1;
        if (a < 2) x += d; else if (a < 4) z += d; else y += d;
        y = constrain(y, 1, (int)o[2]);
      }
    }
  for (int c = 0; c < 3; c++) {                                  // caves: worms through the stone
    int32_t x = (int32_t)voxR(32) << 8, y = (int32_t)(3 + voxR(3)) << 8, z = (int32_t)voxR(32) << 8;
    int16_t a = (int16_t)voxR(1024);
    for (int k = 0; k < 40; k++) {
      x += DOOM_C(a) >> 7; z += DOOM_S(a) >> 7;
      a = (int16_t)(a + (int)voxR(121) - 60);
      y = constrain(y + ((int)voxR(3) - 1) * 48, 2 << 8, 7 << 8);
      for (int dy = -1; dy <= 1; dy++)
        for (int dz = -1; dz <= 1; dz++)
          for (int dx = -1; dx <= 1; dx++) {
            if (dx * dx + dy * dy + dz * dz > 2) continue;
            int cx = (x >> 8) + dx, cy = (y >> 8) + dy, cz = (z >> 8) + dz;
            if (cy < 1 || cy >= height[(cz & 31) * VOX_W + (cx & 31)] - 1) continue;
            uint8_t &b = s->world[voxIdx(cx, cy, cz)];
            if (b != VB_ROCK && b != VB_WATER) b = VB_AIR;
          }
    }
  }
  for (int k = 0, n = 0; k < 80 && n < 9; k++) {                 // trees on the grass, apart
    int x = (int)voxR(32), z = (int)voxR(32), h = height[z * VOX_W + x];
    if (h > 10 || s->world[voxIdx(x, h - 1, z)] != VB_GRASS) continue;
    bool near = false;
    for (int dz = -3; dz <= 3 && !near; dz++)
      for (int dx = -3; dx <= 3 && !near; dx++)
        for (int y = h; y < h + 4 && !near; y++) near = s->world[voxIdx(x + dx, y, z + dz)] == VB_WOOD;
    if (near) continue;
    for (int y = h; y < h + 4; y++) s->world[voxIdx(x, y, z)] = VB_WOOD;
    for (int y = h + 2; y <= h + 5; y++) {
      int r = y < h + 4 ? 2 : 1;
      for (int dz = -r; dz <= r; dz++)
        for (int dx = -r; dx <= r; dx++) {
          if (abs(dx) == r && abs(dz) == r && (r == 1 ? y == h + 5 : voxR(2))) continue;   // round corners
          uint8_t &b = s->world[voxIdx(x + dx, y, z + dz)];
          if (b == VB_AIR) b = VB_LEAF;
        }
    }
    n++;
  }
  for (int i = 0; i < VOX_W * VOX_W; i++) {                      // flowers
    int x = i & 31, z = i >> 5, h = height[i];
    uint8_t &b = s->world[voxIdx(x, h - 1, z)];
    if (b == VB_GRASS && voxR(9) == 0) b = VB_BLOOM;
  }
  for (int z = 0; z < VOX_W; z++)
    for (int x = 0; x < VOX_W; x++) voxColumn(x, z);
  uint32_t cr = voxRnd;
  for (int z = 0; z < 32; z++) {                                 // clouds: clumps of bits
    s->cloud[z] = 0;
    for (int x = 0; x < 32; x++)
      if (voxHash((uint32_t)((x >> 1) * 7 + (z >> 1) * 131) + cr) < 80 && voxHash((uint32_t)(x * 3 + z * 97) + cr) < 200)
        s->cloud[z] |= 1u << x;
  }
}

// the player's box with its feet at x, y, z (1/256 block) touches a solid block
static bool voxBoxHit(int32_t x, int32_t y, int32_t z) {
  for (int32_t by = y >> 8; by <= (y + VOX_PH - 1) >> 8; by++)
    for (int32_t bz = (z - VOX_HW) >> 8; bz <= (z + VOX_HW) >> 8; bz++)
      for (int32_t bx = (x - VOX_HW) >> 8; bx <= (x + VOX_HW) >> 8; bx++)
        if (voxSolid(bx, by, bz)) return true;
  return false;
}

// a new round: the player on dry land near the middle, the bag empty
static void voxStart() {
  VoxState *s = vox;
  for (int r = 0; r < 16; r++)
    for (int dz = -r; dz <= r; dz++)
      for (int dx = -r; dx <= r; dx++) {
        if (abs(dx) != r && abs(dz) != r) continue;              // the ring at distance r
        int x = (16 + dx) & 31, z = (16 + dz) & 31, h = s->top[z * VOX_W + x];
        uint8_t b = voxGet(x, h - 1, z);
        if (b != VB_GRASS && b != VB_BLOOM && b != VB_SAND) continue;
        s->px = x * 256 + 128; s->pz = z * 256 + 128; s->py = h * 256;
        if (voxBoxHit(s->px, s->py, s->pz)) continue;
        goto found;
      }
  s->px = s->pz = 16 * 256 + 128; s->py = 14 * 256;              // no place found: drop in
found:
  s->vy = 0; s->yaw = 0; s->pitch = 0; s->look = 1; s->sel = 0; s->ground = false;
  memset(s->bag, 0, sizeof(s->bag));
  memset(s->ores, 0, sizeof(s->ores));
  s->score = s->dug = s->placed = 0;
}

// ---------------- rays ----------------
struct VoxHit { int16_t x, y, z, qx, qy, qz; };   // the block hit, the cell in front of the face hit

/* A ray of the picture through the air: from cell to cell (x, y or z,
   whichever boundary comes first) until a cell is not empty. Returns the
   block (0 = none within VOX_VIEW or out at the top) and keeps where the
   ray is in r. On its own, so that its values stay in the registers.  */
struct VoxRay { int32_t tmx, tmy, tmz, tdx, tdy, tdz, sx, sy, sz, idx, t; uint8_t face; };
__attribute__((noinline)) static uint8_t voxMarch(const uint8_t *w, VoxRay *r) {
  int32_t tmx = r->tmx, tmy = r->tmy, tmz = r->tmz, tdx = r->tdx, tdy = r->tdy, tdz = r->tdz;
  int32_t sx = r->sx, sy = r->sy, sz = r->sz, idx = r->idx, t = r->t;
  uint8_t face = r->face, b = 0;
  for (;;) {
    if (tmx < tmy && tmx < tmz) { t = tmx; tmx += tdx; idx = (idx & ~31) | ((idx + sx) & 31); face = 0; }
    else if (tmy < tmz)         { t = tmy; tmy += tdy; idx += sy; face = 1; if ((uint32_t)idx >= VOX_N) break; }
    else                        { t = tmz; tmz += tdz; idx = (idx & ~0x3E0) | ((idx + sz) & 0x3E0); face = 2; }
    if (t > VOX_MAXT) break;
#ifdef ARCADE_TRACE
    voxSteps++;                                                  // the speed check (simulator)
#endif
    if ((b = w[idx]) != 0) break;
  }
  r->tmx = tmx; r->tmy = tmy; r->tmz = tmz; r->idx = idx; r->t = t; r->face = face;
  return b;
}

// the first block (not water) along a ray within maxT; directions Q12, lengths Q12
static bool voxCast(int32_t ex, int32_t ey, int32_t ez, int32_t dx, int32_t dy, int32_t dz, int32_t maxT, VoxHit *h) {
  int32_t d[3] = { dx, dy, dz }, e[3] = { ex, ey, ez }, tm[3], td[3];
  for (int k = 0; k < 3; k++) {
    int32_t ad = d[k] < 0 ? -d[k] : d[k], f = e[k] & 255;
    td[k] = ad > 8 ? (1 << 24) / ad : (1 << 21);
    tm[k] = ((d[k] < 0 ? f : 256 - f) * td[k]) >> 8;
  }
  VoxRay r = { tm[0], tm[1], tm[2], td[0], td[1], td[2], dx < 0 ? -1 : 1, dy < 0 ? -1024 : 1024, dz < 0 ? -32 : 32,
               voxIdx(ex >> 8, ey >> 8, ez >> 8), 0, 0 };
  uint8_t b;
  while ((b = voxMarch(vox->world, &r)) == VB_WATER) {}
  if (!b || r.t > maxT) return false;
  int32_t q = r.face == 0 ? (r.idx & ~31) | ((r.idx - r.sx) & 31) : (r.face == 1 ? r.idx - r.sy : (r.idx & ~0x3E0) | ((r.idx - r.sz) & 0x3E0));
  h->x = (int16_t)(r.idx & 31); h->z = (int16_t)((r.idx >> 5) & 31); h->y = (int16_t)(r.idx >> 10);
  h->qx = (int16_t)(q & 31); h->qz = (int16_t)((q >> 5) & 31); h->qy = (int16_t)(q >> 10);
  return true;
}

static int32_t voxEyeY() { int32_t y = vox->py + VOX_EYE; return y < VOX_H * 256 ? y : VOX_H * 256 - 1; }

// the block in the middle of the view, within reach
static bool voxTarget(VoxHit *h) {
  VoxState *s = vox;
  int32_t cp = DOOM_C(s->pitch);
  return voxCast(s->px, voxEyeY(), s->pz, (cp * DOOM_C(s->yaw)) >> 16, DOOM_S(s->pitch) >> 2, (cp * DOOM_S(s->yaw)) >> 16, VOX_REACH, h);
}

/* The picture: a ray per pixel, stepped from cell to cell until it meets
   a block (water and holes in leaves let it through), leaves the world
   at the top or goes further than VOX_VIEW. tgt = the block aimed at.  */
__attribute__((noinline)) static void voxRender(int32_t ex, int32_t ey, int32_t ez, int16_t yaw, int16_t pitch, int32_t tgt, uint32_t now) {
  VoxState *s = vox;
  const uint8_t *w = s->world;
  uint8_t *fb = gfx.buffer();
  int32_t cw = DOOM_C(yaw), sw = DOOM_S(yaw), cp = DOOM_C(pitch), sp = DOOM_S(pitch);   // Q14
  int32_t fX = (cp * cw) >> 12, fY = sp << 2, fZ = (cp * sw) >> 12;                      // ahead, Q16
  int32_t rX = -sw << 2, rZ = cw << 2;                                                   // right
  int32_t uX = (-sp * cw) >> 12, uY = cp << 2, uZ = (-sp * sw) >> 12;                    // up
  int32_t stX = rX * (2 * VOX_TAN) / (VOX_RW << 12), stZ = rZ * (2 * VOX_TAN) / (VOX_RW << 12);   // per column
  const int32_t u0 = -(VOX_RW - 1) * VOX_TAN / VOX_RW;                                   // left column, Q12
  int32_t fx = ex & 255, fz = ez & 255;
  const int32_t idx0 = voxIdx(ex >> 8, ey >> 8, ez >> 8);
  uint8_t wet0 = w[idx0] == VB_WATER;
  int32_t drift = (int32_t)(now / 24);                           // the clouds move
  col sky0 = rgb(VOX_FOGRGB[0], VOX_FOGRGB[1], VOX_FOGRGB[2]), sky1 = rgb(VOX_ZENRGB[0], VOX_ZENRGB[1], VOX_ZENRGB[2]);
  uint32_t steps0 = voxSteps;
  for (int16_t j = 0; j < VOX_RH; j++) {
    int32_t v = (int32_t)(VOX_RH - 1 - 2 * j) * VOX_TAN / VOX_RW;   // Q12
    int32_t aX = fX + ((uX * v) >> 12) + ((rX * u0) >> 12), aZ = fZ + ((uZ * v) >> 12) + ((rZ * u0) >> 12);
    int32_t dy = (fY + ((uY * v) >> 12)) >> 4;                   // the same along the row
    int32_t ady = dy < 0 ? -dy : dy;
    int32_t tdy = ady > 8 ? (1 << 24) / ady : (1 << 21);
    int32_t tmy0 = ((dy < 0 ? (ey & 255) : 256 - (ey & 255)) * tdy) >> 8;
    int32_t sy = dy < 0 ? -1024 : 1024;                          // a step up or down in the world
    int32_t e = dy <= 0 ? 0 : (dy >= 2400 ? 255 : dy * 255 / 2400);
    col skyA = colLerp(sky0, sky1, (uint8_t)e), skyB = colLerp(sky0, sky1, (uint8_t)(e + 28 > 255 ? 255 : e + 28));
    int32_t tc = dy > 64 ? ((26 * 256 - ey) * 65536) / dy : 0x7FFFFFFF;   // to the clouds, Q12
    bool clouds = tc < (80 << 12);
    col cloudC = clouds ? colLerp(C::WHITE, skyA, (uint8_t)((tc >> 12) * 3 > 255 ? 255 : (tc >> 12) * 3)) : 0;
    int32_t tc4 = tc >> 4;
    uint8_t *o = fb + (VOX_VY + 2 * j) * SCR_W;
    for (int16_t i = 0; i < VOX_RW; i++, aX += stX, aZ += stZ) {
      int32_t dx = aX >> 4, dz = aZ >> 4;
      int32_t adx = dx < 0 ? -dx : dx, adz = dz < 0 ? -dz : dz;
      int32_t tdx = adx > 8 ? (1 << 24) / adx : (1 << 21), tdz = adz > 8 ? (1 << 24) / adz : (1 << 21);
      VoxRay r = { ((dx < 0 ? fx : 256 - fx) * tdx) >> 8, tmy0, ((dz < 0 ? fz : 256 - fz) * tdz) >> 8, tdx, tdy, tdz,
                   dx < 0 ? -1 : 1, sy, dz < 0 ? -32 : 32, idx0, 0, 0 };
      uint8_t wet = wet0, b;
      col c = 0;
      for (;;) {
        if (!(b = voxMarch(w, &r))) break;
        if (b == VB_WATER) { wet = 1; continue; }
        // where on the face: two coordinates of the hit point (1/256 block), the cell in front
        int32_t pa, pb, q, t = r.t, idx = r.idx;
        uint8_t face = r.face;
        uint8_t kind;
        if (face == 0)      { pa = ez + ((dz * t) >> 16); pb = ey + ((dy * t) >> 16); kind = 1; q = (idx & ~31) | ((idx - r.sx) & 31); }
        else if (face == 1) { pa = ex + ((dx * t) >> 16); pb = ez + ((dz * t) >> 16); kind = sy < 0 ? 0 : 3; q = idx - sy; }
        else                { pa = ex + ((dx * t) >> 16); pb = ey + ((dy * t) >> 16); kind = 2; q = (idx & ~0x3E0) | ((idx - r.sz) & 0x3E0); }
        uint8_t tx = VOX_FTEX[b][kind == 0 ? 0 : (kind == 3 ? 2 : 1)];
        uint8_t tu = (uint8_t)((pa >> 4) & 15), tv = (uint8_t)((pb >> 4) & 15);
        if (face != 1) tv = 15 - tv;
        uint8_t tone = s->tex[tx][tv * 16 + tu];
        if (!tone && b == VB_LEAF) continue;                     // through a hole in the leaves
        int32_t h = s->top[q & 1023], qy = q >> 10;              // under a roof: shade, deep down: dark
        uint8_t l = kind;
        if (qy < h) l = qy + 3 < h ? 7 : (kind == 3 ? 3 : kind + 4);
        int32_t f = (t - (VOX_FOG0 << 12) + (((i + j) & 1) * 1536)) * 21 >> 16;   // / (6 blocks / 8)
        c = s->shade[tx][l][f <= 0 ? 0 : (f > 7 ? 7 : f)][tone];
        if (idx == tgt) {
          int32_t ew = 4 + 3 * (t >> 12);
          if (((pa + ew) & 255) < 2 * ew || ((pb + ew) & 255) < 2 * ew) c = VOX_EDGE;
        }
        break;
      }
      if (!b) {
        c = ((i ^ j) & 1) ? skyA : skyB;
        if (clouds) {
          int32_t px = ex + ((dx * tc4) >> 12) + drift, pz = ez + ((dz * tc4) >> 12);
          if ((s->cloud[(pz >> 10) & 31] >> ((px >> 10) & 31)) & 1) c = cloudC;
        }
      }
      if (wet) c = s->tint[c];
      o[2 * i] = o[2 * i + 1] = c;
    }
    memcpy(o + SCR_W, o, SCR_W);
  }
  voxRays += VOX_RW * VOX_RH;
  voxFrames++;
  if (voxSteps - steps0 > voxStepsMax) voxStepsMax = voxSteps - steps0;
}

// ---------------- the player ----------------
// one game step: walk (1 ahead, -1 back), gravity, water
static void voxTick(int8_t walk) {
  VoxState *s = vox;
  bool wet = voxGet(s->px >> 8, (s->py + 200) >> 8, s->pz >> 8) == VB_WATER;
  bool blocked = false;
  if (walk) {
    int32_t sp = walk > 0 ? 23 : 15;                             // 3.7 / 2.4 blocks per second
    int32_t mx = walk * ((DOOM_C(s->yaw) * sp) >> 14), mz = walk * ((DOOM_S(s->yaw) * sp) >> 14);
    for (int32_t k = mx; k; k /= 2)                              // up to the wall, sliding along it
      if (!voxBoxHit(s->px + k, s->py, s->pz)) { s->px = (s->px + k) & 8191; break; } else blocked = true;
    for (int32_t k = mz; k; k /= 2)
      if (!voxBoxHit(s->px, s->py, s->pz + k)) { s->pz = (s->pz + k) & 8191; break; } else blocked = true;
    if (blocked && (s->ground || wet) && !voxBoxHit(s->px + mx * 2, s->py + 264, s->pz + mz * 2)) s->vy = VOX_JUMP;   // a step: jump it
    else if (wet && walk > 0 && !blocked) s->vy = 10;            // swim up
  }
  if (wet) { s->vy -= 1; if (s->vy < -8) s->vy = -8; }
  else     { s->vy -= 3; if (s->vy < -60) s->vy = -60; }
  int32_t ny = s->py + s->vy;
  s->ground = false;
  if (voxBoxHit(s->px, ny, s->pz)) {
    if (s->vy < 0) { s->py = ((ny >> 8) + 1) << 8; s->ground = true; }
    else s->py = (((ny + VOX_PH - 1) >> 8) << 8) - VOX_PH;
    s->vy = 0;
  } else s->py = ny;
  if (s->py < 0) { s->py = 14 << 8; s->vy = 0; }                 // never: bedrock below
}

/* OK: the pick digs the block aimed at, a block from the bag goes on its
   face. Returns 0 nothing in reach, 1 dug (*got = the block), 2 put,
   3 not possible.                                                     */
static uint8_t voxAct(uint8_t *got) {
  VoxState *s = vox;
  VoxHit h;
  if (!voxTarget(&h)) return 0;
  if (s->sel == 0) {
    uint8_t b = voxGet(h.x, h.y, h.z);
    if (b == VB_ROCK || !voxEdit(h.x, h.y, h.z, VB_AIR)) return 3;
    *got = b;
    s->dug++;
    if (s->free) return 1;
    if (b >= VB_COAL && b <= VB_GEM) { s->ores[b - VB_COAL]++; s->score += VOX_PTS[b - VB_COAL]; return 1; }
    if (b == VB_GRASS || b == VB_BLOOM) b = VB_DIRT;
    for (uint8_t k = 1; k < VOX_SLOTS; k++) if (VOX_SLOT[k] == b && s->bag[k] < 999) s->bag[k]++;
    return 1;
  }
  if (!voxHave(s->sel) || h.qy < 1 || h.qy > VOX_H - 2) return 3;
  if (voxSolid(h.qx, h.qy, h.qz)) return 3;
  if (s->free && s->edN >= VOX_EDITS) return 3;
  // inside the player? Only under the feet: then the player goes up a block
  int32_t ox = ((h.qx * 256 + 128 - s->px + 4096) & 8191) - 4096, oz = ((h.qz * 256 + 128 - s->pz + 4096) & 8191) - 4096;
  bool inside = abs(ox) < 128 + VOX_HW && abs(oz) < 128 + VOX_HW && h.qy * 256 < s->py + VOX_PH && h.qy * 256 + 256 > s->py;
  if (inside) {
    int32_t ny = (h.qy + 1) << 8;
    if (h.qy != (s->py >> 8) || voxBoxHit(s->px, ny, s->pz)) return 3;
    s->py = ny; s->vy = 0;
  }
  if (!voxEdit(h.qx, h.qy, h.qz, voxSlotBlk(s->sel))) return 3;
  if (!s->free) s->bag[s->sel]--;
  s->placed++;
  return 2;
}

// ---------------- drawing round the view ----------------
// an iso cube of block b, middle (cx, cy), w = half its width
static void voxCube(int16_t cx, int16_t cy, int16_t w, uint8_t b) {
  VoxState *s = vox;
  const uint8_t *ft = VOX_FTEX[b];
  for (int16_t y = cy - w; y < cy + w; y++)
    for (int16_t x = cx - w; x < cx + w; x++) {
      int32_t X = 2 * (x - cx) + 1, Y = 2 * (y - cy + w) + 1;   // half pixels
      int32_t su = (X + 2 * Y) * 8 / (2 * w), sv = (2 * Y - X) * 8 / (2 * w);
      uint8_t tx, l, u, v;
      if (X + 2 * Y >= 0 && 2 * Y - X >= 0 && su < 16 && sv < 16) { tx = ft[0]; l = 0; u = (uint8_t)su; v = (uint8_t)sv; }
      else {
        bool lf = X < 0;
        int32_t a = lf ? X + 2 * w : X;                          // across the face, 0..2w
        int32_t hh = 2 * (y - cy) + (lf ? 2 * w - a : a) / 2;    // down the face, 0..2w
        if (a < 0 || a >= 2 * w || hh < 0 || hh >= 2 * w) continue;
        tx = ft[1]; l = lf ? 1 : 2; u = (uint8_t)(a * 8 / w); v = (uint8_t)(hh * 8 / w);
      }
      uint8_t tone = s->tex[tx][v * 16 + u];
      if (!tone && b == VB_LEAF) tone = 2;
      gfx.pixel(x, y, s->shade[tx][l][0][tone]);
    }
}

// the pick, 14 x 14
static const char *const VOX_PICK[14] = {
  "......sss.....", ".....swwss....", ".......sswss..", ".........nsws.", "........nn.sws", ".......nn...sw",
  "......nn....ss", ".....nn......s", "....nn........", "...nn.........", "..Nn..........", ".NN...........",
  "NN............", "N............." };
static const col VOX_PICK_PAL[] = { GREY3, C::WHITE, rgb(150, 100, 50), rgb(90, 56, 26) };

static void voxBag(bool choosing, uint32_t now, int32_t left) {
  VoxState *s = vox;
  gfx.gradient(0, VOX_BAR, SCR_W, SCR_H - VOX_BAR, rgb(60, 60, 70), rgb(20, 20, 26));
  gfx.hline(0, VOX_BAR, SCR_W, GREY3);
  for (uint8_t k = 0; k < voxSlots(); k++) {
    int16_t x = 3 + k * 31 + (s->free ? 3 : 0), y = VOX_BAR + 3;
    bool on = k == s->sel;
    gfx.fill(x, y, 29, 25, on ? rgb(90, 90, 110) : rgb(36, 36, 44));
    if (k == 0) gfx.sprite(x + 7, y + 5, VOX_PICK, 14, "swnN", VOX_PICK_PAL, 1);
    else if (s->free) voxCube(x + 14, y + 12, 9, VOX_FREE[k]);   // without end: no number
    else {
      voxCube(x + 14, y + 11, 8, VOX_SLOT[k]);
      if (!s->bag[k]) gfx.dim(x, y, 29, 25);
      char b[6];
      snprintf(b, sizeof(b), "%u", s->bag[k]);
      gfx.right(x + 28, y + 16, b, s->bag[k] ? C::WHITE : C::GREY, F_SMALL);
    }
    if (on) gfx.frame(x - 1, y - 1, 31, 27, choosing && (now / 200) % 2 ? C::WHITE : C::YELLOW, 2);
  }
  if (s->free) return;
  // the ore found, the time left
  static const col ORE_C[3] = { rgb(30, 30, 36), rgb(255, 200, 40), rgb(80, 230, 255) };
  for (uint8_t k = 0; k < 3; k++) {
    int16_t x = 192 + k * 26, y = VOX_BAR + 6;
    gfx.fill(x, y + 2, 7, 7, ORE_C[k]);
    gfx.frame(x - 1, y + 1, 9, 9, GREY3);
    char b[6];
    snprintf(b, sizeof(b), "%u", s->ores[k]);
    gfx.text(x + 10, y + 1, b, C::WHITE, F_SMALL);
  }
  gfx.text(192, VOX_BAR + 18, "ore", C::GREY, F_SMALL);
  char b[12];
  int32_t sec = left > 0 ? (left + 999) / 1000 : 0;
  snprintf(b, sizeof(b), "%d:%02d", (int)(sec / 60), (int)(sec % 60));
  gfx.right(SCR_W - 6, VOX_BAR + 6, b, sec <= 20 && (now / 300) % 2 ? C::RED : C::WHITE, F_TEXT);
}

struct VoxChip { int16_t x, y, vx, vy; col c; };

/* Free play in flash ("m3w"): the seed, where the player stands and the
   blocks changed - the rest of the world grows again from the seed.   */
struct VoxSaveHead { uint8_t magic, sel, look, pad; uint32_t seed; int32_t px, py, pz; int16_t yaw; uint16_t n; };
#define VOX_SAVE_MAX (sizeof(VoxSaveHead) + VOX_EDITS * 3)
static void voxKeep() {
  VoxState *s = vox;
  uint8_t *buf = (uint8_t *)malloc(VOX_SAVE_MAX);
  if (!buf) return;
  VoxSaveHead h = { 0x3D, s->sel, s->look, 0, s->seed, s->px, s->py, s->pz, s->yaw, s->edN };
  memcpy(buf, &h, sizeof(h));
  for (uint16_t k = 0; k < s->edN; k++) {
    uint8_t *e = buf + sizeof(h) + k * 3;
    e[0] = (uint8_t)s->edIdx[k]; e[1] = (uint8_t)(s->edIdx[k] >> 8); e[2] = s->edBlk[k];
  }
  prefs.begin("arcade", false);
  prefs.putBytes("m3w", buf, sizeof(h) + s->edN * 3);
  prefs.end();
  free(buf);
  s->dirty = false;
}
// true: a kept world is there (only looks when s is NULL)
static bool voxLoad(VoxState *s) {
  uint8_t *buf = (uint8_t *)malloc(VOX_SAVE_MAX);
  if (!buf) return false;
  prefs.begin("arcade", true);
  size_t n = prefs.getBytes("m3w", buf, VOX_SAVE_MAX);
  prefs.end();
  VoxSaveHead h;
  bool ok = n >= sizeof(h);
  if (ok) { memcpy(&h, buf, sizeof(h)); ok = h.magic == 0x3D && h.n <= VOX_EDITS && n == sizeof(h) + h.n * 3u; }
  if (ok && s) {
    voxGenerate(h.seed);
    voxStart();
    s->seed = h.seed;
    for (uint16_t k = 0; k < h.n; k++) {
      const uint8_t *e = buf + sizeof(h) + k * 3;
      uint16_t i = (uint16_t)(e[0] | e[1] << 8);
      if (i >= VOX_N || e[2] >= 13) continue;
      s->edIdx[s->edN] = i; s->edBlk[s->edN++] = e[2];
      s->world[i] = e[2];
      voxColumn(i & 31, (i >> 5) & 31);
    }
    s->px = h.px; s->py = h.py; s->pz = h.pz; s->yaw = h.yaw;
    s->sel = h.sel < VOX_FSLOTS ? h.sel : 0; s->look = h.look < 4 ? h.look : 1;
    s->dirty = false;
  }
  free(buf);
  return ok;
}

void mine3dRun() {
  vox = (VoxState *)malloc(sizeof(VoxState));
  if (!vox) return;
  VoxState *s = vox;
  bool kept = voxLoad(NULL);
  static const char *const MODES[3] = { "continue my world", "new world (free play)", "ore hunt (3 min)" };
  static const char *const MODES_NEW[2] = { "free play: new world", "ore hunt (3 min)" };
  uint8_t m = kept ? chooseMode("MINE 3D", MODES, 3, rgb(40, 170, 110)) : chooseMode("MINE 3D", MODES_NEW, 2, rgb(40, 170, 110));
  if (m == 255) { free(vox); vox = NULL; return; }
  if (!kept) m++;                                                // 0 continue, 1 new world, 2 ore hunt
  voxInit();
  bool again = true;
  while (again) {
    again = false;
    s->free = m < 2;
    s->edN = 0;
    s->dirty = false;
    if (m != 0 || !voxLoad(s)) {
      s->seed = (uint32_t)random(1, 0x7FFFFFFF);
      voxGenerate(s->seed);
      voxStart();
      s->dirty = s->free;                                        // a new world replaces the kept one
    }
    uint32_t t0 = gameMillis(), next = t0, tapAt = 0, chooseAt = 0, swingUntil = 0, msgUntil = 0, chipUntil = 0, last = t0;
    uint32_t armAt[2] = { 0, 0 };
    bool tapWait = false, choosing = false, over = false, arm[2] = { false, false }, walkHeld[2] = { false, false };
    uint16_t steps = 0;
    uint32_t keptAt = t0;
    char msg[24] = "";
    col msgC = C::WHITE;
    VoxChip chip[8];
    btnClear();
    while (poll()) {
      uint32_t now = gameMillis(), dt = now - last;
      last = now;
      int32_t left = s->free ? 1 : VOX_TIME * 1000 - (int32_t)(now - t0);
      if (s->free && s->dirty && now - keptAt > 30000) { voxKeep(); keptAt = now; }   // now and then into the flash
      // UP / DOWN: tapped = look up / down, held = walk
      for (uint8_t k = 0; k < 2; k++) {
        uint8_t key = k ? B_DOWN : B_UP;
        bool e = btn(key);
        if (choosing) { arm[k] = walkHeld[k] = false; continue; }
        if (e && !walkHeld[k] && !arm[k]) { arm[k] = true; armAt[k] = now; }
        if (arm[k]) {
          if (!btnHeld(key)) {
            arm[k] = false;
            uint8_t nl = (uint8_t)constrain((int)s->look + (k ? 1 : -1), 0, 3);
            if (nl != s->look) { s->look = nl; sfx(500 + nl * 150, 12); }
          } else if (now - armAt[k] >= VOX_TAPMS) { arm[k] = false; walkHeld[k] = true; }
        }
        if (walkHeld[k] && !btnHeld(key)) walkHeld[k] = false;
      }
      // OK: once = use, twice = the bag
      bool act = false;
      if (btn(B_OK)) {
        if (choosing) { choosing = false; sfx(1200, 30); }
        else if (tapWait) { tapWait = false; choosing = true; chooseAt = now; sfx(900, 30); }
        else { tapWait = true; tapAt = now; }
      }
      if (tapWait && now - tapAt > VOX_DBL) { tapWait = false; act = !over; }
      bool l = btn(B_LEFT), r = btn(B_RIGHT);
      if (choosing) {
        if (l || r) { s->sel = (uint8_t)((s->sel + (l ? voxSlots() - 1 : 1)) % voxSlots()); chooseAt = now; sfx(700, 10); }
        if (now - chooseAt > 4000) choosing = false;
      }
      if (act) {
        uint8_t got = 0, res = voxAct(&got);
        swingUntil = now + 160;
        if (res == 1) {
          bool ore = got >= VB_COAL && got <= VB_GEM;
          sfx(ore ? 1300 : 260, ore ? 90 : 30);
          col cc = s->shade[VOX_FTEX[got][1]][1][0][ore ? 12 : 6];
          for (uint8_t i = 0; i < 8; i++)
            chip[i] = { (int16_t)(160 * 16), (int16_t)((VOX_VY + VOX_RH) * 16), (int16_t)((int)random(61) - 30), (int16_t)(-(int)random(40) - 10), cc };
          chipUntil = now + 450;
          if (ore) {
            snprintf(msg, sizeof(msg), "+%u %s", VOX_PTS[got - VB_COAL], VOX_ORE[got - VB_COAL]);
            msgC = got == VB_GEM ? C::CYAN : (got == VB_GOLD ? C::YELLOW : C::LTGREY);
            msgUntil = now + 1100;
          }
        } else if (res == 2) sfx(600, 15);
        else if (res == 3) {
          sfx(150, 40);
          if (s->free && s->edN >= VOX_EDITS) { snprintf(msg, sizeof(msg), "world is full"); msgC = C::ORANGE; msgUntil = now + 1100; }
        }
      }
      // the world goes on in fixed steps
      if ((int32_t)(now - next) > 120) next = now - 120;
      while (!over && (int32_t)(now - next) >= 0) {
        next += VOX_TICK;
        if (!choosing) {
          if (btnHeld(B_LEFT))  s->yaw = (int16_t)((s->yaw - 10) & 1023);
          if (btnHeld(B_RIGHT)) s->yaw = (int16_t)((s->yaw + 10) & 1023);
        }
        int8_t walk = walkHeld[0] ? 1 : (walkHeld[1] ? -1 : 0);
        voxTick(walk);
        if (walk && s->ground) steps++;
        int16_t want = VOX_PITCH[s->look];
        s->pitch = (int16_t)(s->pitch + constrain(want - s->pitch, -14, 14));
      }
      if (left <= 0 && !over) { over = true; sfx(1000, 300); msgUntil = 0; }

      // ---- the view ----
      VoxHit h;
      int32_t tgt = voxTarget(&h) ? voxIdx(h.x, h.y, h.z) : -1;
      int32_t bob = walkHeld[0] || walkHeld[1] ? (DOOM_S(steps * 48) * 5) >> 14 : 0;
      int32_t ey = voxEyeY() + bob;
      if (ey >= VOX_H * 256) ey = VOX_H * 256 - 1;
      voxRender(s->px, ey, s->pz, s->yaw, s->pitch, tgt, now);
      // the hand: the pick or the block, swung on OK
      bool swing = (int32_t)(swingUntil - now) > 0;
      int16_t hx = (int16_t)(236 + (swing ? -14 : 0) + ((DOOM_C(steps * 24) * 4) >> 14)), hy = (int16_t)(VOX_BAR - 50 + (swing ? 10 : 0) + abs(bob));
      if (s->sel == 0) gfx.sprite(hx + 12, hy - 6, VOX_PICK, 14, "swnN", VOX_PICK_PAL, 4);
      else if (voxHave(s->sel)) voxCube(hx + 40, hy + 30, 26, voxSlotBlk(s->sel));
      // chips of the block just dug
      if ((int32_t)(chipUntil - now) > 0) {
        for (uint8_t i = 0; i < 8; i++) {
          VoxChip &c = chip[i];
          c.x = (int16_t)(c.x + c.vx * (int)dt / 16); c.y = (int16_t)(c.y + c.vy * (int)dt / 16);
          c.vy = (int16_t)(c.vy + (int)dt / 4);
          gfx.fill(c.x / 16 - 2, c.y / 16 - 2, 5, 5, c.c);
        }
      }
      // cross hairs
      const int16_t mx = SCR_W / 2, my = VOX_VY + VOX_RH;
      gfx.fill(mx - 7, my - 1, 15, 3, C::BLACK); gfx.fill(mx - 1, my - 7, 3, 15, C::BLACK);
      gfx.hline(mx - 6, my, 13, C::WHITE); gfx.vline(mx, my - 6, 13, C::WHITE);
      // look up / down marks at the left edge
      for (uint8_t k = 0; k < 4; k++) gfx.fill(3, VOX_VY + 62 + k * 16, 5, 10, k == s->look ? C::YELLOW : rgb(60, 60, 70));
      if ((int32_t)(msgUntil - now) > 0) {
        int16_t y = (int16_t)(VOX_VY + 60 - (int32_t)(1100 - (msgUntil - now)) / 40);
        gfx.textShadow(mx - Gfx::width(msg, F_BOLD) / 2, y, msg, msgC, C::BLACK, F_BOLD);
      }
      char hint[48];
      if (choosing) {
        gfx.rbox(40, VOX_BAR - 24, 240, 20, 5, C::BLACK);
        snprintf(hint, sizeof(hint), "< %s >   %s = take", VOX_NAME[voxSlotBlk(s->sel)], okKeys());
        gfx.center(VOX_BAR - 19, hint, C::YELLOW, F_SMALL);
      } else if (now - t0 < 7000) {
        gfx.rbox(10, VOX_VY + 4, 300, 30, 5, C::BLACK);
        gfx.center(VOX_VY + 7, "UP/DOWN hold: walk, tap: look up/down", C::LTGREY, F_SMALL);
        snprintf(hint, sizeof(hint), "%s: dig / build, twice: the bag", okKeys());
        gfx.center(VOX_VY + 20, hint, C::LTGREY, F_SMALL);
      }
      if (over) {
        gfx.rbox(80, VOX_VY + 40, 160, 44, 8, C::BLACK);
        gfx.center(VOX_VY + 50, "TIME UP", C::YELLOW, F_BOLD);
      }
      voxBag(choosing, now, left);
      if (s->free) {                                             // no score: the title and how full the world is
        gfx.gradient(0, 0, SCR_W, TOP_H, colLight(rgb(40, 170, 110)), colDark(rgb(40, 170, 110), 2));
        gfx.textShadow(6, 5, "MINE 3D", C::WHITE, C::BLACK, F_TEXT);
        snprintf(hint, sizeof(hint), "free play   %u%% built", (unsigned)(s->edN * 100u / VOX_EDITS));
        gfx.right(SCR_W - 34, 9, hint, C::WHITE, F_SMALL);
        batIcon(SCR_W - 30, 8);
      } else statusBar("MINE 3D", s->score, curHigh, rgb(40, 170, 110));
      gfx.send();
      if (over && left < -1500) break;
    }
    if (s->free) {                                               // left (or restarted): keep the world
      if (s->dirty) voxKeep();
      break;
    }
    if (!over) break;                                            // left from the pause menu
    again = gameOver(s->score);
  }
  free(vox);
  vox = NULL;
}

// the icon in the library (12 x 12, letters as ICON_KEYS in console.cpp)
static const char *const IC_MINE3D[12] = {
  ".....gg.....", "...gggggg...", ".gggggggggg.", "dggggggggggs", "dddggggggsss", "dddddggsssss",
  "nnnnddssssss", "nnnnnnssyyss", "nnnnnnssyyss", "nnnnnnsssscs", "..nnnnsscc..", "....nnss...." };
