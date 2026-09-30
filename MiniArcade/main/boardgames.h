// Rules of Checkers and Mau-Mau - no screen, no buttons, no radio - so
// the games against the CPU, the online games and the PC tests all run
// the same code. Online, both consoles apply the same moves to the same
// start (Mau-Mau: the same shuffled deck from the session seed).
#pragma once
#include <stdint.h>
#include <string.h>

static inline uint32_t bgRand(uint32_t &s) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }

// =========================================================
//  CHECKERS (English draughts)
// =========================================================
/* 32 dark squares, index = row * 4 + k (row 0 at the top). Player 0 starts
   at the bottom and moves up, player 1 the other way. Men move one step
   forward diagonally, kings one step in any diagonal direction. Taking is
   a must, and a piece that took keeps taking while it can - a man that
   reaches the far row becomes a king and that ends the move.
   A step is one byte: square (5 bits) | direction << 5 | "more" << 7;
   a move of several jumps is several steps.                             */
enum { CK_EMPTY = 0, CK_MAN0 = 1, CK_KING0 = 2, CK_MAN1 = 3, CK_KING1 = 4 };
#define CK_DRAW_PLIES 80              // this many moves without a take or a man moving = draw

struct CkStep { uint8_t from, dir, jump; };   // dir: 0 up-left, 1 up-right, 2 down-left, 3 down-right

struct Checkers {
  uint8_t sq[32];
  uint8_t turn;                        // 0 / 1
  uint8_t quiet;                       // plies without a take or a man moving
  int8_t  chain;                       // square that has to jump on, -1 = none
  uint8_t winner;                      // 0 = running, 1 / 2 = player 0 / 1 won, 3 = draw

  static int8_t rc(uint8_t i, int8_t *c) { int8_t r = i / 4; *c = (i % 4) * 2 + ((r & 1) ? 0 : 1); return r; }
  static int8_t at(int8_t r, int8_t c) {       // square at row / column, -1 = off or light
    if (r < 0 || r > 7 || c < 0 || c > 7 || ((r + c) & 1) == 0) return -1;
    return r * 4 + c / 2;
  }
  static int8_t next(uint8_t i, uint8_t dir, uint8_t dist) {
    int8_t c, r = rc(i, &c);
    static const int8_t DR[4] = { -1, -1, 1, 1 }, DC[4] = { -1, 1, -1, 1 };
    return at(r + DR[dir] * dist, c + DC[dir] * dist);
  }
  static uint8_t owner(uint8_t p) { return p == CK_MAN0 || p == CK_KING0 ? 0 : (p ? 1 : 255); }
  static bool king(uint8_t p) { return p == CK_KING0 || p == CK_KING1; }

  void begin() {
    memset(this, 0, sizeof(*this));
    for (uint8_t i = 0; i < 12; i++) sq[i] = CK_MAN1;
    for (uint8_t i = 20; i < 32; i++) sq[i] = CK_MAN0;
    chain = -1;
  }

  bool dirOk(uint8_t p, uint8_t dir) const {   // men only forward
    if (king(p)) return true;
    return owner(p) == 0 ? dir < 2 : dir >= 2;
  }

  bool canJumpFrom(uint8_t i) const {
    uint8_t p = sq[i];
    for (uint8_t d = 0; d < 4; d++) {
      if (!dirOk(p, d)) continue;
      int8_t m = next(i, d, 1), t = next(i, d, 2);
      if (m >= 0 && t >= 0 && sq[m] && owner(sq[m]) != owner(p) && !sq[t]) return true;
    }
    return false;
  }

  // every legal step of the player to move (only jumps when one exists)
  uint8_t gen(CkStep *out) const {
    uint8_t n = 0;
    bool anyJump = false;
    for (uint8_t i = 0; i < 32; i++) {
      if (!sq[i] || owner(sq[i]) != turn) continue;
      if (chain >= 0 && i != chain) continue;
      for (uint8_t d = 0; d < 4; d++) {
        if (!dirOk(sq[i], d)) continue;
        int8_t m = next(i, d, 1), t = next(i, d, 2);
        if (m >= 0 && t >= 0 && sq[m] && owner(sq[m]) != turn && !sq[t]) {
          if (!anyJump) { anyJump = true; n = 0; }
          out[n++] = { i, d, 1 };
        } else if (!anyJump && chain < 0 && m >= 0 && !sq[m]) out[n++] = { i, d, 0 };
      }
    }
    return n;
  }

