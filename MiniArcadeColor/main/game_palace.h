// MiniArcade Color: Palace. Part of console.cpp, which includes it.
// A cinematic platformer: a nameless runner escapes from a palace
// dungeon, four levels in ten minutes. A level is a grid of rooms of
// 10 x 3 tiles; the screen flips when the runner leaves a room.
//   LEFT/RIGHT run         DOWN+LEFT/RIGHT careful step (safe on spikes,
//   UP+LEFT/RIGHT jump     stops at an edge); a running jump clears three
//   UP alone: jump up to a ledge above (hold UP to climb on)
//   DOWN at an edge: climb down and hang; DOWN while hanging: let go
//   OK draws the sword (it also comes out by itself when a guard is near):
//   then LEFT/RIGHT step, OK strikes, UP parries, DOWN puts it away.
// One floor down is safe, two cost a flask of health, three kill. Loose
// tiles crumble, spikes spring out, plates open gates (and others shut
// them), one plate opens the exit door. Dying restarts the level, the
// clock runs on. Score: 100 per level, plus the seconds left at the end.
// Everything moves in steps of PAL_TICK ms, so the speed does not depend
// on the frame rate. Level data is const (flash); the state is small.

#define PAL_TW     32                  // tile width
#define PAL_TH     63                  // tile height: one floor
#define PAL_FL     55                  // the feet line inside a tile
#define PAL_HUD    (TOP_H + 3 * PAL_TH)   // 215: the strip below a room
#define PAL_TICK   20                  // ms per step
#define PAL_MAXC   50                  // the biggest level: 5 rooms wide ...
#define PAL_MAXR   9                   // ... or 3 rooms high
#define PAL_NG     6                   // guards per level
#define PAL_NL     6                   // loose tiles shaking at once
#define PAL_NF     4                   // tiles falling at once
// motion in 1/256 px per step
#define PAL_RUN    614                 // running: 2.4 px, 120 px/s
#define PAL_ACC    77                  // speeding up per step
#define PAL_STEPV  230                 // careful step
#define PAL_FIGHTV 256                 // a step with the sword drawn
#define PAL_G      23                  // gravity
#define PAL_JUMP   410                 // jump speed up: 14 px high, 35 steps long
#define PAL_JRUN   922                 // running jump: 126 px (three tiles of gap)
#define PAL_JSTAND 585                 // standing jump: 80 px (one or two tiles of gap)
#define PAL_VMAX   1536                // fastest fall
#define PAL_SAFE   100                 // a fall up to this many px is safe ...
#define PAL_HURT   160                 // ... up to this one costs a flask, more kills
#define PAL_GRAB   20                  // how far from a ledge the hands still catch it
#define PAL_HANGY  57                  // hanging: feet this far below the ledge's line
#define PAL_TIME   600000UL            // ten minutes for all levels
#define PAL_LEVELS 4

/* Tiles: ' ' nothing (fall through)  _ floor  # wall  t floor with a
   torch  | floor with a pillar  ^ spikes  ~ loose floor  - rubble
   a..d gate (closed at the start)  1..4 plate that opens gate a..d
   5..8 plate that shuts gate a..d  X plate that opens the exit
   E exit door  h small potion  H big potion (one more flask)  S sword
   @ start  G guard  C the captain. Rows of w*10 characters, h*3 rows. */
struct PalLevel { uint8_t w, h, theme; const char *name; const char *const *rows; };

static const char *const PAL_L1[3] = {
  "#@_t__ __|__  _St_ #### _a_Et#",
  "#__h___t_#____####__tG1__~_X_#",
  "########################__ht_#" };
static const char *const PAL_L2[6] = {
  "#@_t__^___" "_   __|_t_" "_~~_ #####" "##########",
  "##########" "#_h_######" "#_____G_1_" "_a_ __t_H#",
  "##########" "##########" "##########" "### ######",
  "##########" "__Et_|_~__" "_G__  _t__" "__^_t__###",
  "##########" "#######_h#" "####___tX#" "##########",
  "##########" "##########" "##########" "##########" };
static const char *const PAL_L3[6] = {
  "##########" "##########" "##########" "##########" "##########",
  "##########" "__Et__G___" "_ ########" "##########" "##########",
  "##########" "########_X" "__^^_t_c__" "73__G__t__" "___t__ __#",
  "##########" "##########" "##########" "##########" "######_ _#",
  "#@_t__ ___" "_G__~_~_^_" "_ _a__t___" "__G__   __" "__t___h__#",
  "######^###" "####_#_###" "#___1#####" "#####^^^##" "##########" };
static const char *const PAL_L4[9] = {
  "##########" "##########" "##########" "##########",
  "##########" "##########" "####_ _Et#" "##########",
  "##########" "___ ______" "__tC____h#" "##########",
  "##########" "###_ _t__X" "##########" "##########",
  "##########" "#_ __#####" "##########" "2_ _t_####",
  "##########" "#__t_^^___" "__t_b_6_G_" "_|__ _t_H#",
  "##########" "##########" "##########" "####__ __#",
  "#@_t_~~_|_" "__  __G___" "_^_t_ _^__" "__h__t___#",
  "#####__###" "##^^######" "#####^####" "##########" };
static const PalLevel PAL_LEVEL[PAL_LEVELS] = {
  { 3, 1, 0, "THE CELLARS",    PAL_L1 },
  { 4, 2, 0, "THE OLD WELLS",  PAL_L2 },
  { 5, 2, 1, "THE GUARD HALLS", PAL_L3 },
  { 4, 3, 2, "THE SKY TOWER",  PAL_L4 } };

// the guards get better level by level; the last row is the captain
struct PalSkill { uint8_t hp, wind, cool, rnd, block, counter; int16_t speed; };
static const PalSkill PAL_SKILL[PAL_LEVELS + 1] = {
  { 2, 24, 50, 40, 20, 10, 180 }, { 3, 20, 42, 35, 35, 25, 205 }, { 3, 17, 34, 30, 50, 35, 230 },
  { 4, 15, 28, 25, 60, 45, 250 }, { 6, 13, 24, 20, 70, 55, 260 } };

enum { PH_STAND, PH_RUN, PH_STEP, PH_JUMP, PH_FALL, PH_LAND, PH_JUP, PH_HANG, PH_CLIMB, PH_DOWN,
       PH_FIGHT, PH_DEAD, PH_EXIT };
enum { PA_NONE, PA_STRIKE, PA_BLOCK, PA_HIT };
enum { PG_IDLE, PG_READY, PG_STRIKE, PG_BLOCK, PG_HURT, PG_RECOIL, PG_DYING, PG_DEAD };

struct PalHero {
  int32_t  x, y, vx, vy;               // 1/256 px: centre, feet
  int32_t  peak, ty;                   // highest feet in this flight, feet at the take-off
  uint32_t walk;                       // distance run (1/256 px), for the stride
  int16_t  lb;                         // the ledge's edge in px (hanging, climbing)
  int8_t   lr, dir;                    // the ledge's row; facing
  uint8_t  st, act, hp, maxHp, coyote, lt, hurt;
  uint16_t t, at;
  bool     sword, spiked;
};
struct PalGuard {
  int32_t  x, x0, x1;                  // 1/256 px, and the floor it keeps to
  uint32_t walk;
  int8_t   row, dir;
  uint8_t  st, t, hp, sk, cool;
};
struct PalShake { int8_t c, r; uint8_t t; bool on; };
struct PalFall  { int32_t y, vy; int8_t c; bool on, hit; };
struct PalIn    { bool l, r, u, d, uT, dT, okT; };

static char     palMap[PAL_MAXR][PAL_MAXC];
static uint8_t  palW, palH, palLv, palNG;
static uint8_t  palGate[4], palGateWant, palExit;   // gates 0 (shut) .. 64 (open)
static bool     palExitOn;
static PalHero  palHero;
static PalGuard palG[PAL_NG];
static PalShake palSh[PAL_NL];
static PalFall  palF[PAL_NF];
static const char *palMsg = NULL;
static uint16_t palMsgT, palScore;
static uint8_t  palPhase;              // 0 title, 1 playing, 2 a banner (the tests' player looks)
static uint32_t palTicks;
static uint8_t  palStartHp;            // flasks when the level began (a restart gives them back)
static bool     palStartSword;

// ---------------- the map ----------------
static char palAt(int16_t c, int16_t r) {
  if (c < 0 || c >= palW || r < 0) return '#';
  if (r >= palH) return ' ';                         // below the level: the depths
  return palMap[r][c];
}
static bool palIsFloor(char ch) { return ch != ' ' && ch != '#'; }
static bool palGateShut(char ch) { return ch >= 'a' && ch <= 'd' && palGate[ch - 'a'] < 40; }
static int16_t palColOf(int32_t x) { return (int16_t)((x >> 8) / PAL_TW); }
static int16_t palRowOf(int32_t y) { int16_t p = (int16_t)(y >> 8); return p <= 0 ? 0 : (int16_t)((p - 1) / PAL_TH); }
static int16_t palLine(int16_t r) { return (int16_t)(r * PAL_TH + PAL_FL); }   // feet line of a row, px
static int16_t palEdge(int16_t c, int8_t d) { return (int16_t)(d > 0 ? (c + 1) * PAL_TW : c * PAL_TW); }
static void palSay(const char *s) { palMsg = s; palMsgT = 120; }

