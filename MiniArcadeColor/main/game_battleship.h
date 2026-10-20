// MiniArcade Color: Battleship against the CPU. Part of console.cpp, which
// includes it. 8 x 8 sea, ships 4 3 3 2 2 that never touch (not even at a
// corner). Left, big: the enemy sea you shoot at (arrows aim, OK fires).
// Right: your own fleet and where the CPU has shot. A splash is a miss,
// fire a hit, a dark hull a sunk ship; dots mark water next to a sunk ship
// (no need to shoot there). Before the start UP mixes a new fleet, DOWN
// places the ships by hand (arrows move, OK sets it down, a quick double
// OK turns it), OK starts. A win scores (64 - shots) x 10.

#define BS_N     8
#define BS_CELLS 64
#define BS_SHIPS 5
#define BS_LX    22                    // big board (the enemy sea), top left and cell size
#define BS_LY    46
#define BS_LC    22
#define BS_RX    207                   // small board (own fleet)
#define BS_RY    46
#define BS_RC    13

static const uint8_t BS_LEN[BS_SHIPS] = { 4, 3, 3, 2, 2 };
enum { BS_UNKNOWN, BS_MISS, BS_HIT, BS_SUNK, BS_NEAR };   // BS_NEAR: water known without a shot
#define BS_HITBIT 0x80

// fleet: 0 = water, 1..5 = ship number; BS_HITBIT = shot at (water or ship)
static uint8_t bsMine[BS_CELLS], bsTheirs[BS_CELLS];   // theirs: the CPU's fleet
static uint8_t bsShot[BS_CELLS];                        // what we know of their sea
static uint8_t bsAiShot[BS_CELLS];                      // what the CPU knows of ours

static bool bsInside(int x, int y) { return x >= 0 && y >= 0 && x < BS_N && y < BS_N; }

// does a ship of length len fit at (x0, y0) without touching another one?
static bool bsFits(const uint8_t *f, uint8_t len, int x0, int y0, bool hor) {
  if (x0 < 0 || y0 < 0 || (hor ? x0 + len : x0 + 1) > BS_N || (hor ? y0 + 1 : y0 + len) > BS_N) return false;
  for (uint8_t k = 0; k < len; k++) {
    int x = x0 + (hor ? k : 0), y = y0 + (hor ? 0 : k);
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++)
        if (bsInside(x + dx, y + dy) && f[(y + dy) * BS_N + x + dx]) return false;
  }
  return true;
}

static void bsPut(uint8_t *f, uint8_t s, int x0, int y0, bool hor) {
  for (uint8_t k = 0; k < BS_LEN[s]; k++) f[(y0 + (hor ? 0 : k)) * BS_N + x0 + (hor ? k : 0)] = s + 1;
}

// random fleet, ships never touch (not even at a corner)
static void bsPlace(uint8_t *f) {
  for (;;) {
    memset(f, 0, BS_CELLS);
    bool ok = true;
    for (uint8_t s = 0; s < BS_SHIPS && ok; s++) {
      ok = false;
      for (uint8_t tries = 0; tries < 100 && !ok; tries++) {
        bool hor = random(2);
        int x0 = random(hor ? BS_N - BS_LEN[s] + 1 : BS_N), y0 = random(hor ? BS_N : BS_N - BS_LEN[s] + 1);
        if (!bsFits(f, BS_LEN[s], x0, y0, hor)) continue;
        bsPut(f, s, x0, y0, hor);
        ok = true;
      }
    }
    if (ok) return;
  }
}

// a shot at a fleet: BS_MISS, BS_HIT or BS_SUNK (0 = shot there before)
static uint8_t bsFire(uint8_t *f, uint8_t cell) {
  if (f[cell] & BS_HITBIT) return 0;
  f[cell] |= BS_HITBIT;
  uint8_t s = f[cell] & 0x7F;
  if (!s) return BS_MISS;
  for (uint8_t i = 0; i < BS_CELLS; i++) if ((f[i] & 0x7F) == s && !(f[i] & BS_HITBIT)) return BS_HIT;
  return BS_SUNK;
}

