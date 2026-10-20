// MiniArcade Color: Jump & Run. Part of console.cpp, which includes it.
// An endless level, made column by column ahead of the screen. LEFT/RIGHT
// run, UP or OK jumps (let go early for a small jump). Jump on the slimes
// to squash them (50), collect coins (10), do not fall into the gaps;
// one point per tile of distance. Three lives. The level only ever asks
// for jumps the physics can do (JMP_GAP / JMP_UP, checked by "jrfair").

#define JMP_T     20                   // tile size
#define JMP_RING  32                   // columns kept
#define JMP_Q     32                   // positions in 1/32 px
#define JMP_TICK  20                   // ms per step
#define JMP_G     25                   // gravity per step
#define JMP_JUMP  300                  // jump speed
#define JMP_RUN   110                  // top running speed (about 170 px/s)
#define JMP_ACC   9                    // speeding up / slowing down per step
#define JMP_FALL  350                  // fastest fall
#define JMP_GAP   3                    // widest gap in tiles
#define JMP_UP    2                    // highest step up in tiles
#define JMP_EN    4
#define JMP_PW    12                   // the runner
#define JMP_PH    18
#define JMP_EW    18                   // a slime
#define JMP_EH    12

static uint8_t  jmpH[JMP_RING];        // ground height in tiles, 0 = gap
static uint8_t  jmpP[JMP_RING];        // floating platform height, 0 = none
static uint8_t  jmpC[JMP_RING];        // coin height above ground, 0 = none
static uint32_t jmpMade;               // columns made so far
static uint8_t  jmpCur, jmpLeft, jmpMode;   // generator: height, columns left in this piece, piece type
static uint8_t  jmpDiff;               // grows with the distance

struct JmpEn { bool on; int32_t x; int16_t y; int32_t x0, x1; int8_t dir; };
static JmpEn jmpE[JMP_EN];

static uint8_t jmpHeight(int32_t c) { return c < 0 ? 1 : jmpH[c % JMP_RING]; }

// ground top in px (screen y) for a height in tiles
static int16_t jmpTop(uint8_t h) { return (int16_t)(SCR_H - h * JMP_T); }

static void jmpEnemy(uint32_t c0, uint8_t len, uint8_t h) {
  for (uint8_t i = 0; i < JMP_EN; i++) {
    if (jmpE[i].on) continue;
    JmpEn &e = jmpE[i];
    e.on = true;
    e.x0 = (int32_t)c0 * JMP_T * JMP_Q;
    e.x1 = (int32_t)(c0 + len) * JMP_T * JMP_Q - JMP_EW * JMP_Q;
    e.x = e.x1; e.dir = -1;
    e.y = (int16_t)(jmpTop(h) - JMP_EH);
    return;
  }
}

// one more column at the right end of the world
static void jmpMake() {
  uint32_t c = jmpMade++;
  uint8_t i = c % JMP_RING;
  jmpP[i] = jmpC[i] = 0;
  if (!jmpLeft) {                                  // start the next piece
    uint8_t r = (uint8_t)random(10);
    if (c < 12 || jmpMode == 1 || jmpMode == 3 || r < 4) {     // flat ground (always after a gap)
      jmpMode = 0; jmpLeft = (uint8_t)(3 + random(6));
      if (c >= 12 && r < 2 && jmpCur < 3) jmpCur++;
      else if (c >= 12 && r < 4 && jmpCur > 1) jmpCur--;
      if (c >= 16 && jmpLeft >= 4 && random(3) < 1 + (jmpDiff > 3)) jmpEnemy(c + 1, jmpLeft - 1, jmpCur);
    } else if (r < 7) {                            // a gap
      jmpMode = 1;
      uint8_t w = (uint8_t)(1 + random(jmpDiff < 2 ? 2 : 3));
      jmpLeft = w > JMP_GAP ? JMP_GAP : w;
    } else if (r < 9) {                            // steps up
      jmpMode = 2; jmpLeft = (uint8_t)(2 + random(3));
      jmpCur += (uint8_t)(1 + (jmpCur == 1 && random(2)));   // one or two tiles: both within JMP_UP
      if (jmpCur > 3) jmpCur = 3;
    } else {                                       // a wide gap with a platform in the middle
      jmpMode = 3; jmpLeft = 5;
    }
  }
  jmpLeft--;
  switch (jmpMode) {
  case 1: jmpH[i] = 0; break;
  case 3:
    jmpH[i] = 0;
    if (jmpLeft == 2 || jmpLeft == 1) jmpP[i] = jmpCur + 1 > 4 ? 4 : jmpCur + 1;   // gaps of 2 on each side
    break;
  default:
    jmpH[i] = jmpCur;
    if (random(4) == 0) jmpC[i] = 2;               // a coin two tiles up
    break;
  }
  if (jmpP[i] && random(2)) jmpC[i] = (uint8_t)(jmpP[i] + 2 - jmpH[i]);
}