// a map of rows (w x h tiles); sk = the guards' skill (the level)
static void palLoadRows(const char *const *rows, uint8_t w, uint8_t h_, uint8_t sk) {
  palW = w; palH = h_;
  memset(palMap, '#', sizeof(palMap));
  memset(palG, 0, sizeof(palG)); memset(palSh, 0, sizeof(palSh)); memset(palF, 0, sizeof(palF));
  memset(palGate, 0, sizeof(palGate));
  palNG = 0; palGateWant = 0; palExit = 0; palExitOn = false;
  PalHero &h = palHero;
  h.maxHp = palStartHp; h.hp = palStartHp; h.sword = palStartSword;
  for (uint8_t r = 0; r < palH; r++)
    for (uint8_t c = 0; c < palW; c++) {
      char ch = rows[r][c];
      if (ch == '@') {
        h.x = (int32_t)(c * PAL_TW + PAL_TW / 2) << 8; h.y = (int32_t)palLine(r) << 8;
        ch = '_';
      } else if ((ch == 'G' || ch == 'C') && palNG < PAL_NG) {
        PalGuard &g = palG[palNG++];
        g.x = (int32_t)(c * PAL_TW + PAL_TW / 2) << 8; g.row = (int8_t)r; g.dir = -1;
        g.sk = ch == 'C' ? PAL_LEVELS : sk;
        g.hp = PAL_SKILL[g.sk].hp; g.st = PG_IDLE;
        ch = '_';
      }
      palMap[r][c] = ch;
    }
  for (uint8_t i = 0; i < palNG; i++) {              // the floor a guard keeps to: no holes, traps or gates
    PalGuard &g = palG[i];
    int16_t c = palColOf(g.x), lo = c, hi = c, r0 = (int16_t)(c / 10 * 10);
    auto ok = [&](int16_t cc) { char ch = palAt(cc, g.row);
      return cc >= r0 && cc < r0 + 10 && palIsFloor(ch) && ch != '~' && ch != '^' && !(ch >= 'a' && ch <= 'd'); };
    while (ok(lo - 1)) lo--;
    while (ok(hi + 1)) hi++;
    // and not too near the room's sides, so that a fight stays on one screen
    int32_t a = lo * PAL_TW + 10, b = (hi + 1) * PAL_TW - 10, ra = r0 * PAL_TW + 64, rb = (r0 + 10) * PAL_TW - 64;
    g.x0 = (a > ra ? a : ra) << 8; g.x1 = (b < rb ? b : rb) << 8;
    if (g.x1 < g.x0) g.x1 = g.x0;
    if (g.x < g.x0) g.x = g.x0;
    if (g.x > g.x1) g.x = g.x1;
  }
  h.vx = h.vy = 0; h.dir = 1; h.st = PH_STAND; h.act = PA_NONE; h.t = h.at = 0;
  h.coyote = h.lt = h.hurt = 0; h.spiked = false; h.walk = 0; h.lr = -1;
  palTicks = 0; palMsgT = 0;
}
static void palLoad(uint8_t lv) {
  const PalLevel &L = PAL_LEVEL[lv];
  palLv = lv;
  palLoadRows(L.rows, (uint8_t)(L.w * 10), (uint8_t)(L.h * 3), lv);
}

// moving sideways: walls, shut gates and living guards stop the body
static int32_t palMoveX(int32_t x, int32_t dx, int16_t r) {
  if (!dx) return x;
  int32_t nx = x + dx;
  int8_t d = dx > 0 ? 1 : -1;
  int16_t ox = (int16_t)(x >> 8), lim = d > 0 ? 32767 : -32767;
  int16_t fc = (int16_t)(((nx >> 8) + d * 8) / PAL_TW);
  if (palAt(fc, r) == '#') lim = (int16_t)(d > 0 ? fc * PAL_TW - 8 : (fc + 1) * PAL_TW + 8);
  for (int16_t cc = (int16_t)(ox / PAL_TW - 1); cc <= ox / PAL_TW + 1; cc++)
    if (palGateShut(palAt(cc, r))) {
      int16_t gl = (int16_t)(cc * PAL_TW + PAL_TW / 2);
      if (d > 0 && ox <= gl && gl - 9 < lim) lim = (int16_t)(gl - 9);
      if (d < 0 && ox >= gl && gl + 9 > lim) lim = (int16_t)(gl + 9);
    }
  for (uint8_t i = 0; i < palNG; i++) {
    const PalGuard &g = palG[i];
    if (g.row != r || g.st >= PG_DYING) continue;
    int16_t gx = (int16_t)(g.x >> 8);
    if (d > 0 && gx > ox && gx - 24 < lim) lim = (int16_t)(gx - 24);
    if (d < 0 && gx < ox && gx + 24 > lim) lim = (int16_t)(gx + 24);
  }
  if (d > 0 && (nx >> 8) > lim) nx = (int32_t)lim << 8 > x ? (int32_t)lim << 8 : x;
  if (d < 0 && (nx >> 8) < lim) nx = (int32_t)lim << 8 < x ? (int32_t)lim << 8 : x;
  return nx;
}

// a careful move: like palMoveX, but never over an edge
static int32_t palMoveSafe(int32_t x, int32_t dx, int16_t r) {
  int8_t d = dx > 0 ? 1 : -1;
  int16_t c = palColOf(x);
  int32_t nx = palMoveX(x, dx, r);
  int16_t b = palEdge(c, d);
  if (!palIsFloor(palAt(c + d, r)) && (d > 0 ? (nx >> 8) > b - 4 : (nx >> 8) < b + 4))
    nx = (int32_t)(b - d * 4) << 8;
  if (d > 0 ? nx < x : nx > x) nx = x;
  return nx;
}

// ---------------- the runner ----------------
static void palDie() {
  PalHero &h = palHero;
  if (h.st == PH_DEAD) return;
  h.st = PH_DEAD; h.t = 0; h.hp = 0; h.act = PA_NONE; h.vx = 0;
  sfx(140, 600);
}
static void palHurt() {
  PalHero &h = palHero;
  if (h.hp) h.hp--;
  h.hurt = 20;
  if (!h.hp) palDie();
}
static void palFall(bool fromRun) {
  PalHero &h = palHero;
  h.st = PH_FALL; h.vy = 0; h.peak = h.ty = h.y; h.t = 0; h.coyote = fromRun ? 6 : 0; h.act = PA_NONE;
}
static void palJump(int32_t v) {
  PalHero &h = palHero;
  h.st = PH_JUMP; h.vx = h.dir * v; h.vy = -PAL_JUMP; h.peak = h.ty = h.y; h.t = 0;
  sfx(520, 30);
}
static void palHang(int16_t lb, int8_t lr, int8_t d) {
  PalHero &h = palHero;
  h.st = PH_HANG; h.lb = lb; h.lr = lr; h.dir = d; h.t = 0; h.vx = h.vy = 0;
  h.x = (int32_t)(lb - d * 8) << 8; h.y = (int32_t)(palLine(lr) + PAL_HANGY) << 8;
}
// climbing from hanging (t = 0) to standing on the ledge (t = 30)
static void palClimbAt(uint8_t t) {
  static const int8_t KY[4] = { PAL_HANGY, 30, 4, 0 }, KX[4] = { -8, -5, 2, 10 };
  static const uint8_t KT[4] = { 0, 12, 22, 30 };
  PalHero &h = palHero;
  uint8_t k = 0;
  while (k < 2 && t > KT[k + 1]) k++;
  int16_t n = KT[k + 1] - KT[k], f = (int16_t)(t - KT[k]);
  int16_t y = (int16_t)(KY[k] + (KY[k + 1] - KY[k]) * f / n), x = (int16_t)(KX[k] + (KX[k + 1] - KX[k]) * f / n);
  h.y = (int32_t)(palLine(h.lr) + y) << 8; h.x = (int32_t)(h.lb + h.dir * x) << 8;
}

static void palShake(int16_t c, int16_t r, uint8_t t) {
  for (uint8_t i = 0; i < PAL_NL; i++) if (palSh[i].on && palSh[i].c == c && palSh[i].r == r) return;
  for (uint8_t i = 0; i < PAL_NL; i++)
    if (!palSh[i].on) { palSh[i] = { (int8_t)c, (int8_t)r, t, true }; sfx(260, 20); return; }
}
static void palPlate(char ch) {
  if (ch >= '1' && ch <= '4') { uint8_t b = (uint8_t)(1 << (ch - '1')); if (!(palGateWant & b)) { palGateWant |= b; sfx(700, 40); } }
  if (ch >= '5' && ch <= '8') { uint8_t b = (uint8_t)(1 << (ch - '5')); if (palGateWant & b) { palGateWant &= (uint8_t)~b; sfx(300, 40); } }
  if (ch == 'X' && !palExitOn) { palExitOn = true; sfx(1000, 200); palSay("THE EXIT DOOR OPENS"); }
}

// the runner stands on a tile: plates, potions, the sword, loose floor, spikes
static void palEnter(int16_t c, int16_t r) {
  PalHero &h = palHero;
  char &ch = palMap[r][c];
  palPlate(ch);
  switch (ch) {
  case 'h': ch = '_'; if (h.hp < h.maxHp) h.hp++; sfx(1300, 120); palSay("A HEALING DRAUGHT"); break;
  case 'H': ch = '_'; if (h.maxHp < 8) h.maxHp++; h.hp = h.maxHp; sfx(1600, 250); palSay("A DRAUGHT OF LIFE!"); break;
  case 'S': ch = '_'; h.sword = true; sfx(1800, 150); palSay("A SWORD! OK DRAWS IT"); break;
  case '~': palShake(c, r, 0); break;
  case '^': if (h.st == PH_RUN && abs(h.vx) > PAL_STEPV * 3 / 2) { h.spiked = true; palDie(); } break;
  }
}

static void palLand(int16_t c, int16_t r) {
  PalHero &h = palHero;
  int16_t fall = (int16_t)((h.y - h.peak) >> 8);
  bool jump = h.st == PH_JUMP;
  h.vy = 0; h.t = 0;
  if (palAt(c, r) == '^') { h.spiked = true; palDie(); return; }
  if (fall > PAL_HURT) { palDie(); return; }
  h.st = PH_LAND;
  h.vx = jump && abs(h.vx) > PAL_RUN ? h.dir * PAL_RUN : 0;
  if (fall > PAL_SAFE) { h.lt = 24; h.vx = 0; sfx(120, 160); palHurt(); }
  else { h.lt = jump ? 3 : 6; sfx(200, 25); }
  if (h.st == PH_LAND) palEnter(c, r);
}

