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
