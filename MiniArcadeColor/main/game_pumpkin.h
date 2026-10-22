// MiniArcade Color: Pumpkin Maze. Part of console.cpp, which includes it.
// The wooden ball maze, Halloween style: tilt the console (or hold the
// arrow keys) and roll the pumpkin through the crypt into the glowing
// goal hole. Every other hole is a pit - but it only swallows the pumpkin
// when the pumpkin's middle comes close to the pit's middle, like the
// toy: rolling fast, it runs over the rim. Ice lets it slide, mud holds
// it, bumpers kick it away, arrows push it one way only, candies count
// for a star, the keys open the doors, a rune keeps the place to start
// again after a fall, some pits wander to and fro. Three pumpkins a
// level; a star for getting through, one for every candy (a level
// without candy: for losing no pumpkin) and one for a time under par.
// 15 levels, opened one by one, and up to 8 own levels made on the phone
// (/maze, see appMazePut): the level list opens the phone editor too, on
// the console's own hotspot when there is no WLAN. Best = all stars of
// the 15 levels.
// Physics in whole numbers (1/256 px) in steps of 10 ms whatever the
// frame rate. The board is drawn once; after that a frame draws only the
// tiles that changed or had something drawn over them in the last frame,
// so only a few 32-px blocks of the picture go out to the panel.

#define PM_W      20                   // tiles
#define PM_H      13
#define PM_N      (PM_W * PM_H)
#define PM_T      16                   // tile size (px)
#define PM_Y0     30                   // the board's top on the screen (below the status bar)
#define PM_BOT    (PM_Y0 + PM_H * PM_T)   // 238
#define PM_STEP   10                   // ms per physics step
#define PM_R      6                    // the pumpkin's radius (px)
#define PM_ACC    12                   // full tilt: 1/256 px per step per step (about 470 px/s^2)
#define PM_BOOST  24                   // an arrow pushes harder than any tilt
#define PM_VMAX   900                  // 3.5 px a step at most: never through a wall
#define PM_RIM    (9 * 256)            // a hole's rim pulls from this far ...
#define PM_PULL   5                    // ... this much
#define PM_FALL   (5 * 256)            // the middle closer than this: in
#define PM_BUMPR  5                    // a bumper's radius (px) ...
#define PM_KICK   640                  // ... and how hard it kicks
#define PM_BOUNCE 115                  // walls give back 45 %
#define PM_MOVSPD 80                   // moving pits: 1/256 px per step (31 px/s)
#define PM_LEVELS 15
#define PM_OWN    8                    // own levels from the phone
#define PM_ALL    (PM_LEVELS + PM_OWN)
#define PM_LIVES  3
#define PM_KEYS   3
#define PM_MOVERS 6
#define PM_NAME   12
#define PM_EDITOR 200                  // the level list's answer for "phone editor"

/* Tiles, one character each in a level (rows of 20, 13 rows):
   .  floor        #  wall          S  start         G  goal hole
   O  pit (trap)   i  ice           m  mud           *  bumper
   > < ^ v  arrows: push one way    c  candy         k  key
   D  door: opens when every key is taken            C  rune (checkpoint)
   H  pit moving left and right     U  pit moving up and down          */
enum { PT_FLOOR, PT_WALL, PT_START, PT_GOAL, PT_TRAP, PT_ICE, PT_MUD, PT_BUMP, PT_BR, PT_BL, PT_BU, PT_BD,
       PT_CANDY, PT_KEY, PT_DOOR, PT_CHECK, PT_MOVH, PT_MOVV, PT_OPEN };
static const char PM_CH[] = ".#SGOim*><^vckDCHU";
#define PF_TRACK  0x40                 // a moving pit runs over this tile
#define PF_LIT    0x80                 // the rune touched last
#define PTILE(v)  ((v) & 0x1F)
static const int8_t PM_DX[4] = { 1, -1, 0, 0 }, PM_DY[4] = { 0, 0, -1, 1 };   // the arrows > < ^ v

static const char *const PM_NAMES[PM_LEVELS] = {
  "FIRST ROLL", "CANDY LANE", "PIT PARADE", "THIN ICE", "MUDDY SWAMP", "BUMPER BALL", "ARROW ALLEY",
  "LOCKED CRYPT", "THE LONG WAY", "WALKING PITS", "ICE ARROWS", "PIT BRIDGE", "WITCH'S KEYS",
  "GRAVEYARD", "PUMPKIN KING" };

static const char PM_MAPS[PM_LEVELS][PM_H][PM_W + 1] = {
  { "####################",
    "#S.......#.........#",
    "#........#.........#",
    "#........#...c.....#",
    "#...c....#.........#",
    "#........#####.....#",
    "#..................#",
    "#......c...........#",
    "######......########",
    "#..........#.......#",
    "#....c.....#....G..#",
    "#..................#",
    "####################" },
  { "####################",
    "#S..c..c..c..c.....#",
    "#..................#",
    "###############....#",
    "#....c..c..c..c....#",
    "#..................#",
    "#....###############",
    "#..................#",
    "#..c..c..c..c..c...#",
    "###############....#",
    "#..................#",
    "#G.c..c..c..c......#",
    "####################" },
  { "####################",
    "#S.....O......O....#",
    "#..................#",
    "#...O.....O.....c..#",
    "#..........O.......#",
    "#..O...c.......O...#",
    "#......O...........#",
    "#..c.......O....O..#",
    "#....O.............#",
    "#.........O....c...#",
    "#...O.........O....#",
    "#.......O.......O.G#",
    "####################" },
  { "####################",
    "#S.....#...........#",
    "#......#..iiiiiii..#",
    "#..c...#..iiiiiii..#",
    "#......#..iiiOiii..#",
    "#iiiiiii..iiiiiii..#",
    "#iiiiiii..iiiiiic..#",
    "#iiOiiii..iiiiiii..#",
    "#iiiiiii...........#",
    "#......#########...#",
    "#..c...........O...#",
    "#..............O..G#",
    "####################" },
  { "####################",
    "#S..mmmmmmmmmmm....#",
    "#...mmmmmmmmmmm..c.#",
    "#...mmmmOmmmmmm....#",
    "#...mmmmmmmmmmm....#",
    "#c..mmmmmmmmmmm....#",
    "#...######mmmm######",
    "#.........mmmm.....#",
    "#..O......mmmm..O..#",
    "#.........mmmm.....#",
    "#####.....mmmm.....#",
    "#c........mmmm...G.#",
    "####################" },
  { "####################",
    "#S.................#",
    "#....*....*....*...#",
    "#..................#",
    "#..*....*....*....c#",
    "#..................#",
    "#....*....*....*...#",
    "#..................#",
    "#..*....*....*.....#",
    "#c.................#",
    "#....*....*....*...#",
    "#.............OOO.G#",
    "####################" },
  { "####################",
    "#S..>>>>>>>>>>>....#",
    "#################v.#",
    "#.c.............#v.#",
    "#.###########...#v.#",
    "#.#.........#.c.#v.#",
    "#.#..G..O...#...#v.#",
    "#.#.........#...#v.#",
    "#.#####.#####...#v.#",
    "#...............#v.#",
    "#.###############..#",
    "#.<<<<<<<<<<<<<<<<.#",
    "####################" },
  { "####################",
    "#S.....#.......k...#",
    "#......#...........#",
    "#..c...#....O......#",
    "#......#...........#",
    "#......####..####..#",
    "#..................#",
    "#....O.......O...c.#",
    "#..................#",
    "#######D############",
    "#..c...............#",
    "#.G.........O......#",
    "####################" },
  { "####################",
    "#S..O....#....O...c#",
    "#........#.........#",
    "#..O..O..#..O...O..#",
    "#........#.........#",
    "#.O..O.......O.....#",
    "#........#.........#",
    "#####.####...O..####",
    "#...........C......#",
    "#..O...O.......O...#",
    "#.c..O....O..O.....#",
    "#.......O.....O...G#",
    "####################" },
  { "####################",
    "#S.......#.........#",
    "#........#....c....#",
    "#####.####.........#",
    "#H........U........#",
    "#..................#",
    "#.................H#",
    "#..................#",
    "#H.................#",
    "#####.#######.######",
    "#........#........G#",
    "#.c......#.........#",
    "####################" },
  { "####################",
    "#S..iiiiiiiiiiii...#",
    "#...iiiiiiiiiiii...#",
    "#...iiOiiiiiOiii...#",
    "#...iiiiii>>>>>>...#",
    "#...iiiiiiiiiiii.c.#",
    "#..........O.......#",
    "###########.....####",
    "#c..vvv.........O..#",
    "#...vvv....O.......#",
    "#...iii.......O....#",
    "#G..iii.......*...C#",
    "####################" },
  { "####################",
    "#S.....OOOOOOO.....#",
    "#......OOOOOOO.....#",
    "#..*...........*...#",
    "#......OOOOOOO.....#",
    "#OOOOO.OOOOOOO.OOOO#",
    "#OOOOO.OOOOOOO.OOOO#",
    "#...........*......#",
    "#.OOOOOOOOOOOOOOO..#",
    "#.......c..........#",
    "#OOOOOOOOOOOOO..*..#",
    "#G.................#",
    "####################" },
  { "####################",
    "#S.........#k......#",
    "#..........#.......#",
    "#....O.....#...O...#",
    "#..........D.......#",
    "#..........#.....c.#",
    "#####..#####..######",
    "#k.................#",
    "#.......H..........#",
    "#..................#",
    "#####D#######..O...#",
    "#c...G......#......#",
    "####################" },
  { "####################",
    "#S...O.....mmmm....#",
    "#..........mmmm.O..#",
    "#..O...O...mmmm....#",
    "#..........mmmm..C.#",
    "#.....O....mmmm....#",
    "#.C........######..#",
    "#......U...........#",
    "#..O...........O...#",
    "#####..##########..#",
    "#.....c....H.......#",
    "#G.........O....c..#",
    "####################" },
  { "####################",
    "#S..O.....*....O..k#",
    "#.....iiiiiiii.....#",
    "#..O..iiiOiiii..O..#",
    "#.....iiiiiiii.....#",
    "###v######.#########",
    "#..v..C....mmmm....#",
    "#..v...O..Ummmm..H.#",
    "#..>>>>>...mmmm....#",
    "#############D######",
    "#c...*....O....*..c#",
    "#G.................#",
    "####################" } };

// ---------------- progress (flash) ----------------
struct PmSave { uint8_t ver, open; uint8_t stars[PM_ALL]; uint16_t best[PM_ALL]; };   // best in 1/10 s, 0 = never
static PmSave   pmSv;
static bool     pmSvRead = false;
static uint16_t pmOwnVer = 1;          // counts the levels saved from the phone: lists read them again
static uint8_t  pmOwnLast = 255;       // ... the slot saved last

static void pmSvLoad() {
  if (pmSvRead) return;
  pmSvRead = true;
  prefs.begin("arcade", true);
  bool ok = prefs.getBytes("PMZsv", &pmSv, sizeof(pmSv)) == sizeof(pmSv) && pmSv.ver == 1;
  prefs.end();
  if (!ok) { memset(&pmSv, 0, sizeof(pmSv)); pmSv.ver = 1; }
}
static void pmSvSave() { prefs.begin("arcade", false); prefs.putBytes("PMZsv", &pmSv, sizeof(pmSv)); prefs.end(); }
static uint16_t pmStarSum() { uint16_t n = 0; for (uint8_t i = 0; i < PM_LEVELS; i++) n += pmSv.stars[i]; return n; }

// ---------------- checking a level ----------------
static int8_t pmArrow(char c) { const char *p = strchr("><^v", c); return c && p ? (int8_t)(p - "><^v") : -1; }
static bool   pmPassable(char c, bool doorsOpen) { return c != '#' && c != 'O' && c != '*' && (c != 'D' || doorsOpen); }