  bool legal(uint8_t from, uint8_t dir) const {
    CkStep s[48];
    uint8_t n = gen(s);
    for (uint8_t k = 0; k < n; k++) if (s[k].from == from && s[k].dir == dir) return true;
    return false;
  }

  /* Applies a legal step. Returns true when the same player has to go on
     (another jump with the same piece); otherwise the turn passes.   */
  bool apply(uint8_t from, uint8_t dir, bool check = true) {
    if (check && !legal(from, dir)) return false;
    uint8_t p = sq[from];
    int8_t m = next(from, dir, 1), t = next(from, dir, 2);
    bool jump = sq[m] != CK_EMPTY;
    int8_t to = jump ? t : m;
    sq[to] = p; sq[from] = CK_EMPTY;
    if (jump) sq[m] = CK_EMPTY;
    quiet = (jump || !king(p)) ? 0 : quiet + 1;
    bool crowned = false;
    int8_t c, r = rc(to, &c);
    if (p == CK_MAN0 && r == 0) { sq[to] = CK_KING0; crowned = true; }
    if (p == CK_MAN1 && r == 7) { sq[to] = CK_KING1; crowned = true; }
    if (jump && !crowned && canJumpFrom(to)) { chain = to; return true; }
    chain = -1;
    turn ^= 1;
    CkStep s[48];
    if (!gen(s)) winner = turn == 0 ? 2 : 1;        // no move left: lost
    else if (quiet >= CK_DRAW_PLIES) winner = 3;
    return false;
  }

  uint8_t count(uint8_t who) const { uint8_t n = 0; for (uint8_t i = 0; i < 32; i++) if (sq[i] && owner(sq[i]) == who) n++; return n; }

  // ---------------- the CPU ----------------
  int16_t eval(uint8_t me) const {             // from me's point of view
    int16_t v = 0;
    for (uint8_t i = 0; i < 32; i++) {
      uint8_t p = sq[i];
      if (!p) continue;
      int8_t c, r = rc(i, &c);
      int16_t w = king(p) ? 150 : 100 + (owner(p) == 0 ? (7 - r) : r) * 3;   // men: forward is good
      if (c >= 2 && c <= 5 && r >= 2 && r <= 5) w += 4;                       // the middle
      if (!king(p) && ((owner(p) == 0 && r == 7) || (owner(p) == 1 && r == 0))) w += 6;   // the back row guards
      v += owner(p) == me ? w : -w;
    }
    return v;
  }

  // a whole turn (all jumps of it) with alpha-beta; returns the value for "turn"
  int16_t search(uint8_t depth, int16_t alpha, int16_t beta, uint32_t *nodes) const {
    (*nodes)++;
    if (winner) return winner == 3 ? 0 : ((winner - 1) == turn ? 20000 : -20000 - depth);
    if (!depth) return eval(turn);
    CkStep s[48];
    uint8_t n = gen(s);
    for (uint8_t k = 0; k < n; k++) {
      Checkers b = *this;
      bool more = b.apply(s[k].from, s[k].dir, false);
      int16_t v = more ? b.search(depth, alpha, beta, nodes)          // same player goes on
                       : -b.search(depth - 1, -beta, -alpha, nodes);
      if (v > alpha) alpha = v;
      if (alpha >= beta) break;
    }
    return alpha;
  }

