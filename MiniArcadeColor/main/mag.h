// MiniArcade Color: MAG script - games written as text on the SD card.
// Part of console.cpp, which includes it after sdlevels.h - not compiled
// on its own. What an author (or an AI) needs to know is in
// sdcard/MINIARC/games/LANGUAGE.md; this file is the machine behind it:
// - a lexer and a one-pass recursive descent compiler that read a .mag
//   file from the card line by line and write bytecode for a small stack
//   machine; 32-bit integers are the only values;
// - the data blocks (sprites, maps, levels) are not loaded: the compiler
//   keeps where their rows start in the file, the game reads them through
//   a small page cache when it needs them ("Weg 1"); a pulled card only
//   matters when a page is missing (game_sdgames.h waits for the card);
// - the VM, with an instruction budget per frame: an endless loop ends
//   the game with a message instead of hanging the console.
// Every byte of it is on the heap, only while an SD game runs; the one
// static is the pointer to it. game_sdgames.h has the launcher, the
// frame loop, the screens and the save file.
#include <setjmp.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>

#ifndef MAG_ROOM                         // n more bytes on the heap leave enough for the rest
#define MAG_ROOM(n) (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= (size_t)(n) + 16384)
#endif

// ---------------- limits (LANGUAGE.md lists them) ----------------
#define MAG_CODE     24576               // bytecode bytes
#define MAG_GLOBALS  2048                // ints of var and array (8 KB)
#define MAG_FUNCS    250
#define MAG_PARAMS   8
#define MAG_LOCALS   32                  // slots of a function: parameters, locals, 3 per for loop
#define MAG_DEPTH    64                  // calls deep
#define MAG_STACK    1024                // values on the stack
#define MAG_BLOCKS   600                 // sprites + maps + levels
#define MAG_TILESETS 16
#define MAG_SYMS     1200
#define MAG_NAMES    16384               // bytes of names (while compiling)
#define MAG_LINES    4000                // line table entries (statements)
#define MAG_FWDS     2000                // names used before their block
#define MAG_LINELEN  500                 // characters of a line
#define MAG_NAMELEN  31
#define MAG_TEXTLEN  60
#define MAG_SPRMAX   64                  // sprite width and height
#define MAG_MAPW     400
#define MAG_MAPH     200
#define MAG_LOADMAX  16384               // cells of the map loadmap() keeps in RAM
#define MAG_SLOTS    16                  // save() slots
#define MAG_NEST     16                  // blocks inside blocks
#define MAG_EDEPTH   40                  // expression nesting
#ifndef MAG_CSTACK
#define MAG_CSTACK   4096                // bytes of the task's stack (8 KB) the compiler may take for nesting
#endif
#define MAG_BUDGET   150000              // instructions per frame (update + draw)
#define MAG_BUDGET0  2000000             // for init()
#define MAG_PAGE     64                  // page cache: 64 pages of 64 bytes, 16 sets of 4
#define MAG_PAGES    64
#define MAG_WAYS     4
#define MAG_POOL     6144                // sprites decoded to colours
#define MAG_HEAD     512                 // the file's first bytes, hashed: the same file after a remount

// ---------------- the palette: letters of sprites ----------------
// '.' (and ' ') transparent; the letters of the menu icons where they exist
static const char MAG_PALKEYS[] = "kwroygdcbpmsnat";
static const col  MAG_PALCOL[]  = { C::BLACK, C::WHITE, C::RED, C::ORANGE, C::YELLOW, C::GREEN, C::DKGRN, C::CYAN,
                                    C::BLUE, C::PURPLE, C::PINK, C::GREY, C::BROWN, C::DKGREY, C::SAND };
#define MAG_CLEAR 0xFF                   // a transparent pixel of a decoded sprite (no palette colour has 255)
static uint8_t magPal(char ch) {         // 0xFE = no palette letter
  if (ch == '.' || ch == ' ') return MAG_CLEAR;
  const char *k = ch ? strchr(MAG_PALKEYS, ch) : NULL;
  return k ? MAG_PALCOL[k - MAG_PALKEYS] : 0xFE;
}

// ---------------- built-in names ----------------
struct MagConst { const char *name; int32_t v; };
static const MagConst MAG_CONSTS[] = {
  { "UP", B_UP }, { "DOWN", B_DOWN }, { "LEFT", B_LEFT }, { "RIGHT", B_RIGHT }, { "OK", B_OK },
  { "true", 1 }, { "false", 0 }, { "SCREEN_W", SCR_W }, { "SCREEN_H", SCR_H }, { "BAR_H", 26 },
  { "BLACK", C::BLACK }, { "WHITE", C::WHITE }, { "RED", C::RED }, { "GREEN", C::GREEN }, { "BLUE", C::BLUE },
  { "YELLOW", C::YELLOW }, { "CYAN", C::CYAN }, { "ORANGE", C::ORANGE }, { "PURPLE", C::PURPLE }, { "PINK", C::PINK },
  { "GREY", C::GREY }, { "BROWN", C::BROWN }, { "DKGREY", C::DKGREY }, { "LTGREY", C::LTGREY }, { "DKGREEN", C::DKGRN },
  { "DKRED", C::DKRED }, { "DKBLUE", C::DKBLUE }, { "NAVY", C::NAVY }, { "LIME", C::LIME }, { "SAND", C::SAND },
};

/* The built-in functions: name, arguments. Argument letters: n number,
   s a text in quotes, S sprite, M map, T tile set; after '|' optional. */
enum {
  MB_CLS, MB_RECT, MB_BOX, MB_LINE, MB_CIRCLE, MB_DISC, MB_PIXEL, MB_TRI, MB_TEXT, MB_NUM, MB_TEXTW, MB_NUMW,
  MB_SPR, MB_SPRW, MB_SPRH, MB_DRAWMAP, MB_LOADMAP, MB_MGET, MB_MSET, MB_MAPW, MB_MAPH, MB_LEVEL, MB_LEVELS,
  MB_BTN, MB_BTNP, MB_TILTX, MB_TILTY, MB_SFX, MB_RND, MB_TIME, MB_SCORE, MB_GAMEOVER, MB_SAVE, MB_LOAD,
  MB_ABS, MB_MIN, MB_MAX, MB_CLAMP, MB_SIN, MB_COS, MB_ATAN2, MB_SQRT, MB_COL, MB_COUNT
};
struct MagBuiltin { const char *name, *args; };
static const MagBuiltin MAG_BI[MB_COUNT] = {
  { "cls", "n" }, { "rect", "nnnnn" }, { "box", "nnnnn" }, { "line", "nnnnn" }, { "circle", "nnnn" }, { "disc", "nnnn" },
  { "pixel", "nnn" }, { "tri", "nnnnnnn" }, { "text", "nnsn|n" }, { "num", "nnnn|n" }, { "textw", "s|n" }, { "numw", "n|n" },
  { "spr", "Snn|nn" }, { "sprw", "S" }, { "sprh", "S" }, { "drawmap", "MTnn" }, { "loadmap", "M" }, { "mget", "Mnn" },
  { "mset", "Mnnn" }, { "mapw", "|M" }, { "maph", "|M" }, { "level", "n" }, { "levels", "" },
  { "btn", "n" }, { "btnp", "n" }, { "tiltx", "" }, { "tilty", "" }, { "sfx", "nn" }, { "rnd", "n" }, { "time", "" },
  { "score", "n" }, { "gameover", "n" }, { "save", "nn" }, { "load", "n" },
  { "abs", "n" }, { "min", "nn" }, { "max", "nn" }, { "clamp", "nnn" }, { "sin", "n" }, { "cos", "n" }, { "atan2", "nn" },
  { "sqrt", "n" }, { "col", "nnn" },
};
static const char *const MAG_KEYWORDS[] = {
  "game", "author", "info", "fps", "const", "var", "array", "local", "func", "end", "if", "then", "elif", "elseif", "else",
  "while", "do", "for", "break", "continue", "return", "and", "or", "not", "sprite", "map", "tiles",   // (level: a built-in too)
};

// ---------------- bytecode ----------------
enum : uint8_t {
  OP_PUSH0, OP_PUSH1, OP_PUSHB, OP_PUSHW, OP_PUSHL, OP_STR,
  OP_GETG, OP_SETG, OP_GETL, OP_SETL, OP_GETA, OP_SETA,
  OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG, OP_NOT,
  OP_BAND, OP_BOR, OP_BXOR, OP_SHL, OP_SHR,
  OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE, OP_BOOL,
  OP_JMP, OP_JZ, OP_ANDJ, OP_ORJ, OP_FORCHK, OP_FORINC,
  OP_CALL, OP_NAT, OP_RET, OP_RET0, OP_POP,
};

struct MagFn  { uint16_t addr, maxSt; uint8_t np, nl; };
struct MagBlk { uint32_t off; uint16_t w, h, stride; };          // stride 0: off = first entry in the row table
struct MagTiles { uint8_t size; int16_t e[95]; };                 // per character 32..126: -1 none, 0..255 colour, 256+ sprite
struct MagLine { uint16_t pc, line; };
struct MagFrame { uint16_t ret, bp; };

/* The running game. Everything a game needs once it is compiled: the
   code, the tables of its data blocks, the caches, the state of the
   frame. game_sdgames.h fills the header and the file part.          */
struct MagVm {
  uint8_t  *code;   uint16_t codeN;
  MagLine  *lines;  uint16_t nLines;
  MagFn    *fn;     uint8_t  nFn;
  uint8_t  fInit, fUpdate, fDraw, fGinit;           // 255 = none
  int32_t  *g;      uint16_t nG;
  MagBlk   *spr, *map; uint16_t nSpr, nMap;
  uint32_t *rows;   uint16_t nRows;                  // row starts of blocks whose rows are unevenly spaced
  uint16_t *lvl;    uint16_t nLvl;                   // the level blocks: their map numbers
  MagTiles *ts;     uint8_t  nTs;
  char     title[25], author[25], info[101];
  uint8_t  fps;
  // the file on the card
  char     path[80], save[80];
  int      fd;
  uint32_t size, head;                               // its size and the hash of its first bytes
  // the page cache and the decoded sprites
  uint8_t  *pg;     uint32_t tag[MAG_PAGES], use[MAG_PAGES], clock;
  uint8_t  *pool;   int16_t *sprAt; uint16_t poolN;
  // the map loadmap() took into RAM
  uint8_t  *mbuf;   uint32_t mcap; int16_t mid;      // -1 = none
  // running
  int32_t  *stack;  MagFrame fr[MAG_DEPTH];
  int32_t  budget, budgetMax;
  const char *where;                                 // "init", "update", "draw"
  char     err[100];  uint16_t errPc; bool failed;   // a runtime error
  bool     quit;                                     // the player left while the card was out
  bool     held[B_COUNT], press[B_COUNT];
  int32_t  score;   bool bar;
  bool     over;    int32_t overScore;
  int32_t  slots[MAG_SLOTS]; bool dirty; uint32_t savedAt; uint16_t best;
  uint32_t t0;
  // what it cost
  uint32_t misses, reads, readBytes, waits;
  uint32_t opsFrame, opsMax, frames; uint64_t opsSum;
  size_t   heap;                                     // bytes it took
};
static MagVm *mag = NULL;

static bool magCardWait();                           // game_sdgames.h: the card is out, wait for it

static void *magAlloc(size_t n, size_t *count) {
  if (!n) n = 1;
  if (!MAG_ROOM(n)) return NULL;
  void *p = calloc(1, n);
  if (p && count) *count += n;
  return p;
}

// =========================================================
//  THE COMPILER
// =========================================================
enum : uint8_t { MT_EOF, MT_NUM, MT_NAME, MT_STR, MT_OP };
enum : uint8_t { SY_CONST, SY_VAR, SY_ARR, SY_FUNC, SY_SPR, SY_MAP, SY_TILES, SY_FWD };
struct MagSym  { uint16_t name, next; uint8_t kind, argc; bool defined; uint16_t line; int32_t val; uint16_t size; };
struct MagFwd  { uint16_t at, sym, line; char want; bool tile; };
struct MagLoc  { char name[MAG_NAMELEN + 1]; uint8_t slot; };
struct MagLoop { uint16_t brk, cont; };                         // jumps to patch, chained through the code
struct MagInit { uint16_t g; int32_t v; };
struct MagE    { bool k; int32_t v; int16_t sym; };               // k: a constant, not emitted yet; sym: just this name

#define OPC(a, b) ((uint16_t)((uint8_t)(a) | (uint8_t)(b) << 8))

struct MagC {
  FILE    *f;
  char     ln[MAG_LINELEN + 4];
  uint16_t lnNo, pos;
  uint32_t lnOff, nextOff;
  bool     eof;
  // the current token
  uint8_t  tt;
  uint16_t op;
  int32_t  num;
  char     ts[MAG_LINELEN + 1];
  uint16_t tlen, tline, tcol, prevLine;
  uint32_t toff;
  bool     tchar;                                    // the number was a character in quotes
  // output
  uint8_t  *code;  uint16_t pc;
  int16_t  depth, maxDepth;
  MagLine  *lines; uint16_t nLines, capLines; uint16_t lastLine;
  MagFn    *fn;    uint8_t  nFn;
  uint16_t *fnLine;                                  // where each function was first used / defined
  uint16_t nG;
  MagInit  *inits; uint16_t nInits, capInits;
  MagBlk   *blk;   uint8_t *blkKind; uint16_t nBlk, capBlk;   // sprites and maps in file order
  uint16_t nSpr, nMap;
  uint32_t *rows;  uint16_t nRows, capRows;
  uint16_t *lvl;   uint16_t nLvl, capLvl;
  MagTiles *tset;  uint16_t tsLine[MAG_TILESETS]; uint8_t nTs;
  // names
  MagSym   *sym;   uint16_t nSym, capSym;
  char     *names; uint16_t nNames, capNames;
  uint16_t head[64];
  MagFwd   *fwd;   uint16_t nFwd, capFwd;
  // the function being compiled
  bool     inFn;
  MagLoc   loc[MAG_LOCALS]; uint8_t nLoc, nSlots, maxSlots, np;
  MagLoop  loops[MAG_NEST]; uint8_t nLoops;
  uint8_t  nest, edepth;
  char     nbuf[MAG_EDEPTH + 2][MAG_NAMELEN + 1];   // names being read, one per expression level
  bool     header;                                   // still in the header lines
  // the result
  MagVm   *vm;
  size_t   heap;
  jmp_buf  jb;
  char     msg[120];
  uint16_t errLine;
  uint32_t yieldAt;
  uintptr_t stackTop;                                // where the compiler's stack began
  char     tdesc[48];                                // a token as an error names it
};