static bool bsAllSunk(const uint8_t *f) {
  for (uint8_t i = 0; i < BS_CELLS; i++) if ((f[i] & 0x7F) && !(f[i] & BS_HITBIT)) return false;
  return true;
}

static bool bsShipSunk(const uint8_t *f, uint8_t s) {    // ship number 1..5
  for (uint8_t i = 0; i < BS_CELLS; i++) if ((f[i] & 0x7F) == s && !(f[i] & BS_HITBIT)) return false;
  return true;
}

/* notes a result on a sea we shoot at; a sunk ship turns its line of hits
   to BS_SUNK and the water around it to BS_NEAR (ships never touch)    */
static void bsMark(uint8_t *sea, uint8_t cell, uint8_t res) {
  if (res != BS_SUNK) { sea[cell] = res; return; }
  uint8_t stack[BS_CELLS], n = 0;
  sea[cell] = BS_SUNK; stack[n++] = cell;
  while (n) {
    uint8_t c = stack[--n];
    int x = c % BS_N, y = c / BS_N;
    for (int dy = -1; dy <= 1; dy++)
      for (int dx = -1; dx <= 1; dx++) {
        if (!bsInside(x + dx, y + dy)) continue;
        uint8_t o = (y + dy) * BS_N + x + dx;
        if (sea[o] == BS_HIT && (!dx || !dy)) { sea[o] = BS_SUNK; stack[n++] = o; }
        else if (sea[o] == BS_UNKNOWN) sea[o] = BS_NEAR;
      }
  }
}

/* the CPU's shot: finish a ship it has hit, otherwise search on a
   checkerboard (every ship covers at least one black square)          */
static uint8_t bsAiPick(const uint8_t *sea) {
  uint8_t hits[BS_CELLS], nh = 0;
  for (uint8_t i = 0; i < BS_CELLS; i++) if (sea[i] == BS_HIT) hits[nh++] = i;
  uint8_t cand[BS_CELLS], n = 0;
  if (nh) {
    bool line = nh > 1, hor = line && hits[0] / BS_N == hits[1] / BS_N;
    for (uint8_t k = 0; k < nh; k++) {
      int x = hits[k] % BS_N, y = hits[k] / BS_N;
      static const int8_t DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
      for (uint8_t d = 0; d < 4; d++) {
        if (line && (hor ? DY[d] : DX[d])) continue;   // stay on the line of the hits
        if (bsInside(x + DX[d], y + DY[d]) && sea[(y + DY[d]) * BS_N + x + DX[d]] == BS_UNKNOWN)
          cand[n++] = (y + DY[d]) * BS_N + x + DX[d];
      }
    }
  }
  if (!n) for (uint8_t i = 0; i < BS_CELLS; i++) if (sea[i] == BS_UNKNOWN && ((i % BS_N + i / BS_N) & 1) == 0) cand[n++] = i;
  if (!n) for (uint8_t i = 0; i < BS_CELLS; i++) if (sea[i] == BS_UNKNOWN) cand[n++] = i;
  return n ? cand[random(n)] : 0;
}

// ---------------- screen ----------------
/* What the screen shows. A shot is seen in two parts: first the shell
   coming in (aim), then the splash or the fire (fx).                    */
static struct {
  uint8_t shots;
  int8_t  aimCell, fxCell;             // -1 = none
  bool    aimMine, fxMine;             // on our own fleet (the CPU's shot)?
  bool    myGo;                        // the player shoots next ...
  uint8_t cur;                         // ... where the cursor is
  uint8_t fxRes;
  uint32_t aimAt, fxAt;
} bsV;

static void bsWater(int16_t x, int16_t y, uint8_t cs, uint8_t i, uint32_t now) {
  gfx.fill(x, y, cs, cs, rgb(0, 60, 150));
  uint8_t ph = (uint8_t)((i * 7 + now / 500) % 4);              // slow waves
  if (ph < 2 && cs > 10) {
    int16_t wy = y + cs / 3 + ph * cs / 3, wx = x + 2 + (i % 3) * cs / 6;
    gfx.hline(wx, wy, cs / 3, rgb(51, 110, 200));
    gfx.hline(wx + cs / 3, wy - 1, cs / 4, rgb(51, 110, 200));
  }
}

