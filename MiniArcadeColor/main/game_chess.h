// MiniArcade Color: Chess against the CPU (three levels) or for two
// players on one console. Part of console.cpp, which includes it.
/* All the rules: castling, en passant, promotion (the player picks the
   piece), check, checkmate, stalemate, the 50-move rule and too little
   material to mate. Arrows move the cursor, OK picks a piece - its
   moves are marked - and OK on a marked square moves it. OK on the
   picked piece again, or on another own piece, changes the choice.
   Two players: the board can turn round for black, so each player has
   the own side at the bottom. Against the CPU the board faces the
   player, who picks white or black.
   A game in progress is kept in flash ("chsave") after every move and
   when it is left; the next start offers to continue it.
   Squares 0..63: row * 8 + column, row 0 = rank 8 (black's back row).
   Pieces: + white, - black; 1 pawn, 2 knight, 3 bishop, 4 rook,
   5 queen, 6 king.                                                   */

enum { CH_P = 1, CH_N, CH_B, CH_R, CH_Q, CH_K };
#define CH_WK 1                                    // castling rights
#define CH_WQ 2
#define CH_BK 4
#define CH_BQ 8

struct ChPos {
  int8_t  b[64];
  uint8_t white;                                   // 1 = white to move
  uint8_t castle;
  int8_t  ep;                                      // square a pawn can be taken on en passant, -1 = none
  uint8_t half;                                    // moves since a take or a pawn move (50-move rule)
};
struct ChMove { uint8_t from, to, promo; };        // promo: CH_N..CH_Q, 0 = none

static int8_t chSign(int8_t p) { return p > 0 ? 1 : (p < 0 ? -1 : 0); }
static bool   chOn(int r, int c) { return r >= 0 && r < 8 && c >= 0 && c < 8; }

static void chStart(ChPos &p) {
  static const int8_t BACK[8] = { CH_R, CH_N, CH_B, CH_Q, CH_K, CH_B, CH_N, CH_R };
  memset(&p, 0, sizeof(p));
  for (int c = 0; c < 8; c++) {
    p.b[c] = (int8_t)-BACK[c]; p.b[8 + c] = -CH_P;
    p.b[48 + c] = CH_P;        p.b[56 + c] = BACK[c];
  }
  p.white = 1; p.castle = CH_WK | CH_WQ | CH_BK | CH_BQ; p.ep = -1;
}

// is square sq attacked by the side "byWhite"?
static bool chAttacked(const ChPos &p, int sq, bool byWhite) {
  int r = sq / 8, c = sq % 8, s = byWhite ? 1 : -1;
  int pr = r + (byWhite ? 1 : -1);                 // a white pawn attacks upwards, so it stands below
  for (int dc = -1; dc <= 1; dc += 2)
    if (chOn(pr, c + dc) && p.b[pr * 8 + c + dc] == s * CH_P) return true;
  static const int8_t KN[8][2] = { {-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1} };
  for (auto &k : KN) if (chOn(r + k[0], c + k[1]) && p.b[(r + k[0]) * 8 + c + k[1]] == s * CH_N) return true;
  for (int dr = -1; dr <= 1; dr++)
    for (int dc = -1; dc <= 1; dc++) {
      if (!dr && !dc) continue;
      if (chOn(r + dr, c + dc) && p.b[(r + dr) * 8 + c + dc] == s * CH_K) return true;
      bool diag = dr && dc;
      for (int rr = r + dr, cc = c + dc; chOn(rr, cc); rr += dr, cc += dc) {
        int8_t q = p.b[rr * 8 + cc];
        if (!q) continue;
        if (q == s * CH_Q || q == s * (diag ? CH_B : CH_R)) return true;
        break;
      }
    }
  return false;
}

static int chKing(const ChPos &p, bool white) {
  for (int i = 0; i < 64; i++) if (p.b[i] == (white ? CH_K : -CH_K)) return i;
  return -1;
}
static bool chInCheck(const ChPos &p, bool white) {
  int k = chKing(p, white);
  return k >= 0 && chAttacked(p, k, !white);
}

static void chPlay(ChPos &p, const ChMove &m) {
  int8_t pc = p.b[m.from], s = chSign(pc), kind = (int8_t)(pc * s);
  bool take = p.b[m.to] != 0;
  if (kind == CH_P && m.to == p.ep) { p.b[m.to + (s > 0 ? 8 : -8)] = 0; take = true; }   // en passant
  if (kind == CH_K && abs(m.to - m.from) == 2) {                                         // castling: the rook too
    int row = m.from / 8;
    if (m.to > m.from) { p.b[row * 8 + 5] = p.b[row * 8 + 7]; p.b[row * 8 + 7] = 0; }
    else               { p.b[row * 8 + 3] = p.b[row * 8 + 0]; p.b[row * 8 + 0] = 0; }
  }
  p.b[m.to] = m.promo ? (int8_t)(s * m.promo) : pc;
  p.b[m.from] = 0;
  p.ep = (kind == CH_P && abs(m.to - m.from) == 16) ? (int8_t)((m.from + m.to) / 2) : (int8_t)-1;
  p.half = (kind == CH_P || take) ? 0 : (uint8_t)(p.half + 1);
  // a king or rook that moves, or a rook taken at home, ends the castling there
  static const uint8_t SQ[4] = { 63, 56, 7, 0 }, BIT[4] = { CH_WK, CH_WQ, CH_BK, CH_BQ };
  for (int i = 0; i < 4; i++) if (m.from == SQ[i] || m.to == SQ[i]) p.castle &= (uint8_t)~BIT[i];
  if (m.from == 60) p.castle &= (uint8_t)~(CH_WK | CH_WQ);
  if (m.from == 4)  p.castle &= (uint8_t)~(CH_BK | CH_BQ);
  p.white ^= 1;
}

