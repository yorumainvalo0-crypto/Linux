// Real-time two player games for the multiplayer page: Pong and Snake.
// Only the rules - no screen, no buttons, no radio - so both consoles
// (and the PC tests) run exactly the same code. Everything is integer
// math driven by a shared seed and the inputs of both players, which
// keeps the two consoles in step tick by tick (see LkSync in linkcore.h).
//
// Player 0 is the console that got iStart, player 1 the other one.
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define RT_TICK_MS 20                  // one tick of both games

enum { RT_EV_HIT = 1, RT_EV_WALL = 2, RT_EV_POINT = 4, RT_EV_EAT = 8, RT_EV_END = 16 };

static inline uint32_t rtRand(uint32_t &s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

// ---------------- Pong ----------------
/* Input: bit 0 = up, bit 1 = down. Player 0 has the left paddle. First to
   RP_WIN points wins; after a point the ball waits a moment in the middle
   and then goes to the one who lost the point.                         */
#define RP_TOP  16
#define RP_BOT  64
#define RP_PH   13                     // paddle height
#define RP_WIN   5
#define RP_WAIT 40                     // ticks before a serve

struct RtPong {
  int16_t  bx, by, vx, vy;             // ball, 1/16 pixel
  uint8_t  pad[2];                     // paddle tops
  uint8_t  pts[2];
  uint8_t  wait;                       // ticks until the ball moves
  uint8_t  winner;                     // 0 = running, 1 / 2 = player 0 / 1 won
  uint32_t rnd;

  void begin(uint32_t seed) {
    memset(this, 0, sizeof(*this));
    rnd = seed | 1;
    pad[0] = pad[1] = 34;
    serve(rtRand(rnd) & 1);
  }

  void serve(uint8_t to) {             // ball towards player "to"
    bx = 63 << 4; by = 39 << 4;
    vx = to ? 20 : -20;
    vy = 5 + (int16_t)(rtRand(rnd) % 8);
    if (rtRand(rnd) & 1) vy = -vy;
    wait = RP_WAIT;
  }

  uint8_t step(const uint8_t in[2]) {
    if (winner) return 0;
    uint8_t ev = 0;
    for (uint8_t p = 0; p < 2; p++) {
      if (in[p] & 1) pad[p] = pad[p] > RP_TOP + 3 ? pad[p] - 3 : RP_TOP;
      if (in[p] & 2) pad[p] = pad[p] < RP_BOT - RP_PH - 3 ? pad[p] + 3 : RP_BOT - RP_PH;
    }
    if (wait) { wait--; return 0; }

    bx += vx; by += vy;
    if (by < (RP_TOP << 4))       { by = RP_TOP << 4;       vy = -vy; ev |= RT_EV_WALL; }
    if (by > ((RP_BOT - 2) << 4)) { by = (RP_BOT - 2) << 4; vy = -vy; ev |= RT_EV_WALL; }

    int16_t x = bx >> 4, y = by >> 4;
    if (vx < 0 && x <= 5 && x >= 2 && hits(0, y))       { bx = 5 << 4;   bounce(0, y); ev |= RT_EV_HIT; }
    else if (vx > 0 && x >= 121 && x <= 124 && hits(1, y)) { bx = 121 << 4; bounce(1, y); ev |= RT_EV_HIT; }

    x = bx >> 4;
    if (x < 0 || x > 126) {
      uint8_t scorer = x < 0 ? 1 : 0;
      pts[scorer]++;
      ev |= RT_EV_POINT;
      if (pts[scorer] >= RP_WIN) { winner = scorer + 1; ev |= RT_EV_END; }
      else serve(1 - scorer);
    }
    return ev;
  }

  bool hits(uint8_t p, int16_t y) const { return y + 2 >= pad[p] && y <= pad[p] + RP_PH; }

  void bounce(uint8_t p, int16_t y) {
    int16_t s = vx < 0 ? -vx : vx;
    s += 1;                                            // faster each hit
    if (s > 44) s = 44;
    vx = p ? -s : s;
    vy += (y - (pad[p] + RP_PH / 2)) * 2;              // angle from the hit point
    if (vy >  24) vy =  24;
    if (vy < -24) vy = -24;
    if (vy > -3 && vy < 3) vy = (rtRand(rnd) & 1) ? 5 : -5;
  }
};

// ---------------- Snake ----------------
/* Two snakes on the 32 x 12 field, two pieces of food. Input: the last
   direction pressed (1 up, 2 down, 3 left, 4 right, 0 = nothing yet).
   Whoever hits a wall, a body or the other head loses; both at once is
   a draw. The snakes get faster over time.                            */
#define RS_W    32
#define RS_H    12
#define RS_MAX  96                     // longest snake
#define RS_FOOD  2

struct RtSnake {
  uint8_t  x[2][RS_MAX], y[2][RS_MAX]; // [0] = head
  uint8_t  len[2];
  uint8_t  dir[2], want[2];
  uint8_t  fx[RS_FOOD], fy[RS_FOOD];
  uint8_t  every;                      // ticks per move
  uint16_t t;
  uint8_t  winner;                     // 0 running, 1 / 2 player won, 3 draw
  uint32_t rnd;

  void begin(uint32_t seed) {
    memset(this, 0, sizeof(*this));
    rnd = seed | 1;
    for (uint8_t i = 0; i < 4; i++) {
      x[0][i] = 5 - i;  y[0][i] = 3;             // left, heading right
      x[1][i] = 26 + i; y[1][i] = 8;             // right, heading left
    }
    len[0] = len[1] = 4;
    dir[0] = want[0] = 4;
    dir[1] = want[1] = 3;
    every = 7;
    for (uint8_t f = 0; f < RS_FOOD; f++) food(f);
  }

  bool taken(uint8_t cx, uint8_t cy) const {
    for (uint8_t p = 0; p < 2; p++)
      for (uint8_t i = 0; i < len[p]; i++) if (x[p][i] == cx && y[p][i] == cy) return true;
    for (uint8_t f = 0; f < RS_FOOD; f++) if (fx[f] == cx && fy[f] == cy) return true;
    return false;
  }

  void food(uint8_t f) {
    fx[f] = fy[f] = 255;
    for (uint16_t tries = 0; tries < 500; tries++) {
      uint8_t cx = rtRand(rnd) % RS_W, cy = rtRand(rnd) % RS_H;
      if (!taken(cx, cy)) { fx[f] = cx; fy[f] = cy; return; }
    }
  }

  // a body cell that will still be there after this move (tails move on)
  bool body(uint8_t cx, uint8_t cy) const {
    for (uint8_t p = 0; p < 2; p++)
      for (uint8_t i = 0; i + 1 < len[p]; i++) if (x[p][i] == cx && y[p][i] == cy) return true;
    return false;
  }

  uint8_t step(const uint8_t in[2]) {
    if (winner) return 0;
    for (uint8_t p = 0; p < 2; p++) if (in[p] >= 1 && in[p] <= 4) want[p] = in[p];
    if (++t % 250 == 0 && every > 4) every--;          // faster every 5 s
    if (t % every) return 0;

    int16_t nx[2], ny[2];
    bool dead[2];
    for (uint8_t p = 0; p < 2; p++) {
      static const uint8_t OPP[5] = { 0, 2, 1, 4, 3 };
      if (want[p] != OPP[dir[p]]) dir[p] = want[p];     // no turning back
      nx[p] = x[p][0] + (dir[p] == 4) - (dir[p] == 3);
      ny[p] = y[p][0] + (dir[p] == 2) - (dir[p] == 1);
      dead[p] = nx[p] < 0 || nx[p] >= RS_W || ny[p] < 0 || ny[p] >= RS_H ||
                body((uint8_t)nx[p], (uint8_t)ny[p]);
    }
    if (nx[0] == nx[1] && ny[0] == ny[1]) dead[0] = dead[1] = true;          // head on
    if (nx[0] == x[1][0] && ny[0] == y[1][0] && nx[1] == x[0][0] && ny[1] == y[0][0])
      dead[0] = dead[1] = true;                                             // passed each other
    if (dead[0] || dead[1]) {
      winner = (dead[0] && dead[1]) ? 3 : (dead[0] ? 2 : 1);
      return RT_EV_END;
    }

    uint8_t ev = 0;
    for (uint8_t p = 0; p < 2; p++) {
      bool grow = false;
      for (uint8_t f = 0; f < RS_FOOD; f++)
        if (fx[f] == nx[p] && fy[f] == ny[p]) { grow = true; fx[f] = fy[f] = 255; ev |= RT_EV_EAT; }
      if (grow && len[p] < RS_MAX) len[p]++;
      memmove(&x[p][1], &x[p][0], len[p] - 1);
      memmove(&y[p][1], &y[p][0], len[p] - 1);
      x[p][0] = (uint8_t)nx[p];
      y[p][0] = (uint8_t)ny[p];
    }
    for (uint8_t f = 0; f < RS_FOOD; f++) if (fx[f] == 255) food(f);
    return ev;
  }
};

// ---------------- Pac-Man ----------------
/* One or two Pac-Men in a 32 x 12 maze of 4 px tiles, three ghosts.
   Input: the last direction pressed, like Snake. Everything moves tile
   by tile; "p" counts the ticks on the way to the next tile, so the
   screen can draw the in-between pixels. Alone the game goes on level
   after level until the lives are gone; with two players the maze is
   played once and the higher score wins.                              */
#define RM_W     32
#define RM_H     12
#define RM_GH     3                    // ghosts
#define RM_LIVES  3
#define RM_FREEZE 60                   // ticks everything stands after a death

static const char *const RM_MAZE[RM_H] = {
  "################################",
  "#o.........#........#.........o#",
  "#.###.####.#.######.#.####.###.#",
  "#..............................#",
  "#.###.#.####.##==##.####.#.###.#",
  "......#.#....#    #....#.#......",   // row 5: the tunnel, and the ghost house
  "#.###.#.#.##.######.##.#.#.###.#",
  "#.....#..................#.....#",
  "#.###.#.####.######.####.#.###.#",
  "#...#...#..............#...#...#",
  "#o#...#...############...#...#o#",
  "################################",
};

enum { RG_HOUSE, RG_EXIT, RG_CHASE, RG_FRIGHT, RG_EYES };

struct RmEnt {
  uint8_t x, y;                        // tile it comes from
  uint8_t dir;                         // 0 = standing, 1 up 2 down 3 left 4 right
  uint8_t p;                           // ticks on the way to the next tile
  uint8_t state;                       // ghosts: RG_*
  uint16_t wait;                       // ghosts: ticks left in the house
};

struct RtPac {
  uint32_t dots[RM_H], power[RM_H];    // one bit per column
  RmEnt    pac[2], gh[RM_GH];
  uint8_t  want[2];
  uint8_t  np;                         // players
  uint8_t  lives[2];
  uint16_t score[2];
  uint16_t fright;                     // ticks the ghosts stay blue
  uint8_t  chain;                      // ghosts eaten on this power pellet
  uint8_t  freeze;                     // everything stands still
  uint8_t  level;
  uint16_t dotsLeft;
  uint32_t t;
  uint8_t  winner;                     // 1 / 2 player won (two players), 3 draw, 1 = over (alone)
  uint32_t rnd;

  static bool wallAt(int x, int y) {
    if (y < 0 || y >= RM_H) return true;
    x = (x + RM_W) % RM_W;
    return RM_MAZE[y][x] == '#' || RM_MAZE[y][x] == '=';
  }
  static bool doorAt(int x, int y) { return y >= 0 && y < RM_H && RM_MAZE[y][(x + RM_W) % RM_W] == '='; }
  static int dx(uint8_t d) { return (d == 4) - (d == 3); }
  static int dy(uint8_t d) { return (d == 2) - (d == 1); }
  static uint8_t back(uint8_t d) { static const uint8_t O[5] = { 0, 2, 1, 4, 3 }; return O[d]; }

  void begin(uint32_t seed) { begin(seed, 2); }    // online: always two
  void begin(uint32_t seed, uint8_t players) {
    memset(this, 0, sizeof(*this));
    rnd = seed | 1;
    np = players;
    lives[0] = RM_LIVES;
    lives[1] = np > 1 ? RM_LIVES : 0;
    fill();
    place();
  }

  void fill() {
    dotsLeft = 0;
    for (uint8_t y = 0; y < RM_H; y++) {
      dots[y] = power[y] = 0;
      for (uint8_t x = 0; x < RM_W; x++) {
        if (RM_MAZE[y][x] == '.') { dots[y] |= 1UL << x; dotsLeft++; }
        if (RM_MAZE[y][x] == 'o') { power[y] |= 1UL << x; dotsLeft++; }
      }
    }
  }

  void place() {                       // everybody to the start
    static const uint8_t PX[2] = { 13, 18 };
    for (uint8_t p = 0; p < 2; p++) {
      pac[p].x = np > 1 ? PX[p] : 15; pac[p].y = 7;
      pac[p].dir = 0; pac[p].p = 0;
      want[p] = 0;
    }
    for (uint8_t g = 0; g < RM_GH; g++) {
      gh[g].x = 14 + g; gh[g].y = 5; gh[g].dir = 1; gh[g].p = 0;
      gh[g].state = RG_HOUSE;
      gh[g].wait = 20 + g * 120;
    }
    fright = 0;
    freeze = RM_FREEZE;
  }

  uint8_t pacPeriod() const { return 5; }
  uint8_t ghostPeriod(const RmEnt &g) const {
    if (g.state == RG_EYES) return 2;
    if (g.state == RG_FRIGHT) return 9;
    if (g.state == RG_HOUSE || g.state == RG_EXIT) return 7;
    return level < 4 ? 6 - level / 2 : 4;
  }

  // the tile an entity counts as being on (the nearer one of the two)
  void tileOf(const RmEnt &e, uint8_t period, int *x, int *y) const {
    int tx = e.x, ty = e.y;
    if (e.dir && e.p * 2 >= period) { tx = (tx + dx(e.dir) + RM_W) % RM_W; ty += dy(e.dir); }
    *x = tx; *y = ty;
  }

  bool alive(uint8_t p) const { return p < np && lives[p] > 0; }

  // ---- ghosts ----
  bool ghostMay(const RmEnt &g, int x, int y) const {
    if (doorAt(x, y)) return g.state == RG_EXIT || g.state == RG_EYES;
    return !wallAt(x, y);
  }

  void target(uint8_t i, int *tx, int *ty) {
    const RmEnt &g = gh[i];
    if (g.state == RG_EYES) { *tx = 15; *ty = 3; return; }
    static const uint8_t CX[RM_GH] = { 30, 1, 1 }, CY[RM_GH] = { 1, 1, 10 };
    bool scatter = (t % 1350) < 350;                 // 7 s scatter, then 20 s chase
    int best = -1, bd = 1 << 30, px = 0, py = 0;
    for (uint8_t p = 0; p < np; p++) {               // the nearest Pac-Man
      if (!alive(p)) continue;
      int x, y; tileOf(pac[p], pacPeriod(), &x, &y);
      int d = (x - g.x) * (x - g.x) + (y - g.y) * (y - g.y);
      if (d < bd) { bd = d; best = p; px = x; py = y; }
    }
    if (scatter || best < 0) { *tx = CX[i]; *ty = CY[i]; return; }
    if (i == 1) { px += 4 * dx(pac[best].dir); py += 4 * dy(pac[best].dir); }   // cuts him off
    if (i == 2 && bd < 36) { *tx = CX[i]; *ty = CY[i]; return; }               // shy when close
    *tx = px; *ty = py;
  }

  void ghostTurn(uint8_t i) {                        // at a tile: where next
    RmEnt &g = gh[i];
    if (g.state == RG_EXIT) {                        // out of the house: to x 15, then up
      if (g.y <= 3) { g.state = fright ? RG_FRIGHT : RG_CHASE; g.dir = 3; }
      else g.dir = g.x < 15 ? 4 : (g.x > 15 ? 3 : 1);
      return;
    }
    if (g.state == RG_EYES && g.x == 15 && (g.y == 3 || g.y == 4)) { g.dir = 2; return; }   // down through the door
    if (g.state == RG_EYES && g.x == 15 && g.y == 5) { g.state = RG_EXIT; g.dir = 1; return; }
    uint8_t opts[4], n = 0;
    for (uint8_t d = 1; d <= 4; d++)
      if (d != back(g.dir) && ghostMay(g, g.x + dx(d), g.y + dy(d))) opts[n++] = d;
    if (!n) { g.dir = back(g.dir); return; }
    if (g.state == RG_FRIGHT) { g.dir = opts[rtRand(rnd) % n]; return; }
    int tx, ty; target(i, &tx, &ty);
    int bd = 1 << 30;
    for (uint8_t k = 0; k < n; k++) {
      int nx = g.x + dx(opts[k]), ny = g.y + dy(opts[k]);
      int d = (nx - tx) * (nx - tx) + (ny - ty) * (ny - ty);
      if (d < bd) { bd = d; g.dir = opts[k]; }
    }
  }

  void ghostStep(uint8_t i) {
    RmEnt &g = gh[i];
    if (g.state == RG_HOUSE) {                       // bob up and down, then leave
      if (g.wait) { g.wait--; return; }
      g.state = RG_EXIT; g.p = 0;
      ghostTurn(i);
      return;
    }
    if (++g.p < ghostPeriod(g)) return;
    g.p = 0;
    g.x = (g.x + dx(g.dir) + RM_W) % RM_W;
    g.y += dy(g.dir);
    ghostTurn(i);
  }

  // ---- Pac-Men ----
  void pacStep(uint8_t p) {
    RmEnt &e = pac[p];
    uint8_t w = want[p];
    if (e.dir && w == back(e.dir) && e.p) {          // turn round on the way
      e.x = (e.x + dx(e.dir) + RM_W) % RM_W; e.y += dy(e.dir);
      e.p = pacPeriod() - e.p;
      e.dir = w;
    }
    if (e.p == 0) {                                  // on a tile: take the wished way if open
      if (w && !wallAt(e.x + dx(w), e.y + dy(w))) e.dir = w;
      if (e.dir && wallAt(e.x + dx(e.dir), e.y + dy(e.dir))) { e.dir = 0; return; }
      if (!e.dir) return;
    }
    if (++e.p < pacPeriod()) return;
    e.p = 0;
    e.x = (e.x + dx(e.dir) + RM_W) % RM_W;
    e.y += dy(e.dir);
  }

  uint8_t eat(uint8_t p) {                           // dots under Pac-Man p
    int x, y; tileOf(pac[p], pacPeriod(), &x, &y);
    uint32_t bit = 1UL << x;
    if (dots[y] & bit) { dots[y] &= ~bit; score[p] += 10; dotsLeft--; return RT_EV_EAT; }
    if (power[y] & bit) {
      power[y] &= ~bit; score[p] += 50; dotsLeft--;
      fright = level < 6 ? 350 - level * 30 : 170;
      chain = 0;
      for (uint8_t g = 0; g < RM_GH; g++)
        if (gh[g].state == RG_CHASE) { gh[g].state = RG_FRIGHT; }
      return RT_EV_EAT | RT_EV_HIT;
    }
    return 0;
  }

  uint8_t step(const uint8_t in[2]) {
    if (winner) return 0;
    t++;
    for (uint8_t p = 0; p < 2; p++) if (in[p] >= 1 && in[p] <= 4) want[p] = in[p];
    if (freeze) { freeze--; return 0; }
    uint8_t ev = 0;
    for (uint8_t p = 0; p < np; p++) if (alive(p)) { pacStep(p); ev |= eat(p); }
    for (uint8_t g = 0; g < RM_GH; g++) ghostStep(g);
    if (fright && !--fright)
      for (uint8_t g = 0; g < RM_GH; g++) if (gh[g].state == RG_FRIGHT) gh[g].state = RG_CHASE;

    for (uint8_t p = 0; p < np; p++) {               // meetings
      if (!alive(p)) continue;
      int px, py; tileOf(pac[p], pacPeriod(), &px, &py);
      for (uint8_t g = 0; g < RM_GH; g++) {
        int gx, gy; tileOf(gh[g], ghostPeriod(gh[g]), &gx, &gy);
        if (gx != px || gy != py) continue;
        if (gh[g].state == RG_FRIGHT) {              // eaten: back to the house as eyes
          gh[g].state = RG_EYES;
          score[p] += 200 << (chain < 3 ? chain : 3);
          chain++;
          ev |= RT_EV_HIT;
        } else if (gh[g].state == RG_CHASE) {        // caught
          lives[p]--;
          ev |= RT_EV_POINT;
          bool anyone = false;
          for (uint8_t q = 0; q < np; q++) if (alive(q)) anyone = true;
          if (!anyone) { finish(); return ev | RT_EV_END; }
          place();                                   // start over, the dots stay
          return ev;
        }
      }
    }
    if (!dotsLeft) {
      if (np > 1) { finish(); return ev | RT_EV_END; }
      level++;
      fill();
      place();
      ev |= RT_EV_POINT;
    }
    return ev;
  }

  void finish() {
    if (np < 2) { winner = 1; return; }
    winner = score[0] > score[1] ? 1 : (score[1] > score[0] ? 2 : 3);
  }

  // pixel position of an entity (its top left), for the screen
  void pixel(const RmEnt &e, uint8_t period, int *px, int *py) const {
    int off = e.dir ? e.p * 4 / period : 0;
    *px = e.x * 4 + dx(e.dir) * off;
    *py = e.y * 4 + dy(e.dir) * off;
    if (*px < 0) *px += RM_W * 4;
    if (*px >= RM_W * 4) *px -= RM_W * 4;
  }
};

// ---------------- Bomberman ----------------
/* A field of 17 x 9 tiles: walls in a fixed grid, bricks at random (the
   same on both consoles from the seed). Input per player: bits 0-2 the
   direction held (1 up, 2 down, 3 left, 4 right), bit 3 = drop a bomb.
   A bomb goes off after RB_FUSE ticks and burns in four directions as far
   as its owner's range; a brick stops the fire and may leave a power-up
   (one more bomb, a longer range). The last one standing wins; after
   RB_TIME ticks it is a draw. Player 0..3 start in the corners.       */
#define RB_W      17
#define RB_H      9
#define RB_MAXP   4
#define RB_BOMBS  16
#define RB_FUSE   125                  // 2.5 s
#define RB_FIRE   18                   // ticks a flame burns
#define RB_STEP   7                    // ticks to walk one tile
#define RB_TIME   (180 * 50)           // 3 minutes
#define RB_DRAW   9                    // winner value for a draw
enum { RB_EMPTY = 0, RB_WALL, RB_BRICK, RB_PBOMB, RB_PFIRE };
enum { RB_EV_BOMB = 1, RB_EV_BOOM = 2, RB_EV_DEAD = 4, RB_EV_POWER = 8 };

struct RbBomb { uint8_t x, y, owner, range, t; };     // t = 0: unused

struct RtBomb {
  uint8_t  tile[RB_H][RB_W];
  uint8_t  hidden[RB_H][RB_W];         // power-up under a brick
  uint8_t  fire[RB_H][RB_W];           // ticks the flame still burns
  uint8_t  fireBy[RB_H][RB_W];         // whose bomb it was
  RbBomb   bomb[RB_BOMBS];
  uint8_t  n;                          // players
  uint8_t  px[RB_MAXP], py[RB_MAXP], alive[RB_MAXP], maxB[RB_MAXP], range[RB_MAXP], wait[RB_MAXP];
  uint8_t  dir[RB_MAXP];               // last direction walked (for drawing)
  uint16_t score[RB_MAXP];             // brick 10, power-up 20, another player 100
  uint16_t ticks;
  uint8_t  winner;                     // 0 = running, 1..4 = that player, RB_DRAW
  uint32_t rnd;

  static bool corner(uint8_t x, uint8_t y) {           // kept free around the starts
    uint8_t cx = x <= 2 ? x - 1 : RB_W - 2 - x, cy = y <= 2 ? y - 1 : RB_H - 2 - y;
    return (x <= 2 || x >= RB_W - 3) && (y <= 2 || y >= RB_H - 3) && cx + cy <= 1;
  }

  void begin(uint32_t seed, uint8_t players) {
    memset(this, 0, sizeof(*this));
    rnd = seed | 1;
    n = players;
    for (uint8_t y = 0; y < RB_H; y++)
      for (uint8_t x = 0; x < RB_W; x++) {
        if (!x || !y || x == RB_W - 1 || y == RB_H - 1 || (!(x & 1) && !(y & 1))) { tile[y][x] = RB_WALL; continue; }
        if (corner(x, y) || rtRand(rnd) % 100 >= 60) continue;
        tile[y][x] = RB_BRICK;
        uint8_t r = rtRand(rnd) % 10;
        hidden[y][x] = r < 1 ? RB_PBOMB : (r < 2 ? RB_PFIRE : RB_EMPTY);
      }
    static const uint8_t SX[4] = { 1, RB_W - 2, RB_W - 2, 1 }, SY[4] = { 1, RB_H - 2, 1, RB_H - 2 };
    for (uint8_t p = 0; p < n; p++) { px[p] = SX[p]; py[p] = SY[p]; alive[p] = 1; maxB[p] = 1; range[p] = 2; dir[p] = 2; }
  }

  int8_t bombAt(uint8_t x, uint8_t y) const {
    for (uint8_t i = 0; i < RB_BOMBS; i++) if (bomb[i].t && bomb[i].x == x && bomb[i].y == y) return i;
    return -1;
  }
  uint8_t bombsOf(uint8_t p) const { uint8_t k = 0; for (uint8_t i = 0; i < RB_BOMBS; i++) if (bomb[i].t && bomb[i].owner == p) k++; return k; }
  bool open(uint8_t x, uint8_t y) const { return tile[y][x] != RB_WALL && tile[y][x] != RB_BRICK && bombAt(x, y) < 0; }
  static int8_t ddx(uint8_t d) { return d == 3 ? -1 : (d == 4 ? 1 : 0); }
  static int8_t ddy(uint8_t d) { return d == 1 ? -1 : (d == 2 ? 1 : 0); }

  void burn(uint8_t x, uint8_t y, uint8_t owner) { fire[y][x] = RB_FIRE; fireBy[y][x] = owner; }

  void explode(uint8_t i) {
    RbBomb b = bomb[i];
    bomb[i].t = 0;
    burn(b.x, b.y, b.owner);
    for (uint8_t d = 1; d <= 4; d++)
      for (uint8_t k = 1; k <= b.range; k++) {
        int8_t x = b.x + ddx(d) * k, y = b.y + ddy(d) * k;
        uint8_t &t = tile[y][x];
        if (t == RB_WALL) break;
        burn(x, y, b.owner);
        if (t == RB_BRICK) { t = hidden[y][x]; hidden[y][x] = RB_EMPTY; if (b.owner < n) score[b.owner] += 10; break; }
        if (t == RB_PBOMB || t == RB_PFIRE) t = RB_EMPTY;          // an open power-up burns
        int8_t o = bombAt(x, y);
        if (o >= 0) explode(o);                                      // a chain
      }
  }

  uint8_t step(const uint8_t *in) {
    if (winner) return 0;
    uint8_t ev = 0;
    ticks++;
    for (uint8_t y = 0; y < RB_H; y++) for (uint8_t x = 0; x < RB_W; x++) if (fire[y][x]) fire[y][x]--;
    for (uint8_t p = 0; p < n; p++) {
      if (!alive[p]) continue;
      if ((in[p] & 8) && bombsOf(p) < maxB[p] && bombAt(px[p], py[p]) < 0)
        for (uint8_t i = 0; i < RB_BOMBS; i++)
          if (!bomb[i].t) { bomb[i] = { px[p], py[p], p, range[p], RB_FUSE }; ev |= RB_EV_BOMB; break; }
      if (wait[p]) { wait[p]--; continue; }
      uint8_t d = in[p] & 7;
      if (d < 1 || d > 4) continue;
      dir[p] = d;
      uint8_t nx = px[p] + ddx(d), ny = py[p] + ddy(d);
      if (!open(nx, ny)) continue;
      px[p] = nx; py[p] = ny; wait[p] = RB_STEP - 1;
      uint8_t &t = tile[ny][nx];
      if (t == RB_PBOMB) { if (maxB[p] < 5) maxB[p]++; t = RB_EMPTY; score[p] += 20; ev |= RB_EV_POWER; }
      if (t == RB_PFIRE) { if (range[p] < 6) range[p]++; t = RB_EMPTY; score[p] += 20; ev |= RB_EV_POWER; }
    }
    for (uint8_t i = 0; i < RB_BOMBS; i++)
      if (bomb[i].t && !--bomb[i].t) { bomb[i].t = 1; explode(i); ev |= RB_EV_BOOM; }
    uint8_t left = 0, last = 0;
    for (uint8_t p = 0; p < n; p++) {
      if (alive[p] && fire[py[p]][px[p]]) {
        alive[p] = 0; ev |= RB_EV_DEAD;
        uint8_t k = fireBy[py[p]][px[p]];
        if (k != p && k < n) score[k] += 100;
      }
      if (alive[p]) { left++; last = p; }
    }
    if (left <= 1) winner = left ? last + 1 : RB_DRAW;
    else if (ticks >= RB_TIME) winner = RB_DRAW;
    return ev;
  }

  // ---------------- a CPU player ----------------
  /* danger[y][x]: ticks until fire gets there (1 = burning now), 0 = safe.
     A bomb's blast is traced like explode() does, chains included by
     taking the earliest time along the way.                            */
  void danger(uint8_t out[RB_H][RB_W], int8_t extraX = -1, int8_t extraY = -1, uint8_t extraR = 0) const {
    memset(out, 0, RB_H * RB_W);
    for (uint8_t y = 0; y < RB_H; y++) for (uint8_t x = 0; x < RB_W; x++) if (fire[y][x]) out[y][x] = 1;
    RbBomb list[RB_BOMBS + 1];
    uint8_t k = 0;
    for (uint8_t i = 0; i < RB_BOMBS; i++) if (bomb[i].t) list[k++] = bomb[i];
    if (extraX >= 0) list[k++] = { (uint8_t)extraX, (uint8_t)extraY, 0, extraR, RB_FUSE };
    for (uint8_t rep = 0; rep < 3; rep++)                              // chains: a bomb in a blast goes earlier
      for (uint8_t i = 0; i < k; i++) {
        uint8_t t = list[i].t;
        if (out[list[i].y][list[i].x] && out[list[i].y][list[i].x] < t) t = out[list[i].y][list[i].x];
        auto mark = [&](uint8_t x, uint8_t y) { if (!out[y][x] || t < out[y][x]) out[y][x] = t; };
        mark(list[i].x, list[i].y);
        for (uint8_t d = 1; d <= 4; d++)
          for (uint8_t s = 1; s <= list[i].range; s++) {
            int8_t x = list[i].x + ddx(d) * s, y = list[i].y + ddy(d) * s;
            if (tile[y][x] == RB_WALL) break;
            mark(x, y);
            if (tile[y][x] == RB_BRICK) break;
          }
      }
  }

  /* Breadth first from (x, y) over open tiles that are not burning when we
     get there. goal(x, y) says what we look for. Returns the first
     direction, 0 = none found. maxSteps limits the search.           */
  template <class G> uint8_t route(uint8_t x0, uint8_t y0, const uint8_t dng[RB_H][RB_W], G goal, uint8_t maxSteps) const {
    uint8_t first[RB_H][RB_W], dist[RB_H][RB_W];
    memset(first, 0, sizeof(first)); memset(dist, 255, sizeof(dist));
    uint8_t qx[RB_W * RB_H], qy[RB_W * RB_H], h = 0, tl = 0;
    qx[tl] = x0; qy[tl++] = y0; dist[y0][x0] = 0;
    while (h < tl) {
      uint8_t x = qx[h], y = qy[h++];
      if (dist[y][x] && goal(x, y)) return first[y][x];
      if (dist[y][x] >= maxSteps) continue;
      for (uint8_t d = 1; d <= 4; d++) {
        uint8_t nx = x + ddx(d), ny = y + ddy(d);
        if (dist[ny][nx] != 255 || !open(nx, ny)) continue;
        uint16_t arrive = (uint16_t)(dist[y][x] + 1) * RB_STEP;
        if (dng[ny][nx] && dng[ny][nx] <= arrive + RB_STEP && dng[ny][nx] + RB_FIRE >= arrive) continue;   // burns then
        dist[ny][nx] = dist[y][x] + 1;
        first[ny][nx] = dist[y][x] ? first[y][x] : d;
        qx[tl] = nx; qy[tl++] = ny;
      }
    }
    return 0;
  }

  uint8_t ai(uint8_t p) {
    if (!alive[p] || winner) return 0;
    uint8_t x = px[p], y = py[p];
    uint8_t dng[RB_H][RB_W];
    danger(dng);
    auto safe = [&](uint8_t gx, uint8_t gy) { return !dng[gy][gx]; };
    if (dng[y][x]) {                                                  // get out of the way
      uint8_t d = route(x, y, dng, safe, 12);
      return d ? d : (uint8_t)(1 + rtRand(rnd) % 4);
    }
    // a bomb here: next to a brick, or an enemy in the line of fire - if there is a way out
    bool worth = false;
    for (uint8_t d = 1; d <= 4 && !worth; d++)
      for (uint8_t s = 1; s <= range[p]; s++) {
        int8_t nx = x + ddx(d) * s, ny = y + ddy(d) * s;
        if (tile[ny][nx] == RB_WALL) break;
        if (tile[ny][nx] == RB_BRICK) { worth = s == 1; break; }
        for (uint8_t q = 0; q < n; q++) if (q != p && alive[q] && px[q] == nx && py[q] == ny) worth = true;
      }
    if (worth && bombsOf(p) < maxB[p] && bombAt(x, y) < 0 && rtRand(rnd) % 4) {
      uint8_t d2[RB_H][RB_W];
      danger(d2, x, y, range[p]);
      if (route(x, y, d2, [&](uint8_t gx, uint8_t gy) { return !d2[gy][gx]; }, RB_FUSE / RB_STEP - 3)) return 8;
    }
    // walk: a power-up, a place next to a brick, or towards the nearest enemy
    auto target = [&](uint8_t gx, uint8_t gy) {
      if (dng[gy][gx]) return false;
      if (tile[gy][gx] == RB_PBOMB || tile[gy][gx] == RB_PFIRE) return true;
      for (uint8_t d = 1; d <= 4; d++) if (tile[gy + ddy(d)][gx + ddx(d)] == RB_BRICK) return true;
      for (uint8_t q = 0; q < n; q++) if (q != p && alive[q] && (uint8_t)(abs(px[q] - gx) + abs(py[q] - gy)) <= 1) return true;
      return false;
    };
    uint8_t d = route(x, y, dng, target, 30);
    if (d && rtRand(rnd) % 8) return d;
    for (uint8_t t = 0; t < 4; t++) {                                  // else wander, but not into danger
      uint8_t r = 1 + rtRand(rnd) % 4, nx = x + ddx(r), ny = y + ddy(r);
      if (open(nx, ny) && !dng[ny][nx]) return r;
    }
    return 0;
  }
};