// a piece of hull; the sides towards the same ship reach over into its cell
static void bsHull(int16_t x, int16_t y, uint8_t cs, bool l, bool r, bool u, bool d, col c) {
  int16_t in = cs / 5 + 1;
  int16_t x0 = l ? x - in : x + in, x1 = r ? x + cs + in : x + cs - in;
  int16_t y0 = u ? y - in : y + in, y1 = d ? y + cs + in : y + cs - in;
  gfx.rbox(x0, y0, x1 - x0, y1 - y0, in + 1, c);
}

static void bsFlame(int16_t cx, int16_t cy, uint8_t cs, uint32_t now, uint8_t i) {
  int16_t r = cs / 3;
  bool f = ((now / 180) + i) % 2;
  gfx.disc(cx, cy, r + 1, C::DKRED);
  gfx.triangle(cx - r, cy + 1, cx + r, cy + 1, cx + (f ? 1 : -1), cy - r - 3, C::ORANGE);
  gfx.disc(cx, cy + 1, r - 1, C::ORANGE);
  gfx.disc(cx, cy + 2, r / 2, C::YELLOW);
}

static void bsSplash(int16_t cx, int16_t cy, uint8_t cs) {
  gfx.circle(cx, cy, cs / 3, C::WHITE);
  gfx.disc(cx, cy, cs / 8 + 1, C::WHITE);
}

// cells that belong to the same ship as cell i (the same number / both sunk)
static bool bsSame(const uint8_t *a, bool fleet, uint8_t i, int dx, int dy) {
  int x = i % BS_N + dx, y = i / BS_N + dy;
  if (!bsInside(x, y)) return false;
  uint8_t o = a[y * BS_N + x];
  return fleet ? (o & 0x7F) == (a[i] & 0x7F) : o == BS_SUNK;
}

/* A fleet (own ships, the shots on them) or a sea we shoot at; reveal =
   the fleet under that sea, its ships still afloat shown at the end.   */
static void bsBoard(int16_t x0, int16_t y0, uint8_t cs, const uint8_t *a, bool fleet, const uint8_t *reveal) {
  uint32_t now = gameMillis();
  gfx.fill(x0 - 2, y0 - 2, BS_N * cs + 4, BS_N * cs + 4, rgb(0, 30, 80));
  for (uint8_t i = 0; i < BS_CELLS; i++) bsWater(x0 + (i % BS_N) * cs, y0 + (i / BS_N) * cs, cs, i, now);
  for (uint8_t i = 1; i < BS_N; i++) {                          // grid
    gfx.vline(x0 + i * cs, y0, BS_N * cs, rgb(0, 40, 110));
    gfx.hline(x0, y0 + i * cs, BS_N * cs, rgb(0, 40, 110));
  }
  for (uint8_t i = 0; i < BS_CELLS; i++) {                      // hulls first, they reach over
    int16_t x = x0 + (i % BS_N) * cs, y = y0 + (i / BS_N) * cs;
    if (fleet && (a[i] & 0x7F)) {
      bool gone = bsShipSunk(a, a[i] & 0x7F);
      bsHull(x, y, cs, bsSame(a, true, i, -1, 0), bsSame(a, true, i, 1, 0), bsSame(a, true, i, 0, -1),
             bsSame(a, true, i, 0, 1), gone ? C::DKGREY : C::LTGREY);
    } else if (!fleet && a[i] == BS_SUNK) {
      bsHull(x, y, cs, bsSame(a, false, i, -1, 0), bsSame(a, false, i, 1, 0), bsSame(a, false, i, 0, -1),
             bsSame(a, false, i, 0, 1), C::DKGREY);
    } else if (!fleet && reveal && (reveal[i] & 0x7F) && !(reveal[i] & BS_HITBIT)) {
      gfx.rframe(x + 3, y + 3, cs - 6, cs - 6, 3, C::LTGREY);   // a ship that was never found
    }
  }
  for (uint8_t i = 0; i < BS_CELLS; i++) {
    int16_t x = x0 + (i % BS_N) * cs, y = y0 + (i / BS_N) * cs, cx = x + cs / 2, cy = y + cs / 2;
    uint8_t what;
    if (fleet) what = !(a[i] & BS_HITBIT) ? BS_UNKNOWN : ((a[i] & 0x7F) ? BS_HIT : BS_MISS);
    else what = a[i];
    switch (what) {
    case BS_MISS: bsSplash(cx, cy, cs); break;
    case BS_NEAR: gfx.disc(cx, cy, cs / 10, rgb(102, 153, 204)); break;
    case BS_HIT:  bsFlame(cx, cy, cs, now, i); break;
    case BS_SUNK:
      gfx.line(cx - cs / 4, cy - cs / 4, cx + cs / 4, cy + cs / 4, C::RED);
      gfx.line(cx + cs / 4, cy - cs / 4, cx - cs / 4, cy + cs / 4, C::RED);
      break;
    }
  }
  if (fleet)                                                    // sunk own ships: their wreck burns
    for (uint8_t i = 0; i < BS_CELLS; i++)
      if ((a[i] & 0x7F) && (a[i] & BS_HITBIT) && bsShipSunk(a, a[i] & 0x7F)) {
        int16_t cx = x0 + (i % BS_N) * cs + cs / 2, cy = y0 + (i / BS_N) * cs + cs / 2;
        gfx.line(cx - cs / 4, cy - cs / 4, cx + cs / 4, cy + cs / 4, C::RED);
        gfx.line(cx + cs / 4, cy - cs / 4, cx - cs / 4, cy + cs / 4, C::RED);
      }
}