// the living guard in the runner's row nearest to him, if one is that close
static PalGuard *palFoe(int16_t range) {
  PalHero &h = palHero;
  PalGuard *best = NULL;
  int16_t r = palRowOf(h.y);
  for (uint8_t i = 0; i < palNG; i++) {
    PalGuard &g = palG[i];
    if (g.st >= PG_DYING || g.st == PG_IDLE || g.row != r) continue;
    int16_t d = (int16_t)abs((g.x - h.x) >> 8);
    if (d <= range) { range = d; best = &g; }
  }
  return best;
}

static void palJumpUp(int16_t c, int16_t r) {
  PalHero &h = palHero;
  h.st = PH_JUP; h.t = 0; h.vx = 0; h.lr = -1;
  if (r > 0 && palAt(c, r - 1) == ' ')
    for (uint8_t k = 0; k < 2; k++) {
      int8_t d = k ? (int8_t)-h.dir : h.dir;
      char e = palAt(c + d, r - 1);
      if (palIsFloor(e) && !palGateShut(e)) { h.lr = (int8_t)(r - 1); h.dir = d; h.lb = palEdge(c, d); break; }
    }
  sfx(420, 30);
}
static bool palClimbDown(int16_t c, int16_t r) {
  PalHero &h = palHero;
  for (uint8_t k = 0; k < 2; k++) {
    int8_t d = k ? (int8_t)-h.dir : h.dir;
    int16_t b = palEdge(c, d);
    if (palAt(c + d, r) != ' ' || abs(b - (int16_t)(h.x >> 8)) > 14) continue;
    h.st = PH_DOWN; h.t = 0; h.vx = 0; h.dir = (int8_t)-d; h.lb = b; h.lr = (int8_t)r;
    sfx(300, 30);
    return true;
  }
  return false;
}

static void palGround(const PalIn &in) {
  PalHero &h = palHero;
  int16_t c = palColOf(h.x), r = palRowOf(h.y);
  if (!palIsFloor(palAt(c, r))) { palFall(false); return; }
  palEnter(c, r);
  if (h.st == PH_DEAD) return;
  if (h.sword && palFoe(72)) { h.st = PH_FIGHT; h.act = PA_NONE; h.vx = 0; h.t = 0; sfx(1500, 30); return; }
  int8_t d = in.l == in.r ? 0 : (in.l ? -1 : 1);
  if (in.uT) {
    if (palAt(c, r) == 'E' && palExit >= 64) { h.st = PH_EXIT; h.t = 0; h.vx = 0; sfx(1200, 150); return; }
    if (d) { h.dir = d; palJump(h.st == PH_RUN && abs(h.vx) >= PAL_RUN * 5 / 6 ? PAL_JRUN : PAL_JSTAND); }
    else palJumpUp(c, r);
    return;
  }
  if (in.dT && !d) { if (!palClimbDown(c, r)) { h.st = PH_LAND; h.lt = 10; h.t = 0; h.vx = 0; } return; }
  if (in.okT) {
    if (h.sword) { h.st = PH_FIGHT; h.act = PA_NONE; h.vx = 0; h.t = 0; sfx(1500, 30); }
    else palSay("NO SWORD YET");
    return;
  }
  int32_t ox = h.x;
  if (d && in.d) {                                     // careful step
    h.st = PH_STEP; h.dir = d; h.vx = 0;
    h.x = palMoveSafe(h.x, d * PAL_STEPV, r);
  } else if (d) {
    if (d != h.dir) { h.vx = 0; h.dir = d; }           // turning round
    h.st = PH_RUN;
    h.vx += d * PAL_ACC;
    if (h.vx > PAL_RUN) h.vx = PAL_RUN;
    if (h.vx < -PAL_RUN) h.vx = -PAL_RUN;
    h.x = palMoveX(h.x, h.vx, r);
    if (abs(h.x - ox) < abs(h.vx)) h.vx = 0;           // ran into something
  } else {
    h.st = PH_STAND;
    int32_t f = 3 * PAL_ACC;                           // a short skid
    h.vx = h.vx > 0 ? (h.vx > f ? h.vx - f : 0) : (h.vx < -f ? h.vx + f : 0);
    h.x = palMoveX(h.x, h.vx, r);
  }
  h.walk += (uint32_t)abs(h.x - ox);
  if (!palIsFloor(palAt(palColOf(h.x), r))) palFall(h.st == PH_RUN);
}

static void palAir(const PalIn &in) {
  PalHero &h = palHero;
  h.t++;
  if (h.st == PH_FALL && h.coyote) {                   // a jump just after running off an edge
    h.coyote--;
    if (in.uT) { h.y = h.ty; palJump(PAL_JRUN); return; }
  }
  if (h.st == PH_JUMP) {
    int32_t ox = h.x;
    h.x = palMoveX(h.x, h.vx, palRowOf(h.y));
    if (abs(h.x - ox) < abs(h.vx)) h.vx = 0;           // against a wall: straight down
  } else {                                             // falling: down the middle of the shaft
    int32_t dx = ((int32_t)(palColOf(h.x) * PAL_TW + PAL_TW / 2) << 8) - h.x;
    h.x += dx > 384 ? 384 : (dx < -384 ? -384 : dx);
  }
  h.vy += PAL_G;
  if (h.vy > PAL_VMAX) h.vy = PAL_VMAX;
  int32_t ny = h.y + h.vy;
  if (ny < h.peak) h.peak = ny;
  if (h.vy > 0) {
    int16_t p0 = (int16_t)(h.y >> 8), p1 = (int16_t)(ny >> 8);
    int16_t r = (int16_t)((p0 - PAL_FL + 125) / PAL_TH - 1);   // the first feet line at or below
    int16_t fy = palLine(r);
    if (fy <= p1) {
      int16_t c = palColOf(h.x);
      char ch = palAt(c, r);
      if (palIsFloor(ch) || ch == '#') { h.y = (int32_t)fy << 8; palLand(c, r); return; }
      for (uint8_t k = 0; k < 2 && (h.st == PH_JUMP || in.u); k++) {   // catch a ledge
        if (k && h.st == PH_JUMP) break;
        int8_t d = k ? (int8_t)-h.dir : h.dir;
        char e = palAt(c + d, r);
        int16_t b = palEdge(c, d);
        if (palIsFloor(e) && !palGateShut(e) && abs(b - (int16_t)(h.x >> 8)) <= PAL_GRAB) { palHang(b, (int8_t)r, d); sfx(350, 30); return; }
      }
    }
  }
  h.y = ny;
  if (h.st == PH_JUMP && h.vy > 0 && h.y > h.ty + (4 << 8)) { h.st = PH_FALL; h.coyote = 0; }
  if ((h.y >> 8) > palH * PAL_TH + 150) palDie();      // into the depths
}

static void palStrike(PalGuard *g) {
  PalHero &h = palHero;
  if (!g) return;
  int16_t dx = (int16_t)((g->x - h.x) >> 8);
  if ((dx > 0) != (h.dir > 0) || abs(dx) > 52 || g->st >= PG_DYING) return;
  if (g->st == PG_BLOCK) { sfx(2200, 40); return; }
  g->hp--; g->t = 0;
  g->st = g->hp ? PG_HURT : PG_DYING;
  int32_t nx = g->x + h.dir * (10 << 8);
  g->x = nx < g->x0 ? g->x0 : (nx > g->x1 ? g->x1 : nx);
  sfx(g->hp ? 300 : 160, 140);
  if (!g->hp) palSay("THE GUARD FALLS");
}

static void palFight(const PalIn &in) {
  PalHero &h = palHero;
  int16_t c = palColOf(h.x), r = palRowOf(h.y);
  if (!palIsFloor(palAt(c, r))) { palFall(false); return; }
  palEnter(c, r);
  if (h.st != PH_FIGHT) return;
  PalGuard *g = palFoe(150);
  if (h.act == PA_NONE && g) h.dir = g->x > h.x ? 1 : -1;
  h.t++;
  switch (h.act) {
  case PA_STRIKE: if (++h.at == 5) palStrike(g); if (h.at >= 15) h.act = PA_NONE; return;
  case PA_BLOCK:  if (++h.at >= 14) h.act = PA_NONE; return;
  case PA_HIT:    if (++h.at >= 10) h.act = PA_NONE; return;
  }
  int8_t d = in.l == in.r ? 0 : (in.l ? -1 : 1);
  if (in.okT) { h.act = PA_STRIKE; h.at = 0; sfx(900, 25); return; }
  if (!g) {                                            // nobody left: the sword goes away
    if (in.uT || in.dT || d || h.t > 100) { h.st = PH_STAND; palGround(in); }
    return;
  }
  if (in.uT) { h.act = PA_BLOCK; h.at = 0; sfx(1100, 15); return; }
  if (in.dT) { h.st = PH_STAND; return; }
  if (d) {
    int32_t ox = h.x;
    h.x = palMoveSafe(h.x, d * PAL_FIGHTV, r);
    h.walk += (uint32_t)abs(h.x - ox);
  }
}