static void magErr(MagC *c, uint16_t line, const char *fmt, ...) __attribute__((format(printf, 3, 4), noreturn));
static void magErr(MagC *c, uint16_t line, const char *fmt, ...) {
  va_list a;
  va_start(a, fmt);
  vsnprintf(c->msg, sizeof(c->msg), fmt, a);
  va_end(a);
  c->errLine = line;
  longjmp(c->jb, 1);
}
#define MERR(...) magErr(c, c->tline, __VA_ARGS__)

// grows a table by half (at least 16) up to max entries; false = the limit
static bool magGrow(MagC *c, void **p, uint16_t *cap, size_t elem, uint32_t need, uint32_t max) {
  if (need <= *cap) return true;
  if (need > max) return false;
  uint32_t n = *cap + *cap / 2 + 16;
  if (n > max) n = max;
  if (n < need) n = need;
  if (!MAG_ROOM(n * elem)) magErr(c, c->tline, "not enough memory to compile this game");
  void *q = realloc(*p, n * elem);
  if (!q) magErr(c, c->tline, "not enough memory to compile this game");
  c->heap += (n - *cap) * elem;
  *p = q; *cap = (uint16_t)n;
  return true;
}

// ---------------- the lexer ----------------
static bool magRead1(MagC *c) {                      // the next line into ln; false = end of the file
  if (c->eof) return false;
  if (millis() - c->yieldAt > 40) { delay(1); c->yieldAt = millis(); }   // a big file: the other tasks get their turn
  c->lnOff = c->nextOff;
  if (!fgets(c->ln, MAG_LINELEN + 2, c->f)) {
    if (ferror(c->f)) magErr(c, c->lnNo, "the card could not be read here (pulled out?) - start the game again");
    c->eof = true; c->ln[0] = 0;
    return false;
  }
  size_t n = strlen(c->ln);
  c->lnNo++;
  c->tline = c->lnNo;
  if (n == MAG_LINELEN + 1 && c->ln[n - 1] != '\n') magErr(c, c->lnNo, "line too long (at most %d characters)", MAG_LINELEN);
  long at = ftell(c->f);
  if (at >= 0 && (uint32_t)at != c->lnOff + n) magErr(c, c->lnNo, "this line contains a 0 byte - is it a text file?");
  c->nextOff = c->lnOff + (uint32_t)n;
  while (n && (c->ln[n - 1] == '\n' || c->ln[n - 1] == '\r')) c->ln[--n] = 0;
  c->pos = 0;
  if (c->lnNo == 1 && !strncmp(c->ln, "\xEF\xBB\xBF", 3)) c->pos = 3;        // a text editor's BOM
  return true;
}

static bool magNameCh(char ch, bool first) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' || (!first && ch >= '0' && ch <= '9');
}

static void magNext(MagC *c) {
  c->prevLine = c->tline;
  for (;;) {
    char ch = c->ln[c->pos];
    if (ch == ' ' || ch == '\t' || ch == '\r') { c->pos++; continue; }
    if (!ch || (ch == '-' && c->ln[c->pos + 1] == '-')) {
      if (!magRead1(c)) { c->tt = MT_EOF; c->tline = c->lnNo; c->ts[0] = 0; return; }
      continue;
    }
    break;
  }
  const char *s = c->ln + c->pos;
  c->tline = c->lnNo; c->tcol = c->pos; c->tchar = false;
  char ch = *s;
  if (magNameCh(ch, true)) {
    uint16_t n = 0;
    while (magNameCh(s[n], false)) n++;
    if (n > MAG_NAMELEN) MERR("name too long (at most %d characters)", MAG_NAMELEN);
    memcpy(c->ts, s, n); c->ts[n] = 0; c->tlen = n;
    c->pos += n; c->tt = MT_NAME;
    return;
  }
  if (ch >= '0' && ch <= '9') {
    uint32_t v = 0;
    uint16_t n = 0;
    int base = 10;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; n = 2; }
    else if (s[0] == '0' && (s[1] == 'b' || s[1] == 'B')) { base = 2; n = 2; }
    uint16_t d0 = n;
    for (;; n++) {
      char d = s[n];
      int k = d >= '0' && d <= '9' ? d - '0' : d >= 'a' && d <= 'f' ? d - 'a' + 10 : d >= 'A' && d <= 'F' ? d - 'A' + 10 : 99;
      if (k >= base) break;
      if (v > (0xFFFFFFFFu - (uint32_t)k) / (uint32_t)base) MERR("number too big");
      v = v * (uint32_t)base + (uint32_t)k;
    }
    if (n == d0) MERR("a number is missing its digits after 0x / 0b");
    if (s[n] == '.' && s[n + 1] >= '0' && s[n + 1] <= '9') MERR("only whole numbers: no fractions like 1.5 (use bigger units, e.g. x * 16)");
    if (magNameCh(s[n], false)) MERR("a number runs into letters: '%.*s'", (int)(n + 1), s);
    if (base == 10 && v > 2147483647u) MERR("number too big (at most 2147483647)");
    c->num = (int32_t)v; c->tt = MT_NUM; c->pos += n;
    return;
  }
  if (ch == '"') {
    uint16_t n = 1;
    while (s[n] && s[n] != '"') n++;
    if (!s[n]) MERR("text not closed: a \" is missing at the end of the line");
    c->tlen = (uint16_t)(n - 1);
    memcpy(c->ts, s + 1, c->tlen); c->ts[c->tlen] = 0;
    c->toff = c->lnOff + c->pos + 1;
    c->pos += n + 1; c->tt = MT_STR;
    return;
  }
  if (ch == '\'') {
    if (s[1] && s[1] != '\'' && s[2] == '\'' && (uint8_t)s[1] >= 32 && (uint8_t)s[1] < 127) {
      c->num = (uint8_t)s[1]; c->tt = MT_NUM; c->tchar = true; c->pos += 3;
      return;
    }
    MERR("'x' is one character in single quotes (e.g. '#'); text uses double quotes");
  }
  static const char *const TWO[] = { "==", "!=", "<=", ">=", "<<", ">>", "~=", "&&", "||", "+=", "-=", "*=", "/=", "++", "%=" };
  for (const char *t : TWO)
    if (s[0] == t[0] && s[1] == t[1]) { c->op = OPC(t[0], t[1]); c->tt = MT_OP; c->pos += 2; c->ts[0] = t[0]; c->ts[1] = t[1]; c->ts[2] = 0; return; }
  if (strchr("+-*/%()[],=<>&|^!.{}#:;~", ch)) { c->op = OPC(ch, 0); c->tt = MT_OP; c->pos++; c->ts[0] = ch; c->ts[1] = 0; return; }
  if ((uint8_t)ch >= 0x80) MERR("a letter outside ASCII here - names and code use plain English letters");
  MERR("unexpected character '%c'", ch);
}

static bool isOp(MagC *c, char a, char b = 0) { return c->tt == MT_OP && c->op == OPC(a, b); }
static bool isName(MagC *c, const char *n) { return c->tt == MT_NAME && !strcmp(c->ts, n); }
static const char *tokDesc(MagC *c) {               // the token as an error message names it
  char *b = c->tdesc;
  if (c->tt == MT_EOF) return "the end of the file";
  if (c->tt == MT_NUM) { snprintf(b, sizeof(c->tdesc), "the number %ld", (long)c->num); return b; }
  if (c->tt == MT_STR) return "a text in quotes";
  snprintf(b, sizeof(c->tdesc), "'%.40s'", c->ts);
  return b;
}
static void expectOp(MagC *c, char a, char b, const char *what) {
  if (!isOp(c, a, b)) MERR("expected %s, found %s", what, tokDesc(c));
  magNext(c);
}

// ---------------- names ----------------
static uint8_t magHashName(const char *s) { uint32_t h = 5381; while (*s) h = h * 33 + (uint8_t)*s++; return (uint8_t)(h & 63); }
static const char *symName(MagC *c, uint16_t i) { return c->names + c->sym[i].name; }
static int16_t findSym(MagC *c, const char *n) {
  for (uint16_t i = c->head[magHashName(n)]; i != 0xFFFF; i = c->sym[i].next) if (!strcmp(symName(c, i), n)) return (int16_t)i;
  return -1;
}
static int16_t findBuiltin(const char *n) { for (uint8_t i = 0; i < MB_COUNT; i++) if (!strcmp(MAG_BI[i].name, n)) return i; return -1; }
static const MagConst *findConst(const char *n) { for (const MagConst &k : MAG_CONSTS) if (!strcmp(k.name, n)) return &k; return NULL; }
static bool isKeyword(const char *n) { for (const char *k : MAG_KEYWORDS) if (!strcmp(k, n)) return true; return false; }
static int16_t findLocal(MagC *c, const char *n) {
  for (int16_t i = (int16_t)c->nLoc - 1; i >= 0; i--) if (!strcmp(c->loc[i].name, n)) return i;
  return -1;
}
static const char *kindName(uint8_t k) {
  static const char *const N[] = { "a const", "a variable", "an array", "a function", "a sprite", "a map", "a tile set", "a name" };
  return N[k];
}
// the current token's name, kept off the C stack (the compiler recurses; the main task's stack is small)
static char *keepName(MagC *c) {
  char *n = c->nbuf[c->edepth];
  strcpy(n, c->ts);
  return n;
}

// a new name may be declared here (line): not a keyword, not built in, not taken
static void checkNew(MagC *c, const char *n, bool local, uint16_t line) {
  if (isKeyword(n)) magErr(c, line, "'%s' is a keyword and cannot be used as a name", n);
  if (findBuiltin(n) >= 0) magErr(c, line, "'%s' is a built-in function and cannot be used as a name", n);
  if (findConst(n)) magErr(c, line, "'%s' is a built-in constant and cannot be used as a name", n);
  int16_t s = findSym(c, n);
  if (local) {
    if (s >= 0 && c->sym[s].kind != SY_FWD) magErr(c, line, "local '%s' would hide %s of the same name (line %u) - pick another name", n, kindName(c->sym[s].kind), c->sym[s].line);
    if (findLocal(c, n) >= 0) magErr(c, line, "'%s' is already a local or parameter here", n);
  } else if (s >= 0 && c->sym[s].kind != SY_FWD && !(c->sym[s].kind == SY_FUNC && !c->sym[s].defined))
    magErr(c, line, "'%s' is already defined on line %u", n, c->sym[s].line);
}

static uint16_t addSym(MagC *c, const char *n, uint8_t kind, int32_t val, uint16_t line) {
  int16_t old = findSym(c, n);
  if (old >= 0) {                                    // a name used before: it is this now
    MagSym &s = c->sym[old];
    if (s.kind == SY_FWD && (kind == SY_CONST || kind == SY_VAR || kind == SY_ARR))
      magErr(c, line, "'%s' is used on line %u before it is declared here - put const, var and array at the top", n, s.line);
    s.kind = kind; s.val = val; s.line = line;
#ifdef MAG_TRACE_SYM
    MAG_TRACE_SYM(n, kind, val);
#endif
    return (uint16_t)old;
  }
  if (!magGrow(c, (void **)&c->sym, &c->capSym, sizeof(MagSym), c->nSym + 1, MAG_SYMS)) magErr(c, line, "too many names (at most %d)", MAG_SYMS);
  size_t len = strlen(n) + 1;
  if (!magGrow(c, (void **)&c->names, &c->capNames, 1, c->nNames + len, MAG_NAMES)) magErr(c, line, "too many names (%d bytes of names at most)", MAG_NAMES);
  memcpy(c->names + c->nNames, n, len);
  MagSym &s = c->sym[c->nSym];
  memset(&s, 0, sizeof(s));
  s.name = c->nNames; c->nNames = (uint16_t)(c->nNames + len);
  s.kind = kind; s.val = val; s.line = line;
#ifdef MAG_TRACE_SYM
  MAG_TRACE_SYM(n, kind, val);                       // the simulator's bots look at a game's variables
#endif
  uint8_t h = magHashName(n);
  s.next = c->head[h]; c->head[h] = c->nSym;
  return c->nSym++;
}

