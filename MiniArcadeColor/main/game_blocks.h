// MiniArcade Color: Block Puzzle. Part of console.cpp, which includes it.
// An 8 x 8 board and a tray of three pieces on the right. Pick a piece
// (arrows, OK), move it over the board - a ghost shows where it lands,
// red when it does not fit - and OK puts it down. Full rows and columns
// clear: several at once are a combo, clears move after move a streak.
// Over when no piece left in the tray fits anywhere. RIGHT past the
// board's edge takes the piece back to the tray.
// Modes: Classic (endless, the high score), Adventure (hand-made levels
// with gems, stones and ice), Time (2 minutes, clears add seconds) and
// numbered puzzles (a set board and set pieces: clear the board).
// The board is a 64 bit mask (bit row * 8 + column): a fit is one AND.

#define BK_CELL  26                    // cell pitch on the board
#define BK_X      6                    // board, top left
#define BK_Y     30
#define BK_PX   220                    // the panel on the right
#define BK_PW    96
#define BK_SEQ   16                    // pieces of a puzzle at most
#define BK_TIME  120000                // ms of the time mode
#define BK_ANIM  340                   // ms a cleared line glows

enum { BK_PLAIN, BK_GEM, BK_STONE, BK_ICE, BK_CRACK };   // cell kinds
enum { BKM_CLASSIC, BKM_ADV, BKM_TIME, BKM_DAILY };
enum { BKG_GEMS, BKG_BLOCKS, BKG_SCORE };

// the pieces: size, colour, how often they come, rows (bit 0 = left)
struct BkShape { uint8_t w, h, c, wt; uint8_t r[5]; };
static const BkShape BK_SHAPES[] = {
  {1,1,6,2,{1}},                                                               // single
  {2,1,4,3,{3}}, {1,2,4,3,{1,1}},                                              // lines 2..5
  {3,1,1,3,{7}}, {1,3,1,3,{1,1,1}},
  {4,1,4,2,{15}}, {1,4,4,2,{1,1,1,1}},
  {5,1,0,1,{31}}, {1,5,0,1,{1,1,1,1,1}},
  {2,2,2,3,{3,3}}, {3,3,0,1,{7,7,7}},                                          // squares
  {3,2,5,2,{7,7}}, {2,3,5,2,{3,3,3}},                                          // rectangles
  {2,2,3,3,{3,1}}, {2,2,3,3,{3,2}}, {2,2,3,3,{1,3}}, {2,2,3,3,{2,3}},          // small corners
  {2,3,1,2,{1,1,3}}, {2,3,1,2,{2,2,3}}, {2,3,1,2,{3,1,1}}, {2,3,1,2,{3,2,2}},  // L and J
  {3,2,5,2,{7,1}}, {3,2,5,2,{7,4}}, {3,2,5,2,{1,7}}, {3,2,5,2,{4,7}},
  {3,2,6,2,{7,2}}, {3,2,6,2,{2,7}}, {2,3,6,2,{1,3,1}}, {2,3,6,2,{2,3,2}},      // T
  {3,2,7,2,{6,3}}, {3,2,7,2,{3,6}}, {2,3,7,2,{1,3,2}}, {2,3,7,2,{2,3,1}},      // S and Z
  {3,3,2,1,{7,1,1}}, {3,3,2,1,{7,4,4}}, {3,3,2,1,{1,1,7}}, {3,3,2,1,{4,4,7}},  // big corners
};
#define BK_NSHAPE ((uint8_t)(sizeof(BK_SHAPES) / sizeof(BK_SHAPES[0])))

// block colours: face, lit edge, shaded edge
static const col BK_PAL[8][3] = {
  { rgb(255, 51, 51),  rgb(255, 153, 153), rgb(153, 0, 0) },
  { rgb(255, 153, 0),  rgb(255, 212, 102), rgb(153, 85, 0) },
  { rgb(255, 212, 0),  rgb(255, 255, 153), rgb(153, 127, 0) },
  { rgb(51, 204, 51),  rgb(153, 255, 153), rgb(0, 102, 0) },
  { rgb(0, 212, 255),  rgb(153, 255, 255), rgb(0, 85, 153) },
  { rgb(51, 102, 255), rgb(153, 170, 255), rgb(0, 42, 153) },
  { rgb(153, 51, 255), rgb(204, 153, 255), rgb(102, 0, 153) },
  { rgb(255, 85, 170), rgb(255, 170, 212), rgb(153, 0, 102) } };

/* Adventure: 8 rows of the board in one string - '.' empty, 'x' a block,
   'g' a gem (collected when its row or column clears), 's' a stone, 'i'
   ice (cracks at the first clear, goes at the second). Goal: every gem,
   every stone and ice block, or a score - within the moves.           */