// all legal moves of the side to move; returns the count.
// takes = only takes and promotions (the CPU's search at the end)
static int chMoves(const ChPos &p, ChMove *out, bool takes = false) {
  int n = 0;
  bool w = p.white;
  int8_t s = w ? 1 : -1;
  int king = chKing(p, w);
  auto add = [&](int from, int to, bool pawnLast) {
    if (takes && !p.b[to] && !pawnLast && !(to == p.ep && p.b[from] == s * CH_P)) return;
    for (int k = 0; k < (pawnLast ? 4 : 1); k++) {
      ChMove m = { (uint8_t)from, (uint8_t)to, (uint8_t)(pawnLast ? CH_Q - k : 0) };
      ChPos q = p;
      chPlay(q, m);
      int ks = from == king ? to : king;           // where the own king stands now
      if (ks < 0 || !chAttacked(q, ks, !w)) out[n++] = m;
    }
  };
  for (int sq = 0; sq < 64; sq++) {
    int8_t pc = p.b[sq];
    if (chSign(pc) != s) continue;
    int r = sq / 8, c = sq % 8, kind = pc * s;
    if (kind == CH_P) {
      int dir = w ? -1 : 1, home = w ? 6 : 1, last = w ? 0 : 7;
      int r1 = r + dir;
      if (chOn(r1, c) && !p.b[r1 * 8 + c]) {
        add(sq, r1 * 8 + c, r1 == last);
        if (r == home && !p.b[(r1 + dir) * 8 + c]) add(sq, (r1 + dir) * 8 + c, false);
      }
      for (int dc = -1; dc <= 1; dc += 2) {
        if (!chOn(r1, c + dc)) continue;
        int t = r1 * 8 + c + dc;
        if (chSign(p.b[t]) == -s || t == p.ep) add(sq, t, r1 == last);
      }
    } else if (kind == CH_N || kind == CH_K) {
      static const int8_t KN[8][2] = { {-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1} };
      static const int8_t KG[8][2] = { {-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,-1},{1,0},{1,1} };
      const int8_t (*d)[2] = kind == CH_N ? KN : KG;
      for (int k = 0; k < 8; k++) {
        int rr = r + d[k][0], cc = c + d[k][1];
        if (chOn(rr, cc) && chSign(p.b[rr * 8 + cc]) != s) add(sq, rr * 8 + cc, false);
      }
      if (kind == CH_K && !takes && !chAttacked(p, sq, !w)) {       // castling, never out of check
        uint8_t ks = w ? CH_WK : CH_BK, qs = w ? CH_WQ : CH_BQ;
        if ((p.castle & ks) && !p.b[sq + 1] && !p.b[sq + 2] && !chAttacked(p, sq + 1, !w)) add(sq, sq + 2, false);
        if ((p.castle & qs) && !p.b[sq - 1] && !p.b[sq - 2] && !p.b[sq - 3] && !chAttacked(p, sq - 1, !w)) add(sq, sq - 2, false);
      }
    } else {                                                        // the long-range pieces
      for (int dr = -1; dr <= 1; dr++)
        for (int dc = -1; dc <= 1; dc++) {
          if (!dr && !dc) continue;
          bool diag = dr && dc;
          if ((kind == CH_B && !diag) || (kind == CH_R && diag)) continue;
          for (int rr = r + dr, cc = c + dc; chOn(rr, cc); rr += dr, cc += dc) {
            int8_t q = p.b[rr * 8 + cc];
            if (chSign(q) == s) break;
            add(sq, rr * 8 + cc, false);
            if (q) break;
          }
        }
    }
  }
  return n;
}

// only kings, or a king and one knight or bishop against a king: nobody can mate
static bool chTooLittle(const ChPos &p) {
  int minor = 0;
  for (int i = 0; i < 64; i++) {
    int k = abs(p.b[i]);
    if (k == CH_P || k == CH_R || k == CH_Q) return false;
    if (k == CH_N || k == CH_B) minor++;
  }
  return minor <= 1;
}

// for the tests: a position from FEN (board, side, castling, en passant)
[[maybe_unused]] static bool chFromFen(ChPos &p, const char *f) {
  static const char PC[] = "pnbrqk";
  memset(&p, 0, sizeof(p));
  p.ep = -1;
  int sq = 0;
  for (; *f && *f != ' '; f++) {
    if (*f == '/') continue;
    if (*f >= '1' && *f <= '8') { sq += *f - '0'; continue; }
    const char *k = strchr(PC, *f | 0x20);
    if (!k || sq > 63) return false;
    p.b[sq++] = (int8_t)((k - PC + 1) * (*f >= 'a' ? -1 : 1));
  }
  if (*f++ != ' ') return false;
  p.white = *f++ == 'w';
  f++;
  for (; *f && *f != ' '; f++) {
    if (*f == 'K') p.castle |= CH_WK;
    if (*f == 'Q') p.castle |= CH_WQ;
    if (*f == 'k') p.castle |= CH_BK;
    if (*f == 'q') p.castle |= CH_BQ;
  }
  if (*f == ' ' && f[1] >= 'a' && f[1] <= 'h') p.ep = (int8_t)(('8' - f[2]) * 8 + (f[1] - 'a'));
  return sq == 64;
}

