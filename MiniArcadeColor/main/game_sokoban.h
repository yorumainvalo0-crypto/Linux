// MiniArcade Color: Sokoban. Part of console.cpp, which includes it.
// Push every crate onto a goal. Brick walls, wooden crates that turn green
// on a goal, a little warehouse keeper who walks from field to field.
// The levels are the OLED console's (sokoban_levels.h): '#' wall, '.' goal,
// '$' box, '*' box on a goal, '@' player, '+' player on a goal, rows split
// by '|'. They were made by a generator that pulls the boxes backwards
// from the goals; the PC test solves every one, so none is impossible.
// LEFT / RIGHT pick one of the levels solved so far (or the next one), OK
// plays it; in a level OK takes a move back. Best = levels solved. More
// levels can come from the SD card (sdlevels.h): listed after these, all
// open, their progress (fewest moves) kept on the card.

#define SK_W    21                    // largest level
#define SK_H     8
#define SK_UNDO 128
#define SK_ANIM 110                   // ms a step takes on screen
#define SK_Y0   28                    // play area, top and bottom
#define SK_Y1  220

enum { SK_WALL = 1, SK_GOAL = 2, SK_BOX = 4 };

static const char *const SK_LEVELS[] = {
#include "sokoban_levels.h"
};
static const uint8_t SK_N = sizeof(SK_LEVELS) / sizeof(SK_LEVELS[0]);

// the map and the undo list live on the heap while the game runs
static uint8_t (*sk)[SK_W] = NULL;            // [SK_H][SK_W]
static uint8_t  *skUndo = NULL;               // bits 0-1 direction, bit 2 = pushed a box
static uint8_t  skW, skH, skX, skY;           // size, player
static uint16_t skUndoN, skMoves;
static uint8_t  skFace = 1;                   // the way the player looks (as B_UP..B_RIGHT)
static uint8_t  skAnimBox;                    // the last step pushed a box
static uint32_t skAnimAt;                     // when the last step began

static const int8_t SK_DX[4] = { 0, 0, -1, 1 }, SK_DY[4] = { -1, 1, 0, 0 };   // up down left right

static bool skAlloc() {
  uint8_t *m = (uint8_t *)malloc(SK_W * SK_H + SK_UNDO);
  if (!m) return false;
  sk = (uint8_t (*)[SK_W])m;
  skUndo = m + SK_W * SK_H;
  return true;
}
static void skFree() { free(sk); sk = NULL; skUndo = NULL; }

static bool skLoadText(const char *p) {
  memset(sk, 0, SK_W * SK_H);
  skW = skH = 0;
  uint8_t x = 0, y = 0;
  for (; ; p++) {
    if (*p == '|' || !*p) {
      if (x > skW) skW = x;
      y++; x = 0;
      if (!*p || y >= SK_H) break;
      continue;
    }
    if (x >= SK_W) continue;
    uint8_t t = 0;
    switch (*p) {
    case '#': t = SK_WALL; break;
    case '.': t = SK_GOAL; break;
    case '$': t = SK_BOX; break;
    case '*': t = SK_BOX | SK_GOAL; break;
    case '+': t = SK_GOAL; skX = x; skY = y; break;
    case '@': skX = x; skY = y; break;
    }
    sk[y][x++] = t;
  }
  skH = y;
  skUndoN = skMoves = 0;
  skFace = 1;
  skAnimAt = 0;
  return skW && skH;
}
static bool skLoad(uint8_t lvl) { return skLoadText(SK_LEVELS[lvl]); }

/* A level line from the SD card (sdlevels.h), as skLoadText takes it:
   3..8 rows of up to 21 split by '|', only # . $ * @ + and floor (' ',
   '-' or '_'), one player, as many boxes as goals (one at least), and
   walls all round where the player can walk.                          */