struct BkLevel { uint8_t goal, moves; uint16_t target; const char *map; };
static const BkLevel BK_LEVELS[] = {
  { BKG_GEMS, 12, 0,     "........" "........" "........" "........" "........" "........" "xgxx.xxx" "xxx.xgxx" },
  { BKG_GEMS, 14, 0,     "...x...." "...x...." "...g...." "........" "...x...." "...g...." "...x...." "...x...." },
  { BKG_BLOCKS, 14, 0,   "........" "........" "..s..s.." "........" "........" "..s..s.." "........" "........" },
  { BKG_SCORE, 14, 150,  "........" "........" "........" "........" "xxxx.xxx" "xxx.xxxx" "xx.xxxxx" "x.xxxxxx" },
  { BKG_BLOCKS, 16, 0,   "........" "........" "........" "iiii...." "........" "........" "........" "........" },
  { BKG_GEMS, 18, 0,     "g......g" "x......x" "........" "........" "........" "........" "x......x" "g......g" },
  { BKG_SCORE, 18, 180,  "xx....xx" "xx....xx" "........" "........" "........" "........" "xx....xx" "xx....xx" },
  { BKG_GEMS, 20, 0,     "........" "........" "xxgxxgx." "........" "........" ".xgxxgxx" "........" "........" },
  { BKG_BLOCKS, 26, 0,   "s......s" "........" "..s..s.." "...i...." "....i..." "..s..s.." "........" "s......s" },
  { BKG_SCORE, 22, 200,  "........" "........" "........" "...xx..." "...xx..." "........" "........" "........" },
  { BKG_GEMS, 22, 0,     "x.x.x.x." "........" "..g..g.." "........" "........" "..g..g.." "........" ".x.x.x.x" },
  { BKG_BLOCKS, 26, 0,   "........" "..iiii.." "........" "........" "........" "........" "..iiii.." "........" },
  { BKG_GEMS, 24, 0,     "s..g...s" "........" "...ss..." "g.s..s.g" "........" "...ss..." "........" "s..g...s" },
  { BKG_SCORE, 26, 240,  "x......x" ".x....x." "..x..x.." "........" "........" "..x..x.." ".x....x." "x......x" },
  { BKG_BLOCKS, 28, 0,   "...ss..." "...ss..." "........" "ss.ii.ss" "ss.ii.ss" "........" "...ss..." "...ss..." },
  { BKG_GEMS, 30, 0,     "gssssssg" "s......s" "s......s" "s......s" "s......s" "s......s" "s......s" "gssssssg" },
};
#define BK_NLEVEL ((uint8_t)(sizeof(BK_LEVELS) / sizeof(BK_LEVELS[0])))

struct BkState {
  uint64_t b;                          // occupied cells
  uint8_t  tint[64], kind[64];         // colour (0..7) and kind of each cell
  uint8_t  tray[3];                    // shapes waiting, 255 = used
  uint8_t  seq[BK_SEQ], pos[BK_SEQ];   // puzzle: its pieces in order, and where each came from
  uint8_t  seqN, seqAt;
  uint8_t  mode, level, sel, st;       // st: 0 = choosing in the tray, 1 = moving the piece
  int8_t   cr, cc;                     // the moving piece, top left
  uint8_t  streak, lines, moves;       // lines: cleared by the last piece
  uint8_t  gems, blocks;               // gems left; stones and ice left
  uint32_t score, cleared;
  uint16_t puzzle;
  uint32_t tEnd;                       // time mode: when it is up
  uint64_t fxMask;                     // the lines that cleared last ...
  uint32_t fxAt;                       // ... when
  col      fxCol[64];                  // ... and the colours they had
  uint32_t msgAt;
  char     msg[12];                    // "COMBO x2" over the board
};
static BkState *bk = NULL;

static uint64_t bkMask(uint8_t s) {
  uint64_t m = 0;
  for (uint8_t r = 0; r < BK_SHAPES[s].h; r++) m |= (uint64_t)BK_SHAPES[s].r[r] << (r * 8);
  return m;
}
static uint8_t bkCells(uint8_t s) { return (uint8_t)__builtin_popcountll(bkMask(s)); }
static bool bkFits(uint64_t b, uint8_t s, int8_t r, int8_t c) {
  const BkShape &p = BK_SHAPES[s];
  if (r < 0 || c < 0 || r + p.h > 8 || c + p.w > 8) return false;
  return !(b & bkMask(s) << (r * 8 + c));
}
static bool bkFitsAny(uint64_t b, uint8_t s) {
  for (int8_t r = 0; r < 8; r++) for (int8_t c = 0; c < 8; c++) if (bkFits(b, s, r, c)) return true;
  return false;
}
// no piece in the tray fits anywhere (an empty tray counts as stuck too)
static bool bkStuck() {
  for (uint8_t i = 0; i < 3; i++) if (bk->tray[i] != 255 && bkFitsAny(bk->b, bk->tray[i])) return false;
  return true;
}
static bool bkTrayEmpty() { return bk->tray[0] == 255 && bk->tray[1] == 255 && bk->tray[2] == 255; }

// the colours of a cell: face, lit, shaded
static void bkCellCols(uint8_t kind, uint8_t ci, col &f, col &l, col &d) {
  switch (kind) {
  case BK_GEM:   f = rgb(51, 42, 102);   l = rgb(102, 85, 153);  d = rgb(0, 0, 51);    return;
  case BK_STONE: f = GREY3;              l = GREY4;              d = GREY1;            return;
  case BK_ICE:
  case BK_CRACK: f = rgb(153, 212, 255); l = C::WHITE;           d = rgb(51, 127, 204); return;
  default: f = BK_PAL[ci][0]; l = BK_PAL[ci][1]; d = BK_PAL[ci][2];
  }
}

/* Puts shape s at (r, c) - it must fit - and clears every full row and
   column; returns the points. Ice in a cleared line cracks and stays. */