/* The shortest way from the start to the goal in tiles, over everything
   but walls, pits and bumpers, doors only with every key; arrows are not
   entered or left against their direction. -1 = there is none. A state
   is a tile plus the keys taken so far.                               */
static int16_t pmPath(const char *map) {
  uint8_t keyAt[PM_N];
  uint8_t keys = 0;
  int16_t start = -1;
  for (int16_t i = 0; i < PM_N; i++) {
    keyAt[i] = 255;
    if (map[i] == 'k' && keys < PM_KEYS) keyAt[i] = keys++;
    if (map[i] == 'S') start = i;
  }
  if (start < 0) return -1;
  uint8_t full = (uint8_t)((1 << keys) - 1);
  uint16_t states = (uint16_t)(PM_N << keys);
  uint16_t *q = (uint16_t *)malloc(states * sizeof(uint16_t));
  uint8_t *seen = (uint8_t *)calloc((states + 7) / 8, 1);
  if (!q || !seen) { free(q); free(seen); return -1; }
  uint16_t head = 0, tail = 0;
  q[tail++] = (uint16_t)start;
  seen[start >> 3] |= (uint8_t)(1 << (start & 7));
  int16_t dist = -1;
  for (int16_t d = 0; head < tail && dist < 0; d++) {
    uint16_t end = tail;
    for (; head < end && dist < 0; head++) {
      uint16_t s = q[head], cell = s % PM_N, mask = s / PM_N;
      if (map[cell] == 'G') { dist = d; break; }
      int8_t from = pmArrow(map[cell]);
      for (uint8_t k = 0; k < 4; k++) {
        int16_t x = (int16_t)(cell % PM_W + PM_DX[k]), y = (int16_t)(cell / PM_W + PM_DY[k]);
        if (x < 0 || y < 0 || x >= PM_W || y >= PM_H) continue;
        uint16_t n = (uint16_t)(y * PM_W + x);
        int8_t to = pmArrow(map[n]);
        if ((from >= 0 && from == (k ^ 1)) || (to >= 0 && to == (k ^ 1))) continue;   // against an arrow
        if (!pmPassable(map[n], mask == full)) continue;
        uint16_t m = (uint16_t)(keyAt[n] != 255 ? mask | 1 << keyAt[n] : mask);
        uint16_t ns = (uint16_t)(m * PM_N + n);
        if (seen[ns >> 3] >> (ns & 7) & 1) continue;
        seen[ns >> 3] |= (uint8_t)(1 << (ns & 7));
        q[tail++] = ns;
      }
    }
  }
  free(q);
  free(seen);
  return dist;
}

// NULL = playable, else why not; dist = the shortest way in tiles
static const char *pmCheck(const char *map, int16_t *dist = NULL) {
  uint8_t s = 0, g = 0, k = 0, d = 0, m = 0;
  for (int16_t i = 0; i < PM_N; i++) {
    char c = map[i];
    s += c == 'S'; g += c == 'G'; k += c == 'k'; d += c == 'D'; m += c == 'H' || c == 'U';
  }
  if (s != 1) return "exactly one start (S) is needed";
  if (g != 1) return "exactly one goal (G) is needed";
  if (k > PM_KEYS) return "3 keys at most";
  if (d && !k) return "a door needs a key";
  if (m > PM_MOVERS) return "6 moving pits at most";
  int16_t p = pmPath(map);
  if (p < 0) return "the goal cannot be reached from the start";
  if (dist) *dist = p;
  return NULL;
}

// ---------------- own levels (flash "PMo0".."PMo7") ----------------
struct PmOwn { char name[PM_NAME + 1]; char map[PM_N]; };

static bool pmOwnLoad(uint8_t slot, PmOwn &o) {
  char k[8];
  snprintf(k, sizeof(k), "PMo%u", slot);
  prefs.begin("arcade", true);
  size_t n = prefs.getBytes(k, &o, sizeof(o));
  prefs.end();
  return n == sizeof(o) && !o.name[PM_NAME] && o.name[0];
}

// "NAME|row|row|..." -> o; NULL = fine, else why not
static const char *pmParse(const char *t, PmOwn &o) {
  memset(&o, 0, sizeof(o));
  memset(o.map, '.', sizeof(o.map));
  const char *bar = strchr(t, '|');
  if (!bar) return "NAME|row|row|... expected";
  while (*t == ' ') t++;
  uint8_t n = 0;
  for (; t < bar; t++) {
    char c = *t;
    if (c < 32 || c > 126 || c == '"' || c == '\\') return "the name: letters, digits, spaces";
    if (n >= PM_NAME) return "the name: 12 characters at most";
    o.name[n++] = c;
  }
  while (n && o.name[n - 1] == ' ') o.name[--n] = 0;
  if (!n) return "the level needs a name";
  uint8_t x = 0, y = 0;
  for (const char *p = bar + 1; *p; p++) {
    if (*p == '\r' || *p == '\n') continue;
    if (*p == '|') { if (++y >= PM_H) return "13 rows at most"; x = 0; continue; }
    if (x >= PM_W) return "20 tiles a row at most";
    if (!strchr(PM_CH, *p)) return "unknown tile in the level";
    o.map[y * PM_W + x++] = *p;
  }
  return pmCheck(o.map);
}

// ---------------- the game state (on the heap while it runs) ----------------
enum { PS_ROLL, PS_FALL, PS_GOAL, PS_BACK, PS_WON, PS_LOST };

struct PmMover { int32_t a, b, fix, ph; uint8_t vert; };     // a..b along x (or y), fix = the other axis (1/256 px)
struct PmFlash { uint8_t x, y, t; };                         // a bumper lit up

struct PmState {
  uint8_t  g[PM_H][PM_W];              // PT_... and PF_...
  char     name[PM_NAME + 1];
  uint8_t  lv, lives, lost, keys, keysAll, candy, candyAll, st, nMov, fallMov;
  int32_t  x, y, vx, vy;               // the pumpkin: 1/256 px from the board's corner, 1/256 px a step
  int32_t  sx, sy, hx, hy;             // where it starts again; the hole it falls into
  uint16_t stT, par;                   // steps in this state; par in 1/10 s
  uint32_t steps;                      // the clock: steps played
  float    stem[3], face[3];           // the pumpkin's turn: where its stem and its face point (z = up)
  PmMover  mov[PM_MOVERS];
  PmFlash  flash[4];
  uint32_t redraw[PM_H], drawn[PM_H];  // a bit per tile: draw it again / something was drawn over it
  bool     full;                       // the whole board again (after a box or the pause menu)
  uint16_t anim;                       // the animation step of the board (8 a second)
  uint16_t bumps, walls;               // the tests: bumper kicks, hard knocks
  uint8_t  lut[5][256];                // fog towards the edges (four steps) and the shadow
};
static PmState *pm = NULL;

static uint32_t pmSqrt(uint32_t v) {
  uint32_t r = 0, b = 1u << 30;
  while (b > v) b >>= 2;
  while (b) {
    if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1;
    b >>= 2;
  }
  return r;
}

static inline uint8_t pmAt(int16_t tx, int16_t ty) {
  return (tx < 0 || ty < 0 || tx >= PM_W || ty >= PM_H) ? PT_WALL : PTILE(pm->g[ty][tx]);
}
static inline void pmMark(int16_t tx, int16_t ty) { if (tx >= 0 && ty >= 0 && tx < PM_W && ty < PM_H) pm->redraw[ty] |= 1u << tx; }
static inline int32_t pmMid(int16_t t) { return ((int32_t)t * PM_T + PM_T / 2) << 8; }   // a tile's middle, 1/256 px

// the level's map (a built-in one, or own slot lv - PM_LEVELS); false = none there
static bool pmMap(uint8_t lv, char *map, char *name) {
  if (lv < PM_LEVELS) {
    for (uint8_t y = 0; y < PM_H; y++) memcpy(map + y * PM_W, PM_MAPS[lv][y], PM_W);
    snprintf(name, PM_NAME + 1, "%s", PM_NAMES[lv]);
    return true;
  }
  PmOwn o;
  if (lv - PM_LEVELS >= PM_OWN || !pmOwnLoad((uint8_t)(lv - PM_LEVELS), o)) return false;
  memcpy(map, o.map, PM_N);
  memcpy(name, o.name, PM_NAME + 1);
  return true;
}

static bool pmTrackable(uint8_t t) { return t != PT_WALL && t != PT_DOOR && t != PT_BUMP && t != PT_GOAL && t != PT_TRAP; }

static bool pmLoad(uint8_t lv) {
  PmState &s = *pm;
  char map[PM_N];
  if (!pmMap(lv, map, s.name)) return false;
  int16_t dist = 0;
  if (pmCheck(map, &dist)) return false;
  s.lv = lv;
  s.keys = s.keysAll = s.candy = s.candyAll = s.nMov = s.lost = 0;
  s.lives = PM_LIVES;
  s.st = PS_ROLL; s.stT = 0; s.steps = 0; s.vx = s.vy = 0;
  s.bumps = s.walls = 0;
  memset(s.flash, 0, sizeof(s.flash));
  for (int16_t i = 0; i < PM_N; i++) {
    const char *p = strchr(PM_CH, map[i]);
    uint8_t t = (uint8_t)(p ? (int)(p - PM_CH) : (int)PT_FLOOR);
    s.g[i / PM_W][i % PM_W] = t;
    if (t == PT_START) { s.x = s.sx = pmMid((int16_t)(i % PM_W)); s.y = s.sy = pmMid((int16_t)(i / PM_W)); }
    if (t == PT_KEY) s.keysAll++;
    if (t == PT_CANDY) s.candyAll++;
  }
  for (int16_t i = 0; i < PM_N && s.nMov < PM_MOVERS; i++) {    // moving pits: their tracks
    int16_t tx = (int16_t)(i % PM_W), ty = (int16_t)(i / PM_W);
    uint8_t t = s.g[ty][tx];
    if (t != PT_MOVH && t != PT_MOVV) continue;
    bool v = t == PT_MOVV;
    int16_t dx = v ? 0 : 1, dy = v ? 1 : 0, lo = 0, hi = 0;
    while (pmTrackable(pmAt((int16_t)(tx - dx * (lo + 1)), (int16_t)(ty - dy * (lo + 1))))) lo++;
    while (pmTrackable(pmAt((int16_t)(tx + dx * (hi + 1)), (int16_t)(ty + dy * (hi + 1))))) hi++;
    for (int16_t k = -lo; k <= hi; k++) s.g[ty + dy * k][tx + dx * k] |= PF_TRACK;
    PmMover &m = s.mov[s.nMov++];
    m.vert = v;
    m.a = pmMid((int16_t)((v ? ty : tx) - lo));
    m.b = pmMid((int16_t)((v ? ty : tx) + hi));
    m.fix = pmMid(v ? tx : ty);
    m.ph = (int32_t)lo * PM_T * 256;
    s.g[ty][tx] = (uint8_t)(PT_FLOOR | PF_TRACK);
  }
  s.par = (uint16_t)(20 + dist * 3 + s.nMov * 40);              // 2 s, 0.3 s a tile, 4 s a moving pit
  s.stem[0] = 0; s.stem[1] = -0.5f; s.stem[2] = 0.866f;         // the stem up and a little back,
  s.face[0] = 0; s.face[1] = 0.866f; s.face[2] = 0.5f;          // the face looking at the player
  s.full = true;
  return true;
}