static void palHeroTick(const PalIn &in) {
  PalHero &h = palHero;
  if (h.hurt) h.hurt--;
  switch (h.st) {
  case PH_STAND: case PH_RUN: case PH_STEP: palGround(in); break;
  case PH_JUMP: case PH_FALL: palAir(in); break;
  case PH_LAND: {
    int16_t r = palRowOf(h.y);
    if (!palIsFloor(palAt(palColOf(h.x), r))) { palFall(false); break; }
    if (h.vx) { int32_t ox = h.x; h.x = palMoveX(h.x, h.vx, r); h.walk += (uint32_t)abs(h.x - ox); }
    if (!palIsFloor(palAt(palColOf(h.x), r))) { palFall(true); break; }
    if (++h.t >= h.lt) h.st = h.vx ? PH_RUN : PH_STAND;
    break;
  }
  case PH_JUP:
    if (!palIsFloor(palAt(palColOf(h.x), palRowOf(h.y)))) { palFall(false); break; }
    h.t++;
    if (h.lr >= 0) {
      int32_t dx = ((int32_t)(h.lb - h.dir * 8) << 8) - h.x;
      h.x += dx > 512 ? 512 : (dx < -512 ? -512 : dx);
      if (h.t >= 10) palHang(h.lb, h.lr, h.dir);
    } else if (h.t >= 16) h.st = PH_STAND;
    break;
  case PH_HANG: {
    h.t++;
    char e = palAt((int16_t)((h.lb + h.dir * 4) / PAL_TW), h.lr);
    if (!palIsFloor(e) || palGateShut(e)) { palFall(false); break; }
    if (in.u && h.t > 2) { h.st = PH_CLIMB; h.t = 0; sfx(380, 40); }
    else if (in.dT) palFall(false);
    break;
  }
  case PH_CLIMB:
    palClimbAt((uint8_t)++h.t);
    if (h.t >= 30) { h.st = PH_STAND; h.t = 0; palEnter(palColOf(h.x), h.lr); }
    break;
  case PH_DOWN:
    if (++h.t >= 20) palHang(h.lb, h.lr, h.dir);
    else palClimbAt((uint8_t)(30 - h.t * 30 / 20));
    break;
  case PH_FIGHT: palFight(in); break;
  case PH_DEAD: if (h.t < 1000) h.t++; break;
  case PH_EXIT: {
    h.t++;
    int32_t cx = (int32_t)(palColOf(h.x) * PAL_TW + PAL_TW / 2) << 8;
    int32_t dx = cx - h.x;
    h.x += dx > 256 ? 256 : (dx < -256 ? -256 : dx);
    break;
  }
  }
}

// ---------------- guards ----------------
static void palGuardHit(PalGuard &g) {
  PalHero &h = palHero;
  int16_t dx = (int16_t)((h.x - g.x) >> 8);
  if (h.st == PH_DEAD || h.st == PH_EXIT || palRowOf(h.y) != g.row || abs(dx) > 50 || (dx > 0) != (g.dir > 0)) {
    sfx(600, 20);                                      // a miss
    return;
  }
  if (h.st == PH_FIGHT && h.act == PA_BLOCK && (h.dir > 0) == (dx < 0)) {   // parried
    sfx(2000, 40); g.st = PG_RECOIL; g.t = 0;
    return;
  }
  sfx(250, 100);
  palHurt();
  if (h.st == PH_DEAD) return;
  int16_t r = palRowOf(h.y);
  if (h.st == PH_FIGHT) { h.act = PA_HIT; h.at = 0; }
  else if (h.st <= PH_STEP) { h.st = PH_LAND; h.lt = 12; h.t = 0; h.vx = 0; }
  if (h.st == PH_FIGHT || h.st == PH_LAND) h.x = palMoveSafe(h.x, g.dir * (8 << 8), r);
}

static void palGuardTick(PalGuard &g) {
  PalHero &h = palHero;
  if (g.st == PG_DEAD) return;
  if (g.st == PG_DYING) { if (++g.t >= 40) g.st = PG_DEAD; return; }
  const PalSkill &S = PAL_SKILL[g.sk];
  switch (g.st) {
  case PG_STRIKE:
    if (++g.t == S.wind) palGuardHit(g);
    if (g.t >= S.wind + 12) { g.st = PG_READY; g.cool = (uint8_t)(S.cool + random(S.rnd)); }
    return;
  case PG_BLOCK:
    if (++g.t >= 14) { g.st = PG_READY; g.cool = random(100) < S.counter ? 2 : S.cool / 2; }
    return;
  case PG_HURT: case PG_RECOIL:
    if (++g.t >= 14) { g.st = PG_READY; g.cool = S.cool / 2; }
    return;
  }
  bool sees = h.st != PH_DEAD && h.st != PH_EXIT && h.st != PH_HANG && h.st != PH_CLIMB && h.st != PH_DOWN &&
              palRowOf(h.y) == g.row && (h.x >> 8) / SCR_W == (g.x >> 8) / SCR_W;
  if (!sees) { g.st = PG_IDLE; return; }
  if (g.st == PG_IDLE) { g.st = PG_READY; g.cool = S.cool / 2 + 10; }
  int16_t dx = (int16_t)((h.x - g.x) >> 8), dist = (int16_t)abs(dx);
  g.dir = dx > 0 ? 1 : -1;
  if (h.st == PH_FIGHT && h.act == PA_STRIKE && h.at == 1 && dist <= 60 && random(100) < S.block) {
    g.st = PG_BLOCK; g.t = 0;                          // sees the blow coming
    return;
  }
  int32_t v = dist > 36 ? g.dir * S.speed : (dist < 28 ? -g.dir * S.speed * 2 / 3 : 0);
  if (v) {
    int32_t nx = g.x + v;
    if (nx < g.x0) nx = g.x0;
    if (nx > g.x1) nx = g.x1;
    if (g.dir > 0 && h.x - nx < (24 << 8) && nx > g.x) nx = g.x;   // not into the runner
    if (g.dir < 0 && nx - h.x < (24 << 8) && nx < g.x) nx = g.x;
    g.walk += (uint32_t)abs(nx - g.x);
    g.x = nx;
  }
  if (g.cool) g.cool--;
  else if (dist <= 50) { g.st = PG_STRIKE; g.t = 0; }
}

// ---------------- the world: gates, the door, loose and falling tiles ----------------
static void palRubble(int16_t c, int16_t r) {
  char &ch = palMap[r][c];
  palPlate(ch);                                        // a falling tile presses a plate for good
  if (ch == '_') ch = '-';
  else if (ch == '~') palShake(c, r, 22);
  sfx(80, 120);
}

static void palWorldTick() {
  PalHero &h = palHero;
  for (uint8_t i = 0; i < 4; i++) {
    if (palGateWant >> i & 1) { if (palGate[i] < 64) { palGate[i] += 2; if (palGate[i] % 16 == 0) sfx(160, 15); } }
    else if (palGate[i]) { palGate[i] = palGate[i] > 8 ? palGate[i] - 8 : 0; if (!palGate[i]) sfx(90, 90); }
  }
  if (palExitOn && palExit < 64) palExit++;
  uint8_t looseT = palLv >= 2 ? 20 : 25;
  for (uint8_t i = 0; i < PAL_NL; i++) {
    PalShake &s = palSh[i];
    if (!s.on || ++s.t < looseT) continue;
    s.on = false;
    palMap[s.r][s.c] = ' ';
    for (uint8_t k = 0; k < PAL_NF; k++)
      if (!palF[k].on) { palF[k] = { (int32_t)palLine(s.r) << 8, 0, s.c, true, false }; break; }
    sfx(110, 80);
  }
  for (uint8_t k = 0; k < PAL_NF; k++) {
    PalFall &f = palF[k];
    if (!f.on) continue;
    f.vy += PAL_G;
    if (f.vy > PAL_VMAX) f.vy = PAL_VMAX;
    int32_t ny = f.y + f.vy;
    int16_t hx = (int16_t)(h.x >> 8), head = (int16_t)((h.y >> 8) - 44);
    if (!f.hit && h.st != PH_DEAD && hx + 6 > f.c * PAL_TW && hx - 6 < (f.c + 1) * PAL_TW &&
        (f.y >> 8) + 8 < head && (ny >> 8) + 8 >= head) {
      f.hit = true; sfx(150, 100);
      palHurt();
      if (h.st <= PH_STEP || h.st == PH_FIGHT) { h.st = PH_LAND; h.lt = 14; h.t = 0; h.vx = 0; h.act = PA_NONE; }
    }
    int16_t p0 = (int16_t)(f.y >> 8);
    int16_t r = (int16_t)((p0 - PAL_FL + 125) / PAL_TH - 1);
    if (palLine(r) <= (ny >> 8) && r < palH && palAt(f.c, r) != ' ') { palRubble(f.c, r); f.on = false; continue; }
    f.y = ny;
    if ((f.y >> 8) > palH * PAL_TH + 60) f.on = false;
  }
  int16_t r = palRowOf(h.y), hc = palColOf(h.x);       // a gate shutting pushes the runner out
  for (int16_t cc = (int16_t)(hc - 1); cc <= hc + 1; cc++)
    if (palGateShut(palAt(cc, r))) {
      int16_t gl = (int16_t)(cc * PAL_TW + PAL_TW / 2), hx = (int16_t)(h.x >> 8);
      if (abs(hx - gl) < 9) h.x = (int32_t)(hx < gl ? gl - 9 : gl + 9) << 8;
    }
  if (palMsgT) palMsgT--;
}

static void palStep(const PalIn &in) {
  palTicks++;
  palHeroTick(in);
  for (uint8_t i = 0; i < palNG; i++) palGuardTick(palG[i]);
  palWorldTick();
}

// ---------------- figures: a jointed body, posed per step ----------------
constexpr double palTaylor(double x) {
  double t = x, s = x;
  for (int k = 1; k < 10; k++) { t *= -x * x / ((2 * k) * (2 * k + 1)); s += t; }
  return s;
}
struct PalSinTab {
  int8_t v[64];
  constexpr PalSinTab() : v() {
    for (int i = 0; i < 64; i++) {
      int q = i / 16, r = i % 16;
      int iv = (int)(palTaylor(((q & 1) ? 16 - r : r) * 3.14159265358979 / 32) * 127 + 0.5);
      v[i] = (int8_t)(q >= 2 ? -iv : iv);
    }
  }
};
static constexpr PalSinTab PAL_SIN{};
static int16_t palS(int a) { return PAL_SIN.v[a & 63]; }
static int16_t palC(int a) { return PAL_SIN.v[(a + 16) & 63]; }

