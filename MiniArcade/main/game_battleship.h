// MiniArcade: Battleship. Part of MiniArcade.ino, which includes it -
// not compiled on its own. The online version is in multiplayer.h.

// =========================================================
//  BATTLESHIP   (8 x 8, ships 4 3 3 2 2 that do not touch)
// =========================================================
/* Left: the sea you shoot at (cursor with the arrows, OK fires).
   Right: your own fleet and where the other side has hit it.
   Before the start UP shuffles your fleet and OK is ready.            */
#define BS_N     8
#define BS_CELLS 64
#define BS_SHIPS 5
#define BS_C     5            // cell size in px
#define BS_LX    2            // left board
#define BS_RX   (SCR_W - BS_N * BS_C - 2)
#define BS_Y    (TOP_H + 5)

static const uint8_t BS_LEN[BS_SHIPS] = { 4, 3, 3, 2, 2 };
enum { BS_UNKNOWN, BS_MISS, BS_HIT, BS_SUNK };
#define BS_HITBIT 0x80

// fleet: 0 = water, 1..5 = ship number; BS_HITBIT = shot at (water or ship)
static uint8_t bsMine[BS_CELLS], bsTheirs[BS_CELLS];   // theirs: the CPU's fleet
static uint8_t bsShot[BS_CELLS];                        // what we know of their sea
static uint8_t bsAiShot[BS_CELLS];                      // what the CPU knows of ours

static bool bsInside(int x, int y) { return x >= 0 && y >= 0 && x < BS_N && y < BS_N; }

// random fleet, ships never touch (not even at a corner)
void bsPlace(uint8_t *f) {
  for (;;) {
    memset(f, 0, BS_CELLS);
    bool ok = true;
    for (uint8_t s = 0; s < BS_SHIPS && ok; s++) {
      ok = false;
      for (uint8_t tries = 0; tries < 100 && !ok; tries++) {
        bool hor = random(2);
        int x0 = random(hor ? BS_N - BS_LEN[s] + 1 : BS_N), y0 = random(hor ? BS_N : BS_N - BS_LEN[s] + 1);
        bool free_ = true;
        for (uint8_t k = 0; k < BS_LEN[s] && free_; k++) {
          int x = x0 + (hor ? k : 0), y = y0 + (hor ? 0 : k);
          for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
              if (bsInside(x + dx, y + dy) && f[(y + dy) * BS_N + x + dx]) free_ = false;
        }
        if (!free_) continue;
        for (uint8_t k = 0; k < BS_LEN[s]; k++) f[(y0 + (hor ? 0 : k)) * BS_N + x0 + (hor ? k : 0)] = s + 1;
        ok = true;
      }
    }
    if (ok) return;
  }
}

// a shot at a fleet: BS_MISS, BS_HIT or BS_SUNK (0 = shot there before)
uint8_t bsFire(uint8_t *f, uint8_t cell) {
  if (f[cell] & BS_HITBIT) return 0;
  f[cell] |= BS_HITBIT;
  uint8_t s = f[cell] & 0x7F;
  if (!s) return BS_MISS;
  for (uint8_t i = 0; i < BS_CELLS; i++) if ((f[i] & 0x7F) == s && !(f[i] & BS_HITBIT)) return BS_HIT;
  return BS_SUNK;
}

bool bsAllSunk(const uint8_t *f) {
  for (uint8_t i = 0; i < BS_CELLS; i++) if ((f[i] & 0x7F) && !(f[i] & BS_HITBIT)) return false;
  return true;
}

/* notes a result on a sea we shoot at; a sunk ship turns its line of hits
   to BS_SUNK and the water around it to misses (ships never touch)     */
void bsMark(uint8_t *sea, uint8_t cell, uint8_t res) {
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
        else if (sea[o] == BS_UNKNOWN) sea[o] = BS_MISS;
      }
  }
}

/* the CPU's shot: finish a ship it has hit, otherwise search on a
   checkerboard (every ship covers at least one black square)          */