// where a moving pit is now (1/256 px)
static void pmMoverAt(uint8_t i, int32_t &x, int32_t &y) {
  const PmMover &m = pm->mov[i];
  int32_t L = m.b - m.a, p = m.a;
  if (L > 0) {
    int32_t u = (int32_t)(((int64_t)pm->steps * PM_MOVSPD + m.ph) % (2 * L));
    p = m.a + (u <= L ? u : 2 * L - u);
  }
  if (m.vert) { x = m.fix; y = p; } else { x = p; y = m.fix; }
}

// ---------------- physics ----------------
// v turned round the axis (ax, ay, 0) by an angle with this cosine and sine
static void pmTurn(float *v, float ax, float ay, float c, float sn) {
  float kv = ax * v[0] + ay * v[1];
  float cx = ay * v[2], cy = -ax * v[2], cz = ax * v[1] - ay * v[0];
  for (uint8_t i = 0; i < 3; i++) {
    float k = i == 0 ? ax : (i == 1 ? ay : 0), cr = i == 0 ? cx : (i == 1 ? cy : cz);
    v[i] = v[i] * c + cr * sn + k * kv * (1 - c);
  }
}
// the pumpkin rolls: it turns round the axis across its way
static void pmRoll() {
  PmState &s = *pm;
  float d = sqrtf((float)s.vx * s.vx + (float)s.vy * s.vy);
  if (d < 1) return;
  float a = d / (256.0f * PM_R), ax = -s.vy / d, ay = s.vx / d, c = cosf(a), sn = sinf(a);
  pmTurn(s.stem, ax, ay, c, sn);
  pmTurn(s.face, ax, ay, c, sn);
  float k = s.stem[0] * s.face[0] + s.stem[1] * s.face[1] + s.stem[2] * s.face[2];   // keep both upright to each other
  for (uint8_t i = 0; i < 3; i++) s.face[i] -= k * s.stem[i];
  float ls = sqrtf(s.stem[0] * s.stem[0] + s.stem[1] * s.stem[1] + s.stem[2] * s.stem[2]);
  float lf = sqrtf(s.face[0] * s.face[0] + s.face[1] * s.face[1] + s.face[2] * s.face[2]);
  for (uint8_t i = 0; i < 3; i++) { s.stem[i] /= ls; s.face[i] /= lf; }
}

static void pmFx(int32_t x, int32_t y, uint8_t n, col c0, col c1, int16_t speed, uint16_t life, int8_t grav = 0, uint8_t size = 2) {
  fxBurst((int16_t)(x >> 8), (int16_t)((y >> 8) + PM_Y0), n, c0, c1, speed, life, grav, size);
}

static void pmOpenDoors() {
  PmState &s = *pm;
  for (int16_t ty = 0; ty < PM_H; ty++)
    for (int16_t tx = 0; tx < PM_W; tx++)
      if (PTILE(s.g[ty][tx]) == PT_DOOR) {
        s.g[ty][tx] = PT_OPEN;
        pmMark(tx, ty);
        pmFx(pmMid(tx), pmMid(ty), 10, rgb(204, 128, 51), rgb(51, 25, 0), 70, 600, 20);
      }
  sfx(700, 160);
}

// walls and closed doors: the pumpkin pushed out, bounced off
static void pmWalls() {
  PmState &s = *pm;
  const int32_t R = PM_R * 256;
  int16_t cx = (int16_t)(s.x >> 12), cy = (int16_t)(s.y >> 12);
  for (int16_t ty = (int16_t)(cy - 1); ty <= cy + 1; ty++)
    for (int16_t tx = (int16_t)(cx - 1); tx <= cx + 1; tx++) {
      uint8_t t = pmAt(tx, ty);
      if (t != PT_WALL && t != PT_DOOR) continue;
      int32_t x0 = (int32_t)tx * PM_T * 256, y0 = (int32_t)ty * PM_T * 256;
      int32_t px = s.x < x0 ? x0 : (s.x > x0 + PM_T * 256 ? x0 + PM_T * 256 : s.x);
      int32_t py = s.y < y0 ? y0 : (s.y > y0 + PM_T * 256 ? y0 + PM_T * 256 : s.y);
      int32_t dx = s.x - px, dy = s.y - py;
      if (dx >= R || dx <= -R || dy >= R || dy <= -R) continue;
      int32_t d2 = dx * dx + dy * dy;
      if (d2 >= R * R) continue;
      int32_t d = (int32_t)pmSqrt((uint32_t)d2);
      if (d < 8) continue;                                        // the middle inside a wall: cannot happen at these speeds
      int32_t pen = R - d;
      s.x += dx * pen / d; s.y += dy * pen / d;
      int32_t vn = (s.vx * dx + s.vy * dy) / d;
      if (vn >= 0) continue;
      s.vx -= vn * dx / d * (256 + PM_BOUNCE) / 256;
      s.vy -= vn * dy / d * (256 + PM_BOUNCE) / 256;
      if (vn < -260) { s.walls++; sfx((uint16_t)(140 - vn / 16), 25); }
    }
}

// bumpers: pushed out and kicked away
static void pmBumpers() {
  PmState &s = *pm;
  const int32_t R = (PM_R + PM_BUMPR) * 256;
  int16_t cx = (int16_t)(s.x >> 12), cy = (int16_t)(s.y >> 12);
  for (int16_t ty = (int16_t)(cy - 1); ty <= cy + 1; ty++)
    for (int16_t tx = (int16_t)(cx - 1); tx <= cx + 1; tx++) {
      if (pmAt(tx, ty) != PT_BUMP) continue;
      int32_t dx = s.x - pmMid(tx), dy = s.y - pmMid(ty);
      if (dx >= R || dx <= -R || dy >= R || dy <= -R) continue;
      int32_t d2 = dx * dx + dy * dy;
      if (d2 >= R * R) continue;
      int32_t d = (int32_t)pmSqrt((uint32_t)d2);
      if (d < 8) { dx = 0; dy = -256; d = 256; }
      int32_t pen = R - d;
      s.x += dx * pen / d; s.y += dy * pen / d;
      int32_t vn = (s.vx * dx + s.vy * dy) / d;
      if (vn >= PM_KICK) continue;
      int32_t out = -vn > PM_KICK ? -vn : PM_KICK;
      s.vx += (out - vn) * dx / d;
      s.vy += (out - vn) * dy / d;
      s.bumps++;
      uint8_t k = 0;
      for (uint8_t i = 1; i < 4; i++) if (s.flash[i].t < s.flash[k].t) k = i;
      s.flash[k] = { (uint8_t)tx, (uint8_t)ty, 18 };
      pmMark(tx, ty);
      int32_t hx = pmMid(tx) + dx * PM_BUMPR / (PM_R + PM_BUMPR), hy = pmMid(ty) + dy * PM_BUMPR / (PM_R + PM_BUMPR);
      pmFx(hx, hy, 8, C::WHITE, rgb(153, 51, 255), 120, 350);
      sfx(1300, 30);
    }
}

// holes near the pumpkin: the rim pulls, the middle swallows
static void pmHoles() {
  PmState &s = *pm;
  int16_t cx = (int16_t)(s.x >> 12), cy = (int16_t)(s.y >> 12);
  for (int16_t k = -9; k < (int16_t)s.nMov; k++) {
    int32_t hx, hy;
    uint8_t t = PT_TRAP;
    if (k < 0) {                                                // the 3 x 3 tiles round it
      int16_t tx = (int16_t)(cx + (k + 9) % 3 - 1), ty = (int16_t)(cy + (k + 9) / 3 - 1);
      t = pmAt(tx, ty);
      if (t != PT_GOAL && t != PT_TRAP) continue;
      hx = pmMid(tx); hy = pmMid(ty);
    } else pmMoverAt((uint8_t)k, hx, hy);
    int32_t dx = hx - s.x, dy = hy - s.y;
    if (dx >= PM_RIM || dx <= -PM_RIM || dy >= PM_RIM || dy <= -PM_RIM) continue;
    int32_t d = (int32_t)pmSqrt((uint32_t)(dx * dx + dy * dy));
    if (d >= PM_RIM) continue;
    if (d < PM_FALL) {
      s.st = t == PT_GOAL ? PS_GOAL : PS_FALL;
      s.stT = 0; s.hx = hx; s.hy = hy;
      s.fallMov = (uint8_t)(k < 0 ? 255 : k);
      sfx(t == PT_GOAL ? 1200 : 300, 200);
      return;
    }
    s.vx += dx * PM_PULL / d;
    s.vy += dy * PM_PULL / d;
  }
}

// what lies on the tile under the pumpkin's middle
static void pmPickup() {
  PmState &s = *pm;
  int16_t tx = (int16_t)(s.x >> 12), ty = (int16_t)(s.y >> 12);
  uint8_t t = pmAt(tx, ty);
  if (t == PT_CANDY) {
    s.g[ty][tx] = (uint8_t)(PT_FLOOR | (s.g[ty][tx] & PF_TRACK));
    s.candy++;
    pmMark(tx, ty);
    pmFx(pmMid(tx), pmMid(ty), 10, C::PINK, C::ORANGE, 90, 450, 15);
    fxText((int16_t)(tx * PM_T + 8), (int16_t)(ty * PM_T + PM_Y0 - 8), "+1", C::YELLOW);
    sfx(1700, 40);
  } else if (t == PT_KEY) {
    s.g[ty][tx] = (uint8_t)(PT_FLOOR | (s.g[ty][tx] & PF_TRACK));
    s.keys++;
    pmMark(tx, ty);
    fxRing((int16_t)(tx * PM_T + 8), (int16_t)(ty * PM_T + PM_Y0 + 8), 16, C::WHITE, C::YELLOW, 400);
    pmFx(pmMid(tx), pmMid(ty), 12, C::YELLOW, C::ORANGE, 110, 500);
    fxText((int16_t)(tx * PM_T + 8), (int16_t)(ty * PM_T + PM_Y0 - 8), "KEY", C::YELLOW);
    sfx(1900, 80);
    if (s.keys >= s.keysAll) pmOpenDoors();
  } else if (t == PT_CHECK && !(s.g[ty][tx] & PF_LIT)) {
    for (int16_t y = 0; y < PM_H; y++)
      for (int16_t x = 0; x < PM_W; x++)
        if (s.g[y][x] & PF_LIT) { s.g[y][x] &= (uint8_t)~PF_LIT; pmMark(x, y); }
    s.g[ty][tx] |= PF_LIT;
    s.sx = pmMid(tx); s.sy = pmMid(ty);
    pmMark(tx, ty);
    fxRing((int16_t)(tx * PM_T + 8), (int16_t)(ty * PM_T + PM_Y0 + 8), 14, C::WHITE, C::LIME, 450);
    fxText((int16_t)(tx * PM_T + 8), (int16_t)(ty * PM_T + PM_Y0 - 8), "SAVED", C::LIME);
    sfx(1100, 90);
  }
}

static int32_t pmClampV(int32_t v) { return v > PM_VMAX ? PM_VMAX : (v < -PM_VMAX ? -PM_VMAX : v); }

