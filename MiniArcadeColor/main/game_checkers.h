// MiniArcade Color: Checkers (English draughts) against the CPU or two
// players on one console. Part of console.cpp, which includes it; the
// rules and the CPU are in boardgames.h (shared with the OLED console).
// A wooden board, red pieces at the bottom (you), white ones at the top.
// The arrows move the cursor, OK picks a piece and OK on a marked square
// moves it; taking is a must and goes on as long as the piece can take.
// The CPU searches 1 / 3 / 5 turns deep (easy also plays a random move
// now and then). A win scores 60 / 120 / 200 + 10 per own piece left, a
// draw a third of the base.
#include "boardgames.h"

#define CK_C   25                      // square size
#define CK_X   10                      // the board, top left
#define CK_Y   32
#define CK_PX  222                     // the panel on the right

static Checkers ck;
static uint8_t  ckCur = 52;            // cursor, 0..63 on the screen (row * 8 + column)
static int8_t   ckSel = -1;            // picked piece (0..31), -1 = none
// what the screen shows besides the board: a piece on its way, the last move
static struct {
  int8_t   hide, lastFrom, lastTo;     // board squares, -1 = none
  int16_t  fx, fy;                     // the moving piece (centre), fp = 0: none
  uint8_t  fp, mode;
  uint16_t score;
  bool     two;
  bool     flip;                       // online as player 1: the board turned, own side at the bottom
  const char *who[2];                  // online: the names of player 0 / 1, NULL = the usual
} ckV;

// screen square <-> board square; player 0 at the bottom unless flipped
static int8_t ckFromScreen(uint8_t s) { if (ckV.flip) s = 63 - s; return Checkers::at(s / 8, s % 8); }
static int16_t ckSx(int8_t i) { int8_t c; Checkers::rc((uint8_t)i, &c); if (ckV.flip) c = 7 - c; return CK_X + c * CK_C + CK_C / 2; }
static int16_t ckSy(int8_t i) { int8_t c, r = Checkers::rc((uint8_t)i, &c); if (ckV.flip) r = 7 - r; return CK_Y + r * CK_C + CK_C / 2; }

static const col CK_RED = rgb(204, 34, 34), CK_WHITE = rgb(240, 230, 200);

// a piece with some height: shadow, side, top, a groove, a shine; kings wear a crown
static void ckPiece(int16_t cx, int16_t cy, uint8_t p) {
  col base = Checkers::owner(p) == 0 ? CK_RED : CK_WHITE;
  gfx.disc(cx + 2, cy + 3, 10, rgb(40, 20, 0));
  gfx.disc(cx, cy + 2, 10, colDark(base, 2));
  gfx.disc(cx, cy, 10, base);
  gfx.circle(cx, cy, 7, colDark(base));
  gfx.disc(cx - 4, cy - 4, 2, colLight(base, 2));
  if (Checkers::king(p)) {
    col k = C::YELLOW, kd = C::ORANGE;
    gfx.fill(cx - 5, cy, 11, 4, k);
    gfx.triangle(cx - 5, cy, cx - 5, cy - 5, cx - 2, cy, k);
    gfx.triangle(cx - 2, cy, cx, cy - 6, cx + 2, cy, k);
    gfx.triangle(cx + 2, cy, cx + 5, cy - 5, cx + 5, cy, k);
    gfx.hline(cx - 5, cy + 3, 11, kd);
  }
}

