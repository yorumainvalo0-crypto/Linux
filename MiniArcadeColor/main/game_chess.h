// MiniArcade Color: Chess for two players on one console. Part of
// console.cpp, which includes it.
/* All the rules: castling, en passant, promotion (the player picks the
   piece), check, checkmate, stalemate, the 50-move rule and too little
   material to mate. Arrows move the cursor, OK picks a piece - its
   moves are marked - and OK on a marked square moves it. OK on the
   picked piece again, or on another own piece, changes the choice.
   The board can turn round for black, so each player has the own side
   at the bottom.
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

// all legal moves of the side to move; returns the count
static int chMoves(const ChPos &p, ChMove *out) {
  int n = 0;
  bool w = p.white;
  int8_t s = w ? 1 : -1;
  auto add = [&](int from, int to, bool pawnLast) {
    for (int k = 0; k < (pawnLast ? 4 : 1); k++) {
      ChMove m = { (uint8_t)from, (uint8_t)to, (uint8_t)(pawnLast ? CH_Q - k : 0) };
      ChPos q = p;
      chPlay(q, m);
      if (!chInCheck(q, w)) out[n++] = m;
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
      if (kind == CH_K && !chAttacked(p, sq, !w)) {                 // castling, never out of check
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
  ChPos   pos;
  ChMove  moves[220];                               // legal moves right now
  int     nMoves;
  int8_t  sel;                                      // picked square, -1 = none
  uint8_t cr, cc;                                   // cursor on the board (as drawn)
  int8_t  lastFrom, lastTo;
  bool    turnBoard;                                // black plays from the bottom too
  int8_t  takenW[16], takenB[16];                   // taken pieces (white's, black's)
  uint8_t nTakenW, nTakenB;
  char    lastText[12];
};
static ChGame *ch = NULL;                           // the running game (heap)

static bool chFlipped() { return ch->turnBoard && !ch->pos.white; }
static int  chSqAt(int r, int c) { return chFlipped() ? (7 - r) * 8 + (7 - c) : r * 8 + c; }
static void chXY(int sq, int16_t &x, int16_t &y) {
  int r = sq / 8, c = sq % 8;
  if (chFlipped()) { r = 7 - r; c = 7 - c; }
  x = (int16_t)(CH_X0 + c * CH_SQ); y = (int16_t)(CH_Y0 + r * CH_SQ);
}

static void chDraw(const char *status, col statusCol) {
  gfx.gradient(0, 0, SCR_W, TOP_H, colLight(C::BROWN), colDark(C::BROWN, 2));   // a bar without score
  gfx.textShadow(6, 5, "CHESS", C::WHITE, C::BLACK, F_TEXT);
  gfx.right(SCR_W - 34, 9, "2 players", C::LTGREY, F_SMALL);
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
  int16_t cx = (int16_t)(CH_X0 + ch->cc * CH_SQ), cy = (int16_t)(CH_Y0 + ch->cr * CH_SQ);
  gfx.frame(cx, cy, CH_SQ, CH_SQ, C::YELLOW, 3);
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
  gfx.text(px + 8, CH_Y0 + 38, "to move", ch->pos.white ? C::DKGREY : C::LTGREY);
  if (status) {                                    // one or two lines (split at the space)
    char a[16];
    strncpy(a, status, sizeof(a) - 1); a[sizeof(a) - 1] = 0;
    char *sp = strchr(a, ' ');
    if (sp) *sp = 0;
    gfx.text(px + 4, CH_Y0 + 72, a, statusCol, F_TEXT);
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

static void chNew(bool turnBoard) {
  memset(ch, 0, sizeof(*ch));
  chStart(ch->pos);
  ch->turnBoard = turnBoard;
  ch->sel = ch->lastFrom = ch->lastTo = -1;
  ch->cr = 6; ch->cc = 4;                           // on the king's pawn
  ch->nMoves = chMoves(ch->pos, ch->moves);
}

void chessRun() {
  static const char *const OPT[2] = { "board turns round", "board stays" };
  uint8_t mode = chooseMode("CHESS", OPT, 2, C::BROWN);
  if (mode == 255) return;
  ch = (ChGame *)malloc(sizeof(ChGame));
  if (!ch) return;
  chNew(mode == 0);
  const char *end = NULL;
  col endCol = C::WHITE;
  btnClear();
  while (poll()) {
    if (end) {                                       // finished: OK starts a new game
      chDraw(end, endCol);
      gfx.fill(CH_X0 + CH_SQ * 8 + 6, SCR_H - 14, 90, 14, rgb(20, 10, 0));
      gfx.text(CH_X0 + CH_SQ * 8 + 8, SCR_H - 12, "OK new game", C::YELLOW, F_SMALL);
      gfx.send();
      if (btn(B_OK)) { chNew(ch->turnBoard); end = NULL; btnClear(); }
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
        int8_t taken = ch->pos.b[m.to];
        if (abs(ch->pos.b[m.from]) == CH_P && m.to == ch->pos.ep) taken = (int8_t)(ch->pos.white ? -CH_P : CH_P);
        if (taken > 0 && ch->nTakenW < 16) ch->takenW[ch->nTakenW++] = taken;
        if (taken < 0 && ch->nTakenB < 16) ch->takenB[ch->nTakenB++] = taken;
        snprintf(ch->lastText, sizeof(ch->lastText), "%c%c-%c%c", 'a' + m.from % 8, '8' - m.from / 8, 'a' + m.to % 8, '8' - m.to / 8);
        bool turnedBefore = chFlipped();
        chPlay(ch->pos, m);
        ch->lastFrom = (int8_t)m.from; ch->lastTo = (int8_t)m.to;
        ch->sel = -1;
        if (chFlipped() != turnedBefore) { ch->cr = (uint8_t)(7 - ch->cr); ch->cc = (uint8_t)(7 - ch->cc); }   // cursor stays on its square
        ch->nMoves = chMoves(ch->pos, ch->moves);
        bool check = chInCheck(ch->pos, ch->pos.white);
        sfx(taken ? 500 : 900, taken ? 80 : 40);
        if (!ch->nMoves) {
          end = check ? (ch->pos.white ? "BLACK WINS" : "WHITE WINS") : "STALEMATE";
          endCol = check ? C::YELLOW : C::CYAN;
          sfx(check ? 1500 : 600, 400);
        } else if (ch->pos.half >= 100) { end = "DRAW (50)"; endCol = C::CYAN; }
        else if (chTooLittle(ch->pos))  { end = "DRAW"; endCol = C::CYAN; }
        else if (check) sfx(1300, 120);
      } else if (chSign(ch->pos.b[sq]) == (ch->pos.white ? 1 : -1)) {
        ch->sel = ch->sel == sq ? (int8_t)-1 : (int8_t)sq;   // pick, or put back
        sfx(1000, 20);
      } else if (ch->sel >= 0) { ch->sel = -1; sfx(200, 40); }
    }
    bool check = chInCheck(ch->pos, ch->pos.white);
    chDraw(check ? "CHECK!" : NULL, C::RED);
    gfx.send();
  }
  free(ch);
  ch = NULL;
}