/* One step of 10 ms; ax, ay = -256..256, the tilt (or the keys). */
static void pmStep(int16_t ax, int16_t ay) {
  PmState &s = *pm;
  for (uint8_t i = 0; i < 4; i++) if (s.flash[i].t && !--s.flash[i].t) pmMark(s.flash[i].x, s.flash[i].y);   // lit for the last time
  if (s.st == PS_WON || s.st == PS_LOST) return;
  if (s.st != PS_BACK) s.steps++;
  if (s.st == PS_FALL || s.st == PS_GOAL) {                     // rolling into the hole's middle
    if (s.fallMov != 255) pmMoverAt(s.fallMov, s.hx, s.hy);
    s.x += (s.hx - s.x) / 5; s.y += (s.hy - s.y) / 5;
    if (++s.stT == 12 && s.st == PS_FALL) pmFx(s.hx, s.hy, 6, rgb(102, 102, 153), C::BLACK, 30, 900, -12, 5);   // a puff of the pit's breath
    if (s.stT < 45) return;
    if (s.st == PS_GOAL) { s.st = PS_WON; return; }
    s.lost++;
    if (!--s.lives) { s.st = PS_LOST; return; }
    s.x = s.sx; s.y = s.sy; s.vx = s.vy = 0;                    // again from the start or the rune
    s.st = PS_BACK; s.stT = 0;
    return;
  }
  if (s.st == PS_BACK) { if (++s.stT >= 30) s.st = PS_ROLL; return; }
  int16_t tx = (int16_t)(s.x >> 12), ty = (int16_t)(s.y >> 12);
  uint8_t t = pmAt(tx, ty);
  int32_t acc = PM_ACC, fr = 48;                                // floor: about 12 % of the speed left after a second
  if (t == PT_ICE) { acc = PM_ACC * 6 / 10; fr = 900; }          // little grip, hardly slows down
  if (t == PT_MUD) fr = 10;                                     // a crawl
  s.vx += ax * acc / 256;
  s.vy += ay * acc / 256;
  if (t >= PT_BR && t <= PT_BD) {                               // an arrow: pushed along, guided
    uint8_t d = (uint8_t)(t - PT_BR);
    s.vx += PM_DX[d] * PM_BOOST; s.vy += PM_DY[d] * PM_BOOST;
    if (PM_DX[d]) s.vy -= s.vy / 16; else s.vx -= s.vx / 16;
  }
  s.vx -= s.vx / fr; s.vy -= s.vy / fr;
  if (!ax && s.vx > -3 && s.vx < 3) s.vx = 0;                   // comes to rest
  if (!ay && s.vy > -3 && s.vy < 3) s.vy = 0;
  pmHoles();
  if (s.st != PS_ROLL) return;
  s.vx = pmClampV(s.vx); s.vy = pmClampV(s.vy);
  s.x += s.vx; s.y += s.vy;
  pmWalls();
  pmBumpers();
  pmWalls();                                                    // a kick into a corner
  pmRoll();
  pmPickup();
}

// ---------------- drawing the board ----------------
static uint32_t pmHash(int16_t x, int16_t y) {
  uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
  h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
  return h;
}

static const col PM_WOOD[3] = { rgb(102, 42, 0), rgb(102, 42, 51), rgb(102, 42, 0) };
static const col PM_FOG = rgb(25, 0, 40);

static void pmLuts() {
  static const uint8_t T[4] = { 50, 95, 140, 185 };
  for (uint16_t c = 0; c < 256; c++) {
    for (uint8_t k = 0; k < 4; k++) pm->lut[k][c] = fxLerp((col)c, PM_FOG, T[k]);
    pm->lut[4][c] = fxLerp((col)c, C::BLACK, 120);
  }
}

// a box of the picture through a table (the shadows)
static void pmShade(int16_t x, int16_t y, int16_t w, int16_t h, const uint8_t *lut) {
  uint8_t *fb = gfx.buffer();
  for (int16_t j = y; j < y + h; j++)
    for (int16_t i = x; i < x + w; i++)
      if ((uint16_t)i < SCR_W && (uint16_t)j < SCR_H) fb[j * SCR_W + i] = lut[fb[j * SCR_W + i]];
}

// wooden planks, two to a tile, their joints staggered row by row; walls cast a shadow
static void pmFloor(int16_t px, int16_t py, int16_t tx, int16_t ty) {
  for (uint8_t k = 0; k < 2; k++) {
    int16_t row = (int16_t)(ty * 2 + k), y = (int16_t)(py + k * 8), off = (int16_t)(row * 21 % 48);
    int16_t gx = (int16_t)(tx * PM_T), id = (int16_t)((gx + off) / 48), joint = (int16_t)(48 * (id + 1) - off - gx);
    for (uint8_t part = 0; part < 2; part++) {
      int16_t a = part ? joint : 0, b = part ? PM_T : (joint < PM_T ? joint : PM_T);
      if (b <= a) continue;
      uint32_t h = pmHash(row, (int16_t)(id + part));
      col c = PM_WOOD[h % 3];
      gfx.fill((int16_t)(px + a), y, (int16_t)(b - a), 7, c);
      col gr = colDark(c);                                      // grain
      for (uint8_t q = 0; q < 2; q++) {
        int16_t gxx = (int16_t)(((h >> (8 + q * 6)) + tx * 5) % 16), gy = (int16_t)(2 + (h >> (14 + q * 4)) % 4);
        if (gxx >= a && gxx < b) gfx.hline((int16_t)(px + gxx), (int16_t)(y + gy), (int16_t)(gxx + 5 <= b ? 5 : b - gxx), gr);
      }
    }
    gfx.hline(px, (int16_t)(y + 7), PM_T, rgb(51, 0, 0));        // the gap between planks
    if (joint < PM_T) { gfx.vline((int16_t)(px + joint), y, 7, rgb(51, 0, 0)); gfx.pixel((int16_t)(px + joint + 2), (int16_t)(y + 3), GREY2); }
  }
  // the light falls from the top left: a wall above or to the left casts a shadow
  bool up = pmAt(tx, (int16_t)(ty - 1)) == PT_WALL || pmAt(tx, (int16_t)(ty - 1)) == PT_DOOR;
  bool left = pmAt((int16_t)(tx - 1), ty) == PT_WALL || pmAt((int16_t)(tx - 1), ty) == PT_DOOR;
  bool diag = pmAt((int16_t)(tx - 1), (int16_t)(ty - 1)) == PT_WALL;
  if (up) pmShade(px, py, PM_T, 4, pm->lut[4]);
  if (left) pmShade(px, (int16_t)(py + (up ? 4 : 0)), 4, (int16_t)(PM_T - (up ? 4 : 0)), pm->lut[4]);
  if (diag && !up && !left) pmShade(px, py, 4, 4, pm->lut[4]);
}

// stone blocks; their faces lit where no other wall is next to them
static void pmWall(int16_t px, int16_t py, int16_t tx, int16_t ty) {
  uint32_t h = pmHash(tx, ty);
  const col base = (h & 3) ? rgb(102, 85, 102) : rgb(102, 85, 85), mortar = rgb(51, 42, 51);
  gfx.fill(px, py, PM_T, PM_T, base);
  int16_t j = (int16_t)((ty & 1) ? 8 : 4);
  gfx.hline(px, (int16_t)(py + 7), PM_T, mortar);
  gfx.hline(px, (int16_t)(py + 15), PM_T, mortar);
  gfx.vline((int16_t)(px + j), py, 7, mortar);
  gfx.vline((int16_t)(px + (j + 8) % 16), (int16_t)(py + 8), 7, mortar);
  for (uint8_t q = 0; q < 3; q++) gfx.pixel((int16_t)(px + (h >> (4 + q * 5)) % 16), (int16_t)(py + (h >> (7 + q * 5)) % 16), colLight(base));
  if ((h >> 20) % 5 == 0) { gfx.pixel((int16_t)(px + 3), (int16_t)(py + 14), rgb(51, 127, 51)); gfx.pixel((int16_t)(px + 4), (int16_t)(py + 13), rgb(51, 85, 0)); }   // moss
  auto wallAt = [](int16_t x, int16_t y) { uint8_t t = pmAt(x, y); return t == PT_WALL || t == PT_DOOR; };
  if (!wallAt(tx, (int16_t)(ty - 1))) { gfx.hline(px, py, PM_T, rgb(204, 170, 204)); gfx.hline(px, (int16_t)(py + 1), PM_T, rgb(153, 127, 153)); }
  if (!wallAt((int16_t)(tx - 1), ty)) gfx.vline(px, py, PM_T, rgb(153, 127, 153));
  if (!wallAt(tx, (int16_t)(ty + 1))) { gfx.hline(px, (int16_t)(py + 15), PM_T, rgb(25, 0, 25)); gfx.hline(px, (int16_t)(py + 14), PM_T, rgb(51, 42, 51)); }
  if (!wallAt((int16_t)(tx + 1), ty)) gfx.vline((int16_t)(px + 15), py, PM_T, rgb(25, 0, 25));
}

// a hole with depth: a dark rim, the lit far wall, black inside; kind 0 pit, 1 goal, 2 moving pit
static void pmHole(int16_t cx, int16_t cy, uint8_t kind, uint16_t anim) {
  if (kind == 1) {                                              // the goal glows
    static const col G[4] = { rgb(51, 170, 51), rgb(102, 212, 51), rgb(153, 255, 102), rgb(102, 212, 51) };
    gfx.disc(cx, cy, 8, colDark(G[anim % 4]));
    gfx.disc(cx, cy, 7, G[anim % 4]);
    gfx.disc(cx, cy, 5, rgb(0, 85, 0));
    gfx.disc((int16_t)(cx - 1), (int16_t)(cy - 1), 4, rgb(0, 42, 0));
    gfx.disc((int16_t)(cx - 1), (int16_t)(cy - 1), 2, C::BLACK);
    uint8_t a = (uint8_t)(anim * 3);
    gfx.pixel((int16_t)(cx + fxCos(a) * 7 / 64), (int16_t)(cy + fxSin(a) * 7 / 64), C::WHITE);
    gfx.pixel((int16_t)(cx - fxCos(a) * 7 / 64), (int16_t)(cy - fxSin(a) * 7 / 64), C::YELLOW);
    return;
  }
  gfx.disc(cx, cy, 7, kind == 2 ? rgb(102, 42, 153) : rgb(51, 25, 0));
  gfx.disc(cx, cy, 6, kind == 2 ? rgb(51, 0, 102) : rgb(85, 42, 42));
  gfx.disc((int16_t)(cx - 1), (int16_t)(cy - 1), 5, rgb(25, 0, 25));
  gfx.disc((int16_t)(cx - 1), (int16_t)(cy - 1), 3, C::BLACK);
  if (kind == 0 && (pmHash(cx, cy) & 3) == 0) {                 // something looks out of some pits
    gfx.pixel((int16_t)(cx - 3), (int16_t)(cy - 1), rgb(204, 0, 0));
    gfx.pixel((int16_t)(cx), (int16_t)(cy - 1), rgb(204, 0, 0));
  }
}

static void pmCandy(int16_t cx, int16_t cy, uint32_t h) {
  static const col K[4] = { C::PINK, C::LIME, C::CYAN, C::ORANGE };
  col c = K[h % 4];
  gfx.triangle((int16_t)(cx - 7), (int16_t)(cy - 3), (int16_t)(cx - 7), (int16_t)(cy + 3), (int16_t)(cx - 3), cy, colDark(c));
  gfx.triangle((int16_t)(cx + 7), (int16_t)(cy - 3), (int16_t)(cx + 7), (int16_t)(cy + 3), (int16_t)(cx + 3), cy, colDark(c));
  gfx.disc(cx, cy, 4, colDark(c));
  gfx.disc(cx, cy, 3, c);
  gfx.line((int16_t)(cx - 2), (int16_t)(cy + 2), (int16_t)(cx + 2), (int16_t)(cy - 2), C::WHITE);
}

static void pmKey(int16_t cx, int16_t cy, bool shine) {
  const col gold = rgb(255, 212, 0), dk = rgb(153, 85, 0);
  gfx.circle((int16_t)(cx - 4), cy, 3, dk);
  gfx.circle((int16_t)(cx - 4), cy, 2, gold);
  gfx.fill((int16_t)(cx - 1), (int16_t)(cy - 1), 8, 3, dk);
  gfx.hline((int16_t)(cx - 1), cy, 7, gold);
  gfx.fill((int16_t)(cx + 3), (int16_t)(cy + 1), 2, 3, gold);
  gfx.fill((int16_t)(cx + 6), (int16_t)(cy + 1), 1, 2, gold);
  if (shine) fxStar((int16_t)(cx - 5), (int16_t)(cy - 3), 2, C::WHITE);
}