// ---------------- emitting code ----------------
static void emitB(MagC *c, uint8_t b) {
  if (c->pc >= MAG_CODE) MERR("the game is too big: more than %d bytes of code - make functions shorter or share code", MAG_CODE);
  c->code[c->pc++] = b;
}
static void emitW(MagC *c, uint16_t w) { emitB(c, (uint8_t)w); emitB(c, (uint8_t)(w >> 8)); }
static void depth(MagC *c, int d) {
  c->depth = (int16_t)(c->depth + d);
  if (c->depth > c->maxDepth) c->maxDepth = c->depth;
}
static void emitOp(MagC *c, uint8_t op, int d) { emitB(c, op); depth(c, d); }
static void emitPush(MagC *c, int32_t v) {
  if (v == 0) emitB(c, OP_PUSH0);
  else if (v == 1) emitB(c, OP_PUSH1);
  else if (v >= -128 && v <= 127) { emitB(c, OP_PUSHB); emitB(c, (uint8_t)v); }
  else if (v >= -32768 && v <= 32767) { emitB(c, OP_PUSHW); emitW(c, (uint16_t)v); }
  else { emitB(c, OP_PUSHL); emitW(c, (uint16_t)v); emitW(c, (uint16_t)((uint32_t)v >> 16)); }
  depth(c, 1);
}
static void materialize(MagC *c, MagE &e) { if (e.k) { emitPush(c, e.v); e.k = false; } }
static uint16_t emitJump(MagC *c, uint8_t op, int d) { emitOp(c, op, d); uint16_t at = c->pc; emitW(c, 0); return at; }
static void patchTo(MagC *c, uint16_t at, uint16_t to) {
  int32_t rel = (int32_t)to - (int32_t)(at + 2);
  c->code[at] = (uint8_t)rel; c->code[at + 1] = (uint8_t)(rel >> 8);
}
static void patchHere(MagC *c, uint16_t at) { patchTo(c, at, c->pc); }
static void emitJumpBack(MagC *c, uint8_t op, uint16_t to, int d) { uint16_t at = emitJump(c, op, d); patchTo(c, at, to); }
// jumps still to be patched, chained through their own operands (no list on the stack); 0 = none
static uint16_t chainJump(MagC *c, uint8_t op, uint16_t chain) {
  uint16_t at = emitJump(c, op, 0);
  c->code[at] = (uint8_t)chain; c->code[at + 1] = (uint8_t)(chain >> 8);
  return at;
}
static void patchChain(MagC *c, uint16_t chain) {
  while (chain) {
    uint16_t next = (uint16_t)(c->code[chain] | c->code[chain + 1] << 8);
    patchHere(c, chain);
    chain = next;
  }
}

// a statement starts on line l: the line table
static void markLine(MagC *c, uint16_t l) {
  if (l == c->lastLine && c->nLines) return;
  if (c->nLines && c->lines[c->nLines - 1].pc == c->pc) { c->lines[c->nLines - 1].line = l; c->lastLine = l; return; }
  if (!magGrow(c, (void **)&c->lines, &c->capLines, sizeof(MagLine), c->nLines + 1, MAG_LINES)) MERR("too many lines of code (at most %d statements)", MAG_LINES);
  c->lines[c->nLines++] = { c->pc, l };
  c->lastLine = l;
}

// ---------------- expressions ----------------
static MagE expr(MagC *c, int minPrec);
static MagE exprM(MagC *c);
static MagE callExpr(MagC *c, const char *name, uint16_t line);

static int binPrec(MagC *c) {
  if (c->tt == MT_NAME) return !strcmp(c->ts, "or") ? 1 : !strcmp(c->ts, "and") ? 2 : 0;
  if (c->tt != MT_OP) return 0;
  switch (c->op) {
    case OPC('=', '='): case OPC('!', '='): case OPC('<', 0): case OPC('<', '='): case OPC('>', 0): case OPC('>', '='): return 3;
    case OPC('|', 0): return 4;
    case OPC('^', 0): return 5;
    case OPC('&', 0): return 6;
    case OPC('<', '<'): case OPC('>', '>'): return 7;
    case OPC('+', 0): case OPC('-', 0): return 8;
    case OPC('*', 0): case OPC('/', 0): case OPC('%', 0): return 9;
  }
  return 0;
}

static void opMistake(MagC *c) {                    // operators of other languages
  if (c->tt != MT_OP) return;
  switch (c->op) {
    case OPC('&', '&'): MERR("use 'and' instead of '&&'");
    case OPC('|', '|'): MERR("use 'or' instead of '||'");
    case OPC('~', '='): MERR("use != for 'not equal'");
    case OPC('!', 0):   MERR("use 'not' instead of '!'");
    case OPC('+', '='): case OPC('-', '='): case OPC('*', '='): case OPC('/', '='): case OPC('%', '='):
      MERR("'%s' is not supported: write x = x %c ...", c->ts, c->ts[0]);
    case OPC('+', '+'): MERR("'++' is not supported: write x = x + 1");
    case OPC('.', 0):   MERR("'.' is not supported: there are no objects or fields - use separate variables or arrays");
    case OPC('{', 0): case OPC('}', 0): MERR("{ } are not supported: there are no tables - use array name[N]");
    case OPC('#', 0):   MERR("'#' (length) is not supported: keep the size of an array in a const");
    case OPC(':', 0):   MERR("':' is not supported here (no methods, no labels)");
    case OPC('~', 0):   MERR("'~' is not supported: for bitwise not use x ^ -1");
  }
}

static int32_t fold(MagC *c, uint16_t op, int32_t a, int32_t b, uint16_t line) {
  uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
  switch (op) {
    case OPC('+', 0): return (int32_t)(ua + ub);
    case OPC('-', 0): return (int32_t)(ua - ub);
    case OPC('*', 0): return (int32_t)(ua * ub);
    case OPC('/', 0): case OPC('%', 0):
      if (!b) magErr(c, line, "division by zero");
      if (a == INT32_MIN && b == -1) return op == OPC('/', 0) ? a : 0;
      return op == OPC('/', 0) ? a / b : a % b;
    case OPC('&', 0): return a & b;
    case OPC('|', 0): return a | b;
    case OPC('^', 0): return a ^ b;
    case OPC('<', '<'): return (int32_t)(ua << (b & 31));
    case OPC('>', '>'): return a >> (b & 31);
    case OPC('=', '='): return a == b;
    case OPC('!', '='): return a != b;
    case OPC('<', 0): return a < b;
    case OPC('<', '='): return a <= b;
    case OPC('>', 0): return a > b;
    case OPC('>', '='): return a >= b;
  }
  return 0;
}
static uint8_t binOpcode(uint16_t op) {
  switch (op) {
    case OPC('+', 0): return OP_ADD;  case OPC('-', 0): return OP_SUB;  case OPC('*', 0): return OP_MUL;
    case OPC('/', 0): return OP_DIV;  case OPC('%', 0): return OP_MOD;  case OPC('&', 0): return OP_BAND;
    case OPC('|', 0): return OP_BOR;  case OPC('^', 0): return OP_BXOR; case OPC('<', '<'): return OP_SHL;
    case OPC('>', '>'): return OP_SHR; case OPC('=', '='): return OP_EQ; case OPC('!', '='): return OP_NE;
    case OPC('<', 0): return OP_LT;   case OPC('<', '='): return OP_LE; case OPC('>', 0): return OP_GT;
  }
  return OP_GE;
}

// a name used before its sprite, map or tile set is defined: patched at the end
static void addFwd(MagC *c, uint16_t at, uint16_t sym, char want, bool tile, uint16_t line) {
  if (!magGrow(c, (void **)&c->fwd, &c->capFwd, sizeof(MagFwd), c->nFwd + 1, MAG_FWDS)) magErr(c, line, "too many names used before their definition (at most %d)", MAG_FWDS);
  c->fwd[c->nFwd++] = { at, sym, line, want, tile };
}

static MagE primary(MagC *c) {
  MagE e = { false, 0, -1 };
  opMistake(c);
  if (c->tt == MT_NUM) { e.k = true; e.v = c->num; magNext(c); return e; }
  if (c->tt == MT_STR) MERR("a text in quotes can only be the text of text() or textw()");
  if (isOp(c, '(')) {
    magNext(c);
    e = expr(c, 1);
    if (isOp(c, '=')) MERR("use == to compare (= only assigns)");
    expectOp(c, ')', 0, "')'");
    e.sym = -1;
    return e;
  }
  if (c->tt != MT_NAME) MERR("expected a value, found %s", tokDesc(c));
  if (isKeyword(c->ts)) MERR("expected a value, found the keyword '%s'", c->ts);
  const char *n = keepName(c);
  uint16_t line = c->tline;
  magNext(c);
  bool call = isOp(c, '(');
  int16_t l = findLocal(c, n);
  if (l >= 0) {
    if (call) magErr(c, line, "'%s' is a local variable, not a function", n);
    if (isOp(c, '[')) magErr(c, line, "'%s' is a local variable, not an array (arrays are global: array name[N])", n);
    emitOp(c, OP_GETL, 1); emitB(c, c->loc[l].slot);
    return e;
  }
  if (findBuiltin(n) >= 0) {
    if (!call) magErr(c, line, "'%s' is a built-in function: call it as %s(...)", n, n);
    return callExpr(c, n, line);
  }
  if (const MagConst *k = findConst(n)) {
    if (call) magErr(c, line, "'%s' is a constant, not a function", n);
    e.k = true; e.v = k->v; return e;
  }
  if (call) return callExpr(c, n, line);
  int16_t s = findSym(c, n);
  if (s < 0 || c->sym[s].kind == SY_FWD) {           // not known yet: a sprite, map or tile set further down
    if (!strcmp(n, "function")) magErr(c, line, "use 'func' to define a function");
    if (s < 0) s = (int16_t)addSym(c, n, SY_FWD, 0, line);
    emitB(c, OP_PUSHW); addFwd(c, c->pc, (uint16_t)s, 0, false, line); emitW(c, 0);
    depth(c, 1);
    e.sym = s;
    return e;
  }
  uint8_t kind = c->sym[s].kind;
  int32_t val = c->sym[s].val;                        // copied: the table may move while the index is read
  uint16_t size = c->sym[s].size;
  switch (kind) {
    case SY_CONST: e.k = true; e.v = val; return e;
    case SY_VAR: emitOp(c, OP_GETG, 1); emitW(c, (uint16_t)val); return e;
    case SY_ARR:
      if (!isOp(c, '[')) magErr(c, line, "'%s' is an array: use %s[index]", n, n);
      magNext(c);
      exprM(c);
      expectOp(c, ']', 0, "']'");
      emitOp(c, OP_GETA, 0); emitW(c, (uint16_t)val); emitW(c, size);
      return e;
    case SY_FUNC: magErr(c, line, "'%s' is a function: call it as %s(...)", n, n);
    default: e.k = true; e.v = val; e.sym = s; return e;   // a sprite, map or tile set: its number
  }
}

static MagE unary(MagC *c) {
  if (isOp(c, '-')) {
    magNext(c);
    MagE e = unary(c);
    if (e.k) { e.v = (int32_t)(0u - (uint32_t)e.v); e.sym = -1; return e; }
    emitOp(c, OP_NEG, 0); e.sym = -1;
    return e;
  }
  if (isName(c, "not")) {
    magNext(c);
    MagE e = unary(c);
    if (e.k) { e.v = !e.v; e.sym = -1; return e; }
    emitOp(c, OP_NOT, 0); e.sym = -1;
    return e;
  }
  return primary(c);
}

// the recursion of the compiler stays within MAG_CSTACK bytes of the stack, however the game nests
static void stackCheck(MagC *c) {
  volatile char here;
  if (c->stackTop - (uintptr_t)&here > MAG_CSTACK) MERR("nested too deeply (blocks in blocks, brackets in brackets) - split it up into functions");
}

static MagE expr(MagC *c, int minPrec) {
  if (++c->edepth > MAG_EDEPTH) MERR("expression nested too deeply");
  stackCheck(c);
#ifdef MAG_PROBE
  MAG_PROBE();                                       // the simulator measures how deep the compiler's stack goes
#endif
  MagE l = unary(c);
  for (;;) {
    opMistake(c);
    int p = binPrec(c);
    if (!p || p < minPrec) break;
    l.sym = -1;
    if (p <= 2) {                                    // and / or: the right side only when needed, 1 or 0
      bool isAnd = p == 2;
      magNext(c);
      materialize(c, l);
      uint16_t j = emitJump(c, isAnd ? OP_ANDJ : OP_ORJ, -1);
      MagE r = expr(c, p + 1);
      materialize(c, r);
      emitOp(c, OP_BOOL, 0);
      patchHere(c, j);
      continue;
    }
    uint16_t op = c->op, line = c->tline;
    bool div = op == OPC('/', 0) || op == OPC('%', 0);
    magNext(c);
    uint16_t at = c->pc;
    bool lk = l.k;
    materialize(c, l);
    uint16_t pushed = c->pc;
    MagE r = expr(c, p + 1);
    if (r.k && !r.v && div) magErr(c, line, "division by zero");
    if (lk && r.k && c->pc == pushed) {              // both known: no code, just the result
      c->pc = at; depth(c, -1);
      l.k = true; l.v = fold(c, op, l.v, r.v, line);
    } else {
      materialize(c, r);
      emitOp(c, binOpcode(op), -1);
    }
    if (p == 3 && binPrec(c) == 3) MERR("comparisons cannot be chained: write (a < b) and (b < c)");
  }
  c->edepth--;
  return l;
}

static MagE exprM(MagC *c) { MagE e = expr(c, 1); materialize(c, e); return e; }
static int32_t constExpr(MagC *c, const char *what) {
  uint16_t at = c->pc, line = c->tline;
  int16_t d = c->depth;
  MagE e = expr(c, 1);
  if (!e.k || e.sym >= 0) magErr(c, line, "%s must be a constant: numbers, consts and + - * / ... of them", what);
  c->pc = at; c->depth = d;
  return e.v;
}

