// MiniArcade Color: Kart GP - the fixed data: numbers, trigonometry, the
// six drivers, the six worlds and the six tracks. Part of console.cpp
// (through game_kart.h) - everything here stays in flash.

#include "kart_art.h"

// ---------------- numbers ----------------
#define KG_WORLD   1024                // the world is 1024 x 1024 px
#define KG_TILE    32                  // the map: tiles of 32 x 32 px ...
#define KG_TILES   (KG_WORLD / KG_TILE)
#define KG_CELL    4                   // ... of 8 x 8 cells of 4 px, one material (4 bits) each
#define KG_STEP    10                  // ms per physics step
#define KG_HZ      84                  // the horizon's row on the screen
#define KG_FLOOR   (SCR_H - KG_HZ)     // rows of floor below it
#define KG_F       160                 // focal length: 90 degrees across the screen
#define KG_CAMH    22                  // the camera's height above the track (px)
#define KG_CAMD    30                  // ... and how far behind the kart
#define KG_KARTS   6
#define KG_LAPS    3
#define KG_PTS     8                   // centre line points every 8 px
#define KG_VMAX    256                 // top speed at 100cc: 256/256 px a step = 100 px/s
#define KG_R       4                   // a kart's radius (px)
#define KG_FOG     8                   // haze levels towards the horizon
#define KG_DECOR   64
#define KG_BOXES   12
#define KG_COINS   24
#define KG_PEELS   12
#define KG_ROCKETS 6
#define KG_SPS     5                   // kart sprites: 5 sprite px a world px

// ---------------- angles: 65536 a turn, 0 = +x (east), growing clockwise (+y is south) ----------------
constexpr double kgTaylor(double x) {
  double t = x, s = x;
  for (int k = 1; k < 14; k++) { t *= -x * x / ((2 * k) * (2 * k + 1)); s += t; }
  return s;
}
struct KgSinTab {
  int16_t v[1024];
  constexpr KgSinTab() : v() {
    for (int i = 0; i < 1024; i++) {
      int q = i / 256, r = i % 256;
      double x = ((q & 1) ? 256 - r : r) * 3.14159265358979 / 512;
      int iv = (int)(kgTaylor(x) * 16384 + 0.5);
      v[i] = (int16_t)(q >= 2 ? -iv : iv);
    }
  }
};
static constexpr KgSinTab KG_SIN{};
static inline int32_t kgSin(uint16_t a) { return KG_SIN.v[a >> 6]; }             // Q14
static inline int32_t kgCos(uint16_t a) { return KG_SIN.v[(uint16_t)(a + 16384) >> 6]; }

static const uint16_t KG_ATAN[65] = {
  0,163,326,489,651,813,975,1136,1297,1457,1617,1775,1933,2090,2246,2401,2555,2708,2860,3010,3159,3307,3453,
  3599,3742,3884,4025,4164,4302,4438,4572,4705,4836,4966,5094,5220,5344,5467,5589,5708,5826,5943,6058,6171,
  6282,6392,6500,6607,6712,6815,6917,7018,7117,7214,7310,7405,7498,7589,7679,7768,7856,7942,8026,8110,8192 };
// the angle of (x, y)
static uint16_t kgAtan2(int32_t y, int32_t x) {
  if (!x && !y) return 0;
  uint32_t ax = (uint32_t)(x < 0 ? -x : x), ay = (uint32_t)(y < 0 ? -y : y);
  uint16_t a;
  if (ax >= ay) { uint32_t q = (uint32_t)(((uint64_t)ay << 12) / ax); a = (uint16_t)(KG_ATAN[q >> 6] + ((KG_ATAN[(q >> 6) + (q < 4096)] - KG_ATAN[q >> 6]) * (q & 63) >> 6)); }
  else { uint32_t q = (uint32_t)(((uint64_t)ax << 12) / ay); a = (uint16_t)(16384 - KG_ATAN[q >> 6] - ((KG_ATAN[(q >> 6) + (q < 4096)] - KG_ATAN[q >> 6]) * (q & 63) >> 6)); }
  if (x < 0) a = (uint16_t)(32768 - a);
  if (y < 0) a = (uint16_t)(0 - a);
  return a;
}
static uint32_t kgSqrt(uint32_t v) {
  uint32_t r = 0, b = 1UL << 30;
  while (b > v) b >>= 2;
  while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
  return r;
}
static inline int16_t kgAngDiff(uint16_t a, uint16_t b) { return (int16_t)(a - b); }