static bool skValid(const char *t) {
  uint8_t g[SK_H][SK_W];                        // 0 off the rows, 1 wall, 2 floor, 3 reached
  memset(g, 0, sizeof(g));
  uint8_t x = 0, y = 0, w = 0, px = 0, py = 0, players = 0;
  uint16_t boxes = 0, goals = 0;
  for (const char *p = t; ; p++) {
    if (*p == '|' || !*p) {
      if (x > w) w = x;
      y++; x = 0;
      if (!*p) break;
      if (y >= SK_H) return false;
      continue;
    }
    char c = *p;
    if (x >= SK_W || !strchr("#.$*@+ -_", c)) return false;
    boxes += c == '$' || c == '*';
    goals += c == '.' || c == '*' || c == '+';
    if (c == '@' || c == '+') { players++; px = x; py = y; }
    g[y][x++] = c == '#' ? 1 : 2;
  }
  if (y < 3 || w < 3 || players != 1 || boxes != goals || !boxes) return false;
  uint8_t st[SK_W * SK_H], n = 0;               // flood fill from the player: never off the rows
  st[n++] = (uint8_t)(py * SK_W + px);
  g[py][px] = 3;
  while (n) {
    uint8_t c = st[--n];
    for (uint8_t d = 0; d < 4; d++) {
      int8_t nx = (int8_t)(c % SK_W + SK_DX[d]), ny = (int8_t)(c / SK_W + SK_DY[d]);
      if (nx < 0 || ny < 0 || nx >= SK_W || ny >= y || !g[ny][nx]) return false;
      if (g[ny][nx] == 2) { g[ny][nx] = 3; st[n++] = (uint8_t)(ny * SK_W + nx); }
    }
  }
  return true;
}

// one step; returns 0 = blocked, 1 = walked, 2 = pushed a box
static uint8_t skStep(uint8_t d) {
  int8_t nx = (int8_t)(skX + SK_DX[d]), ny = (int8_t)(skY + SK_DY[d]);
  skFace = d;
  if (nx < 0 || ny < 0 || nx >= SK_W || ny >= SK_H || (sk[ny][nx] & SK_WALL)) return 0;
  uint8_t r = 1;
  if (sk[ny][nx] & SK_BOX) {
    int8_t bx = (int8_t)(nx + SK_DX[d]), by = (int8_t)(ny + SK_DY[d]);
    if (bx < 0 || by < 0 || bx >= SK_W || by >= SK_H || (sk[by][bx] & (SK_WALL | SK_BOX))) return 0;
    sk[ny][nx] &= (uint8_t)~SK_BOX;
    sk[by][bx] |= SK_BOX;
    r = 2;
  }
  skX = (uint8_t)nx; skY = (uint8_t)ny;
  skMoves++;
  if (skUndoN == SK_UNDO) { memmove(skUndo, skUndo + 1, SK_UNDO - 1); skUndoN--; }   // forget the oldest
  skUndo[skUndoN++] = (uint8_t)(d | (r == 2 ? 4 : 0));
  return r;
}

static bool skBack() {
  if (!skUndoN) return false;
  uint8_t u = skUndo[--skUndoN], d = u & 3;
  int8_t px = (int8_t)(skX - SK_DX[d]), py = (int8_t)(skY - SK_DY[d]);
  if (u & 4) {                                  // pull the box back
    int8_t bx = (int8_t)(skX + SK_DX[d]), by = (int8_t)(skY + SK_DY[d]);
    sk[by][bx] &= (uint8_t)~SK_BOX;
    sk[skY][skX] |= SK_BOX;
  }
  skX = (uint8_t)px; skY = (uint8_t)py;
  if (skMoves) skMoves--;
  skFace = d;
  skAnimAt = 0;                                 // no glide backwards
  return true;
}

static bool skSolved() {
  for (uint8_t y = 0; y < SK_H; y++)
    for (uint8_t x = 0; x < SK_W; x++)
      if ((sk[y][x] & SK_BOX) && !(sk[y][x] & SK_GOAL)) return false;
  return true;
}

static uint8_t skBoxes() {
  uint8_t n = 0;
  for (uint8_t y = 0; y < SK_H; y++)
    for (uint8_t x = 0; x < SK_W; x++)
      if (sk[y][x] & SK_BOX) n++;
  return n;
}

static uint8_t skBoxesLeft() {
  uint8_t n = 0;
  for (uint8_t y = 0; y < SK_H; y++)
    for (uint8_t x = 0; x < SK_W; x++)
      if ((sk[y][x] & SK_BOX) && !(sk[y][x] & SK_GOAL)) n++;
  return n;
}

// ---------------- drawing ----------------
static void skWall(int16_t px, int16_t py, int16_t t) {
  gfx.fill(px, py, t, t, rgb(170, 85, 51));
  col m = rgb(102, 42, 0);
  int16_t h = t / 2;
  gfx.hline(px, py, t, rgb(204, 128, 102));
  gfx.hline(px, py + h, t, m);
  gfx.hline(px, py + t - 1, t, m);
  gfx.vline(px + t / 3, py + 1, h - 1, m);
  gfx.vline(px + t - 1, py + 1, h - 1, m);
  gfx.vline(px + t * 2 / 3, py + h + 1, t - h - 2, m);
}