// one tile as it is now, fog included
static void pmTile(int16_t tx, int16_t ty) {
  PmState &s = *pm;
  int16_t px = (int16_t)(tx * PM_T), py = (int16_t)(ty * PM_T + PM_Y0), cx = (int16_t)(px + 8), cy = (int16_t)(py + 8);
  uint8_t v = s.g[ty][tx], t = PTILE(v);
  uint16_t an = s.anim;
  // what lies under a thing: ice or mud when most of its neighbours are, else planks
  uint8_t ground = t;
  if (t != PT_WALL && t != PT_ICE && t != PT_MUD && t != PT_DOOR && !(t >= PT_BR && t <= PT_BD)) {
    uint8_t ice = 0, mud = 0;
    for (uint8_t k = 0; k < 4; k++) {
      uint8_t n = pmAt((int16_t)(tx + PM_DX[k]), (int16_t)(ty + PM_DY[k]));
      ice += n == PT_ICE; mud += n == PT_MUD;
    }
    ground = ice >= 2 ? PT_ICE : (mud >= 2 ? PT_MUD : PT_FLOOR);
  }
  if (t == PT_WALL) pmWall(px, py, tx, ty);
  else if (ground == PT_ICE) {
    gfx.fill(px, py, PM_T, PM_T, rgb(153, 212, 255));
    uint32_t h = pmHash(tx, ty);
    for (int8_t q = 0; q < 2; q++) {
      int16_t o = (int16_t)((h >> (q * 4)) % 12);
      gfx.line((int16_t)(px + o), (int16_t)(py + 15), (int16_t)(px + o + 4), (int16_t)(py + 11), C::WHITE);
    }
    gfx.line((int16_t)(px + (h >> 9) % 10), (int16_t)(py + 3), (int16_t)(px + (h >> 9) % 10 + 5), (int16_t)(py + 7), rgb(102, 170, 204));
    if (pmAt(tx, (int16_t)(ty - 1)) == PT_WALL) pmShade(px, py, PM_T, 3, pm->lut[4]);
  } else if (ground == PT_MUD) {
    gfx.fill(px, py, PM_T, PM_T, rgb(51, 42, 0));
    uint32_t h = pmHash(tx, ty);
    for (uint8_t q = 0; q < 4; q++)
      gfx.disc((int16_t)(px + 2 + (h >> (q * 6)) % 12), (int16_t)(py + 2 + (h >> (q * 6 + 3)) % 12), (int16_t)(1 + (h >> (q + 24)) % 2), rgb(102, 85, 0));
    uint8_t b = (uint8_t)((an + h) % 16);
    if (b < 3) gfx.circle((int16_t)(px + 4 + h % 8), (int16_t)(py + 4 + (h >> 3) % 8), b, rgb(153, 127, 51));   // a bubble
  } else if (t == PT_DOOR) {
    gfx.fill(px, py, PM_T, PM_T, rgb(153, 85, 51));
    for (int16_t x = 3; x < PM_T; x += 4) gfx.vline((int16_t)(px + x), py, PM_T, rgb(102, 42, 0));
    gfx.fill(px, (int16_t)(py + 3), PM_T, 2, GREY2);
    gfx.fill(px, (int16_t)(py + 11), PM_T, 2, GREY2);
    gfx.frame(px, py, PM_T, PM_T, rgb(51, 25, 0));
    gfx.fill((int16_t)(cx - 3), (int16_t)(cy - 1), 6, 5, C::YELLOW);   // the lock
    gfx.circle(cx, (int16_t)(cy - 2), 2, rgb(204, 170, 0));
    gfx.pixel(cx, (int16_t)(cy + 1), C::BLACK);
  } else if (t != PT_DOOR) pmFloor(px, py, tx, ty);
  if (t != PT_WALL && t != PT_ICE && t != PT_MUD && t != PT_DOOR) {
    if ((v & PF_TRACK) && ground == PT_FLOOR) {                                         // a groove where a pit wanders
      bool vert = false;
      for (uint8_t i = 0; i < s.nMov; i++) {
        const PmMover &m = s.mov[i];
        int32_t c = m.vert ? pmMid(tx) : pmMid(ty), p = m.vert ? pmMid(ty) : pmMid(tx);
        if (c == m.fix && p >= m.a && p <= m.b) { vert = m.vert; break; }
      }
      if (vert) { gfx.vline((int16_t)(cx - 1), py, PM_T, rgb(51, 0, 0)); gfx.vline(cx, py, PM_T, rgb(25, 0, 25)); gfx.vline((int16_t)(cx + 1), py, PM_T, colLight(PM_WOOD[0])); }
      else { gfx.hline(px, (int16_t)(cy - 1), PM_T, rgb(51, 0, 0)); gfx.hline(px, cy, PM_T, rgb(25, 0, 25)); gfx.hline(px, (int16_t)(cy + 1), PM_T, colLight(PM_WOOD[0])); }
    }
    switch (t) {
    case PT_START:                                              // a ring of straw
      for (uint8_t a = 0; a < 32; a += 2) gfx.pixel((int16_t)(cx + fxCos(a) * 6 / 64), (int16_t)(cy + fxSin(a) * 6 / 64), rgb(204, 170, 51));
      break;
    case PT_GOAL: pmHole(cx, cy, 1, an); break;
    case PT_TRAP: pmHole(cx, cy, 0, an); break;
    case PT_BUMP: {
      bool lit = false;
      for (uint8_t i = 0; i < 4; i++) if (s.flash[i].t && s.flash[i].x == tx && s.flash[i].y == ty) lit = true;
      gfx.disc((int16_t)(cx + 1), (int16_t)(cy + 2), 6, rgb(25, 0, 25));            // its shadow
      gfx.disc(cx, cy, 6, lit ? C::WHITE : rgb(85, 0, 127));
      gfx.disc(cx, cy, 5, lit ? C::YELLOW : rgb(153, 51, 204));
      gfx.disc((int16_t)(cx - 1), (int16_t)(cy - 1), 3, lit ? C::WHITE : rgb(204, 102, 255));
      gfx.disc((int16_t)(cx - 2), (int16_t)(cy - 2), 1, C::WHITE);
      break;
    }
    case PT_BR: case PT_BL: case PT_BU: case PT_BD: {
      uint8_t d = (uint8_t)(t - PT_BR);
      gfx.rbox((int16_t)(px + 1), (int16_t)(py + 1), 14, 14, 3, rgb(51, 0, 0));
      gfx.rframe((int16_t)(px + 1), (int16_t)(py + 1), 14, 14, 3, rgb(153, 42, 0));
      for (uint8_t k = 0; k < 2; k++) {                         // two chevrons, lit in turn along the way
        col c = (an + 2 - k) % 3 == 0 ? rgb(255, 230, 102) : (k == an % 2 ? rgb(255, 127, 0) : rgb(153, 42, 0));
        int16_t o = (int16_t)(k * 5 - 3);
        for (int16_t w = -4; w <= 4; w++) {
          int16_t a = (int16_t)(o - abs(w) / 2 + 1);
          int16_t x = PM_DX[d] ? (int16_t)(cx + a * PM_DX[d]) : (int16_t)(cx + w);
          int16_t y = PM_DX[d] ? (int16_t)(cy + w) : (int16_t)(cy + a * PM_DY[d]);
          gfx.pixel(x, y, c);
          gfx.pixel((int16_t)(x - PM_DX[d]), (int16_t)(y - PM_DY[d]), c);
        }
      }
      break;
    }
    case PT_CANDY: pmCandy(cx, (int16_t)(cy - ((an + tx) % 4 == 0 ? 1 : 0)), pmHash(tx, ty)); break;
    case PT_KEY: pmKey(cx, cy, an % 6 == 0); break;
    case PT_CHECK: {
      bool lit = v & PF_LIT;
      col c = lit ? (an % 2 ? C::LIME : rgb(153, 255, 153)) : rgb(102, 85, 153);
      if (lit) gfx.disc(cx, cy, 6, rgb(0, 85, 0));
      gfx.circle(cx, cy, 6, c);
      gfx.circle(cx, cy, 4, colDark(c));
      gfx.vline(cx, (int16_t)(cy - 3), 7, c);                    // a rune: a cross with a hook
      gfx.hline((int16_t)(cx - 2), (int16_t)(cy - 1), 5, c);
      gfx.pixel((int16_t)(cx + 2), (int16_t)(cy + 2), c);
      break;
    }
    case PT_OPEN:                                               // an open door: its frame is left
      gfx.vline(px, py, PM_T, rgb(102, 42, 0)); gfx.vline((int16_t)(px + 15), py, PM_T, rgb(102, 42, 0));
      gfx.fill((int16_t)(px + 1), (int16_t)(py + 3), 2, 2, GREY3); gfx.fill((int16_t)(px + 1), (int16_t)(py + 11), 2, 2, GREY3);
      break;
    }
  }
  // the fog: darker towards the edges (an ellipse over the board), dithered; 16 = one step
  static const uint8_t BAYER[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
  auto fog = [](int32_t dx, int32_t dy) { return (int32_t)(((uint32_t)(dx * dx * 169 + dy * dy * 400) >> 6) * 745 >> 20) - 22; };
  int32_t fx = px + (px < SCR_W / 2 ? 0 : PM_T - 1) - SCR_W / 2, fy = py + (py < PM_Y0 + PM_H * PM_T / 2 ? 0 : PM_T - 1) - (PM_Y0 + PM_H * PM_T / 2);
  if (fog(fx, fy) + 15 < 16) return;                            // no fog anywhere on this tile
  uint8_t *fb = gfx.buffer();
  for (int16_t j = 0; j < PM_T; j++) {
    int32_t dy = py + j - (PM_Y0 + PM_H * PM_T / 2);
    uint8_t *row = fb + (py + j) * SCR_W + px;
    for (int16_t i = 0; i < PM_T; i++) {
      int32_t l = fog(px + i - SCR_W / 2, dy) + BAYER[(j & 3) * 4 + (i & 3)];
      if (l < 16) continue;
      l >>= 4;
      row[i] = pm->lut[l > 4 ? 3 : l - 1][row[i]];
    }
  }
}

// tiles under a box of the screen get drawn again next frame
static void pmCover(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  int16_t tx0 = (int16_t)(x0 < 0 ? 0 : x0 / PM_T), tx1 = (int16_t)(x1 / PM_T);
  int16_t ty0 = (int16_t)(y0 < PM_Y0 ? 0 : (y0 - PM_Y0) / PM_T), ty1 = (int16_t)((y1 - PM_Y0) / PM_T);
  if (tx1 >= PM_W) tx1 = PM_W - 1;
  if (ty1 >= PM_H) ty1 = PM_H - 1;
  for (int16_t ty = ty0; ty <= ty1; ty++)
    for (int16_t tx = tx0; tx <= tx1; tx++) pm->drawn[ty] |= 1u << tx;
}

// the pumpkin, seen from above: ribs from the stem round to the bottom, a carved face, a shine
static void pmPumpkin(int16_t cx, int16_t cy, int16_t r, uint8_t dark, const float *stem, const float *face) {
  col deep = colDark(rgb(153, 42, 0), dark), body = colDark(rgb(255, 127, 0), dark), rib = colDark(rgb(204, 42, 0), dark);
  gfx.disc(cx, cy, r, deep);
  gfx.disc((int16_t)(cx - (r > 3 ? 1 : 0)), (int16_t)(cy - (r > 3 ? 1 : 0)), (int16_t)(r - 1), body);
  if (r >= 4) {
    float e2[3] = { stem[1] * face[2] - stem[2] * face[1], stem[2] * face[0] - stem[0] * face[2], stem[0] * face[1] - stem[1] * face[0] };
    static const float CS[6] = { 1, 0.5f, -0.5f, -1, -0.5f, 0.5f }, SN[6] = { 0, 0.866f, 0.866f, 0, -0.866f, -0.866f };
    static const float TC[6] = { 0.81f, 0.5f, 0.12f, -0.26f, -0.6f, -0.85f }, TS[6] = { 0.59f, 0.87f, 0.99f, 0.97f, 0.8f, 0.53f };
    for (uint8_t m = 0; m < 6; m++) {                           // six ribs, each a curve of short lines
      int16_t lx = 0, ly = 0;
      bool was = false;
      for (uint8_t k = 0; k < 6; k++) {
        float p[3];
        for (uint8_t i = 0; i < 3; i++) p[i] = stem[i] * TC[k] + (face[i] * CS[m] + e2[i] * SN[m]) * TS[k];
        int16_t x = (int16_t)(cx + lroundf(p[0] * (r - 0.8f))), y = (int16_t)(cy + lroundf(p[1] * (r - 0.8f)));
        bool vis = p[2] > 0.15f;
        if (vis && was) gfx.line(lx, ly, x, y, rib); else if (vis) gfx.pixel(x, y, rib);
        lx = x; ly = y; was = vis;
      }
    }
    if (face[2] > 0.25f) {                                      // the face, glowing from inside
      col glow = colDark(dark ? C::ORANGE : C::YELLOW, dark);
      static const float F[5][2] = { { 0.4f, -0.38f }, { 0.4f, 0.38f }, { -0.35f, -0.3f }, { -0.42f, 0 }, { -0.35f, 0.3f } };
      for (uint8_t q = 0; q < 5; q++) {
        float p[3];
        for (uint8_t i = 0; i < 3; i++) p[i] = face[i] * 0.85f + stem[i] * F[q][0] + e2[i] * F[q][1];
        if (p[2] < 0.25f) continue;
        int16_t x = (int16_t)(cx + lroundf(p[0] * (r - 1))), y = (int16_t)(cy + lroundf(p[1] * (r - 1)));
        gfx.pixel(x, y, glow);
        if (q < 2 && r >= 6) gfx.pixel(x, (int16_t)(y + 1), glow);
      }
    }
    if (!dark) { gfx.pixel((int16_t)(cx - r / 2), (int16_t)(cy - r / 2 - 1), rgb(255, 230, 153)); gfx.pixel((int16_t)(cx - r / 2 - 1), (int16_t)(cy - r / 2), rgb(255, 212, 102)); }
  }
  if (stem[2] > -0.1f) {                                        // the stem on top
    int16_t x = (int16_t)(cx + lroundf(stem[0] * r * 0.8f)), y = (int16_t)(cy + lroundf(stem[1] * r * 0.8f));
    col st = colDark(rgb(102, 85, 0), dark);
    gfx.fill((int16_t)(x - 1), (int16_t)(y - 1), 2, 2, st);
    if (stem[2] > 0.5f) gfx.pixel((int16_t)(x + 1), (int16_t)(y - 2), colDark(rgb(51, 170, 51), dark));
  }
}

// things that move: wandering pits, the pumpkin and its shadow, the effects
static void pmSprites() {
  PmState &s = *pm;
  for (uint8_t i = 0; i < s.nMov; i++) {
    int32_t x, y;
    pmMoverAt(i, x, y);
    int16_t sx = (int16_t)(x >> 8), sy = (int16_t)((y >> 8) + PM_Y0);
    pmHole(sx, sy, 2, s.anim);
    pmCover((int16_t)(sx - 8), (int16_t)(sy - 8), (int16_t)(sx + 8), (int16_t)(sy + 8));
  }
  if (s.st != PS_WON && s.st != PS_LOST) {
    int16_t r = PM_R;
    uint8_t dark = 0;
    if (s.st == PS_FALL || s.st == PS_GOAL) { r = (int16_t)(PM_R - PM_R * s.stT / 45); dark = (uint8_t)(s.stT / 12); }
    if (s.st == PS_BACK) r = (int16_t)(1 + (PM_R - 1) * s.stT / 30);
    int16_t cx = (int16_t)(s.x >> 8), cy = (int16_t)((s.y >> 8) + PM_Y0);
    if (s.st != PS_FALL && s.st != PS_GOAL)                     // the shadow, down to the right
      for (int16_t dy = -r + 1; dy <= r - 1; dy++) {
        int16_t w = (int16_t)pmSqrt((uint32_t)(r * r - dy * dy));
        pmShade((int16_t)(cx + 2 - w), (int16_t)(cy + 3 + dy), (int16_t)(2 * w + 1), 1, pm->lut[4]);
      }
    if (r > 0) pmPumpkin(cx, cy, r, dark, s.stem, s.face);
    pmCover((int16_t)(cx - 9), (int16_t)(cy - 9), (int16_t)(cx + 10), (int16_t)(cy + 11));
  }
  for (uint8_t i = 0; i < FX_N; i++) {                          // where the effects are drawn
    const FxPart &p = fxP[i];
    if (!p.life) continue;
    int16_t x = (int16_t)(p.x >> 8), y = (int16_t)(p.y >> 8), e = (int16_t)(p.size + 2);
    if (p.flags & FX_RING) e = (int16_t)(p.size + 3);
    if (p.flags & FX_GROW) e = (int16_t)(p.size * 3 / 2 + 3);
    if (p.flags & FX_STREAK) e = (int16_t)(abs(p.vx / 400) + abs(p.vy / 400) + 2);
    pmCover((int16_t)(x - e), (int16_t)(y - e), (int16_t)(x + e), (int16_t)(y + e));
  }
  for (uint8_t i = 0; i < FX_TXT; i++) {
    const FxText &t = fxT[i];
    if (!t.s[0]) continue;
    int16_t y = (int16_t)(t.y - t.age * 28 / 1000);
    pmCover((int16_t)(t.x - 1), (int16_t)(y - 1), (int16_t)(t.x + Gfx::width(t.s, t.f) + 2), (int16_t)(y + Gfx::height(t.f) + 2));
  }
  fxDraw(0, 0, PM_Y0, PM_BOT);
}

static void pmBarCandy(int16_t x, int16_t y) {
  gfx.triangle((int16_t)(x - 6), (int16_t)(y - 3), (int16_t)(x - 6), (int16_t)(y + 3), (int16_t)(x - 2), y, C::PINK);
  gfx.triangle((int16_t)(x + 6), (int16_t)(y - 3), (int16_t)(x + 6), (int16_t)(y + 3), (int16_t)(x + 2), y, C::PINK);
  gfx.disc(x, y, 3, C::PINK);
  gfx.pixel((int16_t)(x - 1), (int16_t)(y - 1), C::WHITE);
}

// the status bar: level, candies, keys, pumpkins left, the clock
static void pmHud() {
  PmState &s = *pm;
  const col acc = rgb(255, 102, 0);
  gfx.gradient(0, 0, SCR_W, TOP_H, colLight(acc), colDark(acc, 2));
  gfx.hline(0, TOP_H - 1, SCR_W, colLight(acc, 2));
  gfx.fill(0, TOP_H, SCR_W, PM_Y0 - TOP_H, rgb(51, 0, 0));
  gfx.hline(0, PM_Y0 - 1, SCR_W, C::BLACK);
  char b[16];
  if (s.lv < PM_LEVELS) snprintf(b, sizeof(b), "LV %u", s.lv + 1); else snprintf(b, sizeof(b), "OWN %u", s.lv - PM_LEVELS + 1);
  gfx.textShadow(6, 5, b, C::WHITE, C::BLACK, F_TEXT);
  int16_t x = 92;
  if (s.candyAll) {
    pmBarCandy(x, 13);
    snprintf(b, sizeof(b), "%u/%u", s.candy, s.candyAll);
    gfx.textShadow((int16_t)(x + 10), 5, b, s.candy == s.candyAll ? C::LIME : C::WHITE, C::BLACK, F_TEXT);
  }
  x = 160;
  if (s.keysAll) {
    pmKey((int16_t)(x + 2), 13, false);
    snprintf(b, sizeof(b), "%u", s.keysAll - s.keys);
    gfx.textShadow((int16_t)(x + 12), 5, b, s.keys == s.keysAll ? C::LIME : C::WHITE, C::BLACK, F_TEXT);
  }
  static const float UP[3] = { 0, -0.5f, 0.866f }, FW[3] = { 0, 0.866f, 0.5f };
  for (uint8_t i = 0; i < PM_LIVES; i++) {
    if (i < s.lives) pmPumpkin((int16_t)(198 + i * 13), 14, 5, 0, UP, FW);
    else gfx.circle((int16_t)(198 + i * 13), 14, 4, colDark(acc, 2));
  }
  uint32_t ds = s.steps / 10;
  snprintf(b, sizeof(b), "%lu.%lu", (unsigned long)(ds / 10), (unsigned long)(ds % 10));
  gfx.textShadow((int16_t)(SCR_W - 34 - Gfx::width(b)), 5, b, C::YELLOW, C::BLACK, F_TEXT);
  batIcon(SCR_W - 28, 7);
}

// a frame: the tiles that need it, then everything that moves on top
static void pmFrame(uint32_t now) {
  PmState &s = *pm;
  uint16_t an = (uint16_t)(now / 125);
  if (an != s.anim) {                                           // the animated tiles
    s.anim = an;
    for (int16_t ty = 0; ty < PM_H; ty++)
      for (int16_t tx = 0; tx < PM_W; tx++) {
        uint8_t t = PTILE(s.g[ty][tx]);
        if (t == PT_GOAL || (t >= PT_BR && t <= PT_BD) || t == PT_CANDY || t == PT_KEY || t == PT_MUD || (s.g[ty][tx] & PF_LIT)) pmMark(tx, ty);
      }
  }
  for (uint8_t i = 0; i < 4; i++) if (s.flash[i].t) pmMark(s.flash[i].x, s.flash[i].y);
  for (int16_t ty = 0; ty < PM_H; ty++) {
    uint32_t m = s.full ? 0xFFFFF : s.redraw[ty] | s.drawn[ty];
    for (int16_t tx = 0; m; tx++, m >>= 1) if (m & 1) pmTile(tx, ty);
    s.redraw[ty] = s.drawn[ty] = 0;
  }
  s.full = false;
  gfx.fill(0, PM_BOT, SCR_W, SCR_H - PM_BOT, C::BLACK);
  pmSprites();
  pmHud();
}

// ---------------- boxes over the board ----------------
static void pmStar(int16_t cx, int16_t cy, int16_t r, col c) {
  int16_t px[10], py[10];
  for (uint8_t i = 0; i < 10; i++) {
    float a = -1.5708f + i * 0.6283f, rr = i % 2 ? r * 0.45f : r;
    px[i] = (int16_t)(cx + lroundf(cosf(a) * rr)); py[i] = (int16_t)(cy + lroundf(sinf(a) * rr));
  }
  for (uint8_t i = 0; i < 10; i++) gfx.triangle(cx, cy, px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], c);
}