// ---------------- drawing ----------------
static const char *const CH_ICON[6][12] = {
  { "............", "............", ".....xx.....", "....xoox....", "....xoox....", ".....xx.....",
    "....xoox....", "...xoooox...", "...xoooox...", "..xoooooox..", "..xxxxxxxx..", "............" },   // pawn
  { "............", "....xx......", "...xooxx....", "..xoooooox..", ".xooxooooox.", ".xoooooooox.",
    "..xxxxooox..", "....xooox...", "...xoooox...", "..xoooooox..", ".xxxxxxxxxx.", "............" },   // knight
  { "............", ".....xx.....", "....xoox....", "...xooxox...", "...xoxoox...", "...xoooox...",
    "....xoox....", "....xoox....", "...xoooox...", "..xoooooox..", "..xxxxxxxx..", "............" },   // bishop
  { "............", "..xx.xx.xx..", "..xxxxxxxx..", "..xoooooox..", "...xoooox...", "...xoooox...",
    "...xoooox...", "...xoooox...", "..xoooooox..", ".xoooooooox.", ".xxxxxxxxxx.", "............" },   // rook
  { ".x...xx...x.", ".xx..xx..xx.", ".xox.xx.xox.", ".xooxooxoox.", ".xoooooooox.", "..xoooooox..",
    "..xoooooox..", "...xoooox...", "..xoooooox..", ".xoooooooox.", ".xxxxxxxxxx.", "............" },   // queen
  { ".....xx.....", "....xxxx....", ".....xx.....", "..xx.xx.xx..", ".xooxooxoox.", ".xoooooooox.",
    "..xoooooox..", "...xoooox...", "..xoooooox..", ".xoooooooox.", ".xxxxxxxxxx.", "............" } }; // king
static const char *const IC_CHESS[12] = {
  ".....kk.....", "....kkkk....", ".....kk.....", "..kk.kk.kk..", ".kwwkwwkwwk.", ".kwwwwwwwwk.",
  "..kwwwwwwk..", "...kwwwwk...", "..kwwwwwwk..", ".kwwwwwwwwk.", ".kkkkkkkkkk.", "nnnnnnnnnnnn" };

#define CH_SQ 26                                    // square size
#define CH_X0 8                                     // board corner
#define CH_Y0 (TOP_H + 4)

static const char CH_KEYS[] = "xo";               // outline, body
static const col  CH_WHITE[2] = { C::BLACK, C::WHITE };
static const col  CH_BLACK[2] = { C::LTGREY, rgb(40, 40, 40) };

static void chPiece(int16_t x, int16_t y, int8_t pc, uint8_t scale = 2) {
  gfx.sprite(x + 1, y + 1, CH_ICON[abs(pc) - 1], 12, CH_KEYS, pc > 0 ? CH_WHITE : CH_BLACK, scale);
}

struct ChGame {
  // ---- kept in flash ("chsave"): everything up to moves ----
  uint8_t magic;                                    // CH_MAGIC: a saved game of this layout
  ChPos   pos;
  int8_t  human;                                    // vs CPU: 1 = player has white, 0 = black; -1 = two players
  uint8_t level;                                    // CPU: 0 easy, 1 normal, 2 hard
  bool    turnBoard;                                // two players: black plays from the bottom too
  int8_t  lastFrom, lastTo;
  uint8_t cr, cc;                                   // cursor on the board (as drawn)
  int8_t  takenW[16], takenB[16];                   // taken pieces (white's, black's)
  uint8_t nTakenW, nTakenB;
  uint16_t plies;                                   // half moves played
  char    lastText[12];
  // ---- only while it runs ----
  ChMove  moves[220];                               // legal moves right now
  int     nMoves;
  int8_t  sel;                                      // picked square, -1 = none
};
#define CH_MAGIC 0xC1
#define CH_SAVED offsetof(ChGame, moves)
static ChGame *ch = NULL;                           // the running game (heap)
static const char *chEnd = NULL;                    // the result once the game is over
static bool chHasSave = false;                      // a game is kept in flash

static bool chFlipped() { return ch->human >= 0 ? !ch->human : (ch->turnBoard && !ch->pos.white); }
static bool chCpuGo()   { return ch->human >= 0 && ch->pos.white != ch->human; }
static int  chSqAt(int r, int c) { return chFlipped() ? (7 - r) * 8 + (7 - c) : r * 8 + c; }
static void chXY(int sq, int16_t &x, int16_t &y) {
  int r = sq / 8, c = sq % 8;
  if (chFlipped()) { r = 7 - r; c = 7 - c; }
  x = (int16_t)(CH_X0 + c * CH_SQ); y = (int16_t)(CH_Y0 + r * CH_SQ);
}