static void skCrate(int16_t px, int16_t py, int16_t t, bool done) {
  col face = done ? rgb(51, 204, 51) : rgb(204, 128, 51);
  col edge = done ? rgb(0, 85, 0) : rgb(102, 42, 0);
  gfx.fill(px + 2, py + 2, t - 4, t - 4, face);
  gfx.frame(px + 2, py + 2, t - 4, t - 4, edge, 2);
  gfx.frame(px + 4, py + 4, t - 8, t - 8, colLight(face));
  gfx.line(px + 5, py + 5, px + t - 6, py + t - 6, edge);
  gfx.line(px + 5, py + 6, px + t - 7, py + t - 6, edge);
  if (done) gfx.frame(px + 1, py + 1, t - 2, t - 2, C::YELLOW);
}

static void skPlayer(int16_t px, int16_t py, int16_t t, uint8_t face) {
  int16_t cx = px + t / 2, hr = t / 5;
  int16_t hy = py + t * 3 / 10;
  gfx.rbox(cx - t / 4, py + t / 2, t / 2, t * 2 / 5, 3, C::BLUE);         // body
  gfx.fill(cx - t / 4 + 1, py + t - 3, t / 5, 2, C::DKBLUE);              // feet
  gfx.fill(cx + t / 4 - 1 - t / 5, py + t - 3, t / 5, 2, C::DKBLUE);
  gfx.disc(cx, hy, hr, rgb(255, 212, 153));                               // head
  gfx.hline(cx - hr + 1, hy - hr, hr * 2 - 1, C::BROWN);                  // hair
  gfx.hline(cx - hr, hy - hr + 1, hr * 2 + 1, C::BROWN);
  if (face == 0) return;                                                  // seen from behind
  int16_t ex = face == 2 ? -2 : (face == 3 ? 2 : 0);
  if (face != 3) gfx.fill(cx - 3 + ex, hy, 2, 2, C::BLACK);
  if (face != 2) gfx.fill(cx + 2 + ex, hy, 2, 2, C::BLACK);
}

static void skDrawMap(int16_t t) {
  int16_t ox = (SCR_W - skW * t) / 2, oy = SK_Y0 + (SK_Y1 - SK_Y0 - skH * t) / 2;
  uint32_t now = gameMillis(), at = now - skAnimAt;
  bool anim = skAnimAt && at < SK_ANIM;
  int16_t off = anim ? (int16_t)(t * (int32_t)(SK_ANIM - at) / SK_ANIM) : 0;   // still this far behind
  int16_t ax = -SK_DX[skFace] * off, ay = -SK_DY[skFace] * off;
  uint8_t gbx = 255, gby = 255;                         // the crate being pushed (drawn gliding)
  if (anim && skAnimBox) { gbx = (uint8_t)(skX + SK_DX[skFace]); gby = (uint8_t)(skY + SK_DY[skFace]); }
  col glow = (now / 250) % 2 ? C::YELLOW : C::ORANGE;
  gfx.fill(ox - 3, oy - 3, skW * t + 6, skH * t + 6, C::BLACK);
  for (uint8_t y = 0; y < skH; y++)
    for (uint8_t x = 0; x < skW; x++) {
      int16_t px = ox + x * t, py = oy + y * t;
      uint8_t v = sk[y][x];
      if (v & SK_WALL) { skWall(px, py, t); continue; }
      gfx.fill(px, py, t, t, rgb(51, 42, 51));
      gfx.hline(px, py, t, GREY1);
      gfx.vline(px, py, t, GREY1);
      if (v & SK_GOAL) {
        gfx.circle(px + t / 2, py + t / 2, t / 3, glow);
        gfx.disc(px + t / 2, py + t / 2, t / 8, glow);
      }
    }
  for (uint8_t y = 0; y < skH; y++)
    for (uint8_t x = 0; x < skW; x++)
      if ((sk[y][x] & SK_BOX) && !(x == gbx && y == gby)) skCrate(ox + x * t, oy + y * t, t, sk[y][x] & SK_GOAL);
  if (gbx != 255) skCrate(ox + gbx * t + ax, oy + gby * t + ay, t, false);
  skPlayer(ox + skX * t + ax, oy + skY * t + ay, t, skFace);
}

static int16_t skTile() {
  int16_t t = 28;
  if (skW && 308 / skW < t) t = (int16_t)(308 / skW);
  if (skH && (SK_Y1 - SK_Y0 - 6) / skH < t) t = (int16_t)((SK_Y1 - SK_Y0 - 6) / skH);
  return t;
}

