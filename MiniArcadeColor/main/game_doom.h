// MiniArcade Color: Doom. Part of console.cpp, which includes it.
// A raycaster in the 16 x 16 maze of the OLED game: textured walls lit by
// distance, a flagstone floor and a ceiling with lamps, demons as sprites
// that find their way to you round the walls (breadth first search).
// LEFT/RIGHT turn, UP/DOWN walk, OK fires. A demon that reaches you costs
// a third of your health; 10 points for every demon shot.
//
// Speed: walls, floor and ceiling go straight into the frame buffer in
// fixed point (one ray per column, floor and ceiling a row at a time).
// Textures and light tables are built at the start on the heap and freed
// when the game ends; the maze and the sine table live in flash.

#define DOOM_VY    TOP_H                 // view top
#define DOOM_VH    168                   // view height (even); the HUD below
#define DOOM_HOR   (DOOM_VY + DOOM_VH / 2)
#define DOOM_HUD   (DOOM_VY + DOOM_VH)   // 194
#define DOOM_MON   4
#define DOOM_TS    32                    // texture size (one per map cell)
#define DOOM_NT    6                     // textures: brick, stone, tech, wood, floor, ceiling
#define DOOM_NL    8                     // light levels
#define DOOM_PL    9830                  // camera plane, Q14 = tan(31 deg)
#define DOOM_F     267                   // focal length in px: 160 / 0.6
#define DOOM_TICK  25                    // ms per game step
#define DOOM_R     44                    // the player's radius, 1/256 cell

/* 16 x 16 maze, one bit per cell (1 = wall) - the OLED game's */
static const uint16_t DOOM_MAP[16] = {
  0xFFFF, 0x8001, 0x8FD1, 0x8811, 0x8BB1, 0x8A21, 0x8AAD, 0x8AA1,
  0x8EA1, 0x82A1, 0xBEAF, 0xA0A1, 0xAFA1, 0xA001, 0xBFFD, 0xFFFF
};

// a sine table of 1024 steps per turn, Q14, computed by the compiler (flash)
constexpr double doomTaylor(double x) {
  double t = x, s = x;
  for (int k = 1; k < 12; k++) { t *= -x * x / ((2 * k) * (2 * k + 1)); s += t; }
  return s;
}
struct DoomSinTab {
  int16_t v[1024];
  constexpr DoomSinTab() : v() {
    for (int i = 0; i < 1024; i++) {
      int q = i / 256, r = i % 256;
      double x = ((q & 1) ? 256 - r : r) * 3.14159265358979 / 512;
      int iv = (int)(doomTaylor(x) * 16384 + 0.5);
      v[i] = (int16_t)(q >= 2 ? -iv : iv);
    }
  }
};
static constexpr DoomSinTab DOOM_SIN{};
#define DOOM_S(a) ((int32_t)DOOM_SIN.v[(a) & 1023])
#define DOOM_C(a) ((int32_t)DOOM_SIN.v[((a) + 256) & 1023])

// sprites: '.' clear, the rest index DOOM_SKEYS (demon and gibs share it)
static const char DOOM_SKEYS[] = ".nblrykwRo";
static const uint8_t DOOM_SRGB[10][3] = { { 0, 0, 0 }, { 30, 50, 10 }, { 95, 135, 40 }, { 175, 200, 100 }, { 150, 0, 0 },
                                          { 255, 220, 0 }, { 0, 0, 0 }, { 240, 230, 200 }, { 255, 40, 20 }, { 255, 140, 0 } };
static const char *const DOOM_DEMON[20] = {
  "...n........n...", "..nb........bn..", "..nbbnbbbbnbbn..", "..nbbbbbbbbbbn..", "..nbyRbbbbRybn..",
  "..nbbbbbbbbbbn..", "...nbwkwwkwbn...", "...nbbwkkwbbn...", "..nnbbbbbbbbnn..", ".nbbblllllllbbn.",
  "nbb.bllllllb.bbn", "nb..bllRRllb..bn", "ww..bblllbbb..ww", "....nbbbbbbn....", "....nbb..bbn....",
  "....nbb..bbn....", "....nbb..bbn....", "...nbbb..bbbn...", "..nnnnn..nnnnn..", "................" };