// solid below the point (x px, y px)?  returns the surface y or -1
static int16_t jmpFloor(int32_t xpx, int16_t ypx) {
  uint8_t h = jmpHeight(xpx / JMP_T);
  if (h && ypx >= jmpTop(h)) return jmpTop(h);
  return -1;
}

// ---------------- drawing ----------------
/* The runner, 14 x 20 in full resolution, built from shared rows: head,
   body, and legs for standing, three running steps and the jump. It
   stands 1 px wider than its box on each side and 2 px taller (the cap). */
static const char JH0[] = "....kkkkk.....", JH1[] = "...krrrrrkk...", JH2[] = "..krrrrwrrrk..",
                  JH3[] = "..kRRRRRRRRRRk", JH4[] = "..khhsskskkk..", JH5[] = ".khsshsksssk..",
                  JH6[] = ".khssshhssssk.", JH7[] = "..kkssssskkk..", JH8[] = "...kksssskk...";
static const char JT0[] = "...krrbbrrk...", JT1[] = "..krrrbbbrrk..", JT2[] = ".ksrrbybbybrsk",
                  JT3[] = ".kskbbbbbbbksk", JT4[] = "..kkbbbbbbbkk.";
static const char JU0[] = "...krrbbrrk.k.", JU1[] = "..krrrbbbrrksk", JU2[] = ".ksrrbybbybrk.",   // an arm up
                  JU3[] = ".kskbbbbbbbk..";
static const char JS0[] = "...kbbbbbbk...", JS1[] = "...kbbkkbbk...", JS2[] = "...kBBk.kBBk..",   // standing
                  JS3[] = "..knnnk.knnnk.", JS4[] = "..knnnnkknnnnk", JS5[] = "...kkkk..kkkk.";
static const char JA1[] = "..kbbbkkbbbk..", JA2[] = ".kBBk....kBBk.", JA3[] = "knnnk.....kBk.",   // stride
                  JA4[] = "knnnnk...knnnk", JA5[] = ".kkkk....kkkkk";
static const char JB1[] = "....kbbbbk....", JB2[] = "....kBBBBk....", JB3[] = "...knnnBBk....",   // passing
                  JB4[] = "...knnnnnk....", JB5[] = "....kkkkk.....";
static const char JC1[] = "...kbbbkbbbk..", JC2[] = "...kBBk.kBBk..", JC3[] = "..kBk..knnnk..",   // the other stride
                  JC4[] = ".knnnk.knnnnk.", JC5[] = ".kkkk...kkkk..";
static const char JJ1[] = "..kbbbbbkbbk..", JJ2[] = "..kBBk.kBBnnk.", JJ3[] = ".knnnk..knnnk.",   // tucked jump
                  JJ4[] = ".knnnnk..kkk..", JJ5[] = "..kkkk........";