static void pmTime(char *b, size_t n, uint16_t ds) { snprintf(b, n, "%u.%u s", ds / 10, ds % 10); }

// the level's name over the board for a moment; false = left (pause menu)
static bool pmTitle() {
  PmState &s = *pm;
  uint32_t t0 = gameMillis();
  char b[32], p[24];
  if (s.lv < PM_LEVELS) snprintf(b, sizeof(b), "LEVEL %u", s.lv + 1); else snprintf(b, sizeof(b), "OWN LEVEL %u", s.lv - PM_LEVELS + 1);
  pmTime(p, sizeof(p), s.par);
  char par[32];
  snprintf(par, sizeof(par), "par %s", p);
  bool tilt = cfgTilt && gyroReady;
  btnClear();
  while (gameMillis() - t0 < 2200) {
    if (!poll(false)) return false;
    if (btn(B_OK)) break;
    pmFrame(gameMillis());
    panel(50, 70, 220, 110, NULL, rgb(255, 102, 0));
    gfx.center(80, b, C::ORANGE, F_BOLD);
    gfx.center(112, s.name, C::WHITE, F_TEXT);
    gfx.center(138, par, C::LTGREY, F_SMALL);
    gfx.center(156, tilt ? "tilt the console to roll" : "hold the arrow keys to roll", C::YELLOW, F_SMALL);
    s.full = true;
    gfx.send();
  }
  s.full = true;
  btnClear();
  return true;
}