static const char *const DOOM_GIBS[20] = {
  "................", "................", "................", "................", "................",
  "................", "....R......R....", "..R....R.....R..", "......R.R.......", "....r..rr...r...",
  "..r..rrRRr.r....", "...rrRRRRRrr..r.", ".r.rRRooRRRr....", "..rRRRoooRRRrr..", "...rRRRRRRRr..r.",
  "r.rrRRRRRRrrr...", "..rrrRRRRrrr..r.", ".rrrrrrrrrrrrr..", "..rrrrrrrrrrr...", "................" };
// the pistol and the hand, and the face in the HUD
static const char *const DOOM_GUN[16] = {
  "......kkkk......", ".....kdLLdk.....", ".....kdLsdk.....", ".....kdssdk.....", "....kkdssdkk....",
  "....kdLsssdk....", "....kdssssdk....", "....kdssssdk....", "...hhkddddkhh...", "..hhhhkddkhhhh..",
  ".hhhhhhkkhhhhhh.", ".hhHhhhhhhhhHhh.", "hhhHhhhhhhhhHhhh", "hhhhHhhhhhhhHhhh", "hhhhhhhhhhhhhhhh",
  "hhhhhhhhhhhhhhhh" };
static const char *const DOOM_FACE[2][12] = {
  { "...nnnnnn...", "..nnhhhhnn..", ".nhhhhhhhhn.", ".hhwkhhwkhh.", ".hhhhhhhhhh.", ".hhhhnnhhhh.",
    ".hhhhhhhhhh.", "..hhrrrrhh..", "..hhhhhhhh..", "...hhhhhh...", "....hhhh....", "............" },
  { "...nnnnnn...", "..nnhhhhnn..", ".nhhhhhhhhn.", ".hhkwhhkwhh.", ".hhhhhhhhhh.", ".hhhhnnhhhh.",
    ".hhhhhhhhhh.", "..hhrrrrhh..", "..hhhhhhhh..", "...hhhhhh...", "....hhhh....", "............" } };

struct DoomMon { int32_t x, y; bool alive; };

struct DoomState {
  uint8_t  tex[DOOM_NT][DOOM_TS * DOOM_TS];   // tone 0..15, column by column (x * 32 + y)
  col      shade[DOOM_NT][DOOM_NL][16];       // tone -> colour per light level
  col      spal[DOOM_NL][10];                 // sprite colours per light level
  uint8_t  demon[20 * 16], gibs[20 * 16];     // sprites as DOOM_SKEYS indices
  int32_t  zbuf[SCR_W];                       // wall distance per column, Q16
  uint8_t  dist[16][16];                      // steps to the player
  DoomMon  mon[DOOM_MON];
  int32_t  gibX[DOOM_MON], gibY[DOOM_MON];    // a demon just shot
  uint32_t gibUntil[DOOM_MON];
};

static inline bool doomCell(int16_t cx, int16_t cy) {
  return cx < 0 || cx > 15 || cy < 0 || cy > 15 || ((DOOM_MAP[cy] >> cx) & 1);
}
static bool doomWall(int32_t x, int32_t y) { return doomCell((int16_t)(x >> 8), (int16_t)(y >> 8)); }
static bool doomBlocked(int32_t x, int32_t y) {          // the player is a small box, not a point
  return doomWall(x - DOOM_R, y - DOOM_R) || doomWall(x + DOOM_R, y - DOOM_R) ||
         doomWall(x - DOOM_R, y + DOOM_R) || doomWall(x + DOOM_R, y + DOOM_R);
}
// which wall texture a cell has: stone outside, one kind per quarter inside
static uint8_t doomTexOf(int16_t cx, int16_t cy) {
  if (cx <= 0 || cx >= 15 || cy <= 0 || cy >= 15) return 1;
  return cx < 8 ? (cy < 8 ? 0 : 3) : (cy < 8 ? 2 : 0);
}