static void chDraw(const char *status, col statusCol) {
  static const char *const LV[3] = { "vs CPU easy", "vs CPU normal", "vs CPU hard" };
  gfx.gradient(0, 0, SCR_W, TOP_H, colLight(C::BROWN), colDark(C::BROWN, 2));   // a bar without score
  gfx.textShadow(6, 5, "CHESS", C::WHITE, C::BLACK, F_TEXT);
  gfx.right(SCR_W - 34, 9, ch->human < 0 ? "2 players" : LV[ch->level], C::LTGREY, F_SMALL);
  batIcon(SCR_W - 28, 7);
  gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(60, 40, 20), rgb(20, 10, 0));
  gfx.fill(CH_X0 - 3, CH_Y0 - 3, CH_SQ * 8 + 6, CH_SQ * 8 + 6, rgb(90, 60, 30));
  int kingInCheck = chInCheck(ch->pos, ch->pos.white) ? chKing(ch->pos, ch->pos.white) : -1;
  for (int sq = 0; sq < 64; sq++) {
    int16_t x, y;
    chXY(sq, x, y);
    bool light = ((sq / 8) + (sq % 8)) % 2 == 0;
    col c = light ? rgb(255, 204, 153) : rgb(153, 102, 51);
    if (sq == ch->lastFrom || sq == ch->lastTo) c = light ? rgb(230, 230, 120) : rgb(180, 180, 70);
    if (sq == ch->sel) c = rgb(110, 200, 110);
    if (sq == kingInCheck) c = rgb(230, 70, 70);
    gfx.fill(x, y, CH_SQ, CH_SQ, c);
  }
  for (int sq = 0; sq < 64; sq++) {
    int16_t x, y;
    chXY(sq, x, y);
    if (ch->pos.b[sq]) chPiece(x, y, ch->pos.b[sq]);
  }
  if (ch->sel >= 0)                                 // where the picked piece can go
    for (int i = 0; i < ch->nMoves; i++)
      if (ch->moves[i].from == ch->sel) {
        int16_t x, y;
        chXY(ch->moves[i].to, x, y);
        if (ch->pos.b[ch->moves[i].to]) gfx.frame(x + 1, y + 1, CH_SQ - 2, CH_SQ - 2, rgb(40, 140, 40), 3);
        else gfx.disc(x + CH_SQ / 2, y + CH_SQ / 2, 4, rgb(40, 140, 40));
      }
  if (!chCpuGo() || chEnd) {                        // the cursor, not while the CPU thinks
    int16_t cx = (int16_t)(CH_X0 + ch->cc * CH_SQ), cy = (int16_t)(CH_Y0 + ch->cr * CH_SQ);
    gfx.frame(cx, cy, CH_SQ, CH_SQ, C::YELLOW, 3);
  }
  // files and ranks along the board
  for (int i = 0; i < 8; i++) {
    char f[2] = { (char)('a' + (chFlipped() ? 7 - i : i)), 0 }, r[2] = { (char)('8' - (chFlipped() ? 7 - i : i)), 0 };
    gfx.text(CH_X0 + i * CH_SQ + 1, CH_Y0 + CH_SQ * 8 - 9, f, C::BLACK, F_SMALL);
    gfx.text(CH_X0 + 1, CH_Y0 + i * CH_SQ + 1, r, C::BLACK, F_SMALL);
  }
  // the panel on the right
  int16_t px = CH_X0 + CH_SQ * 8 + 8;
  gfx.rbox(px, CH_Y0, SCR_W - px - 4, 64, 6, ch->pos.white ? C::WHITE : rgb(40, 40, 40));
  gfx.text(px + 8, CH_Y0 + 8, ch->pos.white ? "WHITE" : "BLACK", ch->pos.white ? C::BLACK : C::WHITE, F_BOLD);
  const char *who = ch->human < 0 ? "to move" : (chCpuGo() ? "CPU's go" : "your go");
  gfx.text(px + 8, CH_Y0 + 38, who, ch->pos.white ? C::DKGREY : C::LTGREY);
  if (status) {                                    // one or two lines (split at the space)
    char a[16];
    strncpy(a, status, sizeof(a) - 1); a[sizeof(a) - 1] = 0;
    char *sp = strchr(a, ' ');
    if (sp) *sp = 0;
    gfx.text(px + 4, CH_Y0 + 72, a, statusCol, Gfx::width(a, F_TEXT) <= SCR_W - px - 8 ? F_TEXT : F_SMALL);
    if (sp) gfx.text(px + 4, CH_Y0 + 92, sp + 1, statusCol, F_TEXT);
  }
  if (ch->lastText[0]) gfx.text(px + 4, CH_Y0 + 114, ch->lastText, C::LTGREY, F_SMALL);
  // taken pieces, small
  for (int i = 0; i < ch->nTakenB; i++) chPiece((int16_t)(px + (i % 6) * 13), (int16_t)(CH_Y0 + 128 + (i / 6) * 13), ch->takenB[i], 1);
  for (int i = 0; i < ch->nTakenW; i++) chPiece((int16_t)(px + (i % 6) * 13), (int16_t)(CH_Y0 + 160 + (i / 6) * 13), ch->takenW[i], 1);
  gfx.text(px, SCR_H - 12, "OK pick / move", C::GREY, F_SMALL);
}

// the promoted piece: LEFT/RIGHT, OK
static uint8_t chPickPromo() {
  static const uint8_t P[4] = { CH_Q, CH_R, CH_B, CH_N };
  uint8_t k = 0;
  btnClear();
  while (poll(false)) {
    if (btn(B_LEFT))  { k = (uint8_t)((k + 3) % 4); sfx(700, 15); }
    if (btn(B_RIGHT)) { k = (uint8_t)((k + 1) % 4); sfx(700, 15); }
    if (btn(B_OK)) return P[k];
    panel(60, 80, 200, 90, "PROMOTE", C::BROWN);
    for (int i = 0; i < 4; i++) {
      int16_t x = 74 + i * 46, y = 120;
      gfx.fill(x, y, CH_SQ, CH_SQ, i == k ? rgb(110, 200, 110) : rgb(255, 204, 153));
      chPiece(x, y, (int8_t)(ch->pos.white ? P[i] : -P[i]));
    }
    gfx.send();
  }
  return CH_Q;
}