// ---------------- the drivers ----------------
struct KgDriver {
  const char *name;
  uint8_t speed, accel, handling, weight;      // 1..5
  col body[3], acc[2], suit[2], helm[2];
};
static const KgDriver KG_DRV[KG_KARTS] = {
  { "BLAZE", 3, 3, 3, 3, { rgb(255,110,90), rgb(230,30,30), rgb(140,0,10) },  { rgb(255,230,60), rgb(200,150,0) },  { rgb(200,40,40), rgb(120,10,10) }, { rgb(255,255,255), rgb(170,180,200) } },
  { "MINT",  2, 5, 4, 2, { rgb(130,255,150), rgb(40,200,90), rgb(0,110,50) },  { rgb(250,250,250), rgb(170,190,180) }, { rgb(0,170,150), rgb(0,100,90) },  { rgb(180,255,80), rgb(90,170,30) } },
  { "BOLT",  5, 2, 2, 4, { rgb(255,250,120), rgb(255,210,0), rgb(170,120,0) },  { rgb(70,70,80), rgb(30,30,35) },     { rgb(255,140,0), rgb(170,80,0) },  { rgb(255,230,40), rgb(40,40,40) } },
  { "NOVA",  3, 4, 5, 2, { rgb(210,140,255), rgb(150,60,230), rgb(80,20,140) }, { rgb(255,130,210), rgb(190,50,140) }, { rgb(120,40,200), rgb(60,20,110) }, { rgb(220,225,235), rgb(140,145,160) } },
  { "TANK",  4, 2, 3, 5, { rgb(130,180,255), rgb(40,100,230), rgb(10,40,140) }, { rgb(255,160,40), rgb(200,90,0) },   { rgb(30,50,130), rgb(10,20,70) },  { rgb(90,160,255), rgb(30,80,200) } },
  { "PIXIE", 2, 4, 5, 1, { rgb(255,180,230), rgb(255,90,190), rgb(170,30,120) }, { rgb(80,230,255), rgb(0,150,200) },  { rgb(250,250,250), rgb(180,180,200) }, { rgb(255,120,200), rgb(190,40,130) } },
};

// ---------------- materials of the map ----------------
enum { KM_OUTA, KM_OUTB, KM_ROAD, KM_LINE, KM_KERBA, KM_KERBB, KM_RUN, KM_WALL, KM_PIT, KM_SHORE, KM_BOOST, KM_FINISH,
       KM_ROAD2, KM_EDGE, KM_RUN2, KM_N = 16 };
// which one wins where two stamps meet (lower wins)
static const uint8_t KM_RANK[16] = { 9, 9, 2, 1, 4, 4, 5, 6, 8, 7, 0, 0, 2, 3, 5, 9 };
// patterns of the textures
enum { KT_SOLID, KT_NOISE, KT_GRASS, KT_SAND, KT_STRIPE, KT_BRICK, KT_WAVE, KT_CHEVRON, KT_CHECK, KT_LAVA, KT_ICE, KT_BLOCKS, KT_MARKS };

// ---------------- the worlds ----------------
enum { KW_MEADOW, KW_DESERT, KW_BEACH, KW_ICE, KW_CITY, KW_VOLCANO };
enum { KF_FAR_PEAKS, KF_FAR_MESAS, KF_FAR_ISLANDS, KF_FAR_ALPS, KF_FAR_HILLS, KF_FAR_VOLCANO };
enum { KN_HILLS, KN_DUNES, KN_SEA, KN_PINES, KN_CITY, KN_ROCKS };
enum { KD_NONE, KD_TREE, KD_BUSH, KD_PINE, KD_PALM, KD_CACTUS, KD_ROCK, KD_LAMP, KD_CHEV, KD_DEAD };