static uint32_t bkPlace(uint8_t s, int8_t r, int8_t c) {
  BkState &k = *bk;
  uint64_t m = bkMask(s) << (r * 8 + c);
  k.b |= m;
  for (uint8_t i = 0; i < 64; i++) if (m >> i & 1) { k.tint[i] = BK_SHAPES[s].c; k.kind[i] = BK_PLAIN; }
  k.moves++;
  uint32_t pts = bkCells(s);
  uint64_t full = 0;
  uint8_t n = 0;
  for (uint8_t j = 0; j < 8; j++) {
    uint64_t row = 0xFFull << (j * 8), cl = 0x0101010101010101ull << j;
    if ((k.b & row) == row) { full |= row; n++; }
    if ((k.b & cl) == cl) { full |= cl; n++; }
  }
  k.lines = n;
  if (!n) { k.streak = 0; k.score += pts; return pts; }
  k.streak++;
  pts += (uint32_t)10 * n * n * (k.streak < 5 ? k.streak : 5);      // combo, then the streak
  k.fxMask = full;
  for (uint8_t i = 0; i < 64; i++) {
    if (!(full >> i & 1)) continue;
    col f, l, d;
    bkCellCols(k.kind[i], k.tint[i], f, l, d);
    k.fxCol[i] = f;
    if (k.kind[i] == BK_ICE) { k.kind[i] = BK_CRACK; continue; }
    if (k.kind[i] == BK_GEM && k.gems) k.gems--;
    if ((k.kind[i] == BK_STONE || k.kind[i] == BK_CRACK) && k.blocks) k.blocks--;
    k.kind[i] = BK_PLAIN;
    k.b &= ~(1ull << i);
  }
  k.cleared += n;
  k.score += pts;
  return pts;
}

static uint8_t bkPick() {                       // a random shape, by weight
  uint16_t sum = 0;
  for (uint8_t s = 0; s < BK_NSHAPE; s++) sum += BK_SHAPES[s].wt;
  uint16_t v = (uint16_t)random(sum);
  for (uint8_t s = 0; s < BK_NSHAPE; s++) { if (v < BK_SHAPES[s].wt) return s; v -= BK_SHAPES[s].wt; }
  return 0;
}

/* Three new pieces. A puzzle takes the next ones of its list; else each
   piece is drawn again (a few times) until it fits somewhere on the board
   as it is now - and a single block when nothing else does.          */
static void bkDeal() {
  BkState &k = *bk;
  for (uint8_t i = 0; i < 3; i++) {
    if (k.mode == BKM_DAILY) { k.tray[i] = k.seqAt < k.seqN ? k.seq[k.seqAt++] : 255; continue; }
    uint8_t s = bkPick();
    for (uint8_t t = 0; t < 16 && !bkFitsAny(k.b, s); t++) s = bkPick();
    k.tray[i] = bkFitsAny(k.b, s) ? s : 0;
  }
}

// the board of a level
static void bkLoadLevel(uint8_t lv) {
  BkState &k = *bk;
  const char *m = BK_LEVELS[lv].map;
  k.b = 0; k.gems = k.blocks = 0;
  for (uint8_t i = 0; i < 64; i++) {
    char ch = m[i];
    k.kind[i] = ch == 'g' ? BK_GEM : ch == 's' ? BK_STONE : ch == 'i' ? BK_ICE : BK_PLAIN;
    k.tint[i] = (uint8_t)((i * 5 + i / 8) % 8);
    if (ch != '.') k.b |= 1ull << i;
    if (ch == 'g') k.gems++;
    if (ch == 's' || ch == 'i') k.blocks++;
  }
}

/* Puzzle n, the same on every console: two to four whole rows (or
   columns) are filled, then pieces are cut out of them at random. Those
   pieces, shuffled, are the ones to play - put back they clear every
   line, whatever the order (rows never share a cell).               */
static uint32_t bkRng = 1;
static uint32_t bkRand(uint32_t n) {
  bkRng ^= bkRng << 13; bkRng ^= bkRng >> 17; bkRng ^= bkRng << 5;
  return n ? bkRng % n : 0;
}
static void bkMakePuzzle(uint16_t n) {
  BkState &k = *bk;
  bkRng = 0x9E3779B9u ^ ((uint32_t)n * 2654435761u);
  if (!bkRng) bkRng = 1;
  for (uint8_t i = 0; i < 8; i++) bkRand(1);
  bool cols = bkRand(2);
  uint8_t lines = (uint8_t)(2 + bkRand(3)), used = 0;
  uint64_t U = 0;
  while (__builtin_popcount(used) < lines) {
    uint8_t j = (uint8_t)bkRand(8);
    if (used >> j & 1) continue;
    used |= (uint8_t)(1 << j);
    U |= cols ? 0x0101010101010101ull << j : 0xFFull << (j * 8);
  }
  uint8_t want = (uint8_t)(__builtin_popcountll(U) * (45 + bkRand(25)) / 100), cut = 0;
  uint64_t left = U;
  k.seqN = 0;
  for (uint16_t t = 0; t < 800 && cut < want && k.seqN < BK_SEQ; t++) {
    uint8_t s = (uint8_t)bkRand(BK_NSHAPE);
    if (s == 0 && bkRand(3)) continue;                       // few single blocks
    const BkShape &p = BK_SHAPES[s];
    uint8_t r = (uint8_t)bkRand((uint32_t)(9 - p.h)), c = (uint8_t)bkRand((uint32_t)(9 - p.w));
    uint64_t m = bkMask(s) << (r * 8 + c);
    if ((m & left) != m) continue;
    left &= ~m;
    k.seq[k.seqN] = s; k.pos[k.seqN++] = (uint8_t)(r * 8 + c);
    cut = (uint8_t)(cut + bkCells(s));
  }
  for (uint8_t i = k.seqN; i > 1; i--) {                     // shuffle, the places along
    uint8_t j = (uint8_t)bkRand(i), a = k.seq[i - 1], b = k.pos[i - 1];
    k.seq[i - 1] = k.seq[j]; k.pos[i - 1] = k.pos[j]; k.seq[j] = a; k.pos[j] = b;
  }
  k.b = left;
  for (uint8_t i = 0; i < 64; i++) { k.kind[i] = BK_PLAIN; k.tint[i] = (uint8_t)bkRand(8); }
  k.seqAt = 0;
}