// ---------------- the saved game ----------------
static void chSave() {
  ch->magic = CH_MAGIC;
  prefs.begin("arcade", false);
  prefs.putBytes("chsave", ch, CH_SAVED);
  prefs.end();
  chHasSave = true;
}
static void chUnsave() {                            // one byte: no game of the right size
  if (!chHasSave) return;
  uint8_t z = 0;
  prefs.begin("arcade", false);
  prefs.putBytes("chsave", &z, 1);
  prefs.end();
  chHasSave = false;
}
static bool chLoad() {
  prefs.begin("arcade", true);
  size_t n = prefs.getBytes("chsave", ch, CH_SAVED);
  prefs.end();
  chHasSave = n == CH_SAVED && ch->magic == CH_MAGIC && ch->human >= -1 && ch->human <= 1 && ch->level < 3 &&
              ch->cr < 8 && ch->cc < 8 && ch->nTakenW <= 16 && ch->nTakenB <= 16 &&
              chKing(ch->pos, true) >= 0 && chKing(ch->pos, false) >= 0 && chMoves(ch->pos, ch->moves) > 0;
  return chHasSave;
}

// ---------------- the CPU ----------------
/* Alpha-beta (negamax) on material and piece-square tables, deeper step
   by step until the time or the node budget is used up; at the end of
   the line only takes (and the ways out of check) are searched on. The
   main task's stack is small: the positions and move lists of all plies
   live in one buffer on the heap, the frames are small. Every 256 nodes
   it looks at the clock and, every 40 ms, at the keys (poll).          */
#define CH_PLIES 16                                 // plies the search can go down, takes included
#define CH_STACK 1024                               // the move lists of all plies together
#define CH_MATE  30000
#define CH_INF   32000
struct ChEngine {
  ChPos    pos[CH_PLIES + 1];
  ChMove   mv[CH_STACK];
  int16_t  sc[CH_STACK];                            // order of the moves
  ChMove   killer[CH_PLIES];                        // a quiet move that cut off at that ply
  uint32_t nodes, cap, t0, ms, polled;
  uint8_t  depth;                                   // the depth being searched (the last one when done)
  bool     stop, quit, ui;                          // ui: keys and screen while it thinks
};
static ChEngine *che = NULL;

static const int16_t CH_VAL[7] = { 0, 100, 320, 330, 500, 900, 0 };
// piece-square tables seen from white, a8 first; black reads them mirrored
static const int8_t CH_PST[7][64] = {
  {   0,  0,  0,  0,  0,  0,  0,  0,  50, 50, 50, 50, 50, 50, 50, 50,  10, 10, 20, 30, 30, 20, 10, 10,   // pawn
      5,  5, 10, 25, 25, 10,  5,  5,   0,  0,  0, 20, 20,  0,  0,  0,   5, -5,-10,  0,  0,-10, -5,  5,
      5, 10, 10,-20,-20, 10, 10,  5,   0,  0,  0,  0,  0,  0,  0,  0 },
  { -50,-40,-30,-30,-30,-30,-40,-50, -40,-20,  0,  0,  0,  0,-20,-40, -30,  0, 10, 15, 15, 10,  0,-30,   // knight
    -30,  5, 15, 20, 20, 15,  5,-30, -30,  0, 15, 20, 20, 15,  0,-30, -30,  5, 10, 15, 15, 10,  5,-30,
    -40,-20,  0,  5,  5,  0,-20,-40, -50,-40,-30,-30,-30,-30,-40,-50 },
  { -20,-10,-10,-10,-10,-10,-10,-20, -10,  0,  0,  0,  0,  0,  0,-10, -10,  0,  5, 10, 10,  5,  0,-10,   // bishop
    -10,  5,  5, 10, 10,  5,  5,-10, -10,  0, 10, 10, 10, 10,  0,-10, -10, 10, 10, 10, 10, 10, 10,-10,
    -10,  5,  0,  0,  0,  0,  5,-10, -20,-10,-10,-10,-10,-10,-10,-20 },
  {   0,  0,  0,  0,  0,  0,  0,  0,   5, 10, 10, 10, 10, 10, 10,  5,  -5,  0,  0,  0,  0,  0,  0, -5,   // rook
     -5,  0,  0,  0,  0,  0,  0, -5,  -5,  0,  0,  0,  0,  0,  0, -5,  -5,  0,  0,  0,  0,  0,  0, -5,
     -5,  0,  0,  0,  0,  0,  0, -5,   0,  0,  0,  5,  5,  0,  0,  0 },
  { -20,-10,-10, -5, -5,-10,-10,-20, -10,  0,  0,  0,  0,  0,  0,-10, -10,  0,  5,  5,  5,  5,  0,-10,   // queen
     -5,  0,  5,  5,  5,  5,  0, -5,   0,  0,  5,  5,  5,  5,  0, -5, -10,  5,  5,  5,  5,  5,  0,-10,
    -10,  0,  5,  0,  0,  0,  0,-10, -20,-10,-10, -5, -5,-10,-10,-20 },
  { -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30, -30,-40,-40,-50,-50,-40,-40,-30,   // king, middle game
    -30,-40,-40,-50,-50,-40,-40,-30, -20,-30,-30,-40,-40,-30,-30,-20, -10,-20,-20,-20,-20,-20,-20,-10,
     20, 20,  0,  0,  0,  0, 20, 20,  20, 30, 10,  0,  0, 10, 30, 20 },
  { -50,-40,-30,-20,-20,-30,-40,-50, -30,-20,-10,  0,  0,-10,-20,-30, -30,-10, 20, 30, 30, 20,-10,-30,   // king, end game
    -30,-10, 30, 40, 40, 30,-10,-30, -30,-10, 30, 40, 40, 30,-10,-30, -30,-10, 20, 30, 30, 20,-10,-30,
    -30,-30,  0,  0,  0,  0,-30,-30, -50,-30,-30,-30,-30,-30,-30,-50 } };