struct KgTheme {
  const char *name;
  col sky[3];                 // top, middle, horizon
  col fog;                    // the haze far away
  col far[3];                 // far layer: lit side, shade, top (snow / glow)
  col nearc[3];               // near layer: main, dark, detail
  uint8_t farKind, nearKind, farH, nearH;
  col sun, sunGlow;
  uint16_t sunA;              // where the sun stands
  uint8_t sunY;               // rows above the horizon
  bool night, clouds;
  col mat[KM_N][4];           // four shades of every material
  uint8_t pat[KM_N];
  uint8_t speed[KM_N];        // % of top speed on it; 0 = wall / pit
  uint8_t gripRoad, gripOff;  // /256 of the sideways speed lost a step
  uint8_t kerb, run, wall;    // bands beyond the road's edge (px); kerb 0 = only in bends
  uint8_t outside;            // the world beyond: KM_OUTA or KM_PIT
  uint8_t decor[2];           // sprites by the track
  uint8_t decorBand;          // 0: in the grass beyond, 1: on the runoff (sand / rock)
  bool centreLine;
};

#define C4(a, b, c, d) { a, b, c, d }
static const KgTheme KG_THEME[6] = {
  { "MEADOW",
    { rgb(30, 80, 210), rgb(90, 160, 255), rgb(200, 230, 255) }, rgb(190, 220, 250),
    { rgb(130, 150, 210), rgb(90, 110, 180), rgb(240, 250, 255) }, { rgb(60, 160, 60), rgb(30, 110, 40), rgb(110, 200, 70) },
    KF_FAR_PEAKS, KN_HILLS, 30, 12, rgb(255, 255, 200), rgb(255, 240, 140), 52000, 62, false, true,
    { C4(rgb(51, 170, 51), rgb(51, 127, 51), rgb(102, 212, 51), rgb(0, 127, 0)),           // grass
      C4(rgb(0, 170, 51), rgb(0, 127, 51), rgb(51, 212, 51), rgb(0, 127, 0)),
      C4(GREY2, rgb(102, 85, 102), GREY3, GREY1),                                          // road
      C4(rgb(245, 245, 245), rgb(210, 210, 215), rgb(255, 255, 255), rgb(190, 190, 195)),
      C4(rgb(235, 40, 40), rgb(190, 20, 20), rgb(255, 90, 80), rgb(150, 10, 10)),          // kerbs
      C4(rgb(250, 250, 250), rgb(215, 215, 220), rgb(255, 255, 255), rgb(190, 190, 200)),
      C4(rgb(225, 195, 135), rgb(205, 175, 115), rgb(240, 215, 160), rgb(185, 150, 100)),  // sand trap
      C4(rgb(40, 40, 45), rgb(80, 80, 88), rgb(230, 40, 40), rgb(250, 250, 250)),          // tyre wall
      C4(rgb(40, 110, 220), rgb(60, 140, 240), rgb(130, 200, 255), rgb(30, 80, 190)),      // pond
      C4(rgb(200, 230, 255), rgb(160, 210, 250), rgb(255, 255, 255), rgb(120, 180, 240)),
      C4(rgb(255, 220, 0), rgb(255, 140, 0), rgb(255, 255, 190), rgb(220, 90, 0)),         // boost
      C4(rgb(250, 250, 250), rgb(20, 20, 25), rgb(250, 250, 250), rgb(20, 20, 25)),        // finish
      C4(GREY2, rgb(102, 85, 102), GREY3, GREY1),
      C4(rgb(250, 250, 250), rgb(215, 215, 220), rgb(255, 255, 255), rgb(190, 190, 195)),
      C4(rgb(225, 195, 135), rgb(205, 175, 115), rgb(240, 215, 160), rgb(185, 150, 100)),
      C4(0, 0, 0, 0) },
    { KT_GRASS, KT_GRASS, KT_NOISE, KT_SOLID, KT_STRIPE, KT_STRIPE, KT_SAND, KT_BLOCKS, KT_WAVE, KT_WAVE, KT_CHEVRON, KT_CHECK, KT_MARKS, KT_SOLID, KT_SAND, KT_SOLID },
    { 50, 50, 100, 100, 100, 100, 55, 0, 0, 55, 100, 100, 100, 100, 55, 50 },
    170, 120, 0, 14, 0, KM_OUTA, { KD_TREE, KD_BUSH }, 0, false },
  { "DESERT",
    { rgb(50, 110, 220), rgb(140, 190, 240), rgb(250, 225, 180) }, rgb(240, 215, 170),
    { rgb(210, 120, 70), rgb(150, 70, 45), rgb(240, 170, 110) }, { rgb(235, 185, 105), rgb(200, 140, 70), rgb(250, 215, 150) },
    KF_FAR_MESAS, KN_DUNES, 26, 10, rgb(255, 255, 220), rgb(255, 220, 120), 12000, 66, false, false,
    { C4(rgb(230, 190, 115), rgb(215, 170, 100), rgb(245, 210, 140), rgb(195, 150, 85)),     // sand
      C4(rgb(220, 178, 105), rgb(205, 160, 92), rgb(238, 200, 128), rgb(185, 140, 78)),
      C4(rgb(150, 125, 105), rgb(132, 110, 92), rgb(170, 145, 122), rgb(118, 96, 80)),     // dusty road
      C4(rgb(250, 240, 220), rgb(220, 205, 180), rgb(255, 255, 240), rgb(200, 185, 160)),
      C4(rgb(230, 50, 30), rgb(180, 30, 10), rgb(255, 100, 60), rgb(140, 20, 0)),
      C4(rgb(250, 245, 235), rgb(215, 205, 190), rgb(255, 255, 250), rgb(190, 180, 165)),
      C4(rgb(240, 205, 130), rgb(225, 185, 112), rgb(250, 222, 155), rgb(205, 165, 95)),   // soft sand
      C4(rgb(150, 80, 50), rgb(110, 55, 35), rgb(190, 110, 70), rgb(80, 40, 25)),
      C4(rgb(40, 120, 220), rgb(60, 150, 240), rgb(140, 210, 255), rgb(30, 90, 190)),      // oasis
      C4(rgb(120, 190, 90), rgb(90, 160, 60), rgb(160, 220, 120), rgb(70, 130, 50)),
      C4(rgb(255, 220, 0), rgb(255, 140, 0), rgb(255, 255, 190), rgb(220, 90, 0)),
      C4(rgb(250, 250, 250), rgb(20, 20, 25), rgb(250, 250, 250), rgb(20, 20, 25)),
      C4(rgb(140, 116, 98), rgb(124, 102, 86), rgb(158, 134, 114), rgb(110, 90, 76)),
      C4(rgb(250, 240, 220), rgb(220, 205, 180), rgb(255, 255, 240), rgb(200, 185, 160)),
      C4(rgb(240, 205, 130), rgb(225, 185, 112), rgb(250, 222, 155), rgb(205, 165, 95)),
      C4(0, 0, 0, 0) },
    { KT_SAND, KT_SAND, KT_NOISE, KT_SOLID, KT_STRIPE, KT_STRIPE, KT_SAND, KT_BRICK, KT_WAVE, KT_GRASS, KT_CHEVRON, KT_CHECK, KT_MARKS, KT_SOLID, KT_SAND, KT_SOLID },
    { 55, 55, 100, 100, 100, 100, 60, 0, 0, 60, 100, 100, 100, 100, 60, 55 },
    160, 110, 0, 12, 0, KM_OUTA, { KD_CACTUS, KD_ROCK }, 0, false },
  { "BEACH",
    { rgb(20, 120, 255), rgb(100, 190, 255), rgb(215, 245, 255) }, rgb(200, 235, 255),
    { rgb(60, 150, 80), rgb(30, 100, 60), rgb(240, 230, 180) }, { rgb(30, 110, 200), rgb(10, 70, 160), rgb(220, 245, 255) },
    KF_FAR_ISLANDS, KN_SEA, 14, 6, rgb(255, 255, 230), rgb(255, 245, 170), 40000, 70, false, true,
    { C4(rgb(20, 130, 225), rgb(40, 155, 240), rgb(130, 215, 255), rgb(10, 100, 200)),      // the sea
      C4(rgb(15, 120, 215), rgb(30, 145, 235), rgb(120, 205, 255), rgb(10, 95, 190)),
      C4(rgb(170, 165, 160), rgb(150, 146, 142), rgb(195, 190, 185), rgb(130, 126, 122)),   // promenade
      C4(rgb(255, 255, 255), rgb(220, 220, 225), rgb(255, 255, 255), rgb(200, 200, 205)),
      C4(rgb(240, 60, 60), rgb(190, 30, 30), rgb(255, 120, 110), rgb(150, 20, 20)),
      C4(rgb(250, 250, 250), rgb(215, 215, 220), rgb(255, 255, 255), rgb(195, 195, 205)),
      C4(rgb(245, 225, 165), rgb(230, 205, 145), rgb(255, 240, 190), rgb(210, 185, 125)),  // beach
      C4(rgb(160, 110, 70), rgb(120, 80, 50), rgb(190, 140, 90), rgb(90, 60, 40)),
      C4(rgb(20, 130, 225), rgb(40, 155, 240), rgb(130, 215, 255), rgb(10, 100, 200)),
      C4(rgb(235, 250, 255), rgb(170, 225, 250), rgb(255, 255, 255), rgb(110, 200, 245)),  // surf
      C4(rgb(255, 220, 0), rgb(255, 140, 0), rgb(255, 255, 190), rgb(220, 90, 0)),
      C4(rgb(250, 250, 250), rgb(20, 20, 25), rgb(250, 250, 250), rgb(20, 20, 25)),
      C4(rgb(160, 155, 150), rgb(142, 138, 134), rgb(182, 177, 172), rgb(124, 120, 116)),
      C4(rgb(255, 255, 255), rgb(220, 220, 225), rgb(255, 255, 255), rgb(200, 200, 205)),
      C4(rgb(240, 218, 158), rgb(225, 198, 138), rgb(252, 232, 182), rgb(205, 178, 118)),
      C4(0, 0, 0, 0) },
    { KT_WAVE, KT_WAVE, KT_NOISE, KT_SOLID, KT_STRIPE, KT_STRIPE, KT_SAND, KT_BRICK, KT_WAVE, KT_WAVE, KT_CHEVRON, KT_CHECK, KT_MARKS, KT_SOLID, KT_SAND, KT_SOLID },
    { 0, 0, 100, 100, 100, 100, 60, 0, 0, 55, 100, 100, 100, 100, 60, 0 },
    170, 120, 0, 34, 0, KM_PIT, { KD_PALM, KD_ROCK }, 1, false },
  { "ICE",
    { rgb(100, 160, 230), rgb(180, 215, 250), rgb(240, 250, 255) }, rgb(230, 242, 255),
    { rgb(225, 238, 255), rgb(150, 175, 220), rgb(255, 255, 255) }, { rgb(30, 85, 75), rgb(15, 55, 50), rgb(240, 250, 255) },
    KF_FAR_ALPS, KN_PINES, 40, 14, rgb(255, 255, 255), rgb(255, 250, 220), 22000, 58, false, true,
    { C4(rgb(240, 248, 255), rgb(222, 236, 250), rgb(255, 255, 255), rgb(205, 220, 240)),    // snow
      C4(rgb(232, 242, 252), rgb(214, 230, 248), rgb(250, 252, 255), rgb(198, 214, 236)),
      C4(rgb(165, 212, 242), rgb(145, 195, 232), rgb(205, 236, 255), rgb(125, 175, 222)),   // ice
      C4(rgb(255, 255, 255), rgb(220, 235, 250), rgb(255, 255, 255), rgb(200, 220, 240)),
      C4(rgb(40, 100, 230), rgb(20, 60, 180), rgb(90, 150, 255), rgb(10, 40, 140)),
      C4(rgb(250, 252, 255), rgb(215, 228, 245), rgb(255, 255, 255), rgb(190, 205, 230)),
      C4(rgb(250, 252, 255), rgb(230, 240, 252), rgb(255, 255, 255), rgb(212, 226, 245)),   // deep snow
      C4(rgb(200, 225, 250), rgb(160, 195, 235), rgb(240, 250, 255), rgb(120, 160, 210)),   // snow bank
      C4(rgb(20, 60, 140), rgb(40, 90, 170), rgb(110, 160, 220), rgb(10, 40, 110)),
      C4(rgb(200, 225, 250), rgb(160, 195, 235), rgb(240, 250, 255), rgb(120, 160, 210)),
      C4(rgb(255, 220, 0), rgb(255, 140, 0), rgb(255, 255, 190), rgb(220, 90, 0)),
      C4(rgb(250, 250, 250), rgb(20, 20, 25), rgb(250, 250, 250), rgb(20, 20, 25)),
      C4(rgb(150, 200, 235), rgb(132, 184, 225), rgb(185, 225, 250), rgb(115, 165, 215)),
      C4(rgb(255, 255, 255), rgb(220, 235, 250), rgb(255, 255, 255), rgb(200, 220, 240)),
      C4(rgb(250, 252, 255), rgb(230, 240, 252), rgb(255, 255, 255), rgb(212, 226, 245)),
      C4(0, 0, 0, 0) },
    { KT_SAND, KT_SAND, KT_ICE, KT_SOLID, KT_STRIPE, KT_STRIPE, KT_SAND, KT_BLOCKS, KT_ICE, KT_SOLID, KT_CHEVRON, KT_CHECK, KT_ICE, KT_SOLID, KT_SAND, KT_SOLID },
    { 55, 55, 100, 100, 100, 100, 60, 0, 0, 60, 100, 100, 100, 100, 60, 55 },
    70, 90, 8, 12, 0, KM_OUTA, { KD_PINE, KD_ROCK }, 0, false },
  { "CITY",
    { rgb(0, 0, 30), rgb(20, 15, 70), rgb(70, 45, 120) }, rgb(50, 35, 95),
    { rgb(40, 40, 90), rgb(25, 25, 60), rgb(70, 60, 120) }, { rgb(25, 25, 50), rgb(15, 15, 35), rgb(255, 220, 100) },
    KF_FAR_HILLS, KN_CITY, 14, 34, rgb(250, 250, 230), rgb(150, 150, 190), 30000, 64, true, false,
    { C4(rgb(48, 48, 64), rgb(40, 40, 56), rgb(58, 58, 74), rgb(32, 32, 48)),              // blocks
      C4(rgb(44, 44, 60), rgb(36, 36, 52), rgb(54, 54, 70), rgb(28, 28, 44)),
      C4(rgb(62, 62, 74), rgb(52, 52, 64), rgb(78, 78, 90), rgb(44, 44, 56)),              // asphalt
      C4(rgb(250, 200, 0), rgb(210, 160, 0), rgb(255, 230, 80), rgb(170, 120, 0)),
      C4(rgb(230, 30, 60), rgb(170, 10, 40), rgb(255, 90, 120), rgb(120, 0, 30)),
      C4(rgb(240, 240, 250), rgb(200, 200, 215), rgb(255, 255, 255), rgb(170, 170, 190)),
      C4(rgb(130, 130, 145), rgb(110, 110, 125), rgb(150, 150, 165), rgb(95, 95, 110)),    // pavement
      C4(rgb(230, 30, 50), rgb(245, 245, 250), rgb(255, 90, 110), rgb(160, 160, 175)),     // barrier
      C4(rgb(10, 30, 80), rgb(20, 50, 120), rgb(60, 110, 200), rgb(5, 20, 60)),
      C4(rgb(60, 60, 75), rgb(45, 45, 60), rgb(80, 80, 95), rgb(35, 35, 50)),
      C4(rgb(0, 230, 255), rgb(0, 140, 255), rgb(200, 255, 255), rgb(120, 0, 255)),
      C4(rgb(250, 250, 250), rgb(20, 20, 25), rgb(250, 250, 250), rgb(20, 20, 25)),
      C4(rgb(56, 56, 68), rgb(48, 48, 60), rgb(70, 70, 82), rgb(40, 40, 52)),
      C4(rgb(245, 245, 250), rgb(200, 200, 210), rgb(255, 255, 255), rgb(170, 170, 185)),
      C4(rgb(120, 120, 135), rgb(100, 100, 115), rgb(140, 140, 155), rgb(85, 85, 100)),
      C4(0, 0, 0, 0) },
    { KT_BLOCKS, KT_BLOCKS, KT_NOISE, KT_SOLID, KT_STRIPE, KT_STRIPE, KT_BLOCKS, KT_STRIPE, KT_WAVE, KT_NOISE, KT_CHEVRON, KT_CHECK, KT_MARKS, KT_SOLID, KT_BLOCKS, KT_SOLID },
    { 50, 50, 100, 100, 100, 100, 85, 0, 0, 60, 100, 100, 100, 100, 85, 50 },
    165, 120, 0, 6, 8, KM_OUTA, { KD_LAMP, KD_LAMP }, 2, true },
  { "VOLCANO",
    { rgb(30, 0, 20), rgb(110, 20, 20), rgb(235, 100, 30) }, rgb(160, 45, 25),
    { rgb(70, 25, 25), rgb(40, 12, 12), rgb(255, 140, 0) }, { rgb(30, 10, 10), rgb(15, 5, 5), rgb(255, 90, 0) },
    KF_FAR_VOLCANO, KN_ROCKS, 44, 12, rgb(255, 200, 120), rgb(255, 110, 40), 8000, 40, false, false,
    { C4(rgb(255, 90, 0), rgb(230, 40, 0), rgb(255, 200, 40), rgb(180, 20, 0)),             // lava
      C4(rgb(250, 80, 0), rgb(220, 35, 0), rgb(255, 190, 30), rgb(170, 15, 0)),
      C4(rgb(92, 82, 84), rgb(76, 66, 68), rgb(112, 102, 100), rgb(62, 52, 56)),            // basalt road
      C4(rgb(255, 200, 0), rgb(220, 150, 0), rgb(255, 240, 90), rgb(180, 110, 0)),
      C4(rgb(255, 200, 0), rgb(220, 150, 0), rgb(255, 240, 90), rgb(180, 110, 0)),          // hazard kerbs
      C4(rgb(30, 25, 25), rgb(15, 12, 12), rgb(50, 44, 44), rgb(8, 6, 6)),
      C4(rgb(58, 42, 42), rgb(44, 32, 32), rgb(76, 58, 56), rgb(34, 24, 24)),               // cooled rock
      C4(rgb(40, 30, 30), rgb(70, 55, 55), rgb(255, 120, 0), rgb(20, 15, 15)),
      C4(rgb(255, 90, 0), rgb(230, 40, 0), rgb(255, 200, 40), rgb(180, 20, 0)),
      C4(rgb(255, 150, 20), rgb(220, 70, 0), rgb(255, 230, 120), rgb(160, 30, 0)),          // glowing edge
      C4(rgb(0, 230, 255), rgb(0, 140, 255), rgb(200, 255, 255), rgb(120, 0, 255)),
      C4(rgb(250, 250, 250), rgb(20, 20, 25), rgb(250, 250, 250), rgb(20, 20, 25)),
      C4(rgb(84, 74, 76), rgb(70, 60, 62), rgb(102, 92, 92), rgb(58, 48, 52)),
      C4(rgb(255, 120, 0), rgb(200, 70, 0), rgb(255, 180, 60), rgb(150, 40, 0)),
      C4(rgb(66, 48, 46), rgb(50, 36, 36), rgb(84, 64, 60), rgb(40, 28, 28)),
      C4(0, 0, 0, 0) },
    { KT_LAVA, KT_LAVA, KT_NOISE, KT_SOLID, KT_STRIPE, KT_STRIPE, KT_SAND, KT_BRICK, KT_LAVA, KT_LAVA, KT_CHEVRON, KT_CHECK, KT_MARKS, KT_SOLID, KT_SAND, KT_SOLID },
    { 0, 0, 100, 100, 100, 100, 70, 0, 0, 0, 100, 100, 100, 100, 70, 0 },
    170, 120, 8, 26, 0, KM_PIT, { KD_DEAD, KD_ROCK }, 1, false },
};