// a round from the start: board, goal, pieces
static void bkStart(uint8_t mode, uint8_t lv, uint16_t pz) {
  BkState &k = *bk;
  memset(&k, 0, sizeof(k));
  k.mode = mode; k.level = lv; k.puzzle = pz;
  if (mode == BKM_ADV) bkLoadLevel(lv);
  if (mode == BKM_DAILY) bkMakePuzzle(pz);
  k.tEnd = gameMillis() + BK_TIME;
  bkDeal();
}

// adventure / puzzle: the goal is reached
static bool bkWon() {
  BkState &k = *bk;
  if (k.mode == BKM_DAILY) return !k.b;
  if (k.mode != BKM_ADV) return false;
  const BkLevel &L = BK_LEVELS[k.level];
  return L.goal == BKG_GEMS ? !k.gems : L.goal == BKG_BLOCKS ? !k.blocks : k.score >= L.target;
}

// ---------------- drawing ----------------
static void bkBlock(int16_t x, int16_t y, int16_t s, col f, col l, col d) {
  gfx.fill(x, y, s, s, f);
  uint8_t e = s >= 16 ? 2 : 1;                                // the bevel
  for (uint8_t i = 0; i < e; i++) {
    gfx.hline(x + i, y + i, s - 2 * i, l);  gfx.vline(x + i, y + i, s - 2 * i, l);
    gfx.hline(x + i, y + s - 1 - i, s - 2 * i, d); gfx.vline(x + s - 1 - i, y + i, s - 2 * i, d);
  }
  if (s >= 16) { gfx.fill(x + 4, y + 4, 4, 2, C::WHITE); gfx.pixel(x + 4, y + 6, C::WHITE); }
}

// a cell of the board with its kind
static void bkDrawCell(uint8_t kind, uint8_t ci, int16_t x, int16_t y, int16_t s) {
  col f, l, d;
  bkCellCols(kind, ci, f, l, d);
  bkBlock(x, y, s, f, l, d);
  int16_t cx = x + s / 2, cy = y + s / 2;
  switch (kind) {
  case BK_GEM: {                                              // a cut stone
    int16_t h = s / 3;
    for (int16_t dy = -h; dy <= h; dy++) {
      int16_t w = h - (dy < 0 ? -dy : dy);
      gfx.hline(cx - w, cy + dy, 2 * w + 1, dy < 0 ? rgb(153, 255, 255) : rgb(0, 170, 212));
    }
    gfx.hline(cx - h + 2, cy, 2 * h - 3, rgb(0, 212, 255));
    gfx.pixel(cx - 1, cy - h + 2, C::WHITE); gfx.pixel(cx, cy - h + 3, C::WHITE);
    break;
  }
  case BK_STONE:                                              // speckles and a crack
    gfx.pixel(x + 6, y + s - 8, GREY2); gfx.pixel(x + s - 7, y + 7, GREY2); gfx.pixel(x + s / 2, y + s - 6, GREY2);
    gfx.line(x + s - 6, y + s - 5, x + s / 2 + 1, y + s / 2, GREY2);
    break;
  case BK_CRACK:
    gfx.line(x + 3, y + s / 2, cx, cy - 2, rgb(0, 85, 153)); gfx.line(cx, cy - 2, x + s - 4, y + s - 5, rgb(0, 85, 153));
    gfx.line(cx, cy - 2, cx + 2, y + 3, rgb(0, 85, 153));
    [[fallthrough]];                                          // the shine stays
  case BK_ICE:
    gfx.line(x + s - 8, y + 3, x + 3, y + s - 8, C::WHITE);
    gfx.line(x + s - 5, y + 6, x + 6, y + s - 5, rgb(204, 255, 255));
    break;
  }
}

static int16_t bkCx(uint8_t c) { return BK_X + c * BK_CELL + 1; }
static int16_t bkCy(uint8_t r) { return BK_Y + r * BK_CELL + 1; }

// a shape small in the tray, centred on (cx, cy); dim = cannot be placed
static void bkMini(uint8_t s, int16_t cx, int16_t cy, bool dim) {
  const BkShape &p = BK_SHAPES[s];
  const int16_t q = 9;
  int16_t x0 = cx - p.w * q / 2, y0 = cy - p.h * q / 2;
  for (uint8_t r = 0; r < p.h; r++)
    for (uint8_t c = 0; c < p.w; c++)
      if (p.r[r] >> c & 1) {
        if (dim) bkBlock(x0 + c * q, y0 + r * q, q - 1, GREY2, GREY3, GREY1);
        else bkBlock(x0 + c * q, y0 + r * q, q - 1, BK_PAL[p.c][0], BK_PAL[p.c][1], BK_PAL[p.c][2]);
      }
}