// a box after the level; 0 = to the level list, 1 = again, 2 = the next level
static uint8_t pmEnd(bool won, uint8_t stars, bool best) {
  NoPause np;
  PmState &s = *pm;
  bool last = s.lv == PM_LEVELS - 1, own = s.lv >= PM_LEVELS;
  gfx.dim(0, PM_Y0, SCR_W, SCR_H - PM_Y0);
  btnClear();
  uint32_t t0 = millis();
  uint8_t shown = 0;
  char b[40], t[16];
  while (poll(false)) {
    uint32_t a = millis() - t0;
    if (btn(B_OK)) return won && !last && !own ? 2 : (won ? 0 : 1);
    if (btn(B_LEFT)) return 1;
    col accent = won ? rgb(255, 102, 0) : C::DKRED;
    panel(40, 44, 240, 170, NULL, accent);
    if (won) {
      gfx.center(52, last ? "ALL 15 ESCAPED!" : "LEVEL CLEAR!", C::YELLOW, F_BOLD);
      for (uint8_t k = 0; k < 3; k++) {
        bool on = k < stars && a > 350u + k * 350u;
        if (on && k + 1 > shown) { shown = (uint8_t)(k + 1); sfx((uint16_t)(1000 + k * 300), 70); }
        pmStar((int16_t)(120 + k * 40), 100, on ? 16 : 14, on ? C::YELLOW : GREY1);
        if (on) pmStar((int16_t)(120 + k * 40), 100, 7, rgb(255, 255, 153));
      }
      pmTime(t, sizeof(t), (uint16_t)(s.steps / 10));
      char p[16];
      pmTime(p, sizeof(p), s.par);
      snprintf(b, sizeof(b), "%s  (par %s)", t, p);
      gfx.center(126, b, C::WHITE, F_TEXT);
      if (best) gfx.center(148, (a / 300) % 2 ? "NEW BEST TIME!" : "", C::LIME, F_SMALL);
      if (s.candyAll) { snprintf(b, sizeof(b), "candy %u/%u", s.candy, s.candyAll); gfx.center(162, b, C::PINK, F_SMALL); }
      else { snprintf(b, sizeof(b), "pumpkins lost: %u", s.lost); gfx.center(162, b, C::LTGREY, F_SMALL); }
      gfx.center(184, own || last ? "OK list  LEFT again" : "OK next  LEFT again", C::GREY, F_SMALL);
    } else {
      gfx.center(60, "NO PUMPKINS LEFT", C::WHITE, F_BOLD);
      gfx.center(110, s.name, C::ORANGE, F_TEXT);
      gfx.center(150, "the crypt keeps them all", C::LTGREY, F_SMALL);
      gfx.center(184, "OK again", C::GREY, F_SMALL);
    }
    gfx.center(198, "hold OK = level list", C::GREY, F_SMALL);
    gfx.send();
  }
  return 0;
}

// the result kept: stars, best time, the next level opened, best = all stars
static bool pmScore(uint8_t &stars) {
  PmState &s = *pm;
  stars = 1;
  if (s.candyAll ? s.candy == s.candyAll : !s.lost) stars++;
  uint16_t ds = (uint16_t)(s.steps / 10 > 65000 ? 65000 : s.steps / 10);
  if (ds <= s.par) stars++;
  bool best = !pmSv.best[s.lv] || ds < pmSv.best[s.lv];
  if (best) pmSv.best[s.lv] = ds;
  if (stars > pmSv.stars[s.lv]) pmSv.stars[s.lv] = stars;
  if (s.lv < PM_LEVELS - 1 && s.lv + 1 > pmSv.open) pmSv.open = (uint8_t)(s.lv + 1);
  pmSvSave();
  uint16_t sum = pmStarSum();
  if (sum > curHigh) { curHigh = sum; saveHigh(curGame, curHigh); }
  return best;
}

// one level; 0 = to the library, 1 = again, 2 = the next level, 3 = the level list
static uint8_t pmPlay(uint8_t lv) {
  if (!pmLoad(lv)) return 3;
  PmState &s = *pm;
  fxReset();
  tiltGame = true;
  tiltGameStart();                                              // level: as the console is held now
  if (!pmTitle()) { if (wantRestart) { wantRestart = false; return 1; } return 0; }
  uint32_t last = gameMillis(), acc = 0, paused = pausedMs;
  btnClear();
  for (;;) {
    if (!poll(false)) { if (wantRestart) { wantRestart = false; return 1; } return 0; }
    if (pausedMs != paused) { paused = pausedMs; s.full = true; }
    uint32_t now = gameMillis(), dt = now - last;
    last = now;
    if (dt > 60) dt = 60;
    int16_t ax = keyAxis(B_LEFT, B_RIGHT), ay = keyAxis(B_UP, B_DOWN);
    for (acc += dt; acc >= PM_STEP; acc -= PM_STEP) pmStep(ax, ay);
    fxUpdate();
    pmFrame(now);
    gfx.send();
    if (s.st == PS_WON || s.st == PS_LOST) {
      uint8_t stars = 0;
      bool best = s.st == PS_WON && pmScore(stars);
      if (s.st == PS_WON) {
        sfx(1500, 300);
        pmFx(s.hx, s.hy, 24, C::YELLOW, C::LIME, 160, 900, -10);
      } else sfx(150, 500);
      uint32_t t0 = gameMillis();
      while (gameMillis() - t0 < 700 && poll(false)) { fxUpdate(); pmFrame(gameMillis()); gfx.send(); }
      tiltGame = false;
      uint8_t r = pmEnd(s.st == PS_WON, stars, best);
      return r == 0 ? 3 : r;
    }
  }
}

// ---------------- the level list ----------------
struct PmList { char name[PM_OWN][PM_NAME + 1]; uint16_t ver; };

static void pmListLoad(PmList &l) {
  l.ver = pmOwnVer;
  for (uint8_t i = 0; i < PM_OWN; i++) {
    PmOwn o;
    if (pmOwnLoad(i, o)) memcpy(l.name[i], o.name, PM_NAME + 1); else l.name[i][0] = 0;
  }
}

static void pmListBack(uint32_t t) {
  gfx.gradient(0, 0, SCR_W, SCR_H, rgb(51, 0, 51), C::BLACK);
  for (uint8_t i = 0; i < 18; i++) {                            // a few fireflies
    uint32_t h = (uint32_t)i * 2654435761u;
    int16_t x = (int16_t)((h >> 8) % SCR_W), y = (int16_t)((h >> 18) % SCR_H);
    if (((t / 200) + i) % 7 < 4) gfx.pixel(x, y, i % 2 ? C::YELLOW : C::LIME);
  }
  static const float UP[3] = { 0, -0.5f, 0.866f }, FW[3] = { 0, 0.866f, 0.5f };
  pmPumpkin(16, 15, 8, 0, UP, FW);
  gfx.textShadow(32, 4, "PUMPKIN MAZE", C::ORANGE, rgb(102, 0, 0), F_BOLD);
}

static void pmCardStars(int16_t x, int16_t y, uint8_t n, int16_t r) {
  for (uint8_t k = 0; k < 3; k++) pmStar((int16_t)(x + k * (r * 2 + 2)), y, r, k < n ? C::YELLOW : GREY1);
}

// returns a level (own ones from PM_LEVELS), PM_EDITOR or 255 = back to the library
static uint8_t pmSelect(uint8_t sel) {
  NoPause np;
  tiltGame = false;                                             // the list is steered by the keys only
  PmList l;
  pmListLoad(l);
  bool own = sel >= PM_LEVELS;
  uint8_t cur = own ? (uint8_t)(sel - PM_LEVELS) : sel;
  btnClear();
  while (poll()) {
    if (l.ver != pmOwnVer) pmListLoad(l);
    if (!own) {
      if (btn(B_LEFT))  { cur = (uint8_t)(cur ? cur - 1 : PM_LEVELS - 1); sfx(700, 15); }
      if (btn(B_RIGHT)) { cur = (uint8_t)((cur + 1) % PM_LEVELS); sfx(700, 15); }
      if (btn(B_UP) && cur >= 5) { cur -= 5; sfx(700, 15); }
      if (btn(B_DOWN)) { if (cur + 5 < PM_LEVELS) cur += 5; else { own = true; cur = 0; } sfx(700, 15); }
      if (btn(B_OK)) {
        if (cur <= pmSv.open) { sfx(1200, 50); return cur; }
        sfx(200, 120);
      }
    } else {
      if (btn(B_UP))   { if (cur) cur--; else { own = false; cur = 10; } sfx(700, 15); }
      if (btn(B_DOWN)) { if (cur < PM_OWN) cur++; sfx(700, 15); }
      if (btn(B_OK)) {
        sfx(1200, 50);
        if (cur == PM_OWN || !l.name[cur][0]) return PM_EDITOR;  // an empty slot: make one
        return (uint8_t)(PM_LEVELS + cur);
      }
    }
    pmListBack(millis());
    char b[64], t[16];
    if (!own) {
      gfx.right(SCR_W - 10, 12, "levels", C::LTGREY, F_SMALL);
      for (uint8_t i = 0; i < PM_LEVELS; i++) {
        int16_t x = (int16_t)(8 + (i % 5) * 62), y = (int16_t)(36 + (i / 5) * 52);
        bool open = i <= pmSv.open, on = i == cur;
        gfx.rbox(x, y, 56, 46, 6, on ? rgb(204, 85, 0) : (open ? rgb(85, 25, 25) : rgb(42, 25, 42)));
        if (on) gfx.rframe(x, y, 56, 46, 6, C::WHITE);
        snprintf(b, sizeof(b), "%u", i + 1);
        if (open) {
          gfx.text((int16_t)(x + 28 - Gfx::width(b, F_BOLD) / 2), (int16_t)(y + 4), b, on ? C::WHITE : C::ORANGE, F_BOLD);
          pmCardStars((int16_t)(x + 14), (int16_t)(y + 36), pmSv.stars[i], 5);
        } else {                                                // a padlock
          gfx.fill((int16_t)(x + 21), (int16_t)(y + 20), 14, 11, GREY2);
          gfx.circle((int16_t)(x + 28), (int16_t)(y + 19), 5, GREY2);
          gfx.fill((int16_t)(x + 27), (int16_t)(y + 23), 2, 4, C::BLACK);
        }
      }
      snprintf(b, sizeof(b), "%u  %s", cur + 1, PM_NAMES[cur]);
      gfx.text(10, 196, b, cur <= pmSv.open ? C::WHITE : C::GREY, F_TEXT);
      if (pmSv.best[cur]) { pmTime(t, sizeof(t), pmSv.best[cur]); snprintf(b, sizeof(b), "best %s", t); gfx.right(SCR_W - 10, 200, b, C::YELLOW, F_SMALL); }
      snprintf(b, sizeof(b), "stars %u/%u   DOWN: own levels + phone editor", pmStarSum(), PM_LEVELS * 3);
      gfx.text(10, 222, b, C::GREY, F_SMALL);
    } else {
      gfx.right(SCR_W - 10, 12, "own levels", C::LTGREY, F_SMALL);
      for (uint8_t i = 0; i <= PM_OWN; i++) {
        int16_t y = (int16_t)(34 + i * 21);
        bool on = i == cur;
        if (on) gfx.rbox(6, (int16_t)(y - 2), SCR_W - 12, 20, 5, rgb(204, 85, 0));
        if (i == PM_OWN) { gfx.text(16, y, "+ level editor on the phone", on ? C::WHITE : C::LIME, F_TEXT); continue; }
        snprintf(b, sizeof(b), "%u", i + 1);
        gfx.text(14, y, b, on ? C::WHITE : C::GREY, F_TEXT);
        if (!l.name[i][0]) { gfx.text(40, y + 4, "- empty -", on ? C::WHITE : GREY2, F_SMALL); continue; }
        gfx.text(40, y, l.name[i], on ? C::WHITE : C::LTGREY, F_TEXT);
        pmCardStars(176, (int16_t)(y + 8), pmSv.stars[PM_LEVELS + i], 5);
        if (pmSv.best[PM_LEVELS + i]) { pmTime(t, sizeof(t), pmSv.best[PM_LEVELS + i]); gfx.right(SCR_W - 12, y + 4, t, on ? C::WHITE : C::YELLOW, F_SMALL); }
      }
      gfx.text(10, 226, "UP: the 15 levels   OK play / editor", C::GREY, F_SMALL);
    }
    gfx.send();
  }
  return 255;
}