// the text of text() / textw(): checked, inline in the code
static void textArg(MagC *c, uint8_t n, const char *name) {
  if (c->tt != MT_STR) MERR("argument %u of %s() must be a text in double quotes, like \"SCORE\" (numbers: use num())", n + 1, name);
  if (c->tlen > MAG_TEXTLEN) MERR("text too long (at most %d characters)", MAG_TEXTLEN);
  for (uint16_t i = 0; i < c->tlen; i++) {
    uint8_t ch = (uint8_t)c->ts[i];
    if (ch >= 32 && ch < 127) continue;
    if (ch == 0xC3 && i + 1 < c->tlen && strchr("\xA4\xB6\xBC\x84\x96\x9C\x9F", c->ts[i + 1])) { i++; continue; }
    MERR("the text has a letter the font cannot draw (ASCII and ä ö ü ß only)");
  }
  emitOp(c, OP_STR, 1); emitB(c, (uint8_t)c->tlen);
  for (uint16_t i = 0; i < c->tlen; i++) emitB(c, (uint8_t)c->ts[i]);
  emitB(c, 0);
  magNext(c);
}

// a built-in or own function called; the value stays on the stack
static MagE callExpr(MagC *c, const char *name, uint16_t line) {
  MagE e = { false, 0, -1 };
  expectOp(c, '(', 0, "'('");
  int16_t b = findBuiltin(name);
  if (b >= 0) {
    char types[8];
    uint8_t most = 0, need = 255;
    for (const char *p = MAG_BI[b].args; *p; p++) { if (*p == '|') need = most; else types[most++] = *p; }
    if (need == 255) need = most;
    uint8_t n = 0;
    uint16_t at0 = c->pc;
    bool allK = true;
    int32_t kv[3] = { 0, 0, 0 };                     // col() of constants is a constant
    while (!isOp(c, ')')) {
      if (n) expectOp(c, ',', 0, "',' or ')'");
      if (n >= most) magErr(c, line, "%s() takes %s%u argument%s", name, need < most ? "at most " : "", most, most == 1 ? "" : "s");
      char want = types[n];
      if (want == 's') { textArg(c, n, name); allK = false; n++; continue; }
      MagE a = expr(c, 1);
      if (isOp(c, '=')) MERR("use == to compare (= only assigns)");
      if (want == 'S' || want == 'M' || want == 'T') {
        uint8_t k = want == 'S' ? SY_SPR : want == 'M' ? SY_MAP : SY_TILES;
        int16_t s = a.sym;
        if (s >= 0 && c->sym[s].kind != SY_FWD && c->sym[s].kind != k)
          magErr(c, line, "argument %u of %s() must be %s, but '%s' is %s", n + 1, name, kindName(k), symName(c, (uint16_t)s), kindName(c->sym[s].kind));
        if (s >= 0 && c->sym[s].kind == SY_FWD) c->fwd[c->nFwd - 1].want = want;   // checked when it is defined
      }
      if (!a.k) allK = false;
      else if (n < 3) kv[n] = a.v;
      materialize(c, a);
      n++;
    }
    magNext(c);
    if (n < need) magErr(c, line, "%s() needs %s%u argument%s, got %u", name, need < most ? "at least " : "", need, need == 1 ? "" : "s", n);
    if (b == MB_COL && allK) {
      c->pc = at0; depth(c, -3);
      for (uint8_t i = 0; i < 3; i++) if (kv[i] < 0 || kv[i] > 255) magErr(c, line, "col(r, g, b): each value 0..255");
      e.k = true; e.v = rgb((uint8_t)kv[0], (uint8_t)kv[1], (uint8_t)kv[2]);
      return e;
    }
    emitOp(c, OP_NAT, 1 - n); emitB(c, (uint8_t)b); emitB(c, n);
    return e;
  }
  // an own function, maybe defined further down
  int16_t s = findSym(c, name);
  if (s >= 0 && c->sym[s].kind != SY_FUNC && c->sym[s].kind != SY_FWD)
    magErr(c, line, "'%s' is %s, not a function", name, kindName(c->sym[s].kind));
  if (findLocal(c, name) >= 0) magErr(c, line, "'%s' is a local variable, not a function", name);
  if (s < 0 || c->sym[s].kind == SY_FWD) {
    if (!strcmp(name, "print")) magErr(c, line, "unknown function 'print' - draw text with text(x, y, \"...\", colour)");
    if (s >= 0) magErr(c, line, "'%s' is used as a value on line %u and called as a function here", name, c->sym[s].line);
    if (c->nFn >= MAG_FUNCS - 1) magErr(c, line, "too many functions (at most %d)", MAG_FUNCS - 1);
    s = (int16_t)addSym(c, name, SY_FUNC, c->nFn, line);
    c->sym[s].argc = 255;                              // not known yet: the first call says
    c->fnLine[c->nFn] = line;
    c->fn[c->nFn++] = { 0, 0, 0, 0 };
  }
  uint8_t n = 0;
  while (!isOp(c, ')')) {
    if (n) expectOp(c, ',', 0, "',' or ')'");
    if (c->tt == MT_STR) MERR("a text in quotes can only be the text of text() or textw()");
    exprM(c);
    if (++n > MAG_PARAMS) magErr(c, line, "too many arguments (at most %d)", MAG_PARAMS);
  }
  magNext(c);
  MagSym &y = c->sym[s];                             // (the arguments may have added names: taken only now)
  if (y.argc == 255) y.argc = n;
  else if (y.argc != n) {
    if (y.defined) magErr(c, line, "%s() takes %u argument%s (line %u), called with %u", name, y.argc, y.argc == 1 ? "" : "s", y.line, n);
    magErr(c, line, "%s() is called with %u argument%s on line %u but with %u here", name, y.argc, y.argc == 1 ? "" : "s", c->fnLine[y.val], n);
  }
  emitOp(c, OP_CALL, 1 - n); emitB(c, (uint8_t)y.val);
  return e;
}

// ---------------- statements ----------------
static void block(MagC *c, const char *what, uint16_t line, bool ifs);

static void endOfStatement(MagC *c, uint16_t line) {   // nothing more on the line (declarations)
  if (c->tt != MT_EOF && c->tline == line) {
    opMistake(c);
    if (isOp(c, '=')) MERR("unexpected '=' - to compare use ==");
    MERR("unexpected %s after the declaration (one declaration per line)", tokDesc(c));
  }
}

static uint8_t newLocal(MagC *c, const char *n, uint16_t line) {
  checkNew(c, n, true, line);
  if (c->nSlots >= MAG_LOCALS) magErr(c, line, "too many locals in this function (at most %d, a for loop takes 3)", MAG_LOCALS);
  MagLoc &l = c->loc[c->nLoc++];
  strcpy(l.name, n); l.slot = c->nSlots++;
  if (c->nSlots > c->maxSlots) c->maxSlots = c->nSlots;
  return l.slot;
}
static uint8_t hiddenSlot(MagC *c, uint16_t line) {
  if (c->nSlots >= MAG_LOCALS) magErr(c, line, "too many locals in this function (at most %d, a for loop takes 3)", MAG_LOCALS);
  uint8_t s = c->nSlots++;
  if (c->nSlots > c->maxSlots) c->maxSlots = c->nSlots;
  return s;
}
static void condition(MagC *c) {
  exprM(c);
  if (isOp(c, '=')) MERR("use == to compare (= only assigns)");
}

static void ifStatement(MagC *c, uint16_t line) {
  uint16_t ends = 0;
  for (;;) {
    condition(c);
    if (isName(c, "then")) magNext(c);
    uint16_t jz = emitJump(c, OP_JZ, -1);
    block(c, "if", line, true);
    if (isName(c, "elif") || isName(c, "elseif")) {
      ends = chainJump(c, OP_JMP, ends);
      patchHere(c, jz);
      line = c->tline;
      markLine(c, line);
      magNext(c);
      continue;
    }
    if (isName(c, "else")) {
      uint16_t el = c->tline;
      magNext(c);
      if (isName(c, "if") && c->tline == el) MERR("write 'elif' instead of 'else if'");
      ends = chainJump(c, OP_JMP, ends);
      patchHere(c, jz);
      block(c, "else", el, false);
    } else patchHere(c, jz);
    break;
  }
  patchChain(c, ends);
  magNext(c);                                         // end
}

static void loopStatement(MagC *c, uint16_t line, bool isFor) {
  if (c->nLoops >= MAG_NEST) MERR("loops nested too deeply (at most %d)", MAG_NEST);
  uint8_t keepLoc = c->nLoc, keepSlots = c->nSlots;
  uint16_t top, exitJ;
  uint8_t slot = 0;
  if (isFor) {
    if (c->tt != MT_NAME) MERR("expected the loop variable: for i = 1, 10");
    const char *v = keepName(c);                      // edepth is 0 here: expressions use the slots above
    magNext(c);
    if (isName(c, "in")) MERR("'for ... in' is not supported: for i = first, last [, step]");
    expectOp(c, '=', 0, "'=' (for i = first, last)");
    exprM(c);
    expectOp(c, ',', 0, "',' (for i = first, last)");
    exprM(c);
    if (isOp(c, ',')) {
      magNext(c);
      MagE st = expr(c, 1);
      if (st.k && !st.v) magErr(c, line, "the step of a for loop cannot be 0");
      materialize(c, st);
    } else emitPush(c, 1);
    slot = newLocal(c, v, line);
    hiddenSlot(c, line); hiddenSlot(c, line);
    emitOp(c, OP_SETL, -1); emitB(c, (uint8_t)(slot + 2));
    emitOp(c, OP_SETL, -1); emitB(c, (uint8_t)(slot + 1));
    emitOp(c, OP_SETL, -1); emitB(c, slot);
    if (isName(c, "do")) magNext(c);
    top = c->pc;
    emitOp(c, OP_FORCHK, 0); emitB(c, slot); exitJ = c->pc; emitW(c, 0);
  } else {
    top = c->pc;
    condition(c);
    if (isName(c, "do")) magNext(c);
    exitJ = emitJump(c, OP_JZ, -1);
  }
  MagLoop &L = c->loops[c->nLoops++];
  L.brk = L.cont = 0;
  block(c, isFor ? "for" : "while", line, false);
  uint16_t cont = c->pc;
  if (isFor) { emitOp(c, OP_FORINC, 0); emitB(c, slot); uint16_t at = c->pc; emitW(c, 0); patchTo(c, at, top); }
  else emitJumpBack(c, OP_JMP, top, 0);
  patchHere(c, exitJ);
  patchChain(c, L.brk);
  for (uint16_t j = L.cont; j; ) { uint16_t next = (uint16_t)(c->code[j] | c->code[j + 1] << 8); patchTo(c, j, cont); j = next; }
  c->nLoops--;
  c->nLoc = keepLoc; c->nSlots = keepSlots;
  magNext(c);                                         // end
}

// name = value, name[i] = value, or name(...): the name is in c->nbuf[0]
static void assignOrCall(MagC *c, const char *n, uint16_t line) {
  magNext(c);
  if (isOp(c, '(')) {
    callExpr(c, n, line);
    emitOp(c, OP_POP, -1);
    return;
  }
  int16_t l = findLocal(c, n);
  int16_t s = findSym(c, n);
  if (isOp(c, '[')) {
    if (l >= 0 || s < 0 || c->sym[s].kind != SY_ARR) {
      if (l >= 0 || (s >= 0 && c->sym[s].kind != SY_FWD)) magErr(c, line, "'%s' is not an array", n);
      magErr(c, line, "unknown array '%s' - declare it at the top: array %s[N]", n, n);
    }
    uint16_t base = (uint16_t)c->sym[s].val, size = c->sym[s].size;
    magNext(c);
    exprM(c);
    expectOp(c, ']', 0, "']'");
    expectOp(c, '=', 0, "'='");
    exprM(c);
    emitOp(c, OP_SETA, -2); emitW(c, base); emitW(c, size);
    return;
  }
  if (!isOp(c, '=')) {
    opMistake(c);
    if (c->tt == MT_EOF || c->tline != line) magErr(c, line, "'%s' alone does nothing: assign (%s = ...) or call (%s(...))", n, n, n);
    MERR("expected '=' or '(' after '%s', found %s", n, tokDesc(c));
  }
  magNext(c);
  if (l >= 0) { uint8_t slot = c->loc[l].slot; exprM(c); emitOp(c, OP_SETL, -1); emitB(c, slot); return; }
  if (findBuiltin(n) >= 0 || findConst(n)) magErr(c, line, "'%s' is built in and cannot be assigned", n);
  if (s < 0 || c->sym[s].kind == SY_FWD) magErr(c, line, "unknown variable '%s' - declare it: local %s = ... (in a function) or var %s at the top", n, n, n);
  if (c->sym[s].kind != SY_VAR) magErr(c, line, "'%s' is %s and cannot be assigned", n, kindName(c->sym[s].kind));
  uint16_t g = (uint16_t)c->sym[s].val;
  exprM(c);
  emitOp(c, OP_SETG, -1); emitW(c, g);
}