// can this level be played? the ones solved so far and the next one, every one from the SD card
#define SK_BACK 0xFFFF                          // the level list's answer for "back to the library"
static uint8_t skLastOpen() { return curHigh < SK_N ? (uint8_t)curHigh : SK_N - 1; }
static bool skOpen(uint16_t lvl) { return lvl < SK_N ? lvl <= skLastOpen() : lvl - SK_N < sdl.n; }
static uint16_t skAfter(uint16_t lvl) { return lvl == skLastOpen() && sdl.n ? SK_N : lvl + 1; }   // RIGHT goes here

static uint8_t skLastLoad() {
  prefs.begin("arcade", true);
  uint8_t v = (uint8_t)prefs.getUShort("SOKlv", 0);
  prefs.end();
  return v;
}
static void skLastSave(uint8_t lvl) { prefs.begin("arcade", false); prefs.putUShort("SOKlv", lvl); prefs.end(); }

// a level onto the map for the list; one from the card is read now (into sdl.line)
static bool skShow(uint16_t lvl) {
  if (lvl < SK_N) return skLoad((uint8_t)lvl);
  return sdlRead((uint16_t)(lvl - SK_N)) && skLoadText(sdl.line);
}

/* The level list: LEFT / RIGHT through the open ones, the SD card's
   after the built-in ones (DOWN / UP jumps there and back), OK plays.
   Levels >= SK_N are the card's; SK_BACK = back to the library.     */
static uint16_t skChoose(uint16_t lvl) {
  sdlOpen("sokoban", skValid);
  if (!skOpen(lvl)) lvl = lvl >= SK_N ? skLastOpen() : 0;
  if (!skShow(lvl)) skShow(lvl = skLastOpen());
  btnClear();
  while (poll()) {
    uint16_t to = lvl;
    if (btn(B_LEFT) && lvl > 0) to = lvl == SK_N ? skLastOpen() : lvl - 1;
    if (btn(B_RIGHT)) to = skAfter(lvl);
    if (btn(B_DOWN) && lvl < SK_N && sdl.n) to = SK_N;
    if (btn(B_UP) && lvl >= SK_N) to = skLastOpen();
    if (to != lvl && skOpen(to)) {
      if (skShow(to)) lvl = to;
      else skShow(lvl = skLastOpen());           // the card went: the built-in levels only
      sfx(700, 15);
    }
    if (btn(B_OK)) { sfx(1200, 50); return lvl; }
    char b[24];
    bool sd = lvl >= SK_N;
    statusBar("SOKOBAN", curHigh, curHigh, C::BROWN);
    gfx.fill(0, TOP_H, SCR_W, SCR_H - TOP_H, GREY1);
    skDrawMap(skTile());
    if (sd) snprintf(b, sizeof(b), "SD %u / %u", lvl - SK_N + 1, sdl.n);
    else snprintf(b, sizeof(b), "LEVEL %u / %u", lvl + 1, SK_N);
    int16_t w = Gfx::width(b, F_TEXT) + 44;
    gfx.rbox((SCR_W - w) / 2, SK_Y1 - 2, w, 20, 6, sd ? rgb(0, 85, 102) : rgb(102, 51, 0));
    if (lvl > 0) gfx.triangle((SCR_W - w) / 2 + 8, SK_Y1 + 8, (SCR_W - w) / 2 + 18, SK_Y1 + 2, (SCR_W - w) / 2 + 18, SK_Y1 + 14, C::WHITE);
    if (skOpen(skAfter(lvl)))
      gfx.triangle((SCR_W + w) / 2 - 8, SK_Y1 + 8, (SCR_W + w) / 2 - 18, SK_Y1 + 2, (SCR_W + w) / 2 - 18, SK_Y1 + 14, C::WHITE);
    gfx.center(SK_Y1, b, C::WHITE);
    if (sd) {
      sdlTag(6, SK_Y1 + 2);
      uint16_t best = sdlProg(sdl.hash);
      if (best) { snprintf(b, sizeof(b), TR("best %u", "beste %u"), best); gfx.text(32, SK_Y1 + 4, b, C::LIME, F_SMALL); }
    } else if (lvl < curHigh) gfx.text(6, SK_Y1 + 4, "solved", C::LIME, F_SMALL);
    snprintf(b, sizeof(b), "%s = play", okKeys());
    gfx.right(SCR_W - 6, SK_Y1 + 4, b, C::LTGREY, F_SMALL);
    sdlNote(TOP_H + 3);
    gfx.send();
  }
  return SK_BACK;
}