static uint8_t doomHash(uint32_t a) {
  a ^= a >> 15; a *= 0x2c1b3c6dU; a ^= a >> 12; a *= 0x297a2d39U; a ^= a >> 15;
  return (uint8_t)a;
}

// the brightness (0..15) of texel x, y of texture t
static uint8_t doomTone(uint8_t t, int x, int y) {
  int n = doomHash((uint32_t)(t * 4096 + y * 32 + x)) & 3;       // grain
  int v;
  switch (t) {
  case 0: {                                                      // bricks, every one its own shade
    int row = y / 8, sx = x + (row & 1) * 8, bx = sx & 15, by = y & 7;
    if (by == 7 || bx == 15) return (uint8_t)(1 + (n & 1));
    v = 8 + (doomHash((uint32_t)(row * 7 + sx / 16 + 99)) & 3) - (n == 0);
    if (by == 0 || bx == 0) v += 3;
    if (by == 6 || bx == 14) v -= 3;
    break; }
  case 1: {                                                      // big stone blocks with a bevel
    int row = y / 16, sx = x + (row & 1) * 16, bx = sx & 31, by = y & 15;
    if (by == 15 || bx == 31) return 1;
    v = 6 + n + (doomHash((uint32_t)(row * 5 + sx / 32 + 7)) & 1) * 2;
    if (by == 0 || bx == 0) v = 12 + (n & 1);
    if (by == 14 || bx == 30) v = 3;
    break; }
  case 2: {                                                      // tech panel with a light strip
    if (x >= 14 && x <= 17 && y >= 4 && y <= 27) return (x == 14 || x == 17) ? 11 : 15;
    v = 6 + (n >> 1);
    if (y == 0 || x == 0) v = 11;
    if (y == 31 || x == 31) v = 2;
    if (y == 15) v = 3;
    if (y == 16) v = 10;
    if ((x == 4 || x == 27) && (y == 4 || y == 11 || y == 20 || y == 27)) v = 13;   // rivets
    break; }
  case 3: {                                                      // wooden planks, two iron bands
    if ((x & 7) == 0) return 2;
    v = 7 + (doomHash((uint32_t)(x / 8 * 31 + 5)) & 3) + (n >> 1);
    if ((y * 3 + (doomHash((uint32_t)x * 17) & 15)) % 9 == 0) v -= 3;   // grain
    if (y == 5 || y == 26) v = 13;
    if (y == 6 || y == 27) v = 3;
    break; }
  case 4: {                                                      // floor: four flagstones per cell
    int bx = x & 15, by = y & 15;
    if (bx == 0 || by == 0) return 1;
    v = 6 + n / 2 + (doomHash((uint32_t)((x / 16) * 3 + (y / 16) * 11 + 3)) & 1) * 2;
    if (bx == 1 || by == 1) v += 2;
    if (bx == 15 || by == 15) v -= 2;
    break; }
  default: {                                                     // ceiling: dark panels, a lamp per cell
    if (x >= 11 && x <= 20 && y >= 11 && y <= 20) return (x == 11 || x == 20 || y == 11 || y == 20) ? 10 : 15;
    if (x == 0 || y == 0 || x == 31 || y == 31) return 1;
    v = 4 + (n >> 1);
    break; }
  }
  return (uint8_t)(v < 0 ? 0 : (v > 15 ? 15 : v));
}