static void statement(MagC *c) {
  uint16_t line = c->tline;
  if (c->tt != MT_NAME) {
    opMistake(c);
    MERR("expected a statement (an assignment, a call, if, while, for, ...), found %s", tokDesc(c));
  }
  markLine(c, line);
  c->depth = 0;
  const char *n = c->ts;
  if (!strcmp(n, "local")) {
    magNext(c);
    if (c->tt != MT_NAME) MERR("expected a name after 'local'");
    const char *v = keepName(c);
    magNext(c);
    if (isOp(c, ',')) magErr(c, line, "one local at a time: local a = 1 (then local b = 2)");
    if (isOp(c, '[')) magErr(c, line, "arrays cannot be local: declare them at the top with array %s[N]", v);
    if (isOp(c, '=')) { magNext(c); exprM(c); } else emitPush(c, 0);
    uint8_t slot = newLocal(c, v, line);              // after the value: local x = x is an error, not the old x
    emitOp(c, OP_SETL, -1); emitB(c, slot);
    return;
  }
  if (!strcmp(n, "if")) { magNext(c); ifStatement(c, line); return; }
  if (!strcmp(n, "while") || !strcmp(n, "for")) { bool f = n[0] == 'f'; magNext(c); loopStatement(c, line, f); return; }
  if (!strcmp(n, "break") || !strcmp(n, "continue")) {
    if (!c->nLoops) MERR("'%s' outside a while or for loop", n);
    MagLoop &L = c->loops[c->nLoops - 1];
    if (n[0] == 'b') L.brk = chainJump(c, OP_JMP, L.brk); else L.cont = chainJump(c, OP_JMP, L.cont);
    magNext(c);
    return;
  }
  if (!strcmp(n, "return")) {
    magNext(c);
    if (c->tt != MT_EOF && c->tline == line && !isName(c, "end") && !isName(c, "else") && !isName(c, "elif") && !isName(c, "elseif")) {
      exprM(c);
      emitOp(c, OP_RET, -1);
    } else emitOp(c, OP_RET0, 0);
    return;
  }
  if (!strcmp(n, "else") || !strcmp(n, "elif") || !strcmp(n, "elseif")) MERR("'%s' without an 'if'", n);
  if (!strcmp(n, "end")) MERR("'end' without a func, if, while or for");
  if (!strcmp(n, "func") || !strcmp(n, "function")) MERR("functions cannot be inside functions (or an 'end' is missing above)");
  if (!strcmp(n, "var") || !strcmp(n, "const") || !strcmp(n, "array")) MERR("'%s' only at the top of the file, outside functions (inside: local)", n);
  if (isKeyword(n)) MERR("'%s' cannot start a statement", n);
  assignOrCall(c, keepName(c), line);
}

// statements up to 'end' (or elif / else when ifs)
static void block(MagC *c, const char *what, uint16_t line, bool ifs) {
  if (++c->nest > MAG_NEST) MERR("blocks nested too deeply (at most %d)", MAG_NEST);
  stackCheck(c);
#ifdef MAG_PROBE
  MAG_PROBE();
#endif
  uint8_t keepLoc = c->nLoc, keepSlots = c->nSlots;
  for (;;) {
    if (c->tt == MT_EOF) magErr(c, line, "'%s' on line %u has no 'end'", what, line);
    if (isName(c, "end")) break;
    if (ifs && (isName(c, "elif") || isName(c, "elseif") || isName(c, "else"))) break;
    if (isName(c, "func")) magErr(c, line, "'%s' on line %u has no 'end' (a new func starts on line %u)", what, line, c->tline);
    if (isOp(c, ';')) { magNext(c); continue; }       // allowed between statements, never needed
    statement(c);
  }
  c->nLoc = keepLoc; c->nSlots = keepSlots;
  c->nest--;
}

// ---------------- the top of the file ----------------
static void funcDef(MagC *c) {
  uint16_t line = c->tline;
  magNext(c);
  if (c->tt != MT_NAME) MERR("expected the function's name: func name(a, b)");
  const char *n = keepName(c);
  checkNew(c, n, false, line);
  int16_t s = findSym(c, n);
  if (s >= 0 && c->sym[s].kind == SY_FWD) magErr(c, line, "'%s' is used as a value on line %u - a function is called with ()", n, c->sym[s].line);
  if (s < 0) {
    if (c->nFn >= MAG_FUNCS - 1) magErr(c, line, "too many functions (at most %d)", MAG_FUNCS - 1);
    s = (int16_t)addSym(c, n, SY_FUNC, c->nFn, line);
    c->sym[s].argc = 255;
    c->fnLine[c->nFn] = line;
    c->fn[c->nFn++] = { 0, 0, 0, 0 };
  }
  magNext(c);
  expectOp(c, '(', 0, "'(' after the function's name");
  c->inFn = true; c->nLoc = c->nSlots = c->maxSlots = 0; c->nLoops = 0; c->nest = 0;
  uint8_t np = 0;
  while (!isOp(c, ')')) {
    if (np) expectOp(c, ',', 0, "',' or ')'");
    if (c->tt != MT_NAME) MERR("expected a parameter name");
    if (np >= MAG_PARAMS) MERR("too many parameters (at most %d)", MAG_PARAMS);
    newLocal(c, c->ts, c->tline);
    np++;
    magNext(c);
  }
  magNext(c);
  MagSym &y = c->sym[s];
  if (y.argc != 255 && y.argc != np) magErr(c, line, "%s() has %u parameter%s, but it is called with %u on line %u", symName(c, (uint16_t)s), np, np == 1 ? "" : "s", y.argc, c->fnLine[y.val]);
  y.argc = np; y.defined = true; y.line = line;
  uint8_t fi = (uint8_t)y.val;
  c->fn[fi].addr = c->pc; c->fn[fi].np = np;
  c->maxDepth = 0;
  block(c, "func", line, false);
  markLine(c, c->tline);
  emitOp(c, OP_RET0, 0);
  c->fn[fi].nl = c->maxSlots; c->fn[fi].maxSt = (uint16_t)(c->maxDepth + 2);
  c->inFn = false; c->nLoc = 0;
  magNext(c);                                         // end
  endOfStatement(c, c->prevLine);
}

static void needSameLine(MagC *c, uint16_t line, const char *what) {
  if (c->tt == MT_EOF || c->tline != line) magErr(c, line, "%s", what);
}

// a sprite, map or level block: its rows are checked and only their place in the file kept
static void dataBlock(MagC *c, uint8_t kind, bool level) {
  uint16_t line = c->tline;
  const char *what = kind == SY_SPR ? "sprite" : level ? "level" : "map";
  magNext(c);
  const char *n = "";
  if (!level) {
    if (c->tt != MT_NAME) magErr(c, line, "expected a name: %s NAME WIDTH HEIGHT", what);
    n = keepName(c);
    checkNew(c, n, false, line);
    magNext(c);
  }
  needSameLine(c, line, kind == SY_SPR ? "expected: sprite NAME WIDTH HEIGHT" : level ? "expected: level WIDTH HEIGHT" : "expected: map NAME WIDTH HEIGHT");
  int32_t w = constExpr(c, "the width");
  needSameLine(c, line, "the height is missing");
  int32_t h = constExpr(c, "the height");
  endOfStatement(c, line);
  int32_t mw = kind == SY_SPR ? MAG_SPRMAX : MAG_MAPW, mh = kind == SY_SPR ? MAG_SPRMAX : MAG_MAPH;
  if (w < 1 || w > mw || h < 1 || h > mh) magErr(c, line, "a %s is 1..%ld wide and 1..%ld high", what, (long)mw, (long)mh);
  if (c->nBlk >= MAG_BLOCKS) magErr(c, line, "too many sprites, maps and levels (at most %d)", MAG_BLOCKS);
  if (!magGrow(c, (void **)&c->blk, &c->capBlk, sizeof(MagBlk), c->nBlk + 1, MAG_BLOCKS)) magErr(c, line, "too many blocks");
  uint8_t *kinds = (uint8_t *)realloc(c->blkKind, c->capBlk);
  if (!kinds) magErr(c, line, "not enough memory to compile this game");
  c->blkKind = kinds;
  uint32_t first = 0, stride = 0, prev = 0;
  bool even = true;
  uint16_t rows0 = c->nRows;
  for (int32_t r = 0; r < h; r++) {
    if (c->tt != MT_STR) {
      if (c->tt == MT_EOF || c->tt == MT_NAME) magErr(c, line, "%s %s%shas %ld row%s, but %ld are needed", what, n, n[0] ? " " : "", (long)r, r == 1 ? "" : "s", (long)h);
      MERR("expected row %ld of the %s in double quotes", (long)r + 1, what);
    }
    if (r && c->tline == c->prevLine) MERR("one row per line");
    if (c->tlen != w) MERR("row %ld of %s %s has %u characters, but the %s is %ld wide", (long)r + 1, what, n, c->tlen, what, (long)w);
    for (uint16_t i = 0; i < c->tlen; i++) {
      char ch = c->ts[i];
      if (kind == SY_SPR && magPal(ch) == 0xFE) MERR("'%c' is not a palette letter (. k w r o y g d c b p m s n a t)", ch);
      if (kind == SY_MAP && ((uint8_t)ch < 32 || (uint8_t)ch > 126)) MERR("maps use plain ASCII characters only");
    }
    uint32_t off = c->toff;
    if (r == 0) first = off;
    else if (r == 1) stride = off - prev;
    else if (off - prev != stride) even = false;
    if (!magGrow(c, (void **)&c->rows, &c->capRows, 4, c->nRows + 1, 60000)) MERR("too many rows");
    c->rows[c->nRows++] = off;                        // kept only if the rows are uneven
    prev = off;
    magNext(c);
    if (c->tt != MT_EOF && c->tline == c->prevLine) MERR("one row per line: nothing may follow a row");
  }
  MagBlk &b = c->blk[c->nBlk];
  b.w = (uint16_t)w; b.h = (uint16_t)h;
  if (h == 1) stride = w + 3;
  if (even && stride <= 65535) { b.off = first; b.stride = (uint16_t)stride; c->nRows = rows0; }
  else { b.off = rows0; b.stride = 0; }               // row starts stay in the table
  uint16_t id = kind == SY_SPR ? c->nSpr++ : c->nMap++;
  c->blkKind[c->nBlk++] = kind;
  if (level) {
    if (!magGrow(c, (void **)&c->lvl, &c->capLvl, 2, c->nLvl + 1, MAG_BLOCKS)) magErr(c, line, "too many levels");
    c->lvl[c->nLvl++] = id;
  } else addSym(c, n, kind, id, line);
}

static void tilesBlock(MagC *c) {
  uint16_t line = c->tline;
  magNext(c);
  if (c->tt != MT_NAME) magErr(c, line, "expected: tiles NAME SIZE");
  const char *n = keepName(c);
  checkNew(c, n, false, line);
  magNext(c);
  needSameLine(c, line, "expected: tiles NAME SIZE (the size of a tile in pixels)");
  int32_t size = constExpr(c, "the tile size");
  endOfStatement(c, line);
  if (size < 1 || size > 64) magErr(c, line, "a tile is 1..64 pixels");
  if (c->nTs >= MAG_TILESETS) magErr(c, line, "too many tile sets (at most %d)", MAG_TILESETS);
  MagTiles &t = c->tset[c->nTs];
  t.size = (uint8_t)size;
  for (int16_t &e : t.e) e = -1;
  c->tsLine[c->nTs] = line;
  addSym(c, n, SY_TILES, c->nTs, line);
  for (;;) {
    if (c->tt == MT_EOF) magErr(c, line, "tiles on line %u has no 'end'", line);
    if (isName(c, "end")) { uint16_t el = c->tline; magNext(c); endOfStatement(c, el); break; }
    uint16_t el = c->tline;
    if (c->tt != MT_STR || c->tlen != 1) MERR("expected a tile: \"#\" = sprite or \"#\" = colour (one character in double quotes)");
    uint8_t ch = (uint8_t)c->ts[0];
    if (ch < 32 || ch > 126) MERR("a tile character is plain ASCII");
    if (t.e[ch - 32] != -1) MERR("tile '%c' is given twice", ch);
    magNext(c);
    expectOp(c, '=', 0, "'=' (\"#\" = sprite or colour)");
    int16_t y = c->tt == MT_NAME ? findSym(c, c->ts) : -1;
    bool sprite = c->tt == MT_NAME && !findConst(c->ts) && (y < 0 || c->sym[y].kind == SY_SPR || c->sym[y].kind == SY_FWD);
    if (sprite) {
      if (y < 0) { if (isKeyword(c->ts) || findBuiltin(c->ts) >= 0) MERR("expected a sprite or a colour"); y = (int16_t)addSym(c, c->ts, SY_FWD, 0, el); }
      if (c->sym[y].kind == SY_SPR) t.e[ch - 32] = (int16_t)(256 + c->sym[y].val);
      else { t.e[ch - 32] = 256; addFwd(c, (uint16_t)((c->nTs << 7) | (ch - 32)), (uint16_t)y, 'S', true, el); }
      magNext(c);
    } else {
      int32_t v = constExpr(c, "a tile's colour");
      if (v < 0 || v > 255) magErr(c, el, "a colour is 0..255 (a colour name or col(r, g, b))");
      t.e[ch - 32] = (int16_t)v;
    }
    endOfStatement(c, el);
  }
  c->nTs++;
}

static void declList(MagC *c, bool isConst, bool isArr) {
  uint16_t line = c->tline;
  magNext(c);
  for (;;) {
    if (c->tt != MT_NAME) MERR("expected a name");
    const char *n = keepName(c);
    uint16_t nl = c->tline;
    checkNew(c, n, false, nl);
    magNext(c);
    if (isConst) {
      expectOp(c, '=', 0, "'=' (const NAME = value)");
      int32_t v = constExpr(c, "a const");
      addSym(c, n, SY_CONST, v, nl);
    } else if (isArr) {
      expectOp(c, '[', 0, "'[' (array name[size])");
      int32_t sz = constExpr(c, "an array's size");
      expectOp(c, ']', 0, "']'");
      if (sz < 1 || sz > MAG_GLOBALS) magErr(c, nl, "an array has 1..%d elements", MAG_GLOBALS);
      if (c->nG + sz > MAG_GLOBALS) magErr(c, nl, "too many variables: var and array together have at most %d numbers (8 KB)", MAG_GLOBALS);
      uint16_t s = addSym(c, n, SY_ARR, c->nG, nl);
      c->sym[s].size = (uint16_t)sz;
      c->nG = (uint16_t)(c->nG + sz);
    } else {
      if (isOp(c, '[')) magErr(c, nl, "an array is declared with: array %s[N]", n);
      if (c->nG + 1 > MAG_GLOBALS) magErr(c, nl, "too many variables: var and array together have at most %d numbers (8 KB)", MAG_GLOBALS);
      uint16_t g = c->nG++;
      addSym(c, n, SY_VAR, g, nl);
      if (isOp(c, '=')) {
        magNext(c);
        int32_t v = constExpr(c, "a var's first value");
        if (v) {
          if (!magGrow(c, (void **)&c->inits, &c->capInits, sizeof(MagInit), c->nInits + 1, MAG_GLOBALS)) MERR("too many variables");
          c->inits[c->nInits++] = { g, v };
        }
      }
    }
    if (!isOp(c, ',')) break;
    magNext(c);
  }
  endOfStatement(c, line);
}