static void bkDraw(uint32_t now) {
  BkState &k = *bk;
  gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(51, 0, 102), C::BLACK);
  // the board
  gfx.rbox(BK_X - 4, BK_Y - 4, 8 * BK_CELL + 7, 8 * BK_CELL + 7, 6, rgb(102, 85, 204));
  gfx.fill(BK_X - 1, BK_Y - 1, 8 * BK_CELL + 1, 8 * BK_CELL + 1, rgb(0, 0, 51));
  for (uint8_t i = 0; i < 64; i++) {
    int16_t x = bkCx(i % 8), y = bkCy(i / 8);
    if (k.b >> i & 1) bkDrawCell(k.kind[i], k.tint[i], x, y, BK_CELL - 2);
    else { gfx.fill(x, y, BK_CELL - 2, BK_CELL - 2, rgb(0, 42, 102)); gfx.hline(x, y, BK_CELL - 2, rgb(51, 42, 153)); }
  }
  // the piece being moved, with the lines it would clear
  if (k.st == 1 && k.tray[k.sel] != 255) {
    uint8_t s = k.tray[k.sel];
    const BkShape &p = BK_SHAPES[s];
    bool fit = bkFits(k.b, s, k.cr, k.cc);
    uint64_t m = bkMask(s) << (k.cr * 8 + k.cc), after = k.b | m, full = 0;
    if (fit)
      for (uint8_t j = 0; j < 8; j++) {
        uint64_t row = 0xFFull << (j * 8), cl = 0x0101010101010101ull << j;
        if ((after & row) == row) full |= row;
        if ((after & cl) == cl) full |= cl;
      }
    for (uint8_t i = 0; i < 64; i++) {
      int16_t x = bkCx(i % 8), y = bkCy(i / 8);
      if (full >> i & 1 && !(m >> i & 1)) gfx.frame(x, y, BK_CELL - 2, BK_CELL - 2, C::WHITE, 2);
      if (!(m >> i & 1)) continue;
      if (fit) {
        bkBlock(x, y, BK_CELL - 2, BK_PAL[p.c][0], BK_PAL[p.c][1], BK_PAL[p.c][2]);
        fxDitherFill(x, y, BK_CELL - 2, BK_CELL - 2, rgb(0, 42, 102));    // see-through
        gfx.frame(x, y, BK_CELL - 2, BK_CELL - 2, full >> i & 1 ? C::WHITE : BK_PAL[p.c][1], full >> i & 1 ? 2 : 1);
      } else {
        fxDitherFill(x, y, BK_CELL - 2, BK_CELL - 2, C::RED);
        gfx.frame(x, y, BK_CELL - 2, BK_CELL - 2, C::RED);
      }
    }
  }
  // the lines that just cleared: a white flash that shrinks into their colour
  uint32_t t = now - k.fxAt;
  if (k.fxMask && t < BK_ANIM) {
    int16_t sz = (int16_t)((BK_CELL - 2) * (BK_ANIM - t) / BK_ANIM);
    for (uint8_t i = 0; i < 64; i++) {
      if (!(k.fxMask >> i & 1) || sz < 2) continue;
      int16_t x = bkCx(i % 8) + (BK_CELL - 2 - sz) / 2, y = bkCy(i / 8) + (BK_CELL - 2 - sz) / 2;
      col c = t < BK_ANIM / 3 ? C::WHITE : k.fxCol[i];
      gfx.rbox(x, y, sz, sz, sz / 4, c);
      if (t < BK_ANIM / 3) gfx.rframe(x - 1, y - 1, sz + 2, sz + 2, sz / 4, C::YELLOW);
    }
  }
  // the panel: what this mode counts
  char b[24];
  gfx.rbox(BK_PX, BK_Y - 4, BK_PW, 52, 6, rgb(42, 0, 85));
  gfx.rframe(BK_PX, BK_Y - 4, BK_PW, 52, 6, rgb(102, 85, 204));
  int16_t mx = BK_PX + BK_PW / 2;
  if (k.mode == BKM_TIME) {
    int32_t left = (int32_t)(k.tEnd - now);
    if (left < 0) left = 0;
    snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(left / 60000), (unsigned long)(left / 1000 % 60));
    gfx.text(mx - Gfx::width(b, F_BOLD) / 2, BK_Y - 1, b, left < 10000 ? C::RED : C::WHITE, F_BOLD);
    int16_t w = (int16_t)((BK_PW - 12) * (left > BK_TIME ? BK_TIME : left) / BK_TIME);
    gfx.fill(BK_PX + 6, BK_Y + 32, BK_PW - 12, 6, rgb(0, 0, 51));
    gfx.fill(BK_PX + 6, BK_Y + 32, w, 6, left < 10000 ? C::RED : C::LIME);
  } else if (k.mode == BKM_ADV) {
    const BkLevel &L = BK_LEVELS[k.level];
    if (L.goal == BKG_SCORE) snprintf(b, sizeof(b), "%lu/%u", (unsigned long)k.score, L.target);
    else snprintf(b, sizeof(b), "%u", L.goal == BKG_GEMS ? k.gems : k.blocks);
    if (L.goal != BKG_SCORE) {                                 // the kind still to go, and how many
      bkDrawCell(L.goal == BKG_GEMS ? BK_GEM : BK_STONE, 0, BK_PX + 10, BK_Y + 1, 18);
      gfx.text(BK_PX + 34, BK_Y + 1, b, C::WHITE, F_TEXT);
    } else gfx.text(mx - Gfx::width(b, F_SMALL) / 2, BK_Y + 4, b, C::YELLOW, F_SMALL);
    snprintf(b, sizeof(b), "moves %u", L.moves > k.moves ? L.moves - k.moves : 0);
    gfx.text(mx - Gfx::width(b, F_SMALL) / 2, BK_Y + 28, b, L.moves - k.moves <= 3 ? C::ORANGE : C::LTGREY, F_SMALL);
  } else if (k.mode == BKM_DAILY) {
    gfx.text(mx - Gfx::width("CLEAR ALL", F_SMALL) / 2, BK_Y + 2, "CLEAR ALL", C::YELLOW, F_SMALL);
    uint8_t n = k.seqN - k.seqAt;
    for (uint8_t i = 0; i < 3; i++) n = (uint8_t)(n + (k.tray[i] != 255));
    snprintf(b, sizeof(b), "pieces %u", n);
    gfx.text(mx - Gfx::width(b, F_SMALL) / 2, BK_Y + 22, b, C::LTGREY, F_SMALL);
  } else {
    snprintf(b, sizeof(b), "lines %lu", (unsigned long)k.cleared);
    gfx.text(mx - Gfx::width(b, F_SMALL) / 2, BK_Y + 6, b, C::LTGREY, F_SMALL);
    snprintf(b, sizeof(b), "streak x%u", k.streak);
    gfx.text(mx - Gfx::width(b, F_SMALL) / 2, BK_Y + 24, b, k.streak > 1 ? C::YELLOW : C::GREY, F_SMALL);
  }
  // the tray
  for (uint8_t i = 0; i < 3; i++) {
    int16_t y = BK_Y + 54 + i * 52;
    bool on = i == k.sel;
    gfx.rbox(BK_PX, y, BK_PW, 48, 6, on ? rgb(85, 42, 153) : rgb(42, 0, 85));
    if (on) gfx.rframe(BK_PX, y, BK_PW, 48, 6, k.st ? C::GREY : C::WHITE);
    if (k.tray[i] == 255 || (on && k.st == 1)) continue;
    bkMini(k.tray[i], mx, y + 24, !bkFitsAny(k.b, k.tray[i]));
  }
  if (k.msg[0] && now - k.msgAt < 900) {                      // COMBO x2, rising a little
    int16_t y = (int16_t)(BK_Y + 70 - (now - k.msgAt) / 40);
    gfx.textShadow(BK_X + (8 * BK_CELL - Gfx::width(k.msg, F_BOLD)) / 2, y, k.msg, (now / 120) % 2 ? C::YELLOW : C::ORANGE, C::BLACK, F_BOLD);
  }
}