static void doomInit(DoomState *s) {
  static const uint8_t RAMP[DOOM_NT][3][3] = {                  // dark, middle, light
    { { 50, 15, 10 }, { 165, 65, 40 },  { 245, 175, 130 } },     // brick
    { { 25, 25, 35 }, { 110, 110, 130 }, { 225, 225, 240 } },    // stone
    { { 0, 30, 25 },  { 40, 135, 105 },  { 210, 255, 235 } },    // tech
    { { 45, 25, 5 },  { 145, 95, 40 },   { 240, 195, 120 } },    // wood
    { { 25, 20, 15 }, { 105, 90, 70 },   { 195, 175, 145 } },    // floor
    { { 15, 15, 25 }, { 75, 75, 95 },    { 255, 245, 190 } } };  // ceiling
  for (uint8_t t = 0; t < DOOM_NT; t++) {
    for (int x = 0; x < DOOM_TS; x++)
      for (int y = 0; y < DOOM_TS; y++) s->tex[t][x * DOOM_TS + y] = doomTone(t, x, y);
    col lo = rgb(RAMP[t][0][0], RAMP[t][0][1], RAMP[t][0][2]);
    col mid = rgb(RAMP[t][1][0], RAMP[t][1][1], RAMP[t][1][2]);
    col hi = rgb(RAMP[t][2][0], RAMP[t][2][1], RAMP[t][2][2]);
    for (uint8_t k = 0; k < 16; k++) {
      col c = k < 8 ? colLerp(lo, mid, (uint8_t)(k * 255 / 7)) : colLerp(mid, hi, (uint8_t)((k - 8) * 255 / 7));
      for (uint8_t l = 0; l < DOOM_NL; l++) s->shade[t][l][k] = colLerp(c, C::BLACK, (uint8_t)(l * 255 / (DOOM_NL + 1)));
    }
  }
  for (uint8_t i = 0; i < 10; i++) {
    col c = rgb(DOOM_SRGB[i][0], DOOM_SRGB[i][1], DOOM_SRGB[i][2]);
    for (uint8_t l = 0; l < DOOM_NL; l++) s->spal[l][i] = colLerp(c, C::BLACK, (uint8_t)(l * 255 / (DOOM_NL + 3)));
  }
  for (uint8_t r = 0; r < 20; r++)
    for (uint8_t c = 0; c < 16; c++) {
      const char *k1 = strchr(DOOM_SKEYS, DOOM_DEMON[r][c]), *k2 = strchr(DOOM_SKEYS, DOOM_GIBS[r][c]);
      s->demon[r * 16 + c] = k1 ? (uint8_t)(k1 - DOOM_SKEYS) : 0;
      s->gibs[r * 16 + c] = k2 ? (uint8_t)(k2 - DOOM_SKEYS) : 0;
    }
}

// light level for a distance (Q16 cells): a step every 1.3 cells
static inline uint8_t doomLight(int32_t d) { int32_t l = (d * 3) >> 18; return (uint8_t)(l > DOOM_NL - 1 ? DOOM_NL - 1 : l); }

/* floor and ceiling a row at a time (the walls paint over them), then one
   DDA ray per column for the walls                                     */