// the position for the side to move, in centipawns
static int chEval(const ChPos &p) {
  int s = 0, pieces[2] = { 0, 0 }, king[2] = { 0, 0 };   // [0] white
  for (int sq = 0; sq < 64; sq++) {
    int8_t pc = p.b[sq];
    if (!pc) continue;
    int k = abs(pc), w = pc > 0, i = w ? sq : sq ^ 56;
    if (k == CH_K) { king[!w] = sq; continue; }
    int v = CH_VAL[k] + CH_PST[k - 1][i];
    if (k != CH_P) pieces[!w] += CH_VAL[k];
    s += w ? v : -v;
  }
  int t = pieces[0] + pieces[1] <= 2600 ? 6 : 5;     // few pieces left: the king comes out
  s += CH_PST[t][king[0]] - CH_PST[t][king[1] ^ 56];
  // only a king (and pawns) left against more: drive it to the edge, the own king close
  for (int a = 0; a < 2; a++)
    if (!pieces[1 - a] && pieces[a] >= 400) {
      int r = king[1 - a] / 8, c = king[1 - a] % 8;
      int edge = (r < 4 ? 3 - r : r - 4) + (c < 4 ? 3 - c : c - 4);
      int near = 14 - abs(r - king[a] / 8) - abs(c - king[a] % 8);
      s += (a ? -1 : 1) * (10 * edge + 4 * near);
    }
  return p.white ? s : -s;
}

// every 256 nodes: the clock, the budget and (vs a player) the keys; true = stop
static bool chTick() {
  ChEngine &e = *che;
  if (++e.nodes & 255) return e.stop;
  if (e.nodes >= e.cap || gameMillis() - e.t0 >= e.ms) e.stop = true;
  if (e.ui && millis() - e.polled >= 40) {
    uint32_t paused = pausedMs;
    if (!poll(false)) e.stop = e.quit = true;
    else if (pausedMs != paused) { chDraw("thinking...", C::YELLOW); gfx.send(); }   // the pause menu covered it
    e.polled = millis();
  }
  return e.stop;
}

// order: takes (most valuable victim, cheapest attacker first), promotions, the killer
static void chScore(int ply, int top, int n) {
  const ChPos &p = che->pos[ply];
  const ChMove &kl = che->killer[ply];
  for (int i = top; i < top + n; i++) {
    const ChMove &m = che->mv[i];
    int v = abs(p.b[m.to]), a = abs(p.b[m.from]);
    if (!v && a == CH_P && m.to == p.ep) v = CH_P;
    int s = v ? 1000 + v * 10 - a : 0;
    if (m.promo) s += 900 + m.promo;
    else if (!v && m.from == kl.from && m.to == kl.to) s = 500;
    che->sc[i] = (int16_t)s;
  }
}
static void chPick(int i, int end) {                // the best of the rest to i
  int b = i;
  for (int j = i + 1; j < end; j++) if (che->sc[j] > che->sc[b]) b = j;
  if (b == i) return;
  ChMove m = che->mv[i]; che->mv[i] = che->mv[b]; che->mv[b] = m;
  int16_t s = che->sc[i]; che->sc[i] = che->sc[b]; che->sc[b] = s;
}

// at the end of a line: takes only, as long as they gain something
static int chQuiet(int ply, int top, int alpha, int beta) {
  if (chTick()) return 0;
  const ChPos &p = che->pos[ply];
  int stand = chEval(p);                            // standing still is allowed
  if (stand >= beta || ply >= CH_PLIES - 1 || top + 220 > CH_STACK) return stand;
  if (stand > alpha) alpha = stand;
  int best = stand, n = chMoves(p, che->mv + top, true);
  chScore(ply, top, n);
  for (int i = top; i < top + n; i++) {
    chPick(i, top + n);
    const ChMove &m = che->mv[i];
    int gain = (p.b[m.to] ? CH_VAL[abs(p.b[m.to])] : (m.promo ? 0 : CH_VAL[CH_P])) + (m.promo ? CH_VAL[m.promo] - CH_VAL[CH_P] : 0);
    if (stand + gain + 200 <= alpha) continue;      // cannot get near alpha: left out
    che->pos[ply + 1] = p;
    chPlay(che->pos[ply + 1], m);
    int v = -chQuiet(ply + 1, top + n, -beta, -alpha);
    if (che->stop) return 0;
    if (v > best) best = v;
    if (v > alpha) alpha = v;
    if (alpha >= beta) break;
  }
  return best;
}