#define JMP_HEAD JH0, JH1, JH2, JH3, JH4, JH5, JH6, JH7, JH8
static const char *const JMP_HERO[5][20] = {
  { JMP_HEAD, JT0, JT1, JT2, JT3, JT4, JS0, JS1, JS2, JS3, JS4, JS5 },
  { JMP_HEAD, JT0, JT1, JT2, JT3, JT4, JS0, JA1, JA2, JA3, JA4, JA5 },
  { JMP_HEAD, JT0, JT1, JT2, JT3, JT4, JS0, JB1, JB2, JB3, JB4, JB5 },
  { JMP_HEAD, JT0, JT1, JT2, JT3, JT4, JS0, JC1, JC2, JC3, JC4, JC5 },
  { JMP_HEAD, JU0, JU1, JU2, JU3, JT4, JS0, JJ1, JJ2, JJ3, JJ4, JJ5 } };
#undef JMP_HEAD
static const char JMP_HKEYS[] = "krRwhsbByn";
static const col  JMP_HPAL[10] = { rgb(40, 20, 20), rgb(230, 40, 40), rgb(153, 0, 0), C::WHITE, rgb(102, 42, 0),
                                   rgb(255, 200, 150), rgb(51, 102, 255), rgb(0, 42, 153), C::YELLOW, rgb(130, 70, 30) };

// a slime, 18 x 12: bouncing (frame 0) and squeezed (frame 1)
static const char *const JMP_SLIME[2][12] = {
  { "......kkkkkk......", "....kkppppppkk....", "...kpwwpppppppk...", "..kpwwppppppppdk..",
    "..kpppkwkppkwkpdk.", ".kppppkkkppkkkpdk.", ".kpppppppppppppddk", "kppppppppppppppddk",
    "kpppppkkkkkpppdddk", "kdppppppppppppdddk", ".kddddddddddddddk.", "..kkkkkkkkkkkkkk.." },
  { "..................", ".....kkkkkkkk.....", "...kkpwwpppppkk...", "..kpwwpppppppppk..",
    ".kppppkwkppkwkpdk.", ".kppppkkkppkkkpddk", "kppppppppppppppddk", "kpppppkkkkkpppdddk",
    "kdppppppppppppdddk", "kdpppppppppppddddk", "kddddddddddddddddk", ".kkkkkkkkkkkkkkkk." } };
static const char JMP_SKEYS[] = "kpwd";
static const col  JMP_SPAL[4] = { rgb(51, 0, 51), rgb(170, 70, 210), rgb(240, 200, 255), rgb(110, 30, 150) };

static const char *const JMP_HEART[8] = {
  ".kk.kk.", "krrkwrk", "krrrrrk", "krrrrrk", ".krrrk.", "..krk..", "...k...", "......." };
static const col JMP_HEARTPAL[3] = { rgb(80, 0, 0), rgb(240, 40, 50), C::WHITE };