// ---------------- the tracks ----------------
/* A track is its centre line: points (in 4 px) a closed curve runs
   through (Catmull-Rom), the road's half width, and where the item
   boxes, coins and boost pads lie (a fraction of the lap /256).      */
struct KgTrackDef {
  const char *name;
  uint8_t theme, w, n;
  const uint8_t (*p)[2];
  uint8_t boxes[3], coins[4], boost[3];
  int8_t  boostLat[3];
};
static const uint8_t KG_P_MEADOW[][2] = { {45,200},{105,212},{175,207},{215,180},{212,140},{180,125},{160,100},{172,70},{205,55},{200,28},{160,26},{115,40},{85,75},{100,112},{95,140},{55,140},{35,165} };
static const uint8_t KG_P_DESERT[][2] = { {40,215},{125,220},{205,215},{225,190},{215,160},{190,155},{175,135},{190,110},{220,95},{220,55},{190,35},{140,40},{118,65},{90,60},{62,35},{35,50},{30,95},{50,125},{35,160} };
static const uint8_t KG_P_BEACH[][2] = { {50,205},{115,215},{175,210},{212,190},{220,155},{195,140},{160,150},{130,140},{125,110},{150,90},{190,80},{215,55},{200,30},{150,25},{95,35},{60,60},{40,100},{30,150} };
static const uint8_t KG_P_ICE[][2] = { {45,215},{110,220},{145,205},{135,178},{100,168},{85,140},{105,118},{140,125},{170,150},{205,155},{225,130},{218,100},{190,90},{180,68},{190,45},{170,26},{130,30},{100,52},{65,45},{35,65},{30,115},{40,160} };
static const uint8_t KG_P_CITY[][2] = { {56,215},{72,215},{88,215},{104,215},{120,215},{136,215},{152,215},{168,215},{184,215},{200,215},{213,208},{220,194},{220,180},{220,165},{220,150},{213,137},{200,130},{188,130},{176,130},{162,123},{155,110},{155,96},{162,82},{176,75},{188,75},{200,75},{214,69},{220,55},{214,41},{200,35},{183,35},{165,35},{148,35},{130,35},{113,35},{96,35},{82,42},{75,56},{75,70},{75,85},{69,99},{55,105},{41,111},{35,125},{35,142},{35,160},{35,177},{35,194},{42,208} };
static const uint8_t KG_P_VOLCANO[][2] = { {45,210},{105,220},{165,215},{210,190},{220,145},{200,105},{160,95},{130,115},{115,150},{85,160},{60,140},{65,105},{95,80},{140,65},{180,60},{215,48},{205,22},{115,25},{65,40},{35,75},{28,130},{32,175} };