// ---------------- play ----------------
// the place nearest the middle where the piece fits (the middle if none)
static void bkStartPos(uint8_t s) {
  const BkShape &p = BK_SHAPES[s];
  int8_t r0 = (int8_t)((8 - p.h) / 2), c0 = (int8_t)((8 - p.w) / 2);
  bk->cr = r0; bk->cc = c0;
  int16_t best = 999;
  for (int8_t r = 0; r + p.h <= 8; r++)
    for (int8_t c = 0; c + p.w <= 8; c++) {
      int16_t d = (int16_t)(abs(r - r0) + abs(c - c0));
      if (d < best && bkFits(bk->b, s, r, c)) { best = d; bk->cr = r; bk->cc = c; }
    }
}

static void bkNextSel() {
  for (uint8_t i = 0; i < 3; i++) if (bk->tray[(bk->sel + i) % 3] != 255) { bk->sel = (uint8_t)((bk->sel + i) % 3); return; }
}

// the piece goes down: points, effects, new pieces
static void bkDrop(uint32_t now) {
  BkState &k = *bk;
  uint8_t s = k.tray[k.sel];
  uint8_t gems = k.gems, blocks = k.blocks;
  uint32_t pts = bkPlace(s, k.cr, k.cc);
  k.tray[k.sel] = 255;
  k.st = 0;
  int16_t px = bkCx(k.cc) + BK_SHAPES[s].w * BK_CELL / 2, py = bkCy(k.cr) + BK_SHAPES[s].h * BK_CELL / 2;
  if (k.lines) {
    k.fxAt = now;
    for (uint8_t i = 0; i < 64; i++)
      if (k.fxMask >> i & 1)
        fxBurst(bkCx(i % 8) + BK_CELL / 2, bkCy(i / 8) + BK_CELL / 2, (uint8_t)(k.lines > 1 ? 2 : 1), C::WHITE, k.fxCol[i], 110, 500, 18, 3);
    if (k.gems < gems) for (uint8_t g = 0; g < gems - k.gems; g++) fxBurst(px, py, 6, C::CYAN, C::BLUE, 160, 600, 0, 2, FX_DRAG | FX_STREAK);
    char t[8];
    snprintf(t, sizeof(t), "+%lu", (unsigned long)(pts > 99999 ? 99999 : pts));
    fxText(px, py - 10, t, C::YELLOW, F_BOLD);
    if (k.lines > 1) snprintf(k.msg, sizeof(k.msg), "COMBO x%u", k.lines);
    else if (k.streak > 1) snprintf(k.msg, sizeof(k.msg), "STREAK x%u", k.streak);
    else k.msg[0] = 0;
    k.msgAt = now;
    if (k.lines >= 3) { fxShake(4, 300); fxFlash(C::WHITE, 200, 120); }
    else if (k.lines == 2) fxShake(2, 180);
    if (k.mode == BKM_TIME) k.tEnd += 3000u * k.lines + (k.lines > 1 ? 2000u : 0);
    sfx((uint16_t)(700 + 200 * k.lines + 60 * k.streak), 90);
    if (k.blocks < blocks) sfx(1500, 60);
  } else sfx(320, 25);
  if (bkTrayEmpty()) bkDeal();
  bkNextSel();
}