static void bsLabels(int16_t x0, int16_t y0, uint8_t cs) {      // A..H on top, 1..8 on the left
  char b[2] = { 0, 0 };
  for (uint8_t i = 0; i < BS_N; i++) {
    b[0] = (char)('A' + i);
    gfx.text(x0 + i * cs + (cs - 5) / 2, y0 - 12, b, C::LTGREY, F_SMALL);
    b[0] = (char)('1' + i);
    gfx.text(x0 - 10, y0 + i * cs + (cs - 9) / 2, b, C::LTGREY, F_SMALL);
  }
}

static void bsCursor(int16_t x0, int16_t y0, uint8_t cs, uint8_t cell, col c) {
  int16_t x = x0 + (cell % BS_N) * cs, y = y0 + (cell / BS_N) * cs;
  gfx.frame(x - 1, y - 1, cs + 2, cs + 2, c, 2);
  gfx.hline(x + cs / 2 - 3, y + cs / 2, 7, c);
  gfx.vline(x + cs / 2, y + cs / 2 - 3, 7, c);
}

// the shot on its way (a shell growing out of the sky), then the splash or blast
static void bsEffects(uint32_t now) {
  if (bsV.aimCell >= 0) {
    int16_t x0 = bsV.aimMine ? BS_RX : BS_LX, y0 = bsV.aimMine ? BS_RY : BS_LY;
    uint8_t cs = bsV.aimMine ? BS_RC : BS_LC;
    int16_t cx = x0 + (bsV.aimCell % BS_N) * cs + cs / 2, cy = y0 + (bsV.aimCell / BS_N) * cs + cs / 2;
    uint32_t t = now - bsV.aimAt;                               // 0..BS_AIM_MS
    int16_t h = (int16_t)(40 - (int32_t)t * 40 / 450);
    if (h < 0) h = 0;
    gfx.circle(cx, cy, cs / 2 + h / 4, C::RED);
    gfx.disc(cx, cy - h, 3, C::DKGREY);
    gfx.disc(cx - 1, cy - h - 1, 1, C::LTGREY);
  }
  if (bsV.fxCell >= 0 && now - bsV.fxAt < 700) {
    int16_t x0 = bsV.fxMine ? BS_RX : BS_LX, y0 = bsV.fxMine ? BS_RY : BS_LY;
    uint8_t cs = bsV.fxMine ? BS_RC : BS_LC;
    int16_t cx = x0 + (bsV.fxCell % BS_N) * cs + cs / 2, cy = y0 + (bsV.fxCell / BS_N) * cs + cs / 2;
    int16_t r = (int16_t)(3 + (now - bsV.fxAt) * cs / 700);
    if (bsV.fxRes == BS_MISS) {
      gfx.circle(cx, cy, r, C::WHITE);
      gfx.circle(cx, cy, r + 3, rgb(153, 204, 255));
    } else {
      gfx.circle(cx, cy, r, C::YELLOW);
      gfx.circle(cx, cy, r + 2, C::ORANGE);
      gfx.circle(cx, cy, r + 4, C::RED);
    }
  }
}