static void headerText(MagC *c, char *out, size_t n, const char *what) {
  uint16_t line = c->tline;
  magNext(c);
  if (c->tt != MT_STR) magErr(c, line, "%s needs a text in double quotes: %s \"...\"", what, what);
  if (c->tlen >= n) MERR("the %s text is too long (at most %u characters)", what, (unsigned)(n - 1));
  strcpy(out, c->ts);
  magNext(c);
  endOfStatement(c, line);
}

static void topLevel(MagC *c) {
  MagVm *v = c->vm;
  if (!isName(c, "game")) magErr(c, c->tline, "a game starts with its name: game \"My Game\"");
  headerText(c, v->title, sizeof(v->title), "game");
  if (!v->title[0]) magErr(c, c->prevLine, "the game's name is empty");
  v->fps = 30;
  bool head = true;
  while (c->tt != MT_EOF) {
    if (c->tt != MT_NAME) { opMistake(c); MERR("expected a declaration (const, var, array, func, sprite, map, level, tiles), found %s", tokDesc(c)); }
    const char *n = c->ts;
    if (!strcmp(n, "author") || !strcmp(n, "info") || !strcmp(n, "fps")) {
      if (!head) MERR("'%s' belongs to the header: right after the game line", n);
      if (n[0] == 'a') headerText(c, v->author, sizeof(v->author), "author");
      else if (n[0] == 'i') headerText(c, v->info, sizeof(v->info), "info");
      else {
        uint16_t line = c->tline;
        magNext(c);
        if (c->tt != MT_NUM || (c->num != 30 && c->num != 60)) magErr(c, line, "fps is 30 or 60");
        v->fps = (uint8_t)c->num;
        magNext(c);
        endOfStatement(c, line);
      }
      continue;
    }
    head = false;
    if (!strcmp(n, "game")) MERR("only one 'game' line, at the start");
    else if (!strcmp(n, "const")) declList(c, true, false);
    else if (!strcmp(n, "var")) declList(c, false, false);
    else if (!strcmp(n, "array")) declList(c, false, true);
    else if (!strcmp(n, "func")) funcDef(c);
    else if (!strcmp(n, "function")) MERR("use 'func' to define a function");
    else if (!strcmp(n, "local")) MERR("'local' only inside a function; at the top use var");
    else if (!strcmp(n, "sprite")) dataBlock(c, SY_SPR, false);
    else if (!strcmp(n, "map")) dataBlock(c, SY_MAP, false);
    else if (!strcmp(n, "level")) dataBlock(c, SY_MAP, true);
    else if (!strcmp(n, "tiles")) tilesBlock(c);
    else if (!strcmp(n, "end")) MERR("'end' without a func, if, while or for");
    else MERR("only declarations at the top (const, var, array, func, sprite, map, level, tiles) - code goes inside a func");
  }
}

// the end of the file: every name known, the entry points there, the tables for the VM
static void magFinish(MagC *c) {
  MagVm *v = c->vm;
  for (uint16_t i = 0; i < c->nFwd; i++) {
    MagFwd &f = c->fwd[i];
    MagSym &s = c->sym[f.sym];
    const char *n = symName(c, f.sym);
    if (s.kind == SY_FWD) magErr(c, f.line, "unknown name '%s'", n);
    if (s.kind == SY_FUNC) magErr(c, f.line, "'%s' is a function: call it as %s(...)", n, n);
    if (s.kind != SY_SPR && s.kind != SY_MAP && s.kind != SY_TILES) magErr(c, f.line, "'%s' is used here before its declaration on line %u", n, s.line);
    char want = f.want;
    uint8_t k = want == 'S' ? (uint8_t)SY_SPR : want == 'M' ? (uint8_t)SY_MAP : want == 'T' ? (uint8_t)SY_TILES : s.kind;
    if (k != s.kind) magErr(c, f.line, "'%s' is %s, but %s is needed here", n, kindName(s.kind), kindName(k));
    if (f.tile) c->tset[f.at >> 7].e[f.at & 127] = (int16_t)(256 + s.val);
    else { c->code[f.at] = (uint8_t)s.val; c->code[f.at + 1] = (uint8_t)(s.val >> 8); }
  }
  for (uint16_t i = 0; i < c->nSym; i++) {
    MagSym &s = c->sym[i];
    if (s.kind == SY_FUNC && !s.defined) magErr(c, c->fnLine[s.val], "unknown function '%s'", symName(c, i));
  }
  // every tile sprite: square, and the tile a whole multiple of it
  uint16_t spr = 0;
  for (uint8_t t = 0; t < c->nTs; t++)
    for (uint8_t k = 0; k < 95; k++) {
      int16_t e = c->tset[t].e[k];
      if (e < 256) continue;
      spr = 0;
      for (uint16_t b = 0, id = 0; b < c->nBlk; b++) if (c->blkKind[b] == SY_SPR && id++ == e - 256) { spr = b; break; }
      MagBlk &b = c->blk[spr];
      if (b.w != b.h || c->tset[t].size % b.w) magErr(c, c->tsLine[t], "tile '%c': its sprite is %ux%u, a tile of %u pixels needs a square sprite of %u, or of a part of it (%u, %u, ...)",
                                                    k + 32, b.w, b.h, c->tset[t].size, c->tset[t].size, c->tset[t].size / 2, c->tset[t].size / 4);
    }
  const char *entry[3] = { "init", "update", "draw" };
  uint8_t *slot[3] = { &v->fInit, &v->fUpdate, &v->fDraw };
  for (uint8_t i = 0; i < 3; i++) {
    int16_t s = findSym(c, entry[i]);
    *slot[i] = 255;
    if (s >= 0 && c->sym[s].kind == SY_FUNC && c->sym[s].defined) {
      if (c->sym[s].argc) magErr(c, c->sym[s].line, "%s() has no parameters", entry[i]);
      *slot[i] = (uint8_t)c->sym[s].val;
    } else if (i) magErr(c, c->lnNo, "the game needs a func %s() - the console calls it every frame", entry[i]);
  }
  // the first values of the vars: a function of its own, run before init()
  if (c->nFn >= MAG_FUNCS) magErr(c, c->lnNo, "too many functions (at most %d)", MAG_FUNCS - 1);
  v->fGinit = c->nFn;
  MagFn &gf = c->fn[c->nFn++];
  gf.addr = c->pc; gf.np = gf.nl = 0; gf.maxSt = 2;
  for (uint16_t i = 0; i < c->nInits; i++) { emitPush(c, c->inits[i].v); emitOp(c, OP_SETG, -1); emitW(c, c->inits[i].g); }
  emitOp(c, OP_RET0, 0);
  // the tables the VM keeps: copied into blocks of their own size
  size_t *h = &v->heap;
  v->codeN = c->pc;
  uint8_t *code = (uint8_t *)realloc(c->code, c->pc ? c->pc : 1);
  if (code) { c->code = code; }
  v->code = c->code; c->code = NULL; *h += v->codeN;
#define MAG_TAKE(dst, src, n, T) do { dst = (T *)magAlloc((size_t)(n) * sizeof(T), h); if (!dst) magErr(c, c->lnNo, "not enough memory for this game"); if (n) memcpy(dst, src, (size_t)(n) * sizeof(T)); } while (0)
  MAG_TAKE(v->lines, c->lines, c->nLines, MagLine); v->nLines = c->nLines;
  MAG_TAKE(v->fn, c->fn, c->nFn, MagFn); v->nFn = c->nFn;
  v->nG = c->nG;
  v->g = (int32_t *)magAlloc((size_t)v->nG * 4, h);
  v->spr = (MagBlk *)magAlloc((size_t)c->nSpr * sizeof(MagBlk), h);
  v->map = (MagBlk *)magAlloc((size_t)c->nMap * sizeof(MagBlk), h);
  if (!v->g || !v->spr || !v->map) magErr(c, c->lnNo, "not enough memory for this game");
  for (uint16_t b = 0; b < c->nBlk; b++) {
    if (c->blkKind[b] == SY_SPR) v->spr[v->nSpr++] = c->blk[b];
    else v->map[v->nMap++] = c->blk[b];
  }
  MAG_TAKE(v->rows, c->rows, c->nRows, uint32_t); v->nRows = c->nRows;
  MAG_TAKE(v->lvl, c->lvl, c->nLvl, uint16_t); v->nLvl = c->nLvl;
  MAG_TAKE(v->ts, c->tset, c->nTs, MagTiles); v->nTs = c->nTs;
#undef MAG_TAKE
}

static void magCFree(MagC *c) {
  if (c->f) fclose(c->f);
  free(c->code); free(c->lines); free(c->fn); free(c->fnLine); free(c->inits); free(c->blk); free(c->blkKind);
  free(c->rows); free(c->lvl); free(c->tset); free(c->sym); free(c->names); free(c->fwd);
  free(c);
}

/* Compiles the file at v->path into v. false: msg (and line) say why;
   line 0 = not about a line (no memory, no file).                    */
static size_t magCompilePeak = 0;                    // the most the compiler took (the simulator reports it)
static bool magCompile(MagVm *v, char *msg, size_t msgN, uint16_t *line) {
  *line = 0;
  if (!MAG_ROOM(sizeof(MagC) + MAG_CODE)) { snprintf(msg, msgN, "not enough memory to compile a game"); return false; }
  MagC *c = (MagC *)calloc(1, sizeof(MagC));
  if (!c) { snprintf(msg, msgN, "not enough memory to compile a game"); return false; }
  c->vm = v;
  c->heap = sizeof(MagC);
  memset(c->head, 0xFF, sizeof(c->head));
  c->code = (uint8_t *)malloc(MAG_CODE);
  c->fn = (MagFn *)calloc(MAG_FUNCS, sizeof(MagFn));
  c->fnLine = (uint16_t *)calloc(MAG_FUNCS, 2);
  c->tset = (MagTiles *)calloc(MAG_TILESETS, sizeof(MagTiles));
  c->heap += MAG_CODE + MAG_FUNCS * (sizeof(MagFn) + 2) + MAG_TILESETS * sizeof(MagTiles);
  c->f = fopen(v->path, "rb");
  c->yieldAt = millis();
  volatile char top;
  c->stackTop = (uintptr_t)&top;
  bool ok = false;
  if (!c->code || !c->fn || !c->fnLine || !c->tset) snprintf(msg, msgN, "not enough memory to compile a game");
  else if (!c->f) snprintf(msg, msgN, "cannot read the file");
  else if (setjmp(c->jb)) {
    snprintf(msg, msgN, "%s", c->msg);
    *line = c->errLine;
  } else {
    magNext(c);
    topLevel(c);
    magFinish(c);
    ok = true;
  }
  if (c->heap > magCompilePeak) magCompilePeak = c->heap;
  magCFree(c);
  return ok;
}

// =========================================================
//  THE DATA ON THE CARD
// =========================================================
/* Reads n bytes at off from the game's file. A card that does not
   answer: the game waits for it (magCardWait); false = it will not come
   back (the player left, or the file changed), mag->quit is set then. */
static bool magFileRead(uint32_t off, uint8_t *dst, uint16_t n) {
  MagVm &v = *mag;
  for (;;) {
    if (v.quit) return false;
    if (v.fd >= 0 && sdReady() && lseek(v.fd, (off_t)off, SEEK_SET) == (off_t)off && read(v.fd, dst, n) == (ssize_t)n) {
      v.reads++; v.readBytes += n;
      return true;
    }
    if (!magCardWait()) { v.quit = true; return false; }
  }
}

// a page of the file from the cache (16 sets of 4, the oldest goes); NULL = no card
static const uint8_t *magPage(uint32_t page) {
  MagVm &v = *mag;
  uint32_t set = (page * 2654435761u >> 16) % (MAG_PAGES / MAG_WAYS) * MAG_WAYS;
  uint32_t oldest = set;
  for (uint32_t i = set; i < set + MAG_WAYS; i++) {
    if (v.tag[i] == page + 1) { v.use[i] = ++v.clock; return v.pg + i * MAG_PAGE; }
    if (v.use[i] < v.use[oldest]) oldest = i;
  }
  v.misses++;
  uint32_t off = page * MAG_PAGE;
  uint16_t n = off + MAG_PAGE <= v.size ? MAG_PAGE : (uint16_t)(v.size > off ? v.size - off : 0);
  v.tag[oldest] = 0;
  if (n && !magFileRead(off, v.pg + oldest * MAG_PAGE, n)) return NULL;
  memset(v.pg + oldest * MAG_PAGE + n, 0, MAG_PAGE - n);   // past the end of the file
  v.tag[oldest] = page + 1; v.use[oldest] = ++v.clock;
  return v.pg + oldest * MAG_PAGE;
}
// n bytes at off through the cache; false = the card is gone for good (dst zeroed)
static bool magRead(uint32_t off, uint8_t *dst, uint16_t n) {
  while (n) {
    const uint8_t *p = magPage(off / MAG_PAGE);
    if (!p) { memset(dst, 0, n); return false; }
    uint16_t k = (uint16_t)(MAG_PAGE - off % MAG_PAGE);
    if (k > n) k = n;
    memcpy(dst, p + off % MAG_PAGE, k);
    dst += k; off += k; n = (uint16_t)(n - k);
  }
  return true;
}
static uint32_t magRowOff(const MagBlk &b, uint16_t y) { return b.stride ? b.off + (uint32_t)y * b.stride : mag->rows[b.off + y]; }