  // the best step for the player to move; rnd breaks ties
  CkStep best(uint8_t depth, uint32_t &rnd) const {
    CkStep s[48];
    uint8_t n = gen(s);
    int16_t bestV = -32000;
    CkStep pick = s[0];
    uint8_t ties = 0;
    uint32_t nodes = 0;
    for (uint8_t k = 0; k < n; k++) {
      Checkers b = *this;
      bool more = b.apply(s[k].from, s[k].dir, false);
      int16_t v = more ? b.search(depth, -32000, 32000, &nodes) : -b.search(depth - 1, -32000, 32000, &nodes);
      if (v > bestV) { bestV = v; pick = s[k]; ties = 1; }
      else if (v == bestV && bgRand(rnd) % ++ties == 0) pick = s[k];
    }
    return pick;
  }
};

// =========================================================
//  MAU-MAU
// =========================================================
/* 32 cards: suit = card / 8 (clubs, spades, hearts, diamonds), rank =
   card % 8 (7 8 9 10 J Q K A). Play a card of the same suit or rank.
   7: the next player draws 2 (or lays another 7: then 4, ...)
   8: the next player is skipped
   J: goes on anything but a J, and wishes a suit.
   Who cannot or will not play draws one card and may play it right away,
   else passes. Who has no card left wins.
   A move is one byte: card | wish << 5 (a play), MM_DRAW or MM_PASS.   */
#define MM_CARDS   32
#define MM_HAND    5
#define MM_MAXP    4
#define MM_DRAW    0xFF
#define MM_PASS    0xFE
#define MM_NONE    0xFF
enum { MR_7 = 0, MR_8, MR_9, MR_10, MR_J, MR_Q, MR_K, MR_A };

struct MauMau {
  uint32_t hand[MM_MAXP];              // one bit per card
  uint8_t  deck[MM_CARDS], nDeck;      // draw pile, the top is deck[nDeck - 1]
  uint8_t  pile[MM_CARDS], nPile;      // discard pile, the top is pile[nPile - 1]
  uint8_t  players, turn;
  uint8_t  wish;                       // suit wished with a J, MM_NONE = none
  uint8_t  penalty;                    // cards the player to move must draw (7s)
  bool     drew;                       // the player to move has drawn a card this turn
  uint8_t  drawn;                      // ... this one
  uint8_t  winner;                     // 0 = running, 1.. = player 1.. won
  uint8_t  idle;                       // turns in a row without a card played
  uint32_t rnd;

  static uint8_t suit(uint8_t c) { return c / 8; }
  static uint8_t rank(uint8_t c) { return c % 8; }
  static uint8_t count(uint32_t h) { uint8_t n = 0; while (h) { n += h & 1; h >>= 1; } return n; }

  void begin(uint32_t seed, uint8_t np) {
    memset(this, 0, sizeof(*this));
    rnd = seed | 1;
    players = np;
    wish = MM_NONE;
    for (uint8_t i = 0; i < MM_CARDS; i++) deck[i] = i;
    for (uint8_t i = MM_CARDS; i > 1; i--) { uint8_t j = bgRand(rnd) % i, t = deck[i - 1]; deck[i - 1] = deck[j]; deck[j] = t; }
    nDeck = MM_CARDS;
    for (uint8_t k = 0; k < MM_HAND; k++)
      for (uint8_t p = 0; p < players; p++) hand[p] |= 1UL << deck[--nDeck];
    for (uint8_t k = 0; k < nDeck; k++) {              // first open card: not a 7, 8 or J
      uint8_t i = nDeck - 1 - k, r = rank(deck[i]);
      if (r != MR_7 && r != MR_8 && r != MR_J) {
        uint8_t c = deck[i];
        memmove(&deck[i], &deck[i + 1], nDeck - 1 - i);
        nDeck--;
        pile[nPile++] = c;
        break;
      }
    }
  }

  uint8_t top() const { return pile[nPile - 1]; }

  bool fits(uint8_t c) const {
    uint8_t t = top();
    if (penalty) return rank(c) == MR_7;              // only another 7 helps
    if (rank(c) == MR_J) return rank(t) != MR_J;
    if (rank(t) == MR_J && wish != MM_NONE) return suit(c) == wish;
    return suit(c) == suit(t) || rank(c) == rank(t);
  }