/* A pose: hip height; lean of the body; front leg (thigh angle, knee
   bend), back leg; front arm (upper arm angle, elbow bend), back arm;
   tilt of the whole figure round the feet. Angles in 1/64 turn, 0 =
   straight down, positive = forward.                                  */
enum { P_HIP, P_LEAN, P_FA, P_FK, P_BA, P_BK, P_ARA, P_ARE, P_ALA, P_ALE, P_TILT, P_N };
struct PalPose { int8_t v[P_N]; };
static const PalPose PP_STAND  = {{ 24, 0, 2, 1, -2, 1, 1, 3, -1, 3, 0 }};
static const PalPose PP_GUARD  = {{ 21, 1, 8, 7, -8, 4, 10, 10, -6, 14, 0 }};
static const PalPose PP_WIND   = {{ 21, -2, 8, 7, -8, 4, 22, 14, -8, 12, 0 }};
static const PalPose PP_LUNGE  = {{ 19, 7, 14, 3, -11, 6, 16, 0, -12, 10, 0 }};
static const PalPose PP_BLOCK  = {{ 21, -2, 6, 7, -9, 5, 12, 14, -6, 14, 0 }};
static const PalPose PP_REEL   = {{ 22, -6, 2, 3, -10, 6, 6, 6, -10, 8, 0 }};
static const PalPose PP_JUMP   = {{ 22, 6, 12, 18, -8, 16, 16, 6, -10, 6, 0 }};
static const PalPose PP_LEAP   = {{ 23, 4, 10, 4, -8, 8, 10, 6, -6, 8, 0 }};
static const PalPose PP_FALL   = {{ 24, -1, 4, 6, -3, 10, 26, 4, 22, 6, 0 }};
static const PalPose PP_CROUCH = {{ 15, 8, 15, 30, -4, 26, 8, 8, 4, 8, 0 }};
static const PalPose PP_REACH  = {{ 24, 1, 2, 2, -2, 3, 30, 1, 29, 1, 0 }};
static const PalPose PP_HANG   = {{ 24, 0, 2, 2, -1, 3, 31, 1, 30, 1, 0 }};
static const PalPose PP_PULL   = {{ 22, 2, 6, 10, -2, 8, 28, -14, 27, -14, 0 }};
static const PalPose PP_KNEE   = {{ 14, 10, 20, 34, -6, 20, 20, 6, 14, 6, 0 }};
static const PalPose PP_LIE    = {{ 23, 0, 3, 2, -3, 4, 6, 4, 12, 6, -16 }};

static void palMix(PalPose &o, const PalPose &a, const PalPose &b, int16_t t, int16_t n) {
  if (t < 0) t = 0;
  if (t > n) t = n;
  for (uint8_t i = 0; i < P_N; i++) o.v[i] = (int8_t)(a.v[i] + (b.v[i] - a.v[i]) * t / n);
}
// a running or walking stride; q = phase, 64 per two steps
static void palStride(PalPose &o, uint8_t q, int8_t amp, int8_t knee, int8_t lean) {
  int16_t s = palS(q), c = palC(q);
  o = PP_STAND;
  o.v[P_FA] = (int8_t)(amp * s / 127);  o.v[P_FK] = (int8_t)(3 + (c > 0 ? knee * c / 127 : 0));
  o.v[P_BA] = (int8_t)(-amp * s / 127); o.v[P_BK] = (int8_t)(3 + (c < 0 ? -knee * c / 127 : 0));
  o.v[P_ARA] = (int8_t)(2 - amp * 3 / 4 * s / 127); o.v[P_ARE] = 9;
  o.v[P_ALA] = (int8_t)(2 + amp * 3 / 4 * s / 127); o.v[P_ALE] = 9;
  o.v[P_HIP] = (int8_t)(23 - abs(palS(q * 2)) / 100);
  o.v[P_LEAN] = lean;
}

struct PalLook { col skin, hair, top, legs, boots, sash; bool helm; };
struct PalPt { int16_t x, y; };
static PalPt palGo(PalPt p, int16_t len, int a) {
  return { (int16_t)(p.x + len * palS(a) / 127), (int16_t)(p.y + len * palC(a) / 127) };
}
static void palLimb(PalPt a, PalPt b, col c, uint8_t w) {
  gfx.line(a.x, a.y, b.x, b.y, c);
  if (w >= 1) { gfx.line(a.x + 1, a.y, b.x + 1, b.y, c); gfx.line(a.x, a.y + 1, b.x, b.y + 1, c); }
  if (w >= 2) { gfx.line(a.x - 1, a.y, b.x - 1, b.y, c); gfx.line(a.x, a.y - 1, b.x, b.y - 1, c); gfx.line(a.x + 1, a.y + 1, b.x + 1, b.y + 1, c); }
  if (w >= 3) { gfx.line(a.x + 2, a.y, b.x + 2, b.y, c); gfx.line(a.x - 1, a.y + 1, b.x - 1, b.y + 1, c); gfx.line(a.x + 2, a.y + 1, b.x + 2, b.y + 1, c); }
}

// fx: 0 normal, 1 white (hit), 2 dark (in the doorway)
static void palFigure(int16_t ox, int16_t oy, int8_t dir, const PalPose &p, const PalLook &L, bool sword, uint8_t fx) {
  int8_t lean = p.v[P_LEAN], tl = p.v[P_TILT];
  PalPt hip = { 0, (int16_t)-p.v[P_HIP] };
  PalPt nk = palGo(hip, 15, 32 - lean), hd = palGo(nk, 5, 32 - lean);
  PalPt kF = palGo(hip, 12, p.v[P_FA]), fF = palGo(kF, 12, p.v[P_FA] - p.v[P_FK]);
  PalPt kB = palGo(hip, 12, p.v[P_BA]), fB = palGo(kB, 12, p.v[P_BA] - p.v[P_BK]);
  PalPt eF = palGo(nk, 9, p.v[P_ARA]), hF = palGo(eF, 9, p.v[P_ARA] + p.v[P_ARE]);
  PalPt eB = palGo(nk, 9, p.v[P_ALA]), hB = palGo(eB, 9, p.v[P_ALA] + p.v[P_ALE]);
  int sa = p.v[P_ARA] + p.v[P_ARE] + 4;
  PalPt tip = palGo(hF, 21, sa), gA = palGo(hF, 3, sa + 16), gB = palGo(hF, 3, sa - 16), pm = palGo(hF, 3, sa + 32);
  PalPt toeF = { (int16_t)(fF.x + 4), fF.y }, toeB = { (int16_t)(fB.x + 4), fB.y };
  PalPt crown = palGo(hd, 4, 32 - lean), plume = palGo(hd, 7, 32 - lean - 10);
  PalPt *pts[] = { &hip, &nk, &hd, &kF, &fF, &kB, &fB, &eF, &hF, &eB, &hB, &tip, &gA, &gB, &pm, &toeF, &toeB, &crown, &plume };
  int16_t ts = palS(tl), tc = palC(tl);
  for (PalPt *q : pts) {                              // tilt, mirror, place
    int16_t x = q->x, y = q->y;
    if (tl) { int16_t nx = (int16_t)((x * tc - y * ts) / 127); y = (int16_t)((x * ts + y * tc) / 127); x = nx; }
    q->x = (int16_t)(ox + dir * x); q->y = (int16_t)(oy + y);
  }
  col skin = L.skin, top = L.top, legs = L.legs, boots = L.boots, hair = L.hair, sash = L.sash;
  col steel = rgb(190, 200, 215), gold = rgb(240, 190, 40);
  if (fx == 1) skin = top = legs = boots = hair = sash = steel = gold = C::WHITE;
  if (fx == 2) { skin = colDark(skin, 2); top = colDark(top, 2); legs = colDark(legs, 2); boots = colDark(boots, 2);
                 hair = colDark(hair, 2); sash = colDark(sash, 2); steel = colDark(steel, 2); gold = colDark(gold, 2); }
  palLimb(nk, eB, colDark(top), 1); palLimb(eB, hB, colDark(skin), 1);              // back arm
  palLimb(hip, kB, colDark(legs), 2); palLimb(kB, fB, colDark(legs), 1);            // back leg
  palLimb(fB, toeB, colDark(boots), 1);
  palLimb(hip, nk, top, 3);                                                         // body
  gfx.fill(hip.x - 3, hip.y - 2, 7, 3, sash);
  gfx.disc(hd.x, hd.y, 4, skin);                                                    // head
  if (L.helm) {
    gfx.line(hd.x, hd.y - 1, plume.x, plume.y, colLight(top));
    gfx.disc(crown.x, crown.y + 1, 3, steel);
    gfx.fill(hd.x - 4, hd.y - 3, 9, 2, steel);
  } else {
    gfx.disc(crown.x, crown.y + 1, 3, hair);
    gfx.hline(hd.x - 4, hd.y - 2, 9, sash);
  }
  gfx.pixel(hd.x + dir * 2, hd.y, C::BLACK);
  palLimb(hip, kF, legs, 2); palLimb(kF, fF, legs, 2); palLimb(fF, toeF, boots, 1); // front leg
  if (sword) {
    palLimb(hF, tip, steel, 0);
    gfx.line(hF.x, hF.y - 1, tip.x, tip.y - 1, C::WHITE);
    palLimb(gA, gB, gold, 0);
    gfx.pixel(pm.x, pm.y, gold);
  }
  palLimb(nk, eF, top, 1); palLimb(eF, hF, skin, 1);                                // front arm
}