// the panel under the own fleet: two lines of news, the enemy ships left
static void bsPanel(const char *l1, const char *l2, col c1) {
  gfx.rbox(BS_RX - 4, 158, 112, 78, 8, rgb(0, 20, 60));
  if (l1) gfx.text(BS_RX + 52 - Gfx::width(l1, F_TEXT) / 2, 164, l1, c1, F_TEXT);
  if (l2) gfx.text(BS_RX + 52 - Gfx::width(l2, F_SMALL) / 2, 186, l2, C::LTGREY, F_SMALL);
  gfx.text(BS_RX, 202, "enemy ships:", C::GREY, F_SMALL);
  int16_t x = BS_RX;
  for (uint8_t s = 0; s < BS_SHIPS; s++) {
    bool gone = bsShipSunk(bsTheirs, s + 1);
    int16_t w = BS_LEN[s] * 5;
    if (x + w > BS_RX + 106) break;
    gfx.rbox(x, 216, w, 7, 3, gone ? C::DKGREY : C::LTGREY);
    if (gone) gfx.line(x, 216, x + w - 1, 222, C::RED);
    x += w + 4;
  }
}

static void bsDraw(int16_t cursor, const char *l1, const char *l2, col c1, bool reveal) {
  uint16_t pot = (BS_CELLS - bsV.shots) * 10;
  statusBar("BATTLESHIP", pot, curHigh, rgb(0, 102, 204));
  gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(0, 30, 70), rgb(0, 5, 25));
  bsLabels(BS_LX, BS_LY, BS_LC);
  bsBoard(BS_LX, BS_LY, BS_LC, bsShot, false, reveal ? bsTheirs : NULL);
  gfx.text(BS_RX, BS_RY - 12, "your fleet", C::LTGREY, F_SMALL);
  bsBoard(BS_RX, BS_RY, BS_RC, bsMine, true, NULL);
  uint32_t now = gameMillis();
  if (cursor >= 0 && (now / 250) % 4) bsCursor(BS_LX, BS_LY, BS_LC, (uint8_t)cursor, C::YELLOW);
  bsEffects(now);
  bsPanel(l1, l2, c1);
}

// a shot: the shell comes in for a moment, then the result; false = left the game
#define BS_AIM_MS 450
static bool bsShoot(bool onMine, uint8_t cell, const char *l1, const char *l2) {
  bsV.aimCell = (int8_t)cell; bsV.aimMine = onMine; bsV.aimAt = gameMillis();
  sfx(900, 30);
  while (gameMillis() - bsV.aimAt < BS_AIM_MS) {
    if (!poll()) return false;
    bsDraw(-1, l1, l2, C::WHITE, false);
    gfx.send();
  }
  bsV.aimCell = -1;
  return true;
}

// ---------------- set-up ----------------
// the own fleet big on the left, the keys on the right
static void bsSetupDraw(const uint8_t *show, const char *const *lines, uint8_t n, bool bad) {
  statusBar("BATTLESHIP", BS_CELLS * 10, curHigh, rgb(0, 102, 204));
  gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(0, 30, 70), rgb(0, 5, 25));
  bsLabels(BS_LX, BS_LY, BS_LC);
  bsBoard(BS_LX, BS_LY, BS_LC, show, true, NULL);
  gfx.rbox(BS_RX - 4, BS_LY - 4, 112, 180, 8, rgb(0, 20, 60));
  gfx.text(BS_RX + 4, BS_LY + 4, "YOUR FLEET", C::CYAN, F_SMALL);
  for (uint8_t i = 0; i < n; i++)
    gfx.text(BS_RX + 4, BS_LY + 26 + i * 22, lines[i], bad && i == 1 ? C::RED : C::WHITE, i == 0 ? F_TEXT : F_SMALL);
}