static void doomRender(DoomState *s, int32_t px, int32_t py, int16_t ang) {
  uint8_t *fb = gfx.buffer();
  int32_t dX = DOOM_C(ang), dY = DOOM_S(ang);                    // Q14
  int32_t plX = -(dY * DOOM_PL) >> 14, plY = (dX * DOOM_PL) >> 14;
  int32_t px16 = px << 8, py16 = py << 8;                        // Q16
  const uint8_t *tf = s->tex[4], *tc = s->tex[5];
  for (int16_t k = 0; k < DOOM_VH / 2; k++) {
    int32_t d = ((int32_t)DOOM_F << 16) / (2 * k + 1);           // floor distance of this row, Q16
    int32_t fx = px16 + (int32_t)(((int64_t)d * (dX - plX)) >> 14);
    int32_t fy = py16 + (int32_t)(((int64_t)d * (dY - plY)) >> 14);
    int32_t sx = (int32_t)(((int64_t)d * 2 * plX) >> 14) / SCR_W;
    int32_t sy = (int32_t)(((int64_t)d * 2 * plY) >> 14) / SCR_W;
    uint8_t l = doomLight(d);
    const col *shF = s->shade[4][l], *shC = s->shade[5][l];
    uint8_t *of = fb + (DOOM_HOR + k) * SCR_W, *oc = fb + (DOOM_HOR - 1 - k) * SCR_W;
    for (int16_t x = 0; x < SCR_W; x++) {
      uint16_t t = (uint16_t)((((uint32_t)fx >> 11) & 31) * 32 + (((uint32_t)fy >> 11) & 31));
      of[x] = shF[tf[t]];
      oc[x] = shC[tc[t]];
      fx += sx; fy += sy;
    }
  }
  for (int16_t x = 0; x < SCR_W; x++) {
    int32_t cam = ((int32_t)(2 * x + 1 - SCR_W) << 14) / SCR_W;  // -1..1, Q14
    int32_t rX = dX + ((plX * cam) >> 14), rY = dY + ((plY * cam) >> 14);
    int16_t mx = (int16_t)(px >> 8), my = (int16_t)(py >> 8);
    int32_t ddX = rX ? (1 << 30) / (rX < 0 ? -rX : rX) : (1 << 22);
    int32_t ddY = rY ? (1 << 30) / (rY < 0 ? -rY : rY) : (1 << 22);
    if (ddX > (1 << 22)) ddX = 1 << 22;
    if (ddY > (1 << 22)) ddY = 1 << 22;
    int8_t stX = rX < 0 ? -1 : 1, stY = rY < 0 ? -1 : 1;
    int32_t sdX = ((rX < 0 ? (px & 255) : 256 - (px & 255)) * ddX) >> 8;
    int32_t sdY = ((rY < 0 ? (py & 255) : 256 - (py & 255)) * ddY) >> 8;
    uint8_t side = 0;
    for (uint8_t i = 0; i < 48; i++) {
      if (sdX < sdY) { sdX += ddX; mx += stX; side = 0; }
      else           { sdY += ddY; my += stY; side = 1; }
      if (doomCell(mx, my)) break;
    }
    int32_t perp = side ? sdY - ddY : sdX - ddX;
    if (perp < 2048) perp = 2048;
    s->zbuf[x] = perp;
    int32_t h = ((int32_t)DOOM_F << 16) / perp;                  // wall height in px
    int32_t y0 = DOOM_HOR - h / 2;
    int32_t wx = side ? px16 + (int32_t)(((int64_t)perp * rX) >> 14) : py16 + (int32_t)(((int64_t)perp * rY) >> 14);
    uint8_t tx = (uint8_t)((wx >> 11) & 31);
    if ((side == 0 && rX > 0) || (side == 1 && rY < 0)) tx = 31 - tx;
    uint8_t t = doomTexOf(mx, my), l = doomLight(perp);
    if (side && l < DOOM_NL - 1) l++;                            // one face darker: corners show
    const uint8_t *col0 = s->tex[t] + tx * DOOM_TS;
    const col *sh = s->shade[t][l];
    int32_t step = (DOOM_TS << 16) / (h ? h : 1);
    int32_t ys = y0 < DOOM_VY ? DOOM_VY : y0, ye = y0 + h > DOOM_HUD ? DOOM_HUD : y0 + h;
    uint32_t tp = (uint32_t)((ys - y0) * step);
    uint8_t *o = fb + ys * SCR_W + x;
    for (int32_t y = ys; y < ye; y++) { *o = sh[col0[(tp >> 16) & 31]]; tp += step; o += SCR_W; }
  }
}

/* a billboard: dep / sid in 1/256 cell in front / to the right of the
   eye; hidden column by column behind nearer walls. Returns the screen
   centre and half width (for the aim).                                */