static const PalLook PAL_HERO_LOOK = { rgb(236, 184, 140), rgb(60, 36, 20), rgb(0, 165, 175), rgb(240, 232, 210),
                                       rgb(130, 75, 30), C::RED, false };
static const col PAL_GUARD_TOP[PAL_LEVELS + 1] = { rgb(60, 150, 60), rgb(225, 125, 40), rgb(150, 60, 180),
                                                    rgb(40, 90, 200), rgb(190, 30, 40) };

static void palHeroPose(PalPose &o, int16_t &dy) {
  PalHero &h = palHero;
  dy = 0;
  uint8_t q = (uint8_t)((h.walk >> 8) * 64 / 42);
  switch (h.st) {
  case PH_RUN:
    if (abs(h.vx) < PAL_RUN / 3) palStride(o, (uint8_t)((h.walk >> 8) * 64 / 26), 6, 8, 2);
    else palStride(o, q, 13, 15, 4);
    break;
  case PH_STEP: palStride(o, (uint8_t)((h.walk >> 8) * 64 / 22), 7, 8, 1); break;
  case PH_JUMP:
    if (h.t < 4) palMix(o, PP_CROUCH, PP_LEAP, h.t, 4);
    else if (h.vy < 0) palMix(o, PP_LEAP, PP_JUMP, h.t - 4, 8);
    else palMix(o, PP_JUMP, PP_LEAP, (int16_t)(h.vy / PAL_G), 16);
    break;
  case PH_FALL: o = PP_FALL; o.v[P_ARA] = (int8_t)(26 + palS(h.t * 6) / 40); o.v[P_ALA] = (int8_t)(22 - palS(h.t * 6) / 40); break;
  case PH_LAND: palMix(o, PP_CROUCH, PP_STAND, h.t - h.lt / 2, h.lt / 2 + 1); break;
  case PH_JUP:
    if (h.t < 4) palMix(o, PP_STAND, PP_CROUCH, h.t, 4);
    else palMix(o, PP_CROUCH, PP_REACH, h.t - 4, 5);
    if (h.lr < 0) dy = (int16_t)(h.t > 6 && h.t < 14 ? -(8 - abs(h.t - 10) * 2) : 0);
    break;
  case PH_HANG: o = PP_HANG; o.v[P_FA] = (int8_t)(2 + palS(h.t * 2) / 60); break;
  case PH_CLIMB: case PH_DOWN: {
    int16_t t = h.st == PH_CLIMB ? h.t : 30 - h.t * 30 / 20;
    if (t < 12) palMix(o, PP_HANG, PP_PULL, t, 12);
    else if (t < 22) palMix(o, PP_PULL, PP_KNEE, t - 12, 10);
    else palMix(o, PP_KNEE, PP_STAND, t - 22, 8);
    break;
  }
  case PH_FIGHT:
    if (h.act == PA_STRIKE) {
      if (h.at < 5) palMix(o, PP_GUARD, PP_WIND, h.at, 4);
      else if (h.at < 9) palMix(o, PP_WIND, PP_LUNGE, h.at - 4, 2);
      else palMix(o, PP_LUNGE, PP_GUARD, h.at - 9, 6);
    } else if (h.act == PA_BLOCK) palMix(o, PP_GUARD, PP_BLOCK, h.at < 7 ? h.at : 14 - h.at, 2);
    else if (h.act == PA_HIT) palMix(o, PP_REEL, PP_GUARD, h.at, 10);
    else {
      o = PP_GUARD;
      int8_t w = (int8_t)(palS((int)((h.walk >> 8) * 4)) / 40);
      o.v[P_FA] = (int8_t)(o.v[P_FA] + w); o.v[P_BA] = (int8_t)(o.v[P_BA] + w);
    }
    break;
  case PH_DEAD:
    if (h.spiked) { o = PP_CROUCH; o.v[P_LEAN] = 14; o.v[P_ARA] = 2; o.v[P_ALA] = -4; }
    else palMix(o, PP_STAND, PP_LIE, h.t, 22);
    break;
  case PH_EXIT: palStride(o, (uint8_t)(h.t * 4), 7, 8, 1); dy = (int16_t)(-h.t / 4); break;
  default:
    o = PP_STAND;
    o.v[P_ARE] = (int8_t)(3 + palS((int)(palTicks / 2)) / 64);
    break;
  }
}

static void palGuardPose(const PalGuard &g, PalPose &o) {
  const PalSkill &S = PAL_SKILL[g.sk];
  switch (g.st) {
  case PG_IDLE: o = PP_STAND; o.v[P_ARA] = 8; o.v[P_ARE] = 14; break;
  case PG_STRIKE:
    if (g.t < S.wind) palMix(o, PP_GUARD, PP_WIND, g.t, S.wind);
    else if (g.t < S.wind + 4) o = PP_LUNGE;
    else palMix(o, PP_LUNGE, PP_GUARD, g.t - S.wind - 4, 8);
    break;
  case PG_BLOCK: o = PP_BLOCK; break;
  case PG_HURT: case PG_RECOIL: palMix(o, PP_REEL, PP_GUARD, g.t, 14); break;
  case PG_DYING: palMix(o, PP_REEL, PP_LIE, g.t, 30); break;
  case PG_DEAD: o = PP_LIE; break;
  default: {
    o = PP_GUARD;
    int8_t w = (int8_t)(palS((int)((g.walk >> 8) * 4)) / 40);
    o.v[P_FA] = (int8_t)(o.v[P_FA] + w); o.v[P_BA] = (int8_t)(o.v[P_BA] - w);
  }
  }
}

// ---------------- drawing the rooms ----------------
struct PalTheme { col back, mortar, backLt, top, front, frontDk, wall, wallLt, wallDk; };
static const PalTheme PAL_THEME[3] = {                 // colours straight from the cube
  { rgb(0, 42, 102), rgb(0, 0, 51), rgb(51, 42, 102), rgb(153, 170, 204), rgb(102, 128, 153), rgb(51, 42, 102),
    rgb(51, 85, 153), rgb(102, 128, 204), rgb(0, 42, 102) },                         // dungeon blue
  { rgb(102, 42, 0), rgb(51, 0, 0), rgb(153, 85, 51), rgb(255, 213, 153), rgb(204, 128, 51), rgb(102, 42, 0),
    rgb(204, 170, 102), rgb(255, 213, 153), rgb(153, 85, 51) },                      // sandstone halls
  { rgb(51, 0, 102), rgb(0, 0, 51), rgb(102, 42, 153), rgb(204, 170, 255), rgb(153, 85, 204), rgb(51, 0, 102),
    rgb(102, 85, 153), rgb(153, 128, 204), rgb(51, 42, 102) } };                     // the tower at night

static void palTorch(int16_t x, int16_t y, uint8_t seed) {           // x = middle, y = flame bottom
  gfx.fill(x - 1, y + 2, 3, 9, rgb(90, 60, 30));
  gfx.fill(x - 3, y + 1, 7, 2, rgb(150, 105, 50));
  uint8_t f = (uint8_t)((palTicks / 3 + seed) % 4);
  int16_t fh = (int16_t)(7 + (f & 1) * 2), lean = (int16_t)(f == 2 ? 1 : (f == 3 ? -1 : 0));
  gfx.disc(x, y - 2, 3, C::ORANGE);
  gfx.triangle(x - 3, y - 2, x + 3, y - 2, x + lean, y - 2 - fh, C::ORANGE);
  gfx.disc(x, y - 1, 1, C::YELLOW);
  gfx.vline(x + lean / 2, y - 1 - fh / 2, fh / 2, C::YELLOW);
}

static void palSlab(int16_t sx, int16_t fy, const PalTheme &T, bool capL, bool capR) {
  gfx.fill(sx, fy - 4, PAL_TW, 4, T.top);
  gfx.hline(sx, fy - 4, PAL_TW, colLight(T.top));
  gfx.fill(sx, fy, PAL_TW, 8, T.front);
  gfx.hline(sx, fy + 7, PAL_TW, T.frontDk);
  gfx.vline(sx + PAL_TW - 1, fy, 8, T.frontDk);
  if (capL) gfx.vline(sx, fy - 4, 12, T.frontDk);
  if (capR) gfx.vline(sx + PAL_TW - 1, fy - 4, 12, T.frontDk);
}