static void ckBoard(const Checkers &b, int8_t cursor, int8_t sel) {
  CkStep st[48];
  uint8_t n = sel >= 0 ? b.gen(st) : 0;
  const col LIGHT = rgb(230, 190, 120), DARK = rgb(120, 60, 20);
  gfx.fill(CK_X - 7 + 3, CK_Y - 7 + 3, 8 * CK_C + 14, 8 * CK_C + 14, C::BLACK);
  gfx.fill(CK_X - 7, CK_Y - 7, 8 * CK_C + 14, 8 * CK_C + 14, rgb(60, 30, 0));       // frame
  gfx.frame(CK_X - 7, CK_Y - 7, 8 * CK_C + 14, 8 * CK_C + 14, rgb(153, 85, 40));
  gfx.frame(CK_X - 1, CK_Y - 1, 8 * CK_C + 2, 8 * CK_C + 2, rgb(60, 30, 0));
  for (uint8_t s = 0; s < 64; s++) {
    int16_t x = CK_X + (s % 8) * CK_C, y = CK_Y + (s / 8) * CK_C;
    int8_t i = ckFromScreen(s);
    col c = i < 0 ? LIGHT : DARK;
    if (i >= 0 && (i == ckV.lastFrom || i == ckV.lastTo)) c = rgb(110, 110, 40);
    gfx.fill(x, y, CK_C, CK_C, c);
    for (uint8_t k = 0; k < 3; k++)                             // a little grain
      gfx.hline(x + (s * 7 + k * 5) % 9, y + 4 + k * 8 + (s % 3), 10 + (s + k) % 8, colDark(c));
  }
  for (uint8_t s = 0; s < 64; s++) {
    int8_t i = ckFromScreen(s);
    if (i < 0) continue;
    int16_t cx = CK_X + (s % 8) * CK_C + CK_C / 2, cy = CK_Y + (s / 8) * CK_C + CK_C / 2;
    if (b.sq[i] && i != ckV.hide) ckPiece(cx, cy, b.sq[i]);
    for (uint8_t k = 0; k < n; k++)                             // where the picked piece may go
      if (st[k].from == sel && Checkers::next((uint8_t)sel, st[k].dir, st[k].jump ? 2 : 1) == i) {
        gfx.disc(cx, cy, 5, C::DKGRN);
        gfx.disc(cx, cy, 4, C::LIME);
      }
    if (i == sel && ((gameMillis() / 250) & 1) == 0) gfx.frame(cx - CK_C / 2, cy - CK_C / 2, CK_C, CK_C, C::CYAN, 2);
  }
  if (ckV.fp) ckPiece(ckV.fx, ckV.fy, ckV.fp);
  if (cursor >= 0) {
    int16_t x = CK_X + (cursor % 8) * CK_C, y = CK_Y + (cursor / 8) * CK_C;
    gfx.frame(x, y, CK_C, CK_C, C::YELLOW, 2);
  }
}

// the panel: pieces left, whose go, a note; msg = banner over the board
static void ckDraw(const Checkers &b, int8_t cursor, int8_t sel, const char *line1, const char *line2, const char *msg) {
  statusBar("CHECKERS", ckV.score, curHigh, C::BROWN);
  gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(60, 35, 15), rgb(15, 5, 0));
  ckBoard(b, cursor, sel);
  gfx.rbox(CK_PX, 32, 94, 200, 8, rgb(45, 25, 10));
  for (uint8_t p = 0; p < 2; p++) {                             // the top side first
    uint8_t who = ckV.flip ? p : 1 - p;
    int16_t y = 48 + p * 40;
    ckPiece(CK_PX + 20, y, who ? CK_MAN1 : CK_MAN0);
    char buf[8];
    snprintf(buf, sizeof(buf), "%u", b.count(who));
    gfx.text(CK_PX + 40, y - 8, buf, C::WHITE, F_TEXT);
    const char *name = ckV.who[who] ? ckV.who[who] : ckV.two ? (who ? "top" : "bottom") : (who ? "CPU" : "you");
    gfx.text(CK_PX + 64, y - 3, name, C::LTGREY, F_SMALL);
    if (!b.winner && b.turn == who) gfx.triangle(CK_PX + 4, y - 5, CK_PX + 4, y + 5, CK_PX + 8, y, C::YELLOW);
  }
  if (line1) {
    Font f = Gfx::width(line1, F_TEXT) <= 88 ? F_TEXT : F_SMALL;
    gfx.text(CK_PX + 47 - Gfx::width(line1, f) / 2, 120, line1, C::YELLOW, f);
  }
  if (line2) gfx.text(CK_PX + 47 - Gfx::width(line2, F_SMALL) / 2, 144, line2, C::ORANGE, F_SMALL);
  if (!ckV.two) {
    static const char *const LV[3] = { "easy", "normal", "hard" };
    gfx.text(CK_PX + 8, 168, "level", C::GREY, F_SMALL);
    gfx.text(CK_PX + 40, 168, LV[ckV.mode], C::LTGREY, F_SMALL);
  }
  gfx.text(CK_PX + 8, 196, "OK = pick,", C::GREY, F_SMALL);
  gfx.text(CK_PX + 8, 208, "OK = move", C::GREY, F_SMALL);
  if (msg) {
    int16_t w = Gfx::width(msg, F_BOLD) + 32, x = CK_X + (8 * CK_C - w) / 2;
    gfx.rbox(x + 4, 120, w, 42, 10, C::BLACK);
    gfx.rbox(x, 116, w, 42, 10, rgb(80, 40, 10));
    gfx.rframe(x, 116, w, 42, 10, C::ORANGE);
    gfx.text(x + 16, 125, msg, C::WHITE, F_BOLD);
  }
}

/* The keys for the player to move. Returns a step as (from | dir << 5)
   when one was made, -1 otherwise. ckCur / ckSel keep cursor and pick. */