// sprite s decoded to colours (MAG_CLEAR = transparent); NULL = the card is gone
static const uint8_t *magSprite(uint16_t s) {
  MagVm &v = *mag;
  if (v.sprAt[s] >= 0) return v.pool + v.sprAt[s];
  const MagBlk &b = v.spr[s];
  uint16_t n = (uint16_t)(b.w * b.h);
  if (v.poolN + n > MAG_POOL) {                        // full: all of them go, the ones still drawn come back
    for (uint16_t i = 0; i < v.nSpr; i++) v.sprAt[i] = -1;
    v.poolN = 0;
  }
  uint8_t *d = v.pool + v.poolN;
  for (uint16_t y = 0; y < b.h; y++) {
    if (!magRead(magRowOff(b, y), d + y * b.w, b.w)) return NULL;
    for (uint16_t x = 0; x < b.w; x++) d[y * b.w + x] = magPal((char)d[y * b.w + x]);
  }
  v.sprAt[s] = (int16_t)v.poolN;
  v.poolN = (uint16_t)(v.poolN + n);
  return d;
}

// =========================================================
//  THE VM
// =========================================================
static uint16_t magLineAt(uint16_t pc) {
  uint16_t l = 0;
  for (uint16_t i = 0; i < mag->nLines && mag->lines[i].pc <= pc; i++) l = mag->lines[i].line;
  return l;
}

