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