static int16_t ckInput(const Checkers &b) {
  int8_t r = ckCur / 8, c = ckCur % 8;
  if (btn(B_UP)    && r > 0) { r--; sfx(700, 8); }
  if (btn(B_DOWN)  && r < 7) { r++; sfx(700, 8); }
  if (btn(B_LEFT)  && c > 0) { c--; sfx(700, 8); }
  if (btn(B_RIGHT) && c < 7) { c++; sfx(700, 8); }
  ckCur = (uint8_t)(r * 8 + c);
  if (b.chain >= 0) ckSel = b.chain;                            // taking on: the piece stays picked
  if (!btnTap(B_OK)) return -1;
  int8_t i = ckFromScreen(ckCur);
  if (i < 0) { sfx(250, 40); return -1; }
  CkStep st[48];
  uint8_t n = b.gen(st);
  if (ckSel >= 0)                                               // a target of the picked piece?
    for (uint8_t k = 0; k < n; k++)
      if (st[k].from == ckSel && Checkers::next((uint8_t)ckSel, st[k].dir, st[k].jump ? 2 : 1) == i)
        return ckSel | (st[k].dir << 5);
  if (b.chain < 0) {
    if (i == ckSel) { ckSel = -1; sfx(500, 20); return -1; }    // put it back
    for (uint8_t k = 0; k < n; k++) if (st[k].from == i) { ckSel = i; sfx(900, 20); return -1; }
  }
  sfx(250, 40);                                                 // nothing to do there
  return -1;
}

/* ---- the CPU, light on the stack ----
   The same search as Checkers::best / search in boardgames.h (same order,
   same values, same tie breaks), but the board copy and the move list of
   every ply live in a buffer on the heap, taken only while the CPU
   thinks: the main task's stack is small.                              */
#define CK_PLIES 16                    // depth 5 plus the jumps of long takes
struct CkPly { Checkers b; CkStep s[48]; };
static CkPly *ckPly = NULL;

static int16_t ckSearch(uint8_t ply, uint8_t depth, int16_t alpha, int16_t beta) {
  const Checkers &p = ckPly[ply].b;
  if (p.winner) return p.winner == 3 ? 0 : ((p.winner - 1) == p.turn ? 20000 : -20000 - depth);
  if (!depth || ply + 1 >= CK_PLIES) return p.eval(p.turn);
  uint8_t n = p.gen(ckPly[ply].s);
  for (uint8_t k = 0; k < n; k++) {
    Checkers &b = ckPly[ply + 1].b;
    b = p;
    bool more = b.apply(ckPly[ply].s[k].from, ckPly[ply].s[k].dir, false);
    int16_t v = more ? ckSearch(ply + 1, depth, alpha, beta)          // same player goes on
                     : -ckSearch(ply + 1, depth - 1, -beta, -alpha);
    if (v > alpha) alpha = v;
    if (alpha >= beta) break;
  }
  return alpha;
}

// the best step for the player to move in b; rnd breaks ties
static CkStep ckBest(const Checkers &b, uint8_t depth, uint32_t &rnd) {
  ckPly = (CkPly *)malloc(sizeof(CkPly) * CK_PLIES);
  if (!ckPly) return b.best(1, rnd);                            // no memory: think just one move ahead
  ckPly[0].b = b;
  CkStep *s = ckPly[0].s;
  uint8_t n = b.gen(s);
  int16_t bestV = -32000;
  CkStep pick = s[0];
  uint8_t ties = 0;
  for (uint8_t k = 0; k < n; k++) {
    Checkers &c = ckPly[1].b;
    c = b;
    bool more = c.apply(s[k].from, s[k].dir, false);
    int16_t v = more ? ckSearch(1, depth, -32000, 32000) : -ckSearch(1, depth - 1, -32000, 32000);
    if (v > bestV) { bestV = v; pick = s[k]; ties = 1; }
    else if (v == bestV && bgRand(rnd) % ++ties == 0) pick = s[k];
  }
  free(ckPly);
  ckPly = NULL;
  return pick;
}