static int16_t mc(int32_t v) { return (int16_t)(v < -2048 ? -2048 : v > 2048 ? 2048 : v); }
static Font magFont(int32_t s) { return s <= 1 ? F_SMALL : s == 2 ? F_TEXT : s == 3 ? F_BOLD : F_BIG; }   // size 1..4
static uint32_t magIsqrt(uint32_t n) {
  uint32_t r = 0, b = 1u << 30;
  while (b > n) b >>= 2;
  while (b) { if (n >= r + b) { n -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
  return r;
}
static const int16_t MAG_SIN[91] = {
  0, 18, 36, 54, 71, 89, 107, 125, 143, 160, 178, 195, 213, 230, 248, 265, 282, 299, 316, 333, 350, 367, 384, 400, 416,
  433, 449, 465, 481, 496, 512, 527, 543, 558, 573, 587, 602, 616, 630, 644, 658, 672, 685, 698, 711, 724, 737, 749, 761,
  773, 784, 796, 807, 818, 828, 839, 849, 859, 868, 878, 887, 896, 904, 912, 920, 928, 935, 943, 949, 956, 962, 968, 974,
  979, 984, 989, 994, 998, 1002, 1005, 1008, 1011, 1014, 1016, 1018, 1020, 1022, 1023, 1023, 1024, 1024 };
static int32_t magSin(int32_t a) {
  a %= 360; if (a < 0) a += 360;
  if (a <= 90) return MAG_SIN[a];
  if (a <= 180) return MAG_SIN[180 - a];
  if (a <= 270) return -MAG_SIN[a - 180];
  return -MAG_SIN[360 - a];
}

// a sprite at x, y; k = scale, flip = mirrored left/right
static void magDrawSprite(const uint8_t *d, uint16_t w, uint16_t h, int32_t x, int32_t y, int32_t k, bool flip) {
  for (uint16_t r = 0; r < h; r++) {
    int32_t py = y + r * k;
    if (py + k <= 0 || py >= SCR_H) continue;
    const uint8_t *row = d + r * w;
    for (uint16_t i = 0; i < w; i++) {
      uint8_t cc = row[flip ? w - 1 - i : i];
      if (cc == MAG_CLEAR) continue;
      int32_t px = x + i * k;
      if (k == 1) gfx.pixel((int16_t)px, (int16_t)py, cc);
      else if (px + k > 0 && px < SCR_W) gfx.fill((int16_t)px, (int16_t)py, (int16_t)k, (int16_t)k, cc);
    }
  }
}

static int32_t magArea(int32_t x, int32_t y, int32_t w, int32_t h) {   // what of a box is on the screen
  int32_t x1 = x + w, y1 = y + h;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x1 > SCR_W) x1 = SCR_W;
  if (y1 > SCR_H) y1 = SCR_H;
  return x1 > x && y1 > y ? (x1 - x) * (y1 - y) : 0;
}

static void magFail(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void magFail(const char *fmt, ...) {
  if (mag->failed) return;
  va_list a;
  va_start(a, fmt);
  vsnprintf(mag->err, sizeof(mag->err), fmt, a);
  va_end(a);
  mag->failed = true;
}

static bool magColOk(int32_t c) {
  if ((uint32_t)c <= 255) return true;
  magFail("colour %ld is not 0..255 - use a colour name or col(r, g, b)", (long)c);
  return false;
}
static bool magSprOk(int32_t s) {
  if ((uint32_t)s < mag->nSpr) return true;
  magFail("%ld is not a sprite (there are %u)", (long)s, mag->nSpr);
  return false;
}
static bool magMapOk(int32_t m) {
  if ((uint32_t)m < mag->nMap) return true;
  magFail("%ld is not a map (there are %u maps and levels)", (long)m, mag->nMap);
  return false;
}

// one character of map m (0 outside it); the map loadmap() took comes from RAM
static int32_t magMget(int32_t m, int32_t x, int32_t y) {
  const MagBlk &b = mag->map[m];
  if ((uint32_t)x >= b.w || (uint32_t)y >= b.h) return 0;
  if (m == mag->mid) return mag->mbuf[(uint32_t)y * b.w + x];
  uint8_t ch = 0;
  magRead(magRowOff(b, (uint16_t)y) + (uint32_t)x, &ch, 1);
  return ch;
}

// map m with tile set t, its top left corner at x, y: only the tiles on the screen
static int32_t magDrawMap(int32_t m, int32_t t, int32_t x, int32_t y) {
  MagVm &v = *mag;
  const MagBlk &b = v.map[m];
  const MagTiles &ts = v.ts[t];
  int32_t s = ts.size;
  int32_t tx0 = x < 0 ? -x / s : 0, ty0 = y < 0 ? -y / s : 0;
  int32_t tx1 = (SCR_W - 1 - x) / s, ty1 = (SCR_H - 1 - y) / s;
  if (SCR_W - 1 - x < 0 || SCR_H - 1 - y < 0) return 0;
  if (tx1 >= b.w) tx1 = b.w - 1;
  if (ty1 >= b.h) ty1 = b.h - 1;
  if (tx0 > tx1 || ty0 > ty1) return 0;
  uint8_t row[MAG_MAPW];
  int32_t n = tx1 - tx0 + 1, drawn = 0;
  for (int32_t ty = ty0; ty <= ty1; ty++) {
    if (m == v.mid) memcpy(row, v.mbuf + (uint32_t)ty * b.w + tx0, (size_t)n);
    else if (!magRead(magRowOff(b, (uint16_t)ty) + (uint32_t)tx0, row, (uint16_t)n)) return drawn;
    for (int32_t i = 0; i < n; i++) {
      uint8_t ch = row[i];
      if (ch < 32 || ch > 126) continue;
      int16_t e = ts.e[ch - 32];
      if (e < 0) continue;
      int32_t px = x + (tx0 + i) * s, py = y + ty * s;
      drawn++;
      if (e < 256) { gfx.fill((int16_t)px, (int16_t)py, (int16_t)s, (int16_t)s, (col)e); continue; }
      uint16_t sp = (uint16_t)(e - 256);
      const uint8_t *d = magSprite(sp);
      if (!d) return drawn;
      magDrawSprite(d, v.spr[sp].w, v.spr[sp].h, px, py, s / v.spr[sp].w, false);
    }
  }
  return drawn;
}

static bool magLoadMap(int32_t m) {
  MagVm &v = *mag;
  const MagBlk &b = v.map[m];
  uint32_t n = (uint32_t)b.w * b.h;
  if (n > MAG_LOADMAX) { magFail("loadmap: the map has %lu cells, at most %d fit in RAM", (unsigned long)n, MAG_LOADMAX); return false; }
  if (n > v.mcap) {
    free(v.mbuf); v.heap -= v.mcap; v.mbuf = NULL; v.mcap = 0;
    v.mbuf = (uint8_t *)magAlloc(n, &v.heap);
    if (!v.mbuf) { magFail("loadmap: not enough memory"); return false; }
    v.mcap = n;
  }
  v.mid = -1;
  for (uint16_t y = 0; y < b.h; y++) if (!magFileRead(magRowOff(b, y), v.mbuf + (uint32_t)y * b.w, b.w)) return false;
  v.mid = (int16_t)m;
  return true;
}

static void magSaveSlot(int32_t slot, int32_t val);  // game_sdgames.h

// built-in function id with n arguments at a; returns its value. cost = extra instructions it counts as
static int32_t magNative(uint8_t id, int32_t *a, uint8_t n, int32_t &cost) {
  MagVm &v = *mag;
  switch (id) {
    case MB_CLS:    if (magColOk(a[0])) gfx.clear((col)a[0]); cost = 300; return 0;
    case MB_RECT:
      if (!magColOk(a[4])) return 0;
      if (a[2] > 0 && a[3] > 0) { gfx.fill(mc(a[0]), mc(a[1]), mc(a[2]), mc(a[3]), (col)a[4]); cost = magArea(a[0], a[1], a[2], a[3]) / 256; }
      return 0;
    case MB_BOX:
      if (!magColOk(a[4])) return 0;
      if (a[2] > 0 && a[3] > 0) gfx.frame(mc(a[0]), mc(a[1]), mc(a[2]), mc(a[3]), (col)a[4], 1);
      return 0;
    case MB_LINE: if (magColOk(a[4])) { gfx.line(mc(a[0]), mc(a[1]), mc(a[2]), mc(a[3]), (col)a[4]); cost = (abs(mc(a[2]) - mc(a[0])) + abs(mc(a[3]) - mc(a[1]))) / 64; } return 0;
    case MB_CIRCLE: {
      if (!magColOk(a[3])) return 0;
      int32_t r = a[2] < 0 ? 0 : a[2] > 1000 ? 1000 : a[2];
      if (magArea(a[0] - r, a[1] - r, 2 * r + 1, 2 * r + 1)) { gfx.circle(mc(a[0]), mc(a[1]), (int16_t)r, (col)a[3]); cost = r / 16; }
      return 0;
    }
    case MB_DISC: {
      if (!magColOk(a[3])) return 0;
      int32_t r = a[2] < 0 ? 0 : a[2] > 1000 ? 1000 : a[2], cx = mc(a[0]), cy = mc(a[1]);
      if (!magArea(cx - r, cy - r, 2 * r + 1, 2 * r + 1)) return 0;
      for (int32_t dy = cy - r < 0 ? -cy : -r; dy <= r && cy + dy < SCR_H; dy++) {
        int32_t dx = (int32_t)magIsqrt((uint32_t)(r * r + r - dy * dy));
        gfx.hline((int16_t)(cx - dx), (int16_t)(cy + dy), (int16_t)(2 * dx + 1), (col)a[3]);
      }
      cost = magArea(cx - r, cy - r, 2 * r + 1, 2 * r + 1) / 256;
      return 0;
    }
    case MB_PIXEL: if (magColOk(a[2])) gfx.pixel(mc(a[0]), mc(a[1]), (col)a[2]); return 0;
    case MB_TRI:
      if (magColOk(a[6])) { gfx.triangle(mc(a[0]), mc(a[1]), mc(a[2]), mc(a[3]), mc(a[4]), mc(a[5]), (col)a[6]); cost = 40; }
      return 0;
    case MB_TEXT: case MB_NUM: {
      if (!magColOk(a[3])) return 0;
      char b[16];
      const char *s = id == MB_TEXT ? (const char *)v.code + a[2] : b;
      if (id == MB_NUM) snprintf(b, sizeof(b), "%ld", (long)a[2]);
      gfx.text(mc(a[0]), mc(a[1]), s, (col)a[3], magFont(n > 4 ? a[4] : 2));
      cost = 20;
      return 0;
    }
    case MB_TEXTW: return Gfx::width((const char *)v.code + a[0], magFont(n > 1 ? a[1] : 2));
    case MB_NUMW: { char b[16]; snprintf(b, sizeof(b), "%ld", (long)a[0]); return Gfx::width(b, magFont(n > 1 ? a[1] : 2)); }
    case MB_SPR: {
      if (!magSprOk(a[0])) return 0;
      int32_t k = n > 3 ? (a[3] < 1 ? 1 : a[3] > 8 ? 8 : a[3]) : 1;
      const MagBlk &b = v.spr[a[0]];
      if (!magArea(a[1], a[2], b.w * k, b.h * k)) return 0;
      const uint8_t *d = magSprite((uint16_t)a[0]);
      if (d) magDrawSprite(d, b.w, b.h, mc(a[1]), mc(a[2]), k, n > 4 && a[4]);
      cost = magArea(a[1], a[2], b.w * k, b.h * k) / 64;
      return 0;
    }
    case MB_SPRW: return magSprOk(a[0]) ? v.spr[a[0]].w : 0;
    case MB_SPRH: return magSprOk(a[0]) ? v.spr[a[0]].h : 0;
    case MB_DRAWMAP:
      if (!magMapOk(a[0])) return 0;
      if ((uint32_t)a[1] >= v.nTs) { magFail("%ld is not a tile set", (long)a[1]); return 0; }
      cost = magDrawMap(a[0], a[1], a[2] < -100000 ? -100000 : a[2] > 4096 ? 4096 : a[2],
                        a[3] < -100000 ? -100000 : a[3] > 4096 ? 4096 : a[3]) * v.ts[a[1]].size * v.ts[a[1]].size / 256;
      return 0;
    case MB_LOADMAP: if (magMapOk(a[0])) { magLoadMap(a[0]); cost = v.map[a[0]].h * 20; } return 0;
    case MB_MGET: return magMapOk(a[0]) ? magMget(a[0], a[1], a[2]) : 0;
    case MB_MSET: {
      if (!magMapOk(a[0])) return 0;
      if (a[0] != v.mid) { magFail("mset: this map is not loaded - call loadmap(map) first"); return 0; }
      const MagBlk &b = v.map[a[0]];
      if ((uint32_t)a[1] >= b.w || (uint32_t)a[2] >= b.h) { magFail("mset: %ld, %ld is outside the map (%u x %u)", (long)a[1], (long)a[2], b.w, b.h); return 0; }
      if (a[3] < 32 || a[3] > 126) { magFail("mset: %ld is not a map character (32..126, e.g. '#')", (long)a[3]); return 0; }
      v.mbuf[(uint32_t)a[2] * b.w + a[1]] = (uint8_t)a[3];
      return 0;
    }
    case MB_MAPW: case MB_MAPH: {
      int32_t m = n ? a[0] : v.mid;
      if (n && !magMapOk(m)) return 0;
      if (m < 0) return 0;
      return id == MB_MAPW ? v.map[m].w : v.map[m].h;
    }
    case MB_LEVEL:
      if (a[0] < 1 || a[0] > v.nLvl) { magFail("level(%ld): there %s %u level%s (1..%u)", (long)a[0], v.nLvl == 1 ? "is" : "are", v.nLvl, v.nLvl == 1 ? "" : "s", v.nLvl); return 0; }
      return v.lvl[a[0] - 1];
    case MB_LEVELS: return v.nLvl;
    case MB_BTN: case MB_BTNP:
      if ((uint32_t)a[0] >= B_COUNT) { magFail("%s(%ld): the keys are UP, DOWN, LEFT, RIGHT, OK", id == MB_BTN ? "btn" : "btnp", (long)a[0]); return 0; }
      return id == MB_BTN ? v.held[a[0]] : v.press[a[0]];
    case MB_TILTX: return cfgTilt && gyroReady ? (tiltAn[B_RIGHT] - tiltAn[B_LEFT]) * 100 / 256 : 0;
    case MB_TILTY: return cfgTilt && gyroReady ? (tiltAn[B_DOWN] - tiltAn[B_UP]) * 100 / 256 : 0;
    case MB_SFX: if (a[0] > 0 && a[1] > 0) sfx((uint16_t)(a[0] < 40 ? 40 : a[0] > 15000 ? 15000 : a[0]), (uint16_t)(a[1] > 2000 ? 2000 : a[1])); return 0;
    case MB_RND: return a[0] > 0 ? (int32_t)random(a[0]) : 0;
    case MB_TIME: return (int32_t)(gameMillis() - v.t0);
    case MB_SCORE: v.score = a[0]; v.bar = true; return 0;
    case MB_GAMEOVER: v.over = true; v.overScore = a[0]; return 0;
    case MB_SAVE:
      if ((uint32_t)a[0] >= MAG_SLOTS) { magFail("save(%ld, ...): the slots are 0..%d", (long)a[0], MAG_SLOTS - 1); return 0; }
      magSaveSlot(a[0], a[1]);
      return 0;
    case MB_LOAD:
      if ((uint32_t)a[0] >= MAG_SLOTS) { magFail("load(%ld): the slots are 0..%d", (long)a[0], MAG_SLOTS - 1); return 0; }
      return v.slots[a[0]];
    case MB_ABS: return a[0] < 0 ? (int32_t)(0u - (uint32_t)a[0]) : a[0];
    case MB_MIN: return a[0] < a[1] ? a[0] : a[1];
    case MB_MAX: return a[0] > a[1] ? a[0] : a[1];
    case MB_CLAMP: return a[0] < a[1] ? a[1] : a[0] > a[2] ? a[2] : a[0];
    case MB_SIN: return magSin(a[0]);
    case MB_COS: return magSin(a[0] + 90);
    case MB_ATAN2: {
      if (!a[0] && !a[1]) return 0;
      int32_t d = (int32_t)lroundf(atan2f((float)a[0], (float)a[1]) * 57.29578f);
      return d < 0 ? d + 360 : d % 360;
    }
    case MB_SQRT: return a[0] > 0 ? (int32_t)magIsqrt((uint32_t)a[0]) : 0;
    case MB_COL:
      for (uint8_t i = 0; i < 3; i++) if (a[i] < 0 || a[i] > 255) { magFail("col(r, g, b): each value 0..255, got %ld", (long)a[i]); return 0; }
      return rgb((uint8_t)a[0], (uint8_t)a[1], (uint8_t)a[2]);
  }
  return 0;
}

/* Runs function f to its end. Each instruction counts against
   mag->budget (shared by update() and draw() in a frame); false = it
   stopped: an error (mag->failed, mag->err) or the player left.
   Dispatch by a table of labels (GCC's computed goto): one load and one
   jump per instruction - a switch becomes a chain of compares at -Os.  */
static bool magRun(uint8_t f, const char *where) {
  MagVm &v = *mag;
  const uint8_t *code = v.code;
  int32_t *g = v.g;
  int32_t *st = v.stack, *end = v.stack + MAG_STACK;
  int32_t *sp = st, *bp = st;
  uint32_t depth = 0;
  int32_t budget = v.budget;
  uint32_t pc, at = 0;
  v.where = where;
  static const void *const OPS[] = {
    &&op_push0, &&op_push1, &&op_pushb, &&op_pushw, &&op_pushl, &&op_str,
    &&op_getg, &&op_setg, &&op_getl, &&op_setl, &&op_geta, &&op_seta,
    &&op_add, &&op_sub, &&op_mul, &&op_div, &&op_mod, &&op_neg, &&op_not,
    &&op_band, &&op_bor, &&op_bxor, &&op_shl, &&op_shr,
    &&op_eq, &&op_ne, &&op_lt, &&op_le, &&op_gt, &&op_ge, &&op_bool,
    &&op_jmp, &&op_jz, &&op_andj, &&op_orj, &&op_forchk, &&op_forinc,
    &&op_call, &&op_nat, &&op_ret, &&op_ret0, &&op_pop };
  static_assert(sizeof(OPS) / sizeof(OPS[0]) == OP_POP + 1, "one label per opcode");
  // the call of f, as from nowhere
  {
    const MagFn &fn = v.fn[f];
    for (uint8_t i = 0; i < fn.nl; i++) *sp++ = 0;
    pc = fn.addr;
  }
#define RD16(p) ((int16_t)(code[p] | code[(p) + 1] << 8))
#define FAIL(...) do { v.errPc = (uint16_t)at; magFail(__VA_ARGS__); goto out; } while (0)
#define NEXT do { if (--budget < 0) goto slow; at = pc; uint8_t o_ = code[pc++]; if (o_ > OP_POP) goto broken; goto *OPS[o_]; } while (0)
  NEXT;
op_push0: *sp++ = 0; NEXT;
op_push1: *sp++ = 1; NEXT;
op_pushb: *sp++ = (int8_t)code[pc++]; NEXT;
op_pushw: *sp++ = RD16(pc); pc += 2; NEXT;
op_pushl: *sp++ = (int32_t)((uint32_t)(uint16_t)RD16(pc) | (uint32_t)(uint16_t)RD16(pc + 2) << 16); pc += 4; NEXT;
op_str:   *sp++ = (int32_t)(pc + 1); pc += 2u + code[pc]; NEXT;
op_getg:  *sp++ = g[(uint16_t)RD16(pc)]; pc += 2; NEXT;
op_setg:  g[(uint16_t)RD16(pc)] = *--sp; pc += 2; NEXT;
op_getl:  *sp++ = bp[code[pc++]]; NEXT;
op_setl:  bp[code[pc++]] = *--sp; NEXT;
op_geta: {
    uint32_t base = (uint16_t)RD16(pc), n = (uint16_t)RD16(pc + 2);
    pc += 4;
    int32_t i = sp[-1];
    if ((uint32_t)i >= n) FAIL("array index %ld is outside 0..%lu", (long)i, (unsigned long)(n - 1));
    sp[-1] = g[base + i];
    NEXT;
  }
op_seta: {
    uint32_t base = (uint16_t)RD16(pc), n = (uint16_t)RD16(pc + 2);
    pc += 4;
    int32_t i = sp[-2];
    if ((uint32_t)i >= n) FAIL("array index %ld is outside 0..%lu", (long)i, (unsigned long)(n - 1));
    g[base + i] = sp[-1];
    sp -= 2;
    NEXT;
  }
op_add: sp--; sp[-1] = (int32_t)((uint32_t)sp[-1] + (uint32_t)sp[0]); NEXT;
op_sub: sp--; sp[-1] = (int32_t)((uint32_t)sp[-1] - (uint32_t)sp[0]); NEXT;
op_mul: sp--; sp[-1] = (int32_t)((uint32_t)sp[-1] * (uint32_t)sp[0]); NEXT;
op_div: op_mod: {
    sp--;
    int32_t a = sp[-1], b = sp[0];
    bool div = code[at] == OP_DIV;
    if (!b) FAIL("division by zero");
    if (a == INT32_MIN && b == -1) sp[-1] = div ? a : 0;
    else sp[-1] = div ? a / b : a % b;
    NEXT;
  }
op_neg:  sp[-1] = (int32_t)(0u - (uint32_t)sp[-1]); NEXT;
op_not:  sp[-1] = !sp[-1]; NEXT;
op_band: sp--; sp[-1] &= sp[0]; NEXT;
op_bor:  sp--; sp[-1] |= sp[0]; NEXT;
op_bxor: sp--; sp[-1] ^= sp[0]; NEXT;
op_shl:  sp--; sp[-1] = (int32_t)((uint32_t)sp[-1] << (sp[0] & 31)); NEXT;
op_shr:  sp--; sp[-1] = sp[-1] >> (sp[0] & 31); NEXT;
op_eq:   sp--; sp[-1] = sp[-1] == sp[0]; NEXT;
op_ne:   sp--; sp[-1] = sp[-1] != sp[0]; NEXT;
op_lt:   sp--; sp[-1] = sp[-1] < sp[0]; NEXT;
op_le:   sp--; sp[-1] = sp[-1] <= sp[0]; NEXT;
op_gt:   sp--; sp[-1] = sp[-1] > sp[0]; NEXT;
op_ge:   sp--; sp[-1] = sp[-1] >= sp[0]; NEXT;
op_bool: sp[-1] = sp[-1] != 0; NEXT;
op_jmp:  pc = (uint32_t)((int32_t)pc + 2 + RD16(pc)); NEXT;
op_jz:   pc = *--sp ? pc + 2 : (uint32_t)((int32_t)pc + 2 + RD16(pc)); NEXT;
op_andj: if (!sp[-1]) pc = (uint32_t)((int32_t)pc + 2 + RD16(pc)); else { sp--; pc += 2; } NEXT;
op_orj:  if (sp[-1]) { sp[-1] = 1; pc = (uint32_t)((int32_t)pc + 2 + RD16(pc)); } else { sp--; pc += 2; } NEXT;
op_forchk: {
    int32_t *l = bp + code[pc];
    if (!l[2]) FAIL("the step of a for loop is 0");
    if (l[2] > 0 ? l[0] > l[1] : l[0] < l[1]) pc = (uint32_t)((int32_t)pc + 3 + RD16(pc + 1));
    else pc += 3;
    NEXT;
  }
op_forinc: {
    int32_t *l = bp + code[pc];
    int32_t nx = (int32_t)((uint32_t)l[0] + (uint32_t)l[2]);
    if ((l[2] > 0 && nx < l[0]) || (l[2] < 0 && nx > l[0])) pc += 3;   // ran past the largest number: done
    else { l[0] = nx; pc = (uint32_t)((int32_t)pc + 3 + RD16(pc + 1)); }
    NEXT;
  }
op_call: {
    const MagFn &fn = v.fn[code[pc++]];
    if (depth >= MAG_DEPTH) FAIL("functions call each other too deep (more than %d) - endless recursion?", MAG_DEPTH);
    if (sp + (fn.nl - fn.np) + fn.maxSt > end) FAIL("the stack is full (%d values) - endless recursion?", MAG_STACK);
    v.fr[depth++] = { (uint16_t)pc, (uint16_t)(bp - st) };
    bp = sp - fn.np;
    for (uint8_t i = fn.np; i < fn.nl; i++) *sp++ = 0;
    pc = fn.addr;
    NEXT;
  }
op_nat: {
    uint8_t id = code[pc], n = code[pc + 1];
    pc += 2;
    int32_t cost = 0;
    int32_t r = magNative(id, sp - n, n, cost);
    sp -= n;
    *sp++ = r;
    budget -= 4 + cost;
    if (v.failed) { v.errPc = (uint16_t)at; goto out; }
    if (v.quit) goto out;
    NEXT;
  }
op_ret: op_ret0: {
    int32_t r = code[at] == OP_RET ? sp[-1] : 0;
    if (!depth) { v.budget = budget; return true; }
    const MagFrame &fr = v.fr[--depth];
    sp = bp;
    *sp++ = r;
    bp = st + fr.bp;
    pc = fr.ret;
    NEXT;
  }
op_pop: sp--; NEXT;
slow:
  v.errPc = (uint16_t)at;
  magFail("script too slow or endless loop in %s() (%ld instructions in one frame)", where, (long)v.budgetMax);
  goto out;
broken:
  FAIL("broken code (%u)", code[at]);
out:
  if (budget < 0) budget = 0;
  v.budget = budget;
  return false;
#undef RD16
#undef FAIL
#undef NEXT
}