static const KgTrackDef KG_TRACK[6] = {
  { "MEADOW RING", KW_MEADOW,  26, sizeof(KG_P_MEADOW) / 2,  KG_P_MEADOW,  { 60, 150, 215 }, { 30, 100, 180, 240 }, { 0, 0, 0 }, { 0, 0, 0 } },
  { "DUNE DASH",   KW_DESERT,  26, sizeof(KG_P_DESERT) / 2,  KG_P_DESERT,  { 50, 135, 205 }, { 25, 95, 170, 235 }, { 12, 160, 0 }, { 0, -8, 0 } },
  { "CORAL BAY",   KW_BEACH,   25, sizeof(KG_P_BEACH) / 2,   KG_P_BEACH,   { 55, 140, 210 }, { 30, 105, 175, 240 }, { 0, 0, 0 }, { 0, 0, 0 } },
  { "FROST PEAK",  KW_ICE,     26, sizeof(KG_P_ICE) / 2,     KG_P_ICE,     { 45, 130, 200 }, { 20, 90, 165, 235 }, { 0, 0, 0 }, { 0, 0, 0 } },
  { "NEON CITY",   KW_CITY,    25, sizeof(KG_P_CITY) / 2,    KG_P_CITY,    { 40, 125, 200 }, { 20, 85, 160, 230 }, { 10, 112, 0 }, { -6, 6, 0 } },
  { "MAGMA CORE",  KW_VOLCANO, 25, sizeof(KG_P_VOLCANO) / 2, KG_P_VOLCANO, { 45, 125, 195 }, { 20, 90, 160, 230 }, { 8, 100, 175 }, { 0, 0, 6 } },
};
static const char *const KG_CUP_NAME[2] = { "SUN CUP", "STAR CUP" };
static const uint8_t KG_POINTS[KG_KARTS] = { 9, 6, 4, 3, 2, 1 };