static void bkInput(uint32_t now) {
  BkState &k = *bk;
  if (k.st == 0) {
    if (btn(B_LEFT) || btn(B_UP)) {
      for (uint8_t i = 1; i <= 3; i++) if (k.tray[(k.sel + 3 - i) % 3] != 255) { k.sel = (uint8_t)((k.sel + 3 - i) % 3); break; }
      sfx(700, 12);
    }
    if (btn(B_RIGHT) || btn(B_DOWN)) {
      for (uint8_t i = 1; i <= 3; i++) if (k.tray[(k.sel + i) % 3] != 255) { k.sel = (uint8_t)((k.sel + i) % 3); break; }
      sfx(700, 12);
    }
    if (btn(B_OK) && k.tray[k.sel] != 255) { k.st = 1; bkStartPos(k.tray[k.sel]); sfx(1000, 25); }
    return;
  }
  const BkShape &p = BK_SHAPES[k.tray[k.sel]];
  if (btn(B_UP) && k.cr > 0) k.cr--;
  if (btn(B_DOWN) && k.cr + p.h < 8) k.cr++;
  if (btn(B_LEFT) && k.cc > 0) k.cc--;
  if (btn(B_RIGHT)) { if (k.cc + p.w < 8) k.cc++; else { k.st = 0; sfx(500, 20); } }    // over the edge: back to the tray
  if (btn(B_OK)) {
    if (bkFits(k.b, k.tray[k.sel], k.cr, k.cc)) bkDrop(now);
    else sfx(150, 80);
  }
}

static uint16_t bkGet(const char *key, uint16_t d) { prefs.begin("arcade", true); uint16_t v = prefs.getUShort(key, d); prefs.end(); return v; }
static void bkPut(const char *key, uint16_t v) { prefs.begin("arcade", false); prefs.putUShort(key, v); prefs.end(); }

// a box over the last picture; OK = true, holding OK = false
static bool bkBox(const char *a, const char *b, const char *c, col accent) {
  NoPause np;
  gfx.dim(0, 0, SCR_W, SCR_H);
  btnClear();
  while (poll(false)) {
    if (btn(B_OK)) return true;
    panel(40, 50, 240, 140, NULL, accent);
    gfx.center(64, a, C::YELLOW, F_BOLD);
    gfx.center(104, b, C::WHITE, F_TEXT);
    gfx.center(136, c, C::LTGREY, F_SMALL);
    gfx.center(170, "OK go on - hold OK menu", C::GREY, F_SMALL);
    gfx.send();
  }
  return false;
}

static const char *const BK_TITLE[4] = { "BLOCKS", "ADVENTURE", "TIME", "PUZZLE" };

// one round; 0 = back to the library, 1 = this one again, 2 = on to the next level / puzzle
static uint8_t bkRound() {
  BkState &k = *bk;
  bkStart(k.mode, k.level, k.puzzle);
  uint16_t timeBest = k.mode == BKM_TIME ? bkGet("BLKt", 0) : 0;
  char right[28];
  fxReset();
  btnClear();
  const char *why = NULL;
  uint32_t endAt = 0;
  while (poll()) {
    uint32_t now = gameMillis();
    fxUpdate();
    if (!why) {
      bkInput(now);
      if (bkWon()) why = "won";
      else if (k.mode == BKM_TIME && (int32_t)(now - k.tEnd) >= 0) why = "TIME UP";
      else if (k.mode == BKM_ADV && k.moves >= BK_LEVELS[k.level].moves) why = "OUT OF MOVES";
      else if (k.mode == BKM_DAILY && bkTrayEmpty()) why = "NO PIECES LEFT";
      else if (bkStuck()) why = "NO SPACE";
      if (why) { endAt = now; k.st = 0; if (strcmp(why, "won")) sfx(200, 300); else sfx(1400, 300); }
    } else if (now - endAt > 1300) break;
    uint16_t sc = k.score > 65535 ? 65535 : (uint16_t)k.score;
    barRight = NULL;
    if (k.mode == BKM_ADV) { snprintf(right, sizeof(right), "LEVEL %u  %u", k.level + 1, sc); barRight = right; }
    if (k.mode == BKM_DAILY) { snprintf(right, sizeof(right), "#%u  %u", k.puzzle, sc); barRight = right; }
    bkDraw(now);
    fxDraw();
    fxShakeApply();
    fxFlashApply(BK_X - 4, BK_Y - 4, 8 * BK_CELL + 7, 8 * BK_CELL + 7);
    if (why && now - endAt > 400) {
      const char *w = strcmp(why, "won") ? why : (k.mode == BKM_DAILY ? "CLEARED!" : "GOAL!");
      int16_t ww = Gfx::width(w, F_BOLD) + 24, x = BK_X + (8 * BK_CELL - ww) / 2;
      gfx.rbox(x, 112, ww, 40, 8, strcmp(why, "won") ? C::DKRED : C::DKGRN);
      gfx.rframe(x, 112, ww, 40, 8, C::WHITE);
      gfx.text(x + 12, 120, w, C::WHITE, F_BOLD);
    }
    statusBar(BK_TITLE[k.mode], sc, k.mode == BKM_TIME ? timeBest : curHigh, rgb(102, 85, 204));
    gfx.send();
  }
  barRight = NULL;
  if (!why) return 0;
  uint16_t sc = k.score > 65535 ? 65535 : (uint16_t)k.score;
  char b1[28], b2[28];
  bool won = !strcmp(why, "won");
  switch (k.mode) {
  case BKM_CLASSIC: return gameOver(sc) ? 1 : 0;
  case BKM_TIME:
    if (sc > timeBest) { timeBest = sc; bkPut("BLKt", sc); }
    snprintf(b1, sizeof(b1), "%u points", sc);
    snprintf(b2, sizeof(b2), "best %u", timeBest);
    return bkBox(why, b1, b2, C::ORANGE) ? 1 : 0;
  case BKM_ADV:
    if (won) {
      if (k.level + 1 > bkGet("BLKlv", 0)) bkPut("BLKlv", (uint16_t)(k.level + 1));
      bool last = k.level + 1 >= BK_NLEVEL;
      snprintf(b1, sizeof(b1), "LEVEL %u DONE", k.level + 1);
      snprintf(b2, sizeof(b2), "%u points in %u moves", sc, k.moves);
      return bkBox(last ? "ALL LEVELS DONE!" : b1, b2, last ? "a master builder" : "OK = next level", C::GREEN) && !last ? 2 : 0;
    }
    snprintf(b1, sizeof(b1), "level %u", k.level + 1);
    return bkBox(why, b1, "OK = try again", C::RED) ? 1 : 0;
  default:
    if (won) {
      if (k.puzzle + 1 > bkGet("BLKdy", 1)) bkPut("BLKdy", (uint16_t)(k.puzzle + 1));
      snprintf(b1, sizeof(b1), "puzzle #%u solved", k.puzzle);
      return bkBox("CLEARED!", b1, "OK = next puzzle", C::GREEN) ? 2 : 0;
    }
    snprintf(b1, sizeof(b1), "puzzle #%u", k.puzzle);
    return bkBox(why, b1, "OK = try again", C::RED) ? 1 : 0;
  }
}