static uint8_t jmpHash(int32_t a, int32_t b) {
  uint32_t h = (uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u;
  h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
  return (uint8_t)h;
}

// a hill of the backdrop with a lit rim
static void jmpHill(int16_t x, int16_t r, col c, col rim) {
  gfx.disc(x, SCR_H - 20, r, rim);
  gfx.disc(x + 2, SCR_H - 18, r - 1, c);
  gfx.fill(x - r, SCR_H - 20, 2 * r + 1, 20, c);
}

// a round tree on the near hills
static void jmpTree(int16_t x, int16_t y) {
  gfx.fill(x - 1, y, 3, 10, rgb(102, 42, 0));
  gfx.disc(x, y - 4, 7, rgb(0, 102, 51));
  gfx.disc(x - 2, y - 6, 5, rgb(51, 153, 51));
  gfx.disc(x - 3, y - 7, 2, rgb(102, 204, 51));
}

static void jmpCloud(int16_t x, int16_t y, uint8_t s) {
  col sh = rgb(204, 212, 255);
  gfx.disc(x, y + 1, 9 * s / 4, sh); gfx.disc(x + 13 * s / 4, y - 4 * s / 4 + 1, 12 * s / 4, sh); gfx.disc(x + 27 * s / 4, y + 1, 9 * s / 4, sh);
  gfx.fill(x, y + 1, 27 * s / 4, 9 * s / 4, sh);
  gfx.disc(x, y - 1, 9 * s / 4, C::WHITE); gfx.disc(x + 13 * s / 4, y - 4 * s / 4 - 1, 12 * s / 4, C::WHITE); gfx.disc(x + 27 * s / 4, y - 1, 9 * s / 4, C::WHITE);
  gfx.fill(x, y - 1, 27 * s / 4, 6 * s / 4, C::WHITE);
}

// one column of ground: grass with tufts and an uneven edge, earth with stones, darker further down
static void jmpGround(int16_t sx, int32_t c, uint8_t h) {
  // a band per tile row: light earth, mixed, dark, mixed with darker still (every other pixel)
  static const col EARTH[4] = { rgb(153, 85, 51), rgb(102, 42, 0), rgb(102, 42, 0), rgb(102, 42, 0) };
  static const col MIX[4]   = { 0, rgb(153, 85, 51), 0, rgb(51, 0, 0) };
  int16_t top = jmpTop(h);
  for (uint8_t b = 0; top + b * JMP_T < SCR_H; b++) {
    int16_t y = (int16_t)(top + b * JMP_T);
    uint8_t k3 = b > 3 ? 3 : b;
    col ec = EARTH[k3];
    gfx.fill(sx, y, JMP_T, JMP_T, ec);
    if (MIX[k3]) fxDitherFill(sx, y, JMP_T, JMP_T, MIX[k3]);
    for (uint8_t k = 0; k < 3; k++) {                           // stones, the same every time
      uint8_t hh = jmpHash(c * 3 + k, b + h * 7);
      if (hh & 1) continue;
      int16_t px = (int16_t)(sx + 2 + (hh >> 1) % 15), py = (int16_t)(y + 7 + (hh >> 4) % 11);
      gfx.fill(px, py, 3, 2, colLight(ec));
      gfx.hline(px, py + 2, 3, colDark(ec, 2));
      gfx.pixel(px, py, colLight(ec, 2));
    }
  }
  bool lowL = jmpHeight(c - 1) < h, lowR = jmpHeight(c + 1) < h;
  if (lowL) gfx.vline(sx, top, SCR_H - top, rgb(90, 50, 25));
  if (lowR) { gfx.vline(sx + JMP_T - 1, top, SCR_H - top, rgb(70, 40, 20)); gfx.vline(sx + JMP_T - 2, top, SCR_H - top, rgb(110, 65, 35)); }
  gfx.fill(sx, top, JMP_T, 6, rgb(60, 170, 50));                // grass
  gfx.hline(sx, top, JMP_T, rgb(160, 230, 100));
  gfx.hline(sx, top + 1, JMP_T, rgb(110, 210, 70));
  for (uint8_t x = 0; x < JMP_T; x++) {                          // its lower edge hangs down unevenly
    uint8_t hh = jmpHash(c * JMP_T + x, 99);
    int16_t d = (int16_t)(hh % 4);
    if ((lowL && x == 0) || (lowR && x == JMP_T - 1)) d = 0;
    gfx.vline(sx + x, top + 6, d, rgb(40, 130, 40));
    if ((hh >> 4) % 5 == 0) gfx.vline(sx + x, top - 2, 2, rgb(110, 210, 70));   // a tuft
  }
  if (lowL) gfx.fill(sx, top, 1, 3, rgb(40, 130, 40));
  if (lowR) gfx.fill(sx + JMP_T - 1, top, 1, 3, rgb(40, 130, 40));
}

static void jmpPlank(int16_t sx, int16_t pt, bool l, bool r) {
  gfx.fill(sx, pt, JMP_T, 8, rgb(200, 120, 50));
  gfx.hline(sx, pt, JMP_T, rgb(255, 190, 110));
  gfx.hline(sx, pt + 1, JMP_T, rgb(230, 150, 70));
  gfx.hline(sx + 2, pt + 4, 7, rgb(160, 85, 30));               // grain
  gfx.hline(sx + 11, pt + 3, 6, rgb(160, 85, 30));
  gfx.hline(sx, pt + 7, JMP_T, rgb(110, 55, 20));
  if (l) gfx.vline(sx, pt, 8, rgb(110, 55, 20));
  if (r) gfx.vline(sx + JMP_T - 1, pt, 8, rgb(110, 55, 20));
  gfx.pixel(sx + 3, pt + 2, rgb(80, 40, 20)); gfx.pixel(sx + JMP_T - 4, pt + 2, rgb(80, 40, 20));   // nails
}

// a spinning coin, now and then a glint
static void jmpCoin(int16_t cx, int16_t cy, uint32_t tick, int32_t c) {
  static const uint8_t CW[6] = { 12, 10, 6, 2, 6, 10 };
  uint8_t f = (uint8_t)((tick / 4 + c) % 6), w = CW[f];
  if (w > 2) {
    gfx.rbox(cx - w / 2, cy - 7, w, 14, w / 2, rgb(204, 127, 0));
    gfx.rbox(cx - w / 2 + 1, cy - 6, w - 2, 12, (w - 2) / 2, C::YELLOW);
    if (w > 6) { gfx.vline(cx, cy - 4, 8, rgb(230, 170, 0)); gfx.vline(cx - w / 2 + 2, cy - 4, 5, rgb(255, 250, 200)); }
  } else gfx.fill(cx - 1, cy - 7, 2, 14, rgb(204, 127, 0));
  if ((tick / 3 + c * 5) % 30 < 3) fxStar((int16_t)(cx + 4), (int16_t)(cy - 6), 3, rgb(255, 255, 200));
}

void jumpRun() {
  bool again = true;
  while (again) {
    again = false;
    memset(jmpE, 0, sizeof(jmpE));
    jmpMade = 0; jmpCur = 1; jmpLeft = 0; jmpMode = 0; jmpDiff = 0;
    for (uint8_t k = 0; k < 20; k++) jmpMake();
    int32_t  px = 40 * JMP_Q, cam = 0, lastCam = 0; // world px * JMP_Q, camera in px (and as last drawn)
    int32_t  py = (jmpTop(1) - JMP_PH) * JMP_Q, vx = 0, vy = 0;
    bool     ground = true, faceLeft = false, heldBefore = false;
    uint8_t  lives = 3;
    uint16_t coins = 0, stomps = 0, safe = 0;
    uint32_t far = 0, next = gameMillis(), tick = 0;
    fxReset();
    btnClear();

    while (poll()) {
      uint32_t now = gameMillis();
      bool held = btnHeld(B_UP) || btnHeld(B_OK);
      bool jumpReq = btnTap(B_UP) | btnTap(B_OK) | (held && !heldBefore);   // at once, not after the OK wait
      heldBefore = held;
      bool dead = false;
      for (uint8_t k = 0; k < 2 && lives && (int32_t)(now - next) >= 0; k++) {   // at most 40 ms per frame
        next += JMP_TICK; tick++;
        // ---- input ----
        if (btnHeld(B_RIGHT))     { vx += JMP_ACC; if (vx > JMP_RUN) vx = JMP_RUN; faceLeft = false; }
        else if (btnHeld(B_LEFT)) { vx -= JMP_ACC; if (vx < -JMP_RUN) vx = -JMP_RUN; faceLeft = true; }
        else if (vx > 0) vx -= vx > JMP_ACC ? JMP_ACC : vx;
        else if (vx < 0) vx += -vx > JMP_ACC ? JMP_ACC : -vx;
        if (jumpReq && ground) { vy = -JMP_JUMP; ground = false; jumpReq = false; sfx(700, 30); }
        if (!held && vy < -JMP_JUMP / 3) vy = -JMP_JUMP / 3;      // short hop
        // ---- move ----
        int32_t nx = px + vx;
        if (nx / JMP_Q < cam) nx = cam * JMP_Q;                   // no going back past the screen edge
        int16_t feet = (int16_t)((py >> 5) + JMP_PH);
        // walls: the next column's ground higher than our feet
        int32_t front = nx / JMP_Q + (vx > 0 ? JMP_PW : 0);
        uint8_t fh = jmpHeight(front / JMP_T);
        if (fh && jmpTop(fh) < feet - 2) { nx = px; vx = 0; }
        px = nx;
        vy += JMP_G; if (vy > JMP_FALL) vy = JMP_FALL;
        int32_t ny = py + vy, vland = vy;
        bool wasGround = ground;
        ground = false;
        if (vy >= 0) {                                            // landing: ground or a platform
          int16_t nfeet = (int16_t)((ny >> 5) + JMP_PH);
          for (uint8_t side = 0; side < 2 && !ground; side++) {
            int32_t xs = px / JMP_Q + (side ? JMP_PW - 1 : 0);
            int16_t top = jmpFloor(xs, nfeet);
            if (top >= 0 && feet <= top + 5) { ny = (top - JMP_PH) * JMP_Q; vy = 0; ground = true; }
            uint8_t ph = jmpP[(xs / JMP_T) % JMP_RING];
            if (!ground && ph && xs / JMP_T < (int32_t)jmpMade) {
              int16_t ptop = jmpTop(ph);
              if (feet <= ptop && nfeet >= ptop) { ny = (ptop - JMP_PH) * JMP_Q; vy = 0; ground = true; }
            }
          }
        }
        py = ny;
        int16_t fx0 = (int16_t)(px / JMP_Q - lastCam), fy0 = (int16_t)((py >> 5) + JMP_PH);   // the feet, on screen
        if (ground && !wasGround && vland > 150)                  // dust when landing hard
          for (int8_t s = -1; s <= 1; s += 2)
            for (uint8_t k = 0; k < 3; k++)
              fxEmit((int16_t)(fx0 + JMP_PW / 2 + s * 5), (int16_t)(fy0 - 1), (int16_t)(s * fxRange(20, 60)), (int16_t)-fxRange(5, 30),
                     (uint16_t)fxRange(250, 400), rgb(230, 210, 170), rgb(170, 140, 100), 3, 0, FX_DRAG);
        if (ground && (vx >= JMP_RUN || vx <= -JMP_RUN) && tick % 10 == 0)   // a puff behind the running feet
          fxEmit((int16_t)(fx0 + (vx > 0 ? 0 : JMP_PW)), (int16_t)(fy0 - 2), (int16_t)(vx > 0 ? -20 : 20), -15,
                 300, rgb(230, 215, 180), rgb(190, 170, 130), 2, 0, FX_DRAG | FX_GROW);
        if ((py >> 5) > SCR_H) dead = true;                       // fell into a gap
        // ---- camera and the world ahead ----
        int32_t want = px / JMP_Q - 120;
        if (want > cam) cam = want;
        while ((int32_t)jmpMade * JMP_T < cam + SCR_W + 2 * JMP_T) jmpMake();
        if ((uint32_t)(px / JMP_Q) > far) far = (uint32_t)(px / JMP_Q);
        jmpDiff = (uint8_t)(far / 2000 > 255 ? 255 : far / 2000);
        // ---- coins ----
        int32_t pc = (px / JMP_Q + JMP_PW / 2) / JMP_T;
        uint8_t ci = pc % JMP_RING;
        if (jmpC[ci]) {
          int16_t cy = (int16_t)(jmpTop(jmpH[ci] ? jmpH[ci] : 1) - jmpC[ci] * JMP_T + 10);
          if (abs((int16_t)(py >> 5) + JMP_PH / 2 - cy) < 18) {
            jmpC[ci] = 0; coins++; sfx(1500, 25);
            int16_t sx = (int16_t)(pc * JMP_T + 10 - lastCam);
            fxBurst(sx, cy, 10, C::WHITE, C::YELLOW, 110, 380, 0, 2);
            fxRing(sx, cy, 12, C::YELLOW, rgb(255, 255, 200), 220);
            fxText(sx, (int16_t)(cy - 18), "+10", C::YELLOW);
          }
        }
        // ---- slimes ----
        if (safe) safe--;
        for (uint8_t i = 0; i < JMP_EN; i++) {
          JmpEn &e = jmpE[i];
          if (!e.on) continue;
          if (e.x / JMP_Q < cam - 40) { e.on = false; continue; }
          e.x += e.dir * 30;
          if (e.x < e.x0 || e.x > e.x1) e.dir = (int8_t)-e.dir;
          int32_t ex = e.x / JMP_Q, pxx = px / JMP_Q;
          int16_t pyy = (int16_t)(py >> 5);
          if (pxx + JMP_PW > ex + 2 && pxx < ex + JMP_EW - 2 && pyy + JMP_PH > e.y + 2 && pyy < e.y + JMP_EH) {
            if (vy > 0 && feet <= e.y + 6) {                      // from above: squashed
              e.on = false; vy = -JMP_JUMP / 2; stomps++; sfx(400, 40);
              int16_t sx = (int16_t)(ex + JMP_EW / 2 - lastCam);
              fxBurst(sx, (int16_t)(e.y + JMP_EH - 3), 12, rgb(240, 200, 255), rgb(110, 30, 150), 120, 450, 40, 3);
              fxText(sx, (int16_t)(e.y - 14), "+50", C::WHITE);
            } else if (!safe) dead = true;
          }
        }
        if (dead) {
          sfx(180, 400);
          fxFlash(C::RED, 300, 100);
          fxBurst((int16_t)(px / JMP_Q - lastCam + JMP_PW / 2), (int16_t)((py >> 5) + JMP_PH / 2 > SCR_H - 4 ? SCR_H - 4 : (py >> 5) + JMP_PH / 2),
                  12, C::WHITE, C::RED, 140, 400, 30, 2);
          if (--lives == 0) break;
          dead = false;                                           // back on the ground a bit ahead
          int32_t c = cam / JMP_T + 2;
          while ((!jmpHeight(c) || !jmpHeight(c + 1)) && c + 2 < (int32_t)jmpMade) c++;
          px = c * JMP_T * JMP_Q;
          py = (jmpTop(jmpHeight(c)) - JMP_PH) * JMP_Q;
          vx = vy = 0; safe = 100;
        }
      }
      if ((int32_t)(now - next) > 0) next = now;
      uint32_t sc = far / JMP_T + coins * 10 + stomps * 50;
      uint16_t score = sc > 65535 ? 65535 : (uint16_t)sc;

      // ---- screen ----
      fxScroll((int16_t)(lastCam - cam), 0);                      // what flies stays in the world
      lastCam = cam;
      fxUpdate();
      statusBar("JUMP", score, curHigh, C::BLUE);
      gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(80, 160, 255), rgb(200, 235, 255));
      fxGlow(270, 64, 26, rgb(170, 215, 255), rgb(255, 250, 200), 4);   // the sun
      gfx.disc(270, 64, 14, rgb(255, 245, 160));
      gfx.disc(267, 61, 6, rgb(255, 255, 220));
      for (int32_t k = cam / 32 / 130 - 1; k <= (cam / 32 + SCR_W) / 130 + 1; k++)   // far clouds
        jmpCloud((int16_t)(k * 130 - cam / 32 + (k * 41) % 40), (int16_t)(44 + (k * 37 & 31)), 2);
      for (int32_t k = cam / 8 / 150 - 1; k <= (cam / 8 + SCR_W) / 150 + 1; k++) {    // mountains with snow
        int16_t x = (int16_t)(k * 150 - cam / 8 + (k * 23) % 40), hgt = (int16_t)(70 + (k * 53 & 31));
        gfx.triangle(x - 90, SCR_H - 40, x + 90, SCR_H - 40, x, SCR_H - 40 - hgt, rgb(130, 150, 200));
        gfx.triangle(x, SCR_H - 40 - hgt, x + 90, SCR_H - 40, x + 20, SCR_H - 40, rgb(110, 125, 180));
        gfx.triangle(x - 18, SCR_H - 40 - hgt + 18, x + 18, SCR_H - 40 - hgt + 18, x, SCR_H - 40 - hgt, rgb(240, 245, 255));
      }
      for (int32_t k = cam / 4 / 110 - 1; k <= (cam / 4 + SCR_W) / 110 + 1; k++)   // far hills
        jmpHill((int16_t)(k * 110 - cam / 4), (int16_t)(50 + (k * 29 & 31)), rgb(120, 185, 190), rgb(170, 220, 210));
      for (int32_t k = cam / 16 / 170 - 1; k <= (cam / 16 + SCR_W) / 170 + 1; k++)   // near clouds
        jmpCloud((int16_t)(k * 170 - cam / 16 + (k * 37) % 50), (int16_t)(66 + (k * 53 & 63)), 4);
      for (int32_t k = cam / 2 / 80 - 1; k <= (cam / 2 + SCR_W) / 80 + 1; k++) {   // near hills with trees
        int16_t x = (int16_t)(k * 80 - cam / 2 + 20), r = (int16_t)(26 + (k * 13 & 15));
        jmpHill(x, r, rgb(70, 160, 90), rgb(120, 200, 110));
        if (k & 1) jmpTree((int16_t)(x - r / 3), (int16_t)(SCR_H - 20 - r + 4));
      }
      int32_t c0 = cam / JMP_T;
      for (int32_t c = c0; c <= c0 + SCR_W / JMP_T + 1; c++) {
        int16_t sx = (int16_t)(c * JMP_T - cam);
        uint8_t i = c % JMP_RING, h = jmpHeight(c);
        if (h) jmpGround(sx, c, h);
        if (jmpP[i]) jmpPlank(sx, jmpTop(jmpP[i]), !jmpP[(c + JMP_RING - 1) % JMP_RING], !jmpP[(c + 1) % JMP_RING]);
        if (jmpC[i]) {
          int16_t cy = (int16_t)(jmpTop(h ? h : 1) - jmpC[i] * JMP_T + 10);
          if (cy > TOP_H + 8) jmpCoin(sx + 10, cy, tick, c);
        }
      }
      for (uint8_t i = 0; i < JMP_EN; i++)
        if (jmpE[i].on)
          gfx.sprite((int16_t)(jmpE[i].x / JMP_Q - cam), jmpE[i].y, JMP_SLIME[(tick / 10) & 1], 12, JMP_SKEYS, JMP_SPAL, 1, jmpE[i].dir > 0);
      if (lives && (!safe || (safe / 4) % 2)) {                   // the runner
        static const uint8_t RUN[4] = { 1, 2, 3, 2 };
        uint8_t f = !ground ? 4 : (vx ? RUN[(tick / 4) % 4] : 0);
        gfx.sprite((int16_t)(px / JMP_Q - cam - 1), (int16_t)((py >> 5) - 2), JMP_HERO[f], 20, JMP_HKEYS, JMP_HPAL, 1, faceLeft);
      }
      fxDraw();
      for (uint8_t i = 0; i < lives; i++)                         // hearts
        gfx.sprite(SCR_W - 14 - i * 16, TOP_H + 5, JMP_HEART, 8, "krw", JMP_HEARTPAL, 2);
      char b[12];
      snprintf(b, sizeof(b), "x%u", coins);
      gfx.rbox(5, TOP_H + 4, 9, 13, 4, rgb(204, 127, 0));
      gfx.rbox(6, TOP_H + 5, 7, 11, 3, C::YELLOW);
      gfx.vline(8, TOP_H + 7, 6, rgb(255, 250, 200));
      gfx.textShadow(18, TOP_H + 6, b, C::WHITE, C::BLACK, F_SMALL);
      fxFlashApply();
      gfx.send();
      if (!lives) { delay(600); break; }
    }
    if (lives) return;                                            // left from the pause menu
    uint32_t sc = far / JMP_T + coins * 10 + stomps * 50;
    again = gameOver(sc > 65535 ? 65535 : (uint16_t)sc);
  }
}

static const char *const IC_JUMP[12] = {
  "......yy....", ".....y..y...", "......yy....", "..rrr.......", "..rrrr......", "..ssk.......",
  ".bbbb.......", "s.bb.s..pp..", ".b..b..pkpk.", "n....n.pppp.", "gggg...ggggg", "nnnn...nnnnn" };