static int chSearch(int ply, int top, int depth, int alpha, int beta) {
  const ChPos &p = che->pos[ply];
  bool check = chInCheck(p, p.white);
  if (depth <= 0 && !check) return chQuiet(ply, top, alpha, beta);
  if (depth <= 0) depth = 1;                        // in check at the end: one ply more (finds the mates)
  if (chTick()) return 0;
  if (p.half >= 100) return 0;                      // 50-move rule
  if (ply >= CH_PLIES - 1 || top + 220 > CH_STACK) return chEval(p);
  int n = chMoves(p, che->mv + top);
  if (!n) return check ? -CH_MATE + ply : 0;        // mate (sooner is worse) or stalemate
  chScore(ply, top, n);
  int best = -CH_INF;
  for (int i = top; i < top + n; i++) {
    chPick(i, top + n);
    ChMove m = che->mv[i];
    che->pos[ply + 1] = p;
    chPlay(che->pos[ply + 1], m);
    int v = -chSearch(ply + 1, top + n, depth - 1, -beta, -alpha);
    if (che->stop) return 0;
    if (v > best) best = v;
    if (v > alpha) alpha = v;
    if (alpha >= beta) {
      if (!p.b[m.to] && !m.promo) che->killer[ply] = m;
      break;
    }
  }
  return best;
}

/* The best move for the side to move in p, level 0..2. Easy looks one
   move ahead and adds noise, normal up to 3, hard up to 5 plies (plus
   the takes). false = no move, or the player left while it thought.  */
static const uint8_t  CH_DEPTH[3] = { 1, 3, 5 };
static const uint16_t CH_MS[3]    = { 500, 1000, 2500 };      // think time (ms) ...
static const uint32_t CH_CAP[3]   = { 4000, 15000, 40000 };   // ... and nodes at most
static bool chBest(const ChPos &p, uint8_t level, ChMove &out) {
  ChEngine &e = *che;
  e.pos[0] = p;
  e.nodes = 0; e.cap = CH_CAP[level]; e.ms = CH_MS[level]; e.t0 = gameMillis(); e.polled = millis();
  e.stop = e.quit = false;
  memset(e.killer, 0, sizeof(e.killer));
  int n = chMoves(p, e.mv);
  if (!n) return false;
  for (int i = n - 1; i > 0; i--) {                 // equal moves in a new order each time
    int j = (int)random(i + 1);
    ChMove m = e.mv[i]; e.mv[i] = e.mv[j]; e.mv[j] = m;
  }
  chScore(0, 0, n);
  for (int i = 1; i < n; i++)                       // takes first, otherwise the order stays
    for (int j = i; j > 0 && e.sc[j] > e.sc[j - 1]; j--) {
      ChMove m = e.mv[j]; e.mv[j] = e.mv[j - 1]; e.mv[j - 1] = m;
      int16_t s = e.sc[j]; e.sc[j] = e.sc[j - 1]; e.sc[j - 1] = s;
    }
  out = e.mv[0];
  e.depth = 0;
  for (uint8_t d = 1; d <= CH_DEPTH[level] && n > 1; d++) {
    e.depth = d;
    // easy adds up to +-40 to each score: exact within 80 of the best, bounds below that do not matter
    int alpha = -CH_INF, pick = -CH_INF, best = -1, slack = level ? 0 : 80;
    for (int i = 0; i < n; i++) {
      e.pos[1] = p;
      chPlay(e.pos[1], e.mv[i]);
      int v = -chSearch(1, n, d - 1, -CH_INF, alpha > -CH_INF ? slack - alpha : CH_INF);
      if (e.stop) break;
      if (v > alpha) alpha = v;
      if (!level) v += (int)random(81) - 40;
      if (v > pick) { pick = v; best = i; }
    }
    if (best >= 0) {                                // the best first in the next round
      ChMove m = e.mv[best];
      memmove(e.mv + 1, e.mv, (size_t)best * sizeof(ChMove));
      e.mv[0] = out = m;
    }
    if (e.stop || alpha > CH_MATE - 100) break;     // out of time, or a mate found
  }
  return !e.quit;
}

// the CPU's move in the running game; false = the player left meanwhile
static bool chThink(ChMove &m) {
  che->ui = true;
  if (!chBest(ch->pos, ch->level, m)) return false;
  if (ch->level == 0 && ch->nMoves > 1 && random(4) == 0) m = ch->moves[random(ch->nMoves)];   // easy: now and then any move
  return true;
}

// ---------------- the game ----------------
static void chNew() {                               // keeps the mode
  int8_t human = ch->human;
  uint8_t level = ch->level;
  bool turn = ch->turnBoard;
  memset(ch, 0, sizeof(*ch));
  chStart(ch->pos);
  ch->human = human; ch->level = level; ch->turnBoard = turn;
  ch->sel = ch->lastFrom = ch->lastTo = -1;
  ch->cr = 6; ch->cc = chFlipped() ? 3 : 4;         // on the own king's pawn
  ch->nMoves = chMoves(ch->pos, ch->moves);
  chEnd = NULL;
}