  bool canPlay(uint8_t c) const {
    if (winner || !(hand[turn] & (1UL << c))) return false;
    if (drew && c != drawn) return false;             // after drawing only that card
    return fits(c);
  }

  uint8_t takeCard() {                                // from the draw pile; shuffles the discards in
    if (!nDeck) {
      if (nPile <= 1) return MM_NONE;                 // every card is in a hand
      uint8_t t = top();
      for (uint8_t i = 0; i + 1 < nPile; i++) deck[i] = pile[i];
      nDeck = nPile - 1;
      for (uint8_t i = nDeck; i > 1; i--) { uint8_t j = bgRand(rnd) % i, x = deck[i - 1]; deck[i - 1] = deck[j]; deck[j] = x; }
      pile[0] = t; nPile = 1;
    }
    return deck[--nDeck];
  }

  void nextTurn(uint8_t skip) {
    turn = (turn + 1 + skip) % players;
    drew = false;
    if (++idle >= players * 4) {                      // nobody can play any more: fewest cards wins
      uint8_t w = 0;
      for (uint8_t p = 1; p < players; p++) if (count(hand[p]) < count(hand[w])) w = p;
      winner = w + 1;
    }
  }

  // one move of the player to move; false = not allowed
  bool move(uint8_t m) {
    if (winner) return false;
    if (m == MM_DRAW) {
      if (drew) return false;
      if (penalty) {                                  // take the 7s' cards, the turn is over
        for (uint8_t k = 0; k < penalty; k++) { uint8_t c = takeCard(); if (c != MM_NONE) hand[turn] |= 1UL << c; }
        penalty = 0;
        nextTurn(0);
        return true;
      }
      uint8_t c = takeCard();
      if (c == MM_NONE) { nextTurn(0); return true; } // nothing to draw: just pass
      hand[turn] |= 1UL << c;
      drew = true; drawn = c;
      return true;
    }
    if (m == MM_PASS) {
      if (!drew) return false;
      nextTurn(0);
      return true;
    }
    uint8_t c = m & 31, w = (m >> 5) & 3;
    if (!canPlay(c)) return false;
    hand[turn] &= ~(1UL << c);
    pile[nPile++] = c;
    wish = rank(c) == MR_J ? w : MM_NONE;
    idle = 0;
    if (rank(c) == MR_7) penalty += 2;
    if (!hand[turn]) { winner = turn + 1; return true; }
    idle = 0;
    nextTurn(rank(c) == MR_8 ? 1 : 0);
    idle = 0;
    return true;
  }

  // ---------------- the CPU ----------------
  uint8_t cpuMove() {
    uint32_t h = hand[turn];
    uint8_t bySuit[4] = { 0, 0, 0, 0 };
    for (uint8_t c = 0; c < MM_CARDS; c++) if (h & (1UL << c)) bySuit[suit(c)]++;
    uint8_t minOpp = 99;                              // the next players' smallest hand
    for (uint8_t p = 0; p < players; p++) if (p != turn && count(hand[p]) < minOpp) minOpp = count(hand[p]);
    int16_t bestV = -1000;
    uint8_t best = MM_NONE;
    for (uint8_t c = 0; c < MM_CARDS; c++) {
      if (!canPlay(c)) continue;
      int16_t v = bySuit[suit(c)] * 4 + (bgRand(rnd) % 3);
      if (rank(c) == MR_J) v -= count(h) > 2 ? 30 : 0;        // keep the jacks for later
      if (rank(c) == MR_7) v += minOpp <= 2 ? 25 : 5;
      if (rank(c) == MR_8) v += 6;
      if (v > bestV) { bestV = v; best = c; }
    }
    if (best == MM_NONE) return drew ? MM_PASS : MM_DRAW;
    uint8_t w = 0;
    if (rank(best) == MR_J) {                         // wish the suit held most
      bySuit[suit(best)]--;
      for (uint8_t s = 1; s < 4; s++) if (bySuit[s] > bySuit[w]) w = s;
    }
    return best | (w << 5);
  }
};