/* placing the ships by hand, the biggest first: arrows move it, OK sets
   it down (if it fits), a quick double OK turns it. false = left.      */
static bool bsManual() {
  memset(bsMine, 0, BS_CELLS);
  int x = 0, y = 0;
  bool hor = true, tapWait = false;
  uint32_t tapAt = 0;
  uint8_t s = 0;
  btnClear();
  while (s < BS_SHIPS) {
    if (!poll()) return false;
    uint8_t len = BS_LEN[s];
    if (btn(B_LEFT))  x--;
    if (btn(B_RIGHT)) x++;
    if (btn(B_UP))    y--;
    if (btn(B_DOWN))  y++;
    bool place = false;
    if (btn(B_OK)) {
      if (tapWait) { tapWait = false; hor = !hor; sfx(900, 20); }          // second tap: turn
      else { tapWait = true; tapAt = millis(); }
    }
    if (tapWait && millis() - tapAt > 280) { tapWait = false; place = true; }
    int w = hor ? len : 1, h = hor ? 1 : len;                               // stay on the board
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > BS_N - w) x = BS_N - w;
    if (y > BS_N - h) y = BS_N - h;
    bool fits = bsFits(bsMine, len, x, y, hor);
    if (place) {
      if (fits) { bsPut(bsMine, s, x, y, hor); s++; sfx(1000, 40); }
      else sfx(200, 120);                                                   // touches another ship
      continue;
    }
    uint8_t show[BS_CELLS];                                                 // the fleet plus this ship
    memcpy(show, bsMine, BS_CELLS);
    if (fits || (millis() / 150) % 2) bsPut(show, s, x, y, hor);
    char b[10];
    snprintf(b, sizeof(b), "ship %u", len);
    const char *lines[5] = { b, fits ? "OK = set" : "no room", "2x OK = turn", "arrows = move", NULL };
    bsSetupDraw(show, lines, 4, !fits);
    gfx.send();
  }
  sfx(1400, 80);
  return true;
}

// fleet set-up: UP mixes a new one, DOWN places by hand, OK is ready. false = left
static bool bsSetup() {
  bsPlace(bsMine);
  btnClear();
  while (poll()) {
    if (btn(B_UP)) { bsPlace(bsMine); sfx(700, 15); }
    if (btn(B_DOWN)) {
      sfx(700, 15);
      if (!bsManual()) return false;
      btnClear();
    }
    if (btn(B_OK)) { sfx(1200, 50); return true; }
    static const char *const LINES[4] = { "ready?", "UP = mix", "DOWN = own", "OK = go" };
    bsSetupDraw(bsMine, LINES, 4, false);
    gfx.send();
  }
  return false;
}

// cursor keys on the big board; returns the cell when OK is pressed on a new one
static int16_t bsAim(uint8_t &cur, const uint8_t *sea) {
  if (btn(B_LEFT))  { cur = cur % BS_N ? cur - 1 : cur + BS_N - 1; sfx(700, 8); }
  if (btn(B_RIGHT)) { cur = cur % BS_N < BS_N - 1 ? cur + 1 : cur - BS_N + 1; sfx(700, 8); }
  if (btn(B_UP))    { cur = cur >= BS_N ? cur - BS_N : cur + BS_CELLS - BS_N; sfx(700, 8); }
  if (btn(B_DOWN))  { cur = cur < BS_CELLS - BS_N ? cur + BS_N : cur - BS_CELLS + BS_N; sfx(700, 8); }
  if (!btn(B_OK)) return -1;
  if (sea[cur] != BS_UNKNOWN) { sfx(250, 40); return -1; }
  return cur;
}