static void doomSprite(DoomState *s, const uint8_t *art, int32_t dep, int32_t sid, bool mirror, int16_t *cxOut, int16_t *hwOut) {
  int32_t h = (int32_t)DOOM_F * 200 / dep;                       // 0.78 cells tall
  int32_t w = h * 16 / 20;
  int32_t bottom = DOOM_HOR + (int32_t)DOOM_F * 128 / dep;       // on the floor
  int32_t cx = SCR_W / 2 + sid * DOOM_F / dep;
  *cxOut = (int16_t)(cx < -999 ? -999 : (cx > 999 ? 999 : cx));
  *hwOut = (int16_t)(w / 2 > 999 ? 999 : w / 2);
  if (w < 2 || h < 2) return;
  int32_t left = cx - w / 2, top = bottom - h;
  int32_t ustep = (16 << 16) / w, vstep = (20 << 16) / h;
  int32_t ys = top < DOOM_VY ? DOOM_VY : top, ye = bottom > DOOM_HUD ? DOOM_HUD : bottom;
  const col *pal = s->spal[doomLight(dep << 8)];
  uint8_t *fb = gfx.buffer();
  int32_t x0 = left < 0 ? 0 : left, x1 = left + w > SCR_W ? SCR_W : left + w;
  for (int32_t x = x0; x < x1; x++) {
    if (s->zbuf[x] <= (dep << 8)) continue;
    int32_t u = ((x - left) * ustep) >> 16;
    if (u > 15) u = 15;
    if (mirror) u = 15 - u;
    uint32_t v = (uint32_t)((ys - top) * vstep);
    uint8_t *o = fb + ys * SCR_W + x;
    for (int32_t y = ys; y < ye; y++, o += SCR_W, v += vstep) {
      uint8_t k = art[((v >> 16) < 20 ? (v >> 16) : 19) * 16 + u];
      if (k) *o = pal[k];
    }
  }
}