static void skBar(uint16_t lvl) {
  char b[24];
  if (lvl >= SK_N) snprintf(b, sizeof(b), "SD %u", lvl - SK_N + 1);
  else snprintf(b, sizeof(b), "LEVEL %u", lvl + 1);
  statusBar(b, skMoves, curHigh, C::BROWN);
  gfx.fill(0, TOP_H, SCR_W, SCR_H - TOP_H, GREY1);
}

static void skPlay() {
  uint16_t lvl = skChoose(skLastLoad());
  while (lvl != SK_BACK) {
    bool sd = lvl >= SK_N;
    if (sd) { sdlFree(); skLoadText(sdl.line); }   // the level is in RAM now: the card may go
    else { skLastSave((uint8_t)lvl); skLoad((uint8_t)lvl); }
    int16_t t = skTile();
    btnClear();
    bool done = false;
    while (!done) {
      if (!poll()) return;
      static const uint8_t KEYS[4] = { B_UP, B_DOWN, B_LEFT, B_RIGHT };
      for (uint8_t d = 0; d < 4; d++)
        if (btn(KEYS[d])) {
          uint8_t r = skStep(d);
          if (r) { skAnimAt = gameMillis(); skAnimBox = r == 2; }
          sfx(r == 2 ? 450 : (r ? 700 : 200), r ? 12 : 30);
        }
      if (btn(B_OK) && skBack()) sfx(500, 20);
      skBar(lvl);
      skDrawMap(t);
      char b[40];
      snprintf(b, sizeof(b), "%u / %u on goal", skBoxes() - skBoxesLeft(), skBoxes());
      gfx.text(6, SK_Y1 + 2, b, C::ORANGE, F_TEXT);
      snprintf(b, sizeof(b), "%s = undo", okKeys());
      gfx.right(SCR_W - 6, SK_Y1 + 8, b, C::LTGREY, F_SMALL);
      gfx.send();
      done = skSolved();
    }
    uint8_t saved = SDL_SAME;
    if (sd) saved = sdlSave(skMoves, sdlFewer);       // the card's levels: progress on the card only
    else if (lvl + 1 > curHigh) { curHigh = lvl + 1; saveHigh(curGame, curHigh); }   // best = levels solved
    sfx(1400, 250);
    uint32_t t0 = gameMillis();
    btnClear();
    while (true) {
      if (!poll()) return;
      uint32_t at = gameMillis() - t0;
      if (at > 2500 || (at > 500 && btn(B_OK))) break;
      skBar(lvl);
      skDrawMap(t);
      if (at > SK_ANIM) {
        const char *msg = lvl + 1 == SK_N ? "ALL DONE!" : "SOLVED!";
        int16_t w = Gfx::width(msg, F_BOLD) + 30, h = saved == SDL_FAIL ? 72 : 56;
        const char *nb = TR("not saved: SD card", "nicht gespeichert: SD-Karte");
        if (saved == SDL_FAIL && Gfx::width(nb, F_SMALL) + 16 > w) w = Gfx::width(nb, F_SMALL) + 16;
        int16_t x = (SCR_W - w) / 2, y = 96;
        gfx.rbox(x + 3, y + 3, w, h, 8, C::BLACK);
        gfx.rbox(x, y, w, h, 8, C::DKGRN);
        gfx.rframe(x, y, w, h, 8, C::LIME);
        gfx.text(x + (w - Gfx::width(msg, F_BOLD)) / 2, y + 6, msg, C::WHITE, F_BOLD);
        char b[24];
        snprintf(b, sizeof(b), "%u moves", skMoves);
        gfx.text(x + (w - Gfx::width(b, F_TEXT)) / 2, y + 33, b, C::YELLOW, F_TEXT);
        if (saved == SDL_FAIL) gfx.text(x + (w - Gfx::width(nb, F_SMALL)) / 2, y + 56, nb, C::ORANGE, F_SMALL);
      }
      gfx.send();
    }
    if (sd) { if (sdlNext()) lvl = (uint16_t)(SK_N + sdl.at); else lvl = skChoose(lvl); }   // the card's next level, read now
    else if (lvl + 1 < SK_N) lvl++;                   // on to the next one
    else lvl = skChoose(lvl);
  }
}

void sokobanRun() {
  if (!skAlloc()) return;
  skPlay();
  skFree();
  sdlEnd();
}

static const char *const IC_SOKOBAN[12] = {
  "rrrrrrrrrrrr", "r..........r", "r.nnnnnn...r", "r.nyyyyn...r", "r.nynnyn...r", "r.nyyyyn...r",
  "r.nnnnnn...r", "r..........r", "r.......oo.r", "r......o..or", "r.......oo.r", "rrrrrrrrrrrr" };