// ---------------- the phone editor: hotspot, address, QR code ----------------
// a WLAN login for the camera: WIFI:T:WPA;S:name;P:password;; with \ before ; , : " and \ .
static void pmQrEsc(char *o, size_t n, const char *s) {
  size_t k = strlen(o);
  for (; *s && k + 3 < n; s++) {
    if (strchr(";,:\"\\", *s)) o[k++] = '\\';
    o[k++] = *s;
  }
  o[k] = 0;
}

static void pmEditor() {
  NoPause np;
  if (runClock < 80) {
    uint32_t t0 = millis();
    btnClear();
    while (poll() && millis() - t0 < 3000 && !btn(B_OK)) {
      pmListBack(millis());
      gfx.center(100, "the phone editor needs WLAN:", C::WHITE);
      gfx.center(126, "CPU clock 80 MHz or more", C::YELLOW);
      gfx.center(152, "(settings)", C::LTGREY);
      gfx.send();
    }
    return;
  }
  NetState st0 = netState();
  bool opened = st0 != NET_ONLINE && st0 != NET_PLAY;           // a hotspot of its own: closed again when leaving
  if (opened) netPlay();
  void (*hook)() = pollHook;
  pollHook = phoneHook;                                         // the phone's requests are answered while this is open
  uint8_t *qr = (uint8_t *)malloc(QR_BUF);
  char want[128], made[128] = "", link[48], got[PM_NAME + 1] = "";
  uint8_t qn = 0, which = 0;                                     // QR code: 0 = the WLAN login, 1 = the link
  uint16_t ver = pmOwnVer, counted = 0;
  uint8_t n = 0;
  uint32_t gotAt = 0;
  btnClear();
  while (poll()) {
    if (btn(B_OK)) break;
    if (btn(B_LEFT) || btn(B_RIGHT)) { which ^= 1; sfx(700, 15); }
    NetState st = netState();
    bool home = st == NET_ONLINE;
    snprintf(link, sizeof(link), "http://%s/maze", home ? netAddress() : "192.168.4.1");
    bool login = which == 0 && st == NET_PLAY;
    if (login) {
      const char *pw = netApPass();
      snprintf(want, sizeof(want), "WIFI:T:%s;S:", pw[0] ? "WPA" : "nopass");
      pmQrEsc(want, sizeof(want), netApName());
      if (pw[0]) { strncat(want, ";P:", sizeof(want) - strlen(want) - 1); pmQrEsc(want, sizeof(want), pw); }
      strncat(want, ";;", sizeof(want) - strlen(want) - 1);
    } else snprintf(want, sizeof(want), "%s", link);
    if (strcmp(want, made)) { qn = qr ? qrMake(want, qr) : 0; snprintf(made, sizeof(made), "%s", want); }
    if (ver != pmOwnVer) {                                      // a level came from the phone
      ver = pmOwnVer;
      PmOwn o;
      if (pmOwnLast < PM_OWN && pmOwnLoad(pmOwnLast, o)) memcpy(got, o.name, sizeof(got)); else got[0] = 0;
      gotAt = millis() | 1;
      sfx(1500, 120);
    }
    pmListBack(millis());
    gfx.right(SCR_W - 10, 12, "phone editor", C::LTGREY, F_SMALL);
    int16_t y = 40;
    if (st == NET_PLAY) {
      gfx.text(8, y, "1  join this WLAN", C::LTGREY, F_SMALL);
      gfx.text(8, y + 12, netApName(), C::YELLOW, F_TEXT);
      const char *pw = netApPass();
      gfx.text(8, y + 38, pw[0] ? "password" : "no password", C::LTGREY, F_SMALL);
      if (pw[0]) gfx.text(8, y + 50, pw, C::YELLOW, strlen(pw) <= 17 ? F_TEXT : F_SMALL);
      y = (int16_t)(y + 78);
    } else if (home) {
      gfx.text(8, y, "1  phone in the WLAN", C::LTGREY, F_SMALL);
      gfx.text(8, y + 12, netSsid(), C::YELLOW, strlen(netSsid()) <= 17 ? F_TEXT : F_SMALL);
      y = (int16_t)(y + 40);
    } else {
      gfx.text(8, y, "starting the hotspot...", C::YELLOW, F_SMALL);
      y = (int16_t)(y + 40);
    }
    gfx.text(8, y, "2  open in the browser", C::LTGREY, F_SMALL);
    const char *shortLink = link + 7;                           // without "http://"
    gfx.text(8, (int16_t)(y + 12), shortLink, C::CYAN, Gfx::width(shortLink) <= 176 ? F_TEXT : F_SMALL);
    if (qn) {
      uint8_t s = qn * 3 + 18 <= 130 ? 3 : 2;
      int16_t w = (int16_t)((qn + 6) * s), qx = (int16_t)(SCR_W - 8 - w);
      qrDraw(qr, qn, qx, 36, s);
      const char *cap = login ? "QR: join the WLAN" : "QR: open the editor";
      gfx.text((int16_t)(qx + (w - Gfx::width(cap, F_SMALL)) / 2), (int16_t)(40 + w), cap, C::LTGREY, F_SMALL);
      if (st == NET_PLAY) gfx.text((int16_t)(qx + (w - Gfx::width("LEFT/RIGHT: other QR", F_SMALL)) / 2), (int16_t)(52 + w), "LEFT/RIGHT: other QR", C::GREY, F_SMALL);
    }
    if (counted != pmOwnVer) {                                  // read from flash only when a level came in
      counted = pmOwnVer;
      n = 0;
      for (uint8_t i = 0; i < PM_OWN; i++) { PmOwn o; if (pmOwnLoad(i, o)) n++; }
    }
    char b[48];
    snprintf(b, sizeof(b), "own levels %u/%u", n, PM_OWN);
    gfx.text(8, 186, b, C::WHITE, F_SMALL);
    if (gotAt && millis() - gotAt < 6000) {
      snprintf(b, sizeof(b), "saved: %s", got);
      gfx.text(8, 200, b, (millis() / 250) % 2 ? C::LIME : C::WHITE, F_TEXT);
    }
    gfx.text(8, 226, "OK = done (the hotspot goes off)", C::GREY, F_SMALL);
    gfx.send();
  }
  free(qr);
  pollHook = hook;
  if (opened) netStop();
  btnClear();
}

// ---------------- the phone's side (phone.cpp, page /maze) ----------------
/* slot < 0: the own levels as JSON {"slots":8,"l":[{"n":name,"s":stars,
   "t":best 1/10 s},...]} ("" = an empty slot); else that level as text
   "NAME|row|row|..." ("" = empty). Runs in the UI loop (netTick).     */
int appMazeGet(int slot, char *out, int max) {
  pmSvLoad();
  int n = 0;
  if (slot < 0) {
    n = snprintf(out, max, "{\"slots\":%u,\"l\":[", PM_OWN);
    for (uint8_t i = 0; i < PM_OWN && n < max - 48; i++) {
      PmOwn o;
      bool have = pmOwnLoad(i, o);
      n += snprintf(out + n, max - n, "%s{\"n\":\"%s\",\"s\":%u,\"t\":%u}", i ? "," : "", have ? o.name : "",
                    have ? pmSv.stars[PM_LEVELS + i] : 0, have ? pmSv.best[PM_LEVELS + i] : 0);
    }
    n += snprintf(out + n, max - n, "]}");
    return n < max ? n : max - 1;
  }
  PmOwn o;
  if (slot >= PM_OWN || max < PM_N + PM_H + PM_NAME + 2 || !pmOwnLoad((uint8_t)slot, o)) { out[0] = 0; return 0; }
  n = snprintf(out, max, "%s", o.name);
  for (uint8_t y = 0; y < PM_H; y++) { out[n++] = '|'; memcpy(out + n, o.map + y * PM_W, PM_W); n += PM_W; }
  out[n] = 0;
  return n;
}

// a level from the phone into slot 0..7 ("" empties it); NULL = saved, else why not
const char *appMazePut(uint8_t slot, const char *text) {
  if (slot >= PM_OWN) return "slot 1..8";
  pmSvLoad();
  char k[8];
  snprintf(k, sizeof(k), "PMo%u", slot);
  PmOwn o, old;
  bool had = pmOwnLoad(slot, old);
  if (!text[0]) {
    uint8_t z = 0;
    prefs.begin("arcade", false);
    prefs.putBytes(k, &z, 1);
    prefs.end();
  } else {
    const char *why = pmParse(text, o);
    if (why) return why;
    prefs.begin("arcade", false);
    prefs.putBytes(k, &o, sizeof(o));
    prefs.end();
    PmOwn back;
    if (!pmOwnLoad(slot, back) || memcmp(&back, &o, sizeof(o))) return "the flash is full";
  }
  if (!text[0] || !had || memcmp(old.map, o.map, PM_N)) {       // a new level: its stars and time start again
    pmSv.stars[PM_LEVELS + slot] = 0;
    pmSv.best[PM_LEVELS + slot] = 0;
    pmSvSave();
  }
  pmOwnLast = slot;
  pmOwnVer++;
  return NULL;
}

void pumpkinRun() {
  pm = (PmState *)calloc(1, sizeof(PmState));
  if (!pm) return;
  pmSvLoad();
  pmLuts();
  uint8_t sel = pmSv.open;
  for (;;) {
    uint8_t lv = pmSelect(sel);
    if (lv == 255) break;
    if (lv == PM_EDITOR) { sel = PM_LEVELS; pmEditor(); continue; }
    sel = lv;
    uint8_t r;
    while ((r = pmPlay(lv)) == 1 || r == 2) {
      if (r == 2) lv = sel = (uint8_t)(lv + 1);
    }
    if (r == 0) break;                                          // "quit" in the pause menu
  }
  tiltGame = true;
  free(pm);
  pm = NULL;
}

static const char *const IC_PUMPKIN[12] = {
  "......dg....", ".....nn.....", "..oorooroo..", ".oooroorooo.", "ooyyooooyyoo", "oyyyorroyyyo",
  "ooooorrooooo", "oyooooooooyo", "ooyyyyyyyyoo", ".oryyyyyyro.", "..oorooroo..", "....oooo...." };