// plays a move on the board; sets chEnd when the game is over
static col chApply(ChMove m) {
  int8_t taken = ch->pos.b[m.to];
  if (abs(ch->pos.b[m.from]) == CH_P && m.to == ch->pos.ep) taken = (int8_t)(ch->pos.white ? -CH_P : CH_P);
  if (taken > 0 && ch->nTakenW < 16) ch->takenW[ch->nTakenW++] = taken;
  if (taken < 0 && ch->nTakenB < 16) ch->takenB[ch->nTakenB++] = taken;
  snprintf(ch->lastText, sizeof(ch->lastText), "%c%c-%c%c", 'a' + m.from % 8, '8' - m.from / 8, 'a' + m.to % 8, '8' - m.to / 8);
  bool turnedBefore = chFlipped();
  chPlay(ch->pos, m);
  ch->plies++;
  ch->lastFrom = (int8_t)m.from; ch->lastTo = (int8_t)m.to;
  ch->sel = -1;
  if (chFlipped() != turnedBefore) { ch->cr = (uint8_t)(7 - ch->cr); ch->cc = (uint8_t)(7 - ch->cc); }   // cursor stays on its square
  ch->nMoves = chMoves(ch->pos, ch->moves);
  bool check = chInCheck(ch->pos, ch->pos.white);
  sfx(taken ? 500 : 900, taken ? 80 : 40);
  col endCol = C::CYAN;
  if (!ch->nMoves) {
    if (!check) chEnd = "STALEMATE";
    else if (ch->human < 0) chEnd = ch->pos.white ? "BLACK WINS" : "WHITE WINS";
    else chEnd = ch->pos.white != ch->human ? "YOU WIN" : "CPU WINS";
    if (check) endCol = C::YELLOW;
    sfx(check ? 1500 : 600, 400);
  } else if (ch->pos.half >= 100) chEnd = "DRAW (50)";
  else if (chTooLittle(ch->pos))  chEnd = "DRAW";
  else if (check) sfx(1300, 120);
  if (chEnd) chUnsave(); else chSave();
  return endCol;
}

// the mode screens; false = left
static bool chSetup() {
  static const char *const MODES[5] = { "continue saved game", "1P easy", "1P normal", "1P hard", "2 players" };
  static const char *const SIDE[2] = { "white", "black" };
  static const char *const TURN[2] = { "board turns round", "board stays" };
  bool saved = chLoad();
  while (true) {
    uint8_t k = chooseMode("CHESS", MODES + !saved, saved ? 5 : 4, C::BROWN);
    if (k == 255) return false;
    if (saved && k == 0) {                          // on where it was left
      ch->sel = -1;
      ch->nMoves = chMoves(ch->pos, ch->moves);
      chEnd = NULL;
      return true;
    }
    k = (uint8_t)(k - saved);                       // 0..2 the CPU's level, 3 two players
    uint8_t sub = k < 3 ? chooseMode("YOUR SIDE", SIDE, 2, C::BROWN) : chooseMode("CHESS", TURN, 2, C::BROWN);
    if (sub == 255) continue;                       // back to the first choice
    ch->human = k < 3 ? (int8_t)(sub == 0) : (int8_t)-1;
    ch->level = k < 3 ? k : 0;
    ch->turnBoard = k == 3 && sub == 0;
    chNew();
    chUnsave();                                     // a new game: the saved one is gone
    return true;
  }
}

void chessRun() {
  ch = (ChGame *)malloc(sizeof(ChGame));
  if (!ch) return;
  memset(ch, 0, sizeof(*ch));
  if (!chSetup() || (ch->human >= 0 && !(che = (ChEngine *)malloc(sizeof(ChEngine))))) { free(ch); ch = NULL; return; }
  col endCol = C::WHITE;
  btnClear();
  while (poll()) {
    if (chEnd) {                                     // finished: OK starts a new game
      chDraw(chEnd, endCol);
      gfx.fill(CH_X0 + CH_SQ * 8 + 6, SCR_H - 14, 90, 14, rgb(20, 10, 0));
      gfx.text(CH_X0 + CH_SQ * 8 + 8, SCR_H - 12, "OK new game", C::YELLOW, F_SMALL);
      gfx.send();
      if (btn(B_OK)) { chNew(); btnClear(); }
      continue;
    }
    if (chCpuGo()) {                                 // the CPU: thinks, then moves
      chDraw("thinking...", C::YELLOW);
      gfx.send();
      uint32_t t0 = gameMillis();
      ChMove m = { 0, 0, 0 };
      bool on = chThink(m);
      while (on && gameMillis() - t0 < 500) {        // at least a moment, so the move can be followed
        on = poll();
        if (on) { chDraw("thinking...", C::YELLOW); gfx.send(); }
      }
      if (!on) break;
      endCol = chApply(m);
      btnClear();
      continue;
    }
    if (btn(B_UP)    && ch->cr > 0) { ch->cr--; sfx(500, 8); }
    if (btn(B_DOWN)  && ch->cr < 7) { ch->cr++; sfx(500, 8); }
    if (btn(B_LEFT)  && ch->cc > 0) { ch->cc--; sfx(500, 8); }
    if (btn(B_RIGHT) && ch->cc < 7) { ch->cc++; sfx(500, 8); }
    if (btn(B_OK)) {
      int sq = chSqAt(ch->cr, ch->cc);
      int move = -1;
      if (ch->sel >= 0)
        for (int i = 0; i < ch->nMoves; i++)
          if (ch->moves[i].from == ch->sel && ch->moves[i].to == sq) { move = i; break; }
      if (move >= 0) {
        ChMove m = ch->moves[move];
        if (m.promo) m.promo = chPickPromo();
        endCol = chApply(m);
      } else if (chSign(ch->pos.b[sq]) == (ch->pos.white ? 1 : -1)) {
        ch->sel = ch->sel == sq ? (int8_t)-1 : (int8_t)sq;   // pick, or put back
        sfx(1000, 20);
      } else if (ch->sel >= 0) { ch->sel = -1; sfx(200, 40); }
    }
    bool check = chInCheck(ch->pos, ch->pos.white);
    chDraw(check ? "CHECK!" : NULL, C::RED);
    gfx.send();
  }
  if (wantRestart) chUnsave();                       // restart: a new game
  else if (!chEnd) chSave();                         // left: kept for the next time
  free(che);
  free(ch);
  che = NULL; ch = NULL;
}