uint8_t bsAiPick(const uint8_t *sea) {
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
static void bsCell(int16_t px, int16_t py, uint8_t what, bool ship) {
  if (ship) oled.drawFrame(px, py, 4, 4);
  switch (what) {
  case BS_MISS: oled.drawPixel(px + 1, py + 1); break;
  case BS_HIT:  oled.drawLine(px, py, px + 3, py + 3); oled.drawLine(px + 3, py, px, py + 3); break;
  case BS_SUNK: oled.drawBox(px, py, 4, 4); break;
  }
}

/* sea: our shots (left), fleet: ours with their hits (right)          */
void bsDraw(const uint8_t *sea, const uint8_t *fleet, int16_t cursor, const char *l1, const char *l2) {
  oled.drawFrame(BS_LX - 1, BS_Y - 1, BS_N * BS_C + 1, BS_N * BS_C + 1);
  oled.drawFrame(BS_RX - 1, BS_Y - 1, BS_N * BS_C + 1, BS_N * BS_C + 1);
  for (uint8_t i = 0; i < BS_CELLS; i++) {
    int16_t x = (i % BS_N) * BS_C, y = BS_Y + (i / BS_N) * BS_C;
    if (sea) bsCell(BS_LX + x, y, sea[i], false);
    uint8_t f = fleet[i];
    uint8_t what = !(f & BS_HITBIT) ? BS_UNKNOWN : ((f & 0x7F) ? BS_HIT : BS_MISS);
    if (what == BS_HIT) {                           // a whole ship down: solid
      bool gone = true;
      for (uint8_t j = 0; j < BS_CELLS; j++) if ((fleet[j] & 0x7F) == (f & 0x7F) && !(fleet[j] & BS_HITBIT)) gone = false;
      if (gone) what = BS_SUNK;
    }
    bsCell(BS_RX + x, y, what, f & 0x7F);
  }
  if (cursor >= 0 && (millis() / 200) % 4)
    oled.drawFrame(BS_LX + (cursor % BS_N) * BS_C - 1, BS_Y + (cursor / BS_N) * BS_C - 1, BS_C + 1, BS_C + 1);
  int16_t mx = BS_LX + BS_N * BS_C + 3, mw = BS_RX - mx - 2;
  if (l1) oled.drawStr(mx + (mw - oled.getStrWidth(l1)) / 2 + 1, BS_Y + 15, l1);
  if (l2) oled.drawStr(mx + (mw - oled.getStrWidth(l2)) / 2 + 1, BS_Y + 25, l2);
  oled.sendBuffer();
}

static void bsBar(const char *right) {
  oled.setFont(FONT_B);
  oled.drawStr(2, 12, "BATTLESHIP");
  oled.setFont(FONT);
  if (right) rightStr(12, right);
  oled.drawHLine(0, TOP_H - 1, SCR_W);
}

// fleet set-up: UP shuffles, OK is ready. false = left with a long OK
bool bsSetup(const char *right) {
  bsPlace(bsMine);
  btnClear();
  while (poll()) {
    if (btn(B_UP)) { bsPlace(bsMine); sfx(700, 15); }
    if (btn(B_OK)) { sfx(1200, 50); return true; }
    bsBar(right);
    bsDraw(NULL, bsMine, -1, "UP=new", "OK=go");
  }
  return false;
}

// cursor keys on the left board; returns the cell when OK is pressed on a new one
int16_t bsAim(uint8_t &cur, const uint8_t *sea) {
  if (btn(B_LEFT))  cur = cur % BS_N ? cur - 1 : cur + BS_N - 1;
  if (btn(B_RIGHT)) cur = cur % BS_N < BS_N - 1 ? cur + 1 : cur - BS_N + 1;
  if (btn(B_UP))    cur = cur >= BS_N ? cur - BS_N : cur + BS_CELLS - BS_N;
  if (btn(B_DOWN))  cur = cur < BS_CELLS - BS_N ? cur + BS_N : cur - BS_CELLS + BS_N;
  if (!btn(B_OK)) return -1;
  if (sea[cur] != BS_UNKNOWN) { sfx(250, 40); return -1; }
  return cur;
}

void battleshipRun() {
  bool again = true;
  while (again) {
    again = false;
    if (!bsSetup(NULL)) return;
    bsPlace(bsTheirs);
    memset(bsShot, 0, sizeof(bsShot));
    memset(bsAiShot, 0, sizeof(bsAiShot));
    uint8_t cur = 27, shots = 0;
    bool mine = true;
    uint32_t cpuAt = 0;
    const char *msg = "your go";
    btnClear();
    while (poll()) {
      if (mine) {
        int16_t c = bsAim(cur, bsShot);
        if (c >= 0) {
          uint8_t r = bsFire(bsTheirs, c);
          bsMark(bsShot, c, r);
          shots++;
          sfx(r == BS_MISS ? 300 : 1000, r == BS_SUNK ? 250 : 60);
          msg = r == BS_MISS ? "miss" : (r == BS_HIT ? "hit!" : "sunk!");
          if (bsAllSunk(bsTheirs)) {
            uint16_t score = (BS_CELLS - shots) * 10;
            for (uint8_t i = 0; i < 90 && poll(); i++) { bsBar(NULL); bsDraw(bsShot, bsMine, -1, "YOU", "WIN!"); }
            again = gameOver(score);
            break;
          }
          mine = false;
          cpuAt = gameMillis() + 700;
        }
      } else if (gameMillis() >= cpuAt) {           // the CPU shoots
        uint8_t c = bsAiPick(bsAiShot);
        uint8_t r = bsFire(bsMine, c);
        bsMark(bsAiShot, c, r);
        sfx(r == BS_MISS ? 250 : 180, r == BS_MISS ? 40 : 200);
        if (bsAllSunk(bsMine)) {
          for (uint8_t i = 0; i < 90 && poll(); i++) {    // show where its ships were
            bsBar(NULL);
            uint8_t reveal[BS_CELLS];
            for (uint8_t k = 0; k < BS_CELLS; k++) reveal[k] = bsShot[k] ? bsShot[k] : ((bsTheirs[k] & 0x7F) ? BS_HIT : 0);
            bsDraw(reveal, bsMine, -1, "CPU", "WINS");
          }
          again = gameOver(0);
          break;
        }
        mine = true;
        msg = r == BS_MISS ? "your go" : (r == BS_HIT ? "ouch!" : "sunk us");
      }
      char b[12];
      snprintf(b, sizeof(b), "%u shots", shots);
      bsBar(b);
      bsDraw(bsShot, bsMine, mine ? cur : -1, mine ? msg : "CPU...", mine ? NULL : msg);
    }
  }
}