// adventure: where to begin (255 = back)
static uint8_t bkPickLevel() {
  uint8_t n = (uint8_t)bkGet("BLKlv", 0);
  if (n >= BK_NLEVEL) n = BK_NLEVEL - 1;
  if (!n) return 0;
  char txt[BK_NLEVEL][22];
  const char *opt[BK_NLEVEL];
  for (uint8_t i = 0; i <= n; i++) {                          // the level reached first, then 1, 2, ...
    if (!i) snprintf(txt[i], sizeof(txt[i]), "continue: level %u", n + 1);
    else snprintf(txt[i], sizeof(txt[i]), "level %u", i);
    opt[i] = txt[i];
  }
  uint8_t k = chooseMode("ADVENTURE", opt, (uint8_t)(n + 1), rgb(102, 85, 204));
  return k == 255 ? 255 : (uint8_t)(k ? k - 1 : n);
}

/* Puzzles: the console keeps no date, so they are numbered. The next
   one not solved yet is offered; LEFT / RIGHT (UP / DOWN by ten) pick
   any other. 0 = back.                                              */
static uint16_t bkPickPuzzle() {
  uint16_t next = bkGet("BLKdy", 1), n = next;
  NoPause np;
  btnClear();
  while (poll()) {
    if (btn(B_LEFT) && n > 1) { n--; sfx(700, 12); }
    if (btn(B_RIGHT) && n < 999) { n++; sfx(700, 12); }
    if (btn(B_DOWN)) { n = n > 10 ? n - 10 : 1; sfx(700, 12); }
    if (btn(B_UP)) { n = n + 10 <= 999 ? n + 10 : 999; sfx(700, 12); }
    if (btn(B_OK)) { sfx(1200, 50); return n; }
    gfx.gradient(0, 0, SCR_W, SCR_H, rgb(42, 0, 85), C::BLACK);
    gfx.center(14, "PUZZLES", C::WHITE, F_BOLD);
    gfx.hline(40, 44, SCR_W - 80, rgb(102, 85, 204));
    char b[24];
    snprintf(b, sizeof(b), "#%u", n);
    gfx.rbox(90, 70, 140, 64, 10, rgb(85, 42, 153));
    gfx.center(84, b, C::YELLOW, F_BIG);
    gfx.triangle(70, 102, 80, 92, 80, 112, C::WHITE);
    gfx.triangle(250, 102, 240, 92, 240, 112, C::WHITE);
    gfx.center(146, n < next ? "solved before" : n == next ? "the next one" : "ahead", n < next ? C::LIME : C::LTGREY);
    gfx.center(176, "use every piece and clear the board", C::GREY, F_SMALL);
    gfx.center(SCR_H - 30, "LEFT/RIGHT 1   UP/DOWN 10", C::GREY, F_SMALL);
    snprintf(b, sizeof(b), "%s = start   hold = back", okKeys());
    gfx.center(SCR_H - 16, b, C::GREY, F_SMALL);
    gfx.send();
  }
  return 0;
}

void blocksRun() {
  static const char *const MODES[4] = { "classic", "adventure", "time 2:00", "puzzles" };
  uint8_t mode = chooseMode("BLOCK PUZZLE", MODES, 4, rgb(102, 85, 204));
  if (mode == 255) return;
  uint8_t lv = 0;
  uint16_t pz = 1;
  if (mode == BKM_ADV && (lv = bkPickLevel()) == 255) return;
  if (mode == BKM_DAILY && !(pz = bkPickPuzzle())) return;
  bk = (BkState *)calloc(1, sizeof(BkState));
  if (!bk) return;
  for (;;) {
    bk->mode = mode; bk->level = lv; bk->puzzle = pz;
    uint8_t r = bkRound();
    if (!r) break;
    if (r == 2) { if (mode == BKM_ADV) lv++; else pz = pz < 999 ? pz + 1 : 1; }
  }
  free(bk);
  bk = NULL;
}

static const char *const IC_BLOCKS[12] = {
  "....oooo....", "....oooo....", "....oo......", "....oo......", "............", "............",
  "pp......cccc", "pp......cccc", "ppgggg..ccbb", "ppgggg..ccbb", "rrrryyyyggbb", "rrrryyyyggbb" };