// slides the piece of a step to its square, then applies it; false = left the game
static bool ckMove(uint8_t from, uint8_t dir, const char *line1) {
  int8_t m = Checkers::next(from, dir, 1);
  bool jump = ck.sq[m] != CK_EMPTY;
  int8_t to = jump ? Checkers::next(from, dir, 2) : m;
  int16_t x0 = ckSx((int8_t)from), y0 = ckSy((int8_t)from), x1 = ckSx(to), y1 = ckSy(to);
  uint16_t ms = jump ? 420 : 300;
  ckV.hide = (int8_t)from; ckV.fp = ck.sq[from];
  uint32_t t0 = gameMillis();
  while (true) {
    if (!poll()) { ckV.hide = -1; ckV.fp = 0; return false; }
    uint32_t t = gameMillis() - t0;
    if (t > ms) t = ms;
    ckV.fx = (int16_t)(x0 + (x1 - x0) * (int32_t)t / ms);
    ckV.fy = (int16_t)(y0 + (y1 - y0) * (int32_t)t / ms);
    if (jump) ckV.fy -= (int16_t)(8 * (int32_t)t * (ms - t) * 4 / ((int32_t)ms * ms));   // hops over
    ckDraw(ck, -1, -1, line1, NULL, NULL);
    gfx.send();
    if (t >= ms) break;
  }
  ckV.hide = -1; ckV.fp = 0;
  ckV.lastFrom = (int8_t)from; ckV.lastTo = to;
  ck.apply(from, dir);
  sfx(jump ? 900 : 600, jump ? 60 : 30);
  return true;
}

void checkersRun() {
  static const char *const MODES[4] = { "1P easy", "1P normal", "1P hard", "2 players" };
  bool again = true;
  while (again) {
    again = false;
    uint8_t mode = chooseMode("CHECKERS", MODES, 4, C::BROWN);
    if (mode == 255) return;
    ckV.two = mode == 3;
    ckV.mode = mode < 3 ? mode : 0;
    ckV.score = 0;
    ckV.hide = ckV.lastFrom = ckV.lastTo = -1;
    ckV.fp = 0;
    uint32_t rnd = millis() | 1;
    ck.begin();
    ckCur = 52; ckSel = -1;
    btnClear();
    while (!ck.winner) {
      if (!poll()) return;
      bool cpu = !ckV.two && ck.turn == 1;
      if (!cpu) {
        int16_t m = ckInput(ck);                                // two players: both from the bottom view
        const char *who = ckV.two ? (ck.turn ? "top's go" : "bottom's go") : "your go";
        if (m >= 0) {
          if (!ckMove((uint8_t)(m & 31), (uint8_t)(m >> 5), who)) return;
          if (ck.chain < 0) ckSel = -1;                         // the turn is over
          btnClear();
          continue;
        }
        ckDraw(ck, (int8_t)ckCur, ckSel, who, ck.chain >= 0 ? "take on!" : NULL, NULL);
        gfx.send();
      } else {                                                  // the CPU: shown thinking, then moves
        ckDraw(ck, -1, -1, "thinking...", NULL, NULL);
        gfx.send();
        static const uint8_t DEPTH[3] = { 1, 3, 5 };
        CkStep s = ckBest(ck, DEPTH[mode], rnd);
        if (mode == 0 && bgRand(rnd) % 3 == 0) {                // easy: now and then any move
          CkStep all[48];
          uint8_t n = ck.gen(all);
          s = all[bgRand(rnd) % n];
        }
        uint32_t t0 = gameMillis();
        while (gameMillis() - t0 < 600) {                       // the piece it picked, a moment
          if (!poll()) return;
          ckDraw(ck, -1, (int8_t)s.from, "CPU moves", NULL, NULL);
          gfx.send();
        }
        if (!ckMove(s.from, s.dir, "CPU moves")) return;
        btnClear();
      }
    }
    const char *msg = ck.winner == 3 ? "DRAW" : (ckV.two ? (ck.winner == 1 ? "BOTTOM WINS" : "TOP WINS")
                                                         : (ck.winner == 1 ? "YOU WIN" : "CPU WINS"));
    if (!ckV.two) {
      static const uint16_t BASE[3] = { 60, 120, 200 };
      if (ck.winner == 1) ckV.score = BASE[mode] + 10 * ck.count(0);
      if (ck.winner == 3) ckV.score = BASE[mode] / 3;
    }
    sfx(ck.winner == 1 ? 1400 : 250, 250);
    uint32_t t0 = gameMillis();
    btnClear();
    while (gameMillis() - t0 < 2500) {
      if (!poll()) return;
      if (btn(B_OK) && gameMillis() - t0 > 600) break;
      ckDraw(ck, -1, -1, NULL, NULL, msg);
      gfx.send();
    }
    again = gameOver(ckV.score);
  }
}

static const char *const IC_CHECKERS[12] = {
  "nwnooonwnooo", "wwwooowwwooo", "nwnooonwnooo", "ooonnnooonnn", "ooonnnooonnn", "ooonnnooonnn",
  "nrnooonnnooo", "rrrooonnnooo", "nrnooonnnooo", "ooonrnooonrn", "ooorrrooorrr", "ooonrnooonrn" };