void battleshipRun() {
  bool again = true;
  while (again) {
    again = false;
    memset(bsShot, 0, sizeof(bsShot));
    memset(bsAiShot, 0, sizeof(bsAiShot));
    bsV.shots = 0; bsV.aimCell = bsV.fxCell = -1;
    if (!bsSetup()) return;
    bsPlace(bsTheirs);
    uint8_t &cur = bsV.cur;
    cur = 27;
    bool &mine = bsV.myGo;
    mine = true;
    uint32_t cpuAt = 0;
    const char *msg = "your go", *sub = "OK = fire";
    col mc = C::WHITE;
    btnClear();
    while (poll()) {
      if (mine) {
        int16_t c = bsAim(cur, bsShot);
        if (c >= 0) {
          if (!bsShoot(false, (uint8_t)c, "fire!", NULL)) return;
          uint8_t r = bsFire(bsTheirs, (uint8_t)c);
          bsMark(bsShot, (uint8_t)c, r);
          bsV.shots++;
          bsV.fxCell = (int8_t)c; bsV.fxMine = false; bsV.fxRes = r; bsV.fxAt = gameMillis();
          sfx(r == BS_MISS ? 300 : 1000, r == BS_SUNK ? 250 : 60);
          msg = r == BS_MISS ? "miss" : (r == BS_HIT ? "hit!" : "sunk!");
          mc = r == BS_MISS ? C::LTGREY : (r == BS_HIT ? C::ORANGE : C::RED);
          sub = "CPU's turn";
          if (bsAllSunk(bsTheirs)) {
            uint16_t score = (BS_CELLS - bsV.shots) * 10;
            uint32_t t0 = gameMillis();
            btnClear();
            while (gameMillis() - t0 < 2500) {
              if (!poll()) return;
              if (btn(B_OK) && gameMillis() - t0 > 600) break;
              bsDraw(-1, "YOU WIN!", "all sunk", C::YELLOW, false);
              gfx.send();
            }
            again = gameOver(score);
            break;
          }
          mine = false;
          cpuAt = gameMillis() + 1000;                             // calm: the result stays a moment
        }
      } else if (gameMillis() >= cpuAt) {                          // the CPU shoots
        uint8_t c = bsAiPick(bsAiShot);
        if (!bsShoot(true, c, "CPU fires", NULL)) return;
        uint8_t r = bsFire(bsMine, c);
        bsMark(bsAiShot, c, r);
        bsV.fxCell = (int8_t)c; bsV.fxMine = true; bsV.fxRes = r; bsV.fxAt = gameMillis();
        sfx(r == BS_MISS ? 250 : 180, r == BS_MISS ? 40 : 200);
        if (bsAllSunk(bsMine)) {
          uint32_t t0 = gameMillis();
          btnClear();
          while (gameMillis() - t0 < 3000) {                        // show where its ships were
            if (!poll()) return;
            if (btn(B_OK) && gameMillis() - t0 > 600) break;
            bsDraw(-1, "CPU WINS", "fleet lost", C::RED, true);
            gfx.send();
          }
          bsV.shots = BS_CELLS;                                   // nothing to score
          again = gameOver(0);
          break;
        }
        mine = true;
        msg = r == BS_MISS ? "your go" : (r == BS_HIT ? "ouch!" : "sunk us");
        mc = r == BS_MISS ? C::WHITE : C::RED;
        sub = r == BS_MISS ? "CPU missed" : "OK = fire";
        btnClear();
      }
      bsDraw(mine ? cur : -1, mine ? msg : "CPU...", mine ? sub : msg, mine ? mc : C::LTGREY, false);
      gfx.send();
    }
  }
}

static const char *const IC_BATTLESHIP[12] = {
  ".....r......", ".....srr....", ".....s......", "....sss.....", "..ssssss.o..", "sssssssssyo.",
  ".sskssksss..", "..sssssss...", "bbbbbbbbbbbb", "bcbbbbcbbbbb", "bbbbbcbbbbcb", "bbbbbbbbbbbb" };