// steps from every free cell to the player: demons walk round the walls
static void doomFlow(DoomState *s, int32_t px, int32_t py) {
  static const int8_t DIR[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
  uint8_t q[256];
  uint16_t head = 0, tail = 0;
  memset(s->dist, 255, sizeof(s->dist));
  uint8_t sx = (uint8_t)(px >> 8), sy = (uint8_t)(py >> 8);
  s->dist[sy][sx] = 0;
  q[tail++] = (uint8_t)(sy * 16 + sx);
  while (head < tail) {
    uint8_t c = q[head++];
    int8_t cx = c & 15, cy = c >> 4;
    for (uint8_t k = 0; k < 4; k++) {
      int8_t nx = cx + DIR[k][0], ny = cy + DIR[k][1];
      if (doomCell(nx, ny) || s->dist[ny][nx] != 255) continue;
      s->dist[ny][nx] = s->dist[cy][cx] + 1;
      q[tail++] = (uint8_t)(ny * 16 + nx);
    }
  }
}

static void doomSpawn(DoomState *s, uint8_t i, int32_t px, int32_t py) {
  for (uint8_t t = 0; t < 60; t++) {
    int32_t x = (random(14) + 1) * 256 + 128, y = (random(14) + 1) * 256 + 128;
    if (!doomWall(x, y) && abs(x - px) + abs(y - py) > 700) { s->mon[i] = { x, y, true }; return; }
  }
  s->mon[i].alive = false;
}

static void doomHud(uint8_t hp, uint16_t kills, uint32_t now, bool hurt) {
  gfx.gradient(0, DOOM_HUD, SCR_W, SCR_H - DOOM_HUD, rgb(110, 105, 95), rgb(45, 40, 35));
  gfx.hline(0, DOOM_HUD, SCR_W, C::LTGREY);
  gfx.hline(0, DOOM_HUD + 1, SCR_W, C::DKGREY);
  static const char *const PCT[4] = { "0%", "33%", "67%", "100%" };
  gfx.frame(4, DOOM_HUD + 4, 128, 40, rgb(40, 35, 30));
  gfx.text(10, DOOM_HUD + 7, "HEALTH", C::LTGREY, F_SMALL);
  gfx.textShadow(10, DOOM_HUD + 17, PCT[hp], hp > 1 ? C::RED : C::ORANGE, C::BLACK, F_BOLD);
  for (uint8_t i = 0; i < 3; i++) {                                  // a red cross per third
    col c = i < hp ? C::RED : rgb(60, 50, 45);
    gfx.fill(84 + i * 15, DOOM_HUD + 25, 11, 3, c);
    gfx.fill(88 + i * 15, DOOM_HUD + 21, 3, 11, c);
  }
  // the face: looks round, red with pain
  col skin = hurt ? C::RED : (hp >= 3 ? rgb(230, 170, 120) : (hp == 2 ? rgb(220, 140, 100) : rgb(210, 100, 80)));
  const col FPAL[] = { skin, C::BROWN, C::WHITE, C::BLACK, C::DKRED };
  gfx.fill(141, DOOM_HUD + 4, 40, 40, rgb(40, 35, 30));
  gfx.sprite(143, DOOM_HUD + 6, DOOM_FACE[(now / 900) % 2], 12, "hnwkr", FPAL, 3);
  gfx.frame(190, DOOM_HUD + 4, 126, 40, rgb(40, 35, 30));
  gfx.text(196, DOOM_HUD + 7, "KILLS", C::LTGREY, F_SMALL);
  char b[8];
  snprintf(b, sizeof(b), "%u", kills);
  gfx.textShadow(196, DOOM_HUD + 17, b, C::YELLOW, C::BLACK, F_BOLD);
}

void doomRun() {
  DoomState *s = (DoomState *)malloc(sizeof(DoomState));
  if (!s) return;
  doomInit(s);
  static const int8_t DIR[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
  bool again = true;
  while (again) {
    again = false;
    int32_t px = 2 * 256 + 128, py = 2 * 256 + 128;
    int16_t ang = 0;
    AxisStep turn;                                                // tilting: turn as far as tilted
    uint8_t hp = 3;
    uint16_t score = 0, walk = 0;
    uint32_t next = gameMillis(), hurtUntil = 0, flashUntil = 0;
    bool over = false;
    for (uint8_t i = 0; i < DOOM_MON; i++) { doomSpawn(s, i, px, py); s->gibUntil[i] = 0; }
    btnClear();
    while (poll()) {
      uint32_t now = gameMillis();
      if ((int32_t)(now - next) > 120) next = now - 120;          // a long frame: no huge jump
      while (!over && (int32_t)(now - next) >= 0) {               // fixed steps, as many as time passed
        next += DOOM_TICK;
        ang = (ang + turn(keyAxis(B_LEFT, B_RIGHT), 9)) & 1023;
        int16_t fwd = keyAxis(B_DOWN, B_UP);
        if (fwd) {                                                // up to 2.5 cells per second
          int32_t nx = px + (DOOM_C(ang) >> 10) * fwd / 256, ny = py + (DOOM_S(ang) >> 10) * fwd / 256;
          if (!doomBlocked(nx, py)) px = nx;
          if (!doomBlocked(px, ny)) py = ny;
          walk++;
        }
        doomFlow(s, px, py);
        for (uint8_t i = 0; i < DOOM_MON; i++) {
          DoomMon &m = s->mon[i];
          if (!m.alive) { doomSpawn(s, i, px, py); continue; }
          int8_t mcx = (int8_t)(m.x >> 8), mcy = (int8_t)(m.y >> 8);
          int32_t tx = px, ty = py;                               // same cell: straight at you
          uint8_t best = s->dist[mcy][mcx];
          for (uint8_t k = 0; k < 4; k++) {                       // else to the next cell on the way
            int8_t nx = mcx + DIR[k][0], ny = mcy + DIR[k][1];
            if (doomCell(nx, ny)) continue;
            if (s->dist[ny][nx] < best) { best = s->dist[ny][nx]; tx = nx * 256 + 128; ty = ny * 256 + 128; }
          }
          int32_t mx = m.x + constrain(tx - m.x, -3, 3), my = m.y + constrain(ty - m.y, -3, 3);
          if (!doomWall(mx, m.y)) m.x = mx;
          if (!doomWall(m.x, my)) m.y = my;
          if (abs(px - m.x) + abs(py - m.y) < 90) {               // it got you
            hp--;
            m.alive = false;
            hurtUntil = now + 350;
            sfx(150, 200);
            if (!hp) { over = true; break; }
          }
        }
      }

      // ---- the view ----
      doomRender(s, px, py, ang);
      bool fire = btnTap(B_OK) && !over;
      if (fire) { flashUntil = now + 110; sfx(1100, 40); }
      int32_t dX = DOOM_C(ang), dY = DOOM_S(ang);
      uint8_t order[DOOM_MON * 2], n = 0;                         // demons and gibs, far ones first
      int32_t dep[DOOM_MON * 2], sid[DOOM_MON * 2];
      for (uint8_t i = 0; i < DOOM_MON * 2; i++) {
        bool gib = i >= DOOM_MON;
        uint8_t m = i % DOOM_MON;
        if (gib ? (int32_t)(s->gibUntil[m] - now) <= 0 : !s->mon[m].alive) continue;
        int32_t rx = (gib ? s->gibX[m] : s->mon[m].x) - px, ry = (gib ? s->gibY[m] : s->mon[m].y) - py;
        dep[i] = (rx * dX + ry * dY) >> 14;
        sid[i] = (ry * dX - rx * dY) >> 14;
        if (dep[i] < 60) continue;
        uint8_t j = n++;
        while (j && dep[order[j - 1]] < dep[i]) { order[j] = order[j - 1]; j--; }
        order[j] = i;
      }
      for (uint8_t k = 0; k < n; k++) {
        uint8_t i = order[k], m = i % DOOM_MON;
        bool gib = i >= DOOM_MON;
        int16_t cx, hw;
        doomSprite(s, gib ? s->gibs : s->demon, dep[i], sid[i], !gib && ((now / 260 + m) & 1), &cx, &hw);
        if (!gib && fire && abs(cx - SCR_W / 2) < hw + 4 && s->zbuf[SCR_W / 2] > (dep[i] << 8)) {
          s->mon[m].alive = false;                                // every demon in the line of fire
          s->gibX[m] = s->mon[m].x; s->gibY[m] = s->mon[m].y; s->gibUntil[m] = now + 450;
          score += 10;
          sfx(900, 60);
        }
      }
      // crosshair, the gun (it bobs while you walk), the muzzle flash
      gfx.hline(SCR_W / 2 - 7, DOOM_HOR, 5, C::GREEN);
      gfx.hline(SCR_W / 2 + 3, DOOM_HOR, 5, C::GREEN);
      gfx.vline(SCR_W / 2, DOOM_HOR - 7, 5, C::GREEN);
      gfx.vline(SCR_W / 2, DOOM_HOR + 3, 5, C::GREEN);
      int16_t bx = (int16_t)((DOOM_S(walk * 40) * 6) >> 14), by = (int16_t)(abs(DOOM_C(walk * 40) * 4) >> 14);
      bool flash = (int32_t)(flashUntil - now) > 0;
      if (flash) {
        gfx.disc(SCR_W / 2 + bx, DOOM_HUD - 60 + by, 16, C::ORANGE);
        gfx.disc(SCR_W / 2 + bx, DOOM_HUD - 62 + by, 10, C::YELLOW);
        gfx.disc(SCR_W / 2 + bx, DOOM_HUD - 62 + by, 4, C::WHITE);
      }
      static const col GPAL[] = { C::BLACK, GREY1, GREY2, GREY3, rgb(230, 170, 120), rgb(180, 120, 80) };
      gfx.sprite(SCR_W / 2 - 32 + bx, DOOM_HUD - 54 + by + (flash ? 4 : 0), DOOM_GUN, 16, "kdsLhH", GPAL, 4);
      bool hurt = (int32_t)(hurtUntil - now) > 0 || over;
      if (hurt) gfx.frame(0, DOOM_VY, SCR_W, DOOM_VH, C::RED, 5);
      doomHud(hp, (uint16_t)(score / 10), now, hurt);
      statusBar("DOOM", score, curHigh, C::RED);
      gfx.send();
      if (over) { delay(700); break; }
    }
    if (!over) break;                                             // left from the pause menu
    again = gameOver(score);
  }
  free(s);
}

// the icon in the library (12 x 12, letters as ICON_KEYS in console.cpp)
static const char *const IC_DOOM[12] = {
  "w..........w", "wn........nw", ".nn......nn.", ".nnnnnnnnnn.", ".nyrnnnnryn.", ".nnnnnnnnnn.",
  "..nkwkwkwn..", "..nnkkkknn..", "...nnnnnn...", "..r..o..r...", ".rorroorror.", "roooyooyooor" };