static void palCell(int16_t c, int16_t r, int16_t sx, int16_t sy, const PalTheme &T, int16_t hc, int16_t hr) {
  char ch = palAt(c, r);
  uint32_t hs = (uint32_t)(c * 7919 + r * 104729) * 2654435761u;
  if (ch == '#') {
    gfx.fill(sx, sy, PAL_TW, PAL_TH, T.wall);
    for (uint8_t k = 0; k < 4; k++) {                    // stone courses
      int16_t y = (int16_t)(sy + k * 16);
      gfx.hline(sx, y + 15, PAL_TW, T.wallDk);
      gfx.vline(sx + ((k + c) & 1) * 16 + 4, y, 15, T.wallDk);
      if ((hs >> (k * 3)) % 5 == 0) gfx.fill(sx + ((k + c) & 1) * 16 + 6, y + 2, 9, 11, colLight(T.wall));
    }
    if (palAt(c, r - 1) != '#') gfx.hline(sx, sy, PAL_TW, T.wallLt);
    if (palAt(c - 1, r) != '#') gfx.vline(sx, sy, PAL_TH, T.wallLt);
    if (palAt(c + 1, r) != '#') gfx.vline(sx + PAL_TW - 1, sy, PAL_TH, T.wallDk);
    return;
  }
  gfx.fill(sx, sy, PAL_TW, PAL_TH, T.back);              // the wall behind
  for (uint8_t k = 0; k < 4; k++) {
    int16_t y = (int16_t)(sy + k * 13);
    gfx.hline(sx, y + 12, PAL_TW, T.mortar);
    gfx.vline(sx + ((k + c) & 1) * 16 + 6, y, 12, T.mortar);
    if ((hs >> (k * 4)) % 7 == 0) gfx.fill(sx + ((k + c) & 1) * 16 + 8, y + 1, 12, 10, T.backLt);
  }
  int16_t fy = (int16_t)(sy + PAL_FL);
  if (ch == 't') palTorch(sx + 16, sy + 20, (uint8_t)(c * 3 + r));
  if (ch == 'E') {                                       // the exit door, its panel rises
    int16_t dx = sx + 3, dy = sy + 8, dw = 26, dh = (int16_t)(fy - 4 - dy);
    gfx.fill(dx - 4, dy - 6, dw + 8, dh + 6, T.wallLt);
    gfx.triangle(dx - 4, dy - 6, dx + dw + 3, dy - 6, dx + dw / 2, dy - 16, T.wallLt);
    gfx.fill(dx, dy, dw, dh, rgb(14, 10, 8));
    for (uint8_t k = 0; k < 4; k++) gfx.fill(dx + 2, dy + dh - 4 - k * 9, dw - 4, 3, colDark(T.top, (uint8_t)(k + 1)));
    int16_t ph = (int16_t)(dh * (64 - palExit) / 64);
    if (ph > 0) {
      gfx.fill(dx, dy, dw, ph, rgb(150, 100, 40));
      for (int16_t y = dy + 5; y < dy + ph; y += 7) gfx.hline(dx, y, dw, rgb(110, 70, 25));
      gfx.vline(dx + dw / 2, dy, ph, rgb(110, 70, 25));
      gfx.hline(dx, dy + ph - 1, dw, rgb(220, 170, 80));
    }
    gfx.disc(dx + dw / 2, dy - 9, 2, palExit >= 64 ? C::YELLOW : C::DKRED);
  }
  if (!palIsFloor(ch)) return;
  bool shake = false;
  if (ch == '~') for (uint8_t i = 0; i < PAL_NL; i++) if (palSh[i].on && palSh[i].c == c && palSh[i].r == r) shake = true;
  int16_t sh = shake ? (int16_t)((palTicks & 2) ? 1 : -1) : 0;
  palSlab(sx, fy + sh, T, !palIsFloor(palAt(c - 1, r)), !palIsFloor(palAt(c + 1, r)));
  bool onIt = hc == c && hr == r && palHero.st != PH_JUMP && palHero.st != PH_FALL;
  switch (ch) {
  case '~':
    gfx.line(sx + 5, fy - 3 + sh, sx + 12, fy + 4 + sh, T.frontDk);
    gfx.line(sx + 20, fy + sh, sx + 27, fy - 3 + sh, T.frontDk);
    break;
  case '-':
    for (uint8_t k = 0; k < 5; k++) gfx.disc(sx + 4 + k * 6, fy - 4 - (k & 1), 2, k & 1 ? T.front : T.frontDk);
    break;
  case '^': {
    bool out = (hr == r && abs(hc - c) <= 1) || palAt(c, r - 1) == ' ';   // near, or at the bottom of a pit
    for (uint8_t k = 0; k < 4; k++) {
      int16_t x = (int16_t)(sx + 3 + k * 7);
      if (out) { gfx.triangle(x, fy - 3, x + 5, fy - 3, x + 2, fy - 17, rgb(205, 210, 220)); gfx.vline(x + 2, fy - 15, 11, C::WHITE); }
      else gfx.fill(x + 1, fy - 5, 3, 2, rgb(150, 150, 160));
    }
    if (out && palHero.spiked && hc == c) for (uint8_t k = 0; k < 6; k++) gfx.pixel(sx + 5 + k * 4, fy - 8 - (k * 5) % 7, C::RED);
    break;
  }
  case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case 'X': {
    col pc = ch == 'X' ? rgb(240, 200, 60) : (ch <= '4' ? rgb(200, 170, 120) : rgb(170, 80, 60));
    gfx.fill(sx + 6, fy - (onIt ? 5 : 7), 20, onIt ? 2 : 4, pc);
    gfx.hline(sx + 6, fy - (onIt ? 5 : 7), 20, colLight(pc));
    break;
  }
  case 'h': case 'H': {
    bool big = ch == 'H';
    col pc = big ? rgb(60, 220, 120) : rgb(230, 40, 50);
    int16_t x = sx + 16, y = (int16_t)(fy - 9);
    gfx.disc(x, y, big ? 5 : 4, pc);
    gfx.fill(x - 1, y - (big ? 10 : 8), 3, 5, rgb(200, 220, 230));
    gfx.fill(x - 2, y - (big ? 11 : 9), 5, 2, C::BROWN);
    gfx.pixel(x - 2, y - 2, C::WHITE);
    if ((palTicks / 8 + c) % 3 == 0) gfx.pixel(x + 1, y - 12 - (int16_t)(palTicks / 4 % 4), colLight(pc));
    break;
  }
  case 'S':
    gfx.line(sx + 4, fy - 5, sx + 24, fy - 5, rgb(200, 210, 225));
    gfx.line(sx + 5, fy - 6, sx + 23, fy - 6, C::WHITE);
    gfx.vline(sx + 25, fy - 8, 6, rgb(240, 190, 40));
    gfx.hline(sx + 26, fy - 5, 3, rgb(240, 190, 40));
    break;
  }
}

// pillars and gates stand in front of the figures
static void palFront(int16_t c, int16_t r, int16_t sx, int16_t sy, const PalTheme &T) {
  char ch = palAt(c, r);
  if (ch == '|') {
    int16_t x = sx + 9, top = sy, bot = (int16_t)(sy + PAL_FL - 4);
    gfx.fill(x, top + 5, 14, bot - top - 10, T.wallLt);
    gfx.fill(x + 8, top + 5, 6, bot - top - 10, T.wall);
    gfx.vline(x + 3, top + 5, bot - top - 10, C::WHITE);
    gfx.fill(x - 3, top, 20, 5, T.wallLt);
    gfx.fill(x - 3, bot - 5, 20, 5, T.wallLt);
    gfx.hline(x - 3, bot - 1, 20, T.wallDk);
  }
  if (ch >= 'a' && ch <= 'd') {
    int16_t lvl = palGate[ch - 'a'];
    int16_t len = (int16_t)((PAL_FL - 4) * (64 - lvl) / 64);
    gfx.fill(sx + 9, sy, 14, 3, T.wallDk);
    if (len > 0) {
      for (uint8_t k = 0; k < 4; k++) gfx.vline(sx + 10 + k * 4, sy + 3, len, rgb(70, 74, 84));
      for (int16_t y = (int16_t)(sy + 3 + len - 2); y > sy + 3; y -= 10) gfx.hline(sx + 9, y, 14, rgb(110, 115, 130));
      for (uint8_t k = 0; k < 4; k++) gfx.pixel(sx + 10 + k * 4, sy + 2 + len, rgb(150, 155, 170));
    }
  }
}

static void palView(int16_t &rx, int16_t &ry) {
  PalHero &h = palHero;
  int16_t row = (h.st == PH_HANG || h.st == PH_CLIMB || h.st == PH_DOWN) ? h.lr : palRowOf(h.y);
  if (row >= palH) row = (int16_t)(palH - 1);
  rx = (int16_t)((h.x >> 8) / SCR_W); ry = (int16_t)(row / 3);
  if (rx > palW / 10 - 1) rx = (int16_t)(palW / 10 - 1);
  if (rx < 0) rx = 0;
}

static void palTris(int16_t x, int16_t y, uint8_t n, uint8_t full, col c, int8_t step) {
  for (uint8_t i = 0; i < n; i++) {
    int16_t xx = (int16_t)(x + i * step);
    if (i < full) { gfx.triangle(xx, y + 12, xx + 9, y + 12, xx + 4, y, c); gfx.vline(xx + 3, y + 5, 5, colLight(c, 2)); }
    else { gfx.line(xx, y + 12, xx + 4, y, colDark(c, 2)); gfx.line(xx + 9, y + 12, xx + 4, y, colDark(c, 2)); gfx.hline(xx, y + 12, 10, colDark(c, 2)); }
  }
}

static void palDraw(int32_t left) {
  PalHero &h = palHero;
  int16_t rx, ry;
  palView(rx, ry);
  const PalTheme &T = PAL_THEME[PAL_LEVEL[palLv].theme];
  int16_t c0 = (int16_t)(rx * 10), r0 = (int16_t)(ry * 3), ox = (int16_t)(rx * SCR_W), oy = (int16_t)(ry * 3 * PAL_TH - TOP_H);
  int16_t hc = palColOf(h.x), hr = palRowOf(h.y);
  if (h.st == PH_HANG || h.st == PH_CLIMB || h.st == PH_DOWN || h.st == PH_JUMP || h.st == PH_FALL) hr = -1;
  for (int16_t r = 0; r < 3; r++)
    for (int16_t c = 0; c < 10; c++) palCell(c0 + c, r0 + r, c * PAL_TW, TOP_H + r * PAL_TH, T, hc, hr);
  for (uint8_t k = 0; k < PAL_NF; k++)
    if (palF[k].on) {
      int16_t sx = (int16_t)(palF[k].c * PAL_TW - ox), sy = (int16_t)((palF[k].y >> 8) - oy);
      if (sx >= 0 && sx < SCR_W) palSlab(sx, sy, T, true, true);
    }
  for (uint8_t pass = 0; pass < 2; pass++)              // the fallen first, then the living
    for (uint8_t i = 0; i < palNG; i++) {
      const PalGuard &g = palG[i];
      if ((g.st >= PG_DYING) != (pass == 0)) continue;
      int16_t gx = (int16_t)((g.x >> 8) - ox), gy = (int16_t)(palLine(g.row) - 1 - oy);
      if (gx < -40 || gx > SCR_W + 40 || gy < 0 || gy > PAL_HUD + 50) continue;
      PalLook L = { rgb(200, 150, 110), C::BLACK, PAL_GUARD_TOP[g.sk], rgb(70, 60, 90), rgb(50, 40, 40),
                    g.sk == PAL_LEVELS ? rgb(240, 190, 40) : rgb(40, 40, 40), true };
      PalPose p;
      palGuardPose(g, p);
      palFigure(gx, gy, g.dir, p, L, true, g.st == PG_HURT && g.t < 4 ? 1 : 0);
    }
  if (!(h.st == PH_EXIT && h.t > 34)) {
    PalPose p;
    int16_t dy;
    palHeroPose(p, dy);
    bool sw = h.st == PH_FIGHT;
    palFigure((int16_t)((h.x >> 8) - ox), (int16_t)((h.y >> 8) - 1 - oy + dy), h.dir, p, PAL_HERO_LOOK, sw,
              h.st == PH_EXIT && h.t > 12 ? 2 : (h.hurt && (h.hurt / 3) % 2 && h.st != PH_DEAD ? 1 : 0));
  }
  for (int16_t r = 0; r < 3; r++)
    for (int16_t c = 0; c < 10; c++) palFront(c0 + c, r0 + r, c * PAL_TW, TOP_H + r * PAL_TH, T);

  // the strip below: flasks, the message or the level, the guard's flasks
  gfx.gradient(0, PAL_HUD, SCR_W, SCR_H - PAL_HUD, rgb(30, 22, 16), C::BLACK);
  gfx.hline(0, PAL_HUD, SCR_W, rgb(120, 90, 40));
  bool blink = h.hp == 1 && (palTicks / 10) % 2;
  palTris(6, PAL_HUD + 6, h.maxHp, blink ? 0 : h.hp, C::RED, 12);
  PalGuard *g = palFoe(200);
  if (g) palTris(SCR_W - 16, PAL_HUD + 6, PAL_SKILL[g->sk].hp, g->hp, PAL_GUARD_TOP[g->sk], -12);
  char b[32];
  const char *s = palMsgT ? palMsg : b;
  if (h.st == PH_DEAD) s = "YOU HAVE FALLEN";
  if (s == b) snprintf(b, sizeof(b), "LEVEL %u", palLv + 1);
  gfx.text(180 - Gfx::width(s, F_SMALL) / 2, PAL_HUD + 9, s, palMsgT || h.st == PH_DEAD ? C::YELLOW : C::LTGREY, F_SMALL);

  statusBar("PALACE", palScore, curHigh, rgb(210, 150, 40));
  int32_t sec = left > 0 ? (left + 999) / 1000 : 0;
  snprintf(b, sizeof(b), "%ld:%02ld", (long)(sec / 60), (long)(sec % 60));
  bool late = sec < 60 && (gameMillis() / 400) % 2;
  gfx.textShadow(82, 5, b, late ? C::RED : C::WHITE, C::BLACK, F_TEXT);
}

// ---------------- title, the run ----------------
static bool palTitle() {
  palPhase = 0;
  btnClear();
  uint32_t t0 = gameMillis();
  while (poll()) {
    if (btn(B_OK)) { sfx(1200, 60); return true; }
    uint32_t t = gameMillis() - t0;
    palTicks = t / PAL_TICK;
    gfx.gradient(0, 0, SCR_W, SCR_H, rgb(10, 10, 50), rgb(90, 40, 90));
    for (uint8_t i = 0; i < 40; i++) {
      uint32_t hs = (uint32_t)i * 2654435761u;
      gfx.pixel((int16_t)(hs % SCR_W), (int16_t)((hs >> 12) % 120), ((t / 300 + i) % 7) ? C::GREY : C::WHITE);
    }
    gfx.disc(262, 44, 16, rgb(255, 240, 190));
    gfx.disc(270, 38, 15, rgb(16, 14, 58));
    col sil = rgb(24, 14, 40), win = rgb(255, 200, 80);
    gfx.fill(20, 150, 280, 60, sil);                       // the palace
    gfx.disc(160, 150, 38, sil);
    gfx.triangle(156, 112, 164, 112, 160, 96, sil);
    gfx.disc(80, 150, 22, sil); gfx.disc(240, 150, 22, sil);
    gfx.fill(34, 104, 10, 50, sil); gfx.disc(39, 102, 7, sil); gfx.triangle(35, 98, 43, 98, 39, 86, sil);
    gfx.fill(276, 104, 10, 50, sil); gfx.disc(281, 102, 7, sil); gfx.triangle(277, 98, 285, 98, 281, 86, sil);
    for (uint8_t k = 0; k < 7; k++)
      if ((k * 5 + t / 900) % 4) gfx.fill(52 + k * 34, 166, 6, 9, win);
    gfx.rbox(150, 176, 20, 34, 9, win);
    gfx.textShadow(160 - Gfx::width("PALACE", F_BIG) / 2, 14, "PALACE", rgb(250, 200, 70), rgb(120, 40, 20), F_BIG);
    gfx.center(54, "escape before the time runs out", C::LTGREY, F_SMALL);
    PalPose p;
    palStride(p, (uint8_t)(t / 12), 13, 15, 4);
    palFigure((int16_t)(-20 + (t / 8) % 360), 222, 1, p, PAL_HERO_LOOK, false, 0);
    gfx.fill(0, 222, SCR_W, 18, rgb(40, 26, 30));
    gfx.text(8, 70, "L/R run   DOWN+L/R careful step", C::WHITE, F_SMALL);
    gfx.text(8, 82, "UP+L/R jump   UP climb   DOWN hang/drop", C::WHITE, F_SMALL);
    gfx.text(8, 94, "OK sword: OK strike  UP parry", C::WHITE, F_SMALL);
    if ((t / 500) % 2) {
      char b[24];
      snprintf(b, sizeof(b), "%s = start", okKeys());
      gfx.center(226, b, C::YELLOW, F_SMALL);
    }
    gfx.send();
  }
  return false;
}

// a few seconds of a still picture with a box on it (level done, time up, the end)
static bool palBanner(const char *a, const char *b, uint16_t ms) {
  palPhase = 2;
  uint32_t t0 = gameMillis();
  while (gameMillis() - t0 < ms) {
    if (!poll(false)) return false;
    gfx.rbox(40, 80, 240, 72, 8, C::BLACK);
    gfx.rframe(40, 80, 240, 72, 8, rgb(210, 150, 40));
    gfx.center(88, a, C::YELLOW, F_BOLD);
    gfx.center(122, b, C::WHITE, F_TEXT);
    gfx.send();
  }
  palPhase = 1;
  return true;
}

void palaceRun() {
  bool again = true, title = true;
  while (again) {
    again = false;
    if (title && !palTitle()) return;
    title = false;
    palScore = 0; palStartHp = 3; palStartSword = false;
    palLoad(0);
    uint32_t deadline = gameMillis() + PAL_TIME;
    char b[32];
    palDraw((int32_t)PAL_TIME);
    snprintf(b, sizeof(b), "LEVEL %u", palLv + 1);
    if (!palBanner(b, PAL_LEVEL[palLv].name, 1600)) return;
    deadline += 1600;
    uint32_t next = gameMillis();
    uint8_t ended = 0;                                   // 1 = time up, 2 = all levels done
    PalIn in = {};
    btnClear();
    while (!ended && poll()) {
      uint32_t now = gameMillis();
      in.l = btnHeld(B_LEFT); in.r = btnHeld(B_RIGHT); in.u = btnHeld(B_UP); in.d = btnHeld(B_DOWN);
      in.uT |= btnTap(B_UP); in.dT |= btnTap(B_DOWN); in.okT |= btnTap(B_OK);
      bool done = false;
      for (uint8_t k = 0; k < 4 && (int32_t)(now - next) >= 0; k++) {   // at most 80 ms per frame
        next += PAL_TICK;
        palStep(in);
        in.uT = in.dT = in.okT = false;
        if (palHero.st == PH_DEAD && palHero.t >= 90) palLoad(palLv);    // again from the level's start
        if (palHero.st == PH_EXIT && palHero.t >= 40) { done = true; break; }
      }
      if ((int32_t)(now - next) > 0) next = now;
      int32_t left = (int32_t)(deadline - now);
      palDraw(left);
      gfx.send();
      if (left <= 0) { ended = 1; break; }
      if (!done) continue;
      palScore += 100;
      snprintf(b, sizeof(b), "LEVEL %u COMPLETE", palLv + 1);
      uint32_t t0 = gameMillis();
      if (palLv + 1 >= PAL_LEVELS) {
        uint32_t sec = (uint32_t)left / 1000;
        palScore = (uint16_t)(palScore + sec);
        char s[32];
        snprintf(s, sizeof(s), "%lu:%02lu to spare", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
        sfx(1500, 400);
        if (!palBanner("FREE AT LAST!", s, 3500)) return;
        ended = 2;
        break;
      }
      sfx(1400, 250);
      if (!palBanner(b, PAL_LEVEL[palLv + 1].name, 2200)) return;
      deadline += gameMillis() - t0;                     // the clock stops between levels
      next = gameMillis();
      palStartHp = palHero.maxHp; palStartSword = palHero.sword;
      palLoad((uint8_t)(palLv + 1));
      btnClear();
    }
    if (!ended) return;                                  // left from the pause menu
    if (ended == 1 && !palBanner("TIME IS UP", "the palace keeps you", 2500)) return;
    again = gameOver(palScore);
  }
}

static const char *const IC_PALACE[12] = {
  ".....yy.....", "....oooo....", "...oooooo...", "..oooooooo..", "..oooooooo..", ".ssssssssss.",
  ".syssssssys.", ".ssssssssss.", ".sssskkssss.", ".sysskkssys.", ".sssskkssss.", "nnnnnnnnnnnn" };
