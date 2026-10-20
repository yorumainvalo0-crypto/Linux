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
static const char *const JMP_HERO[3][9] = {
  { ".rrr..", ".rrrrr", ".sssk.", ".ssss.", "bbbbbb", "sbbbbs", ".bbbb.", ".b..b.", ".n..n." },
  { ".rrr..", ".rrrrr", ".sssk.", ".ssss.", "bbbbbb", "sbbbbs", ".bbbb.", "bb..b.", "n...nn" },
  { ".rrr..", ".rrrrr", ".sssk.", "sssss.", "bbbbbs", "bbbbb.", ".bbbb.", "bb.bb.", "n...n." } };
static const char JMP_HKEYS[] = "rskbn";
static const char *const JMP_SLIME[2][6] = {
  { "..ppppp..", ".ppppppp.", "pwkpppwkp", "ppppppppp", ".ppppppp.", ".kk...kk." },
  { "..ppppp..", ".ppppppp.", "pkwpppkwp", "ppppppppp", "ppppppppp", "..kk.kk.." } };
static const char JMP_SKEYS[] = "pwk";

static void jmpHill(int16_t x, int16_t r, col c) {
  gfx.disc(x, SCR_H - 20, r, c);
  gfx.fill(x - r, SCR_H - 20, 2 * r + 1, 20, c);
}

void jumpRun() {
  bool again = true;
  while (again) {
    again = false;
    memset(jmpE, 0, sizeof(jmpE));
    jmpMade = 0; jmpCur = 1; jmpLeft = 0; jmpMode = 0; jmpDiff = 0;
    for (uint8_t k = 0; k < 20; k++) jmpMake();
    int32_t  px = 40 * JMP_Q, cam = 0;             // world px * JMP_Q, camera in px
    int32_t  py = (jmpTop(1) - JMP_PH) * JMP_Q, vx = 0, vy = 0;
    bool     ground = true, faceLeft = false, heldBefore = false;
    uint8_t  lives = 3;
    uint16_t coins = 0, stomps = 0, safe = 0;
    uint32_t far = 0, next = gameMillis(), tick = 0;
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
        int32_t ny = py + vy;
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
          if (abs((int16_t)(py >> 5) + JMP_PH / 2 - cy) < 18) { jmpC[ci] = 0; coins++; sfx(1500, 25); }
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
            } else if (!safe) dead = true;
          }
        }
        if (dead) {
          sfx(180, 400);
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
      statusBar("JUMP", score, curHigh, C::BLUE);
      gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(80, 160, 255), rgb(200, 235, 255));
      gfx.disc(270, 62, 16, rgb(255, 245, 160));
      for (int32_t k = cam / 16 / 170 - 1; k <= (cam / 16 + SCR_W) / 170 + 1; k++) {   // clouds
        int16_t x = (int16_t)(k * 170 - cam / 16 + (k * 37) % 50);
        int16_t y = (int16_t)(50 + (k * 53 & 63));
        gfx.disc(x, y, 9, C::WHITE); gfx.disc(x + 13, y - 5, 12, C::WHITE); gfx.disc(x + 27, y, 9, C::WHITE);
        gfx.fill(x, y, 27, 9, C::WHITE);
      }
      for (int32_t k = cam / 4 / 110 - 1; k <= (cam / 4 + SCR_W) / 110 + 1; k++)   // far hills
        jmpHill((int16_t)(k * 110 - cam / 4), (int16_t)(50 + (k * 29 & 31)), rgb(130, 190, 200));
      for (int32_t k = cam / 2 / 80 - 1; k <= (cam / 2 + SCR_W) / 80 + 1; k++)     // near hills
        jmpHill((int16_t)(k * 80 - cam / 2 + 20), (int16_t)(26 + (k * 13 & 15)), rgb(90, 170, 110));
      int32_t c0 = cam / JMP_T;
      for (int32_t c = c0; c <= c0 + SCR_W / JMP_T + 1; c++) {
        int16_t sx = (int16_t)(c * JMP_T - cam);
        uint8_t i = c % JMP_RING, h = jmpHeight(c);
        if (h) {                                                  // grass on top, earth below
          int16_t top = jmpTop(h);
          gfx.fill(sx, top, JMP_T, SCR_H - top, rgb(150, 95, 50));
          for (int16_t y = top + 10; y < SCR_H; y += 7)
            gfx.fill(sx + (int16_t)((y * 7 + c * 5) % 14) + 2, y, 3, 2, rgb(110, 65, 35));
          gfx.fill(sx, top, JMP_T, 6, rgb(70, 180, 60));
          gfx.hline(sx, top, JMP_T, rgb(150, 230, 100));
          if (jmpHeight(c - 1) < h) { gfx.vline(sx, top, SCR_H - top, rgb(90, 55, 30)); gfx.pixel(sx, top, rgb(150, 230, 100)); }
          if (jmpHeight(c + 1) < h) gfx.vline(sx + JMP_T - 1, top, SCR_H - top, rgb(90, 55, 30));
        }
        if (jmpP[i]) {                                            // a floating plank
          int16_t pt = jmpTop(jmpP[i]);
          gfx.fill(sx, pt, JMP_T, 8, rgb(200, 120, 50));
          gfx.hline(sx, pt, JMP_T, rgb(250, 190, 110));
          gfx.hline(sx, pt + 7, JMP_T, rgb(120, 60, 20));
          gfx.vline(sx + JMP_T - 1, pt, 8, rgb(120, 60, 20));
        }
        if (jmpC[i]) {                                            // a spinning coin
          int16_t cy = (int16_t)(jmpTop(h ? h : 1) - jmpC[i] * JMP_T + 10);
          static const uint8_t CW[4] = { 12, 8, 3, 8 };
          uint8_t w = CW[(tick / 6 + c) % 4];
          if (cy > TOP_H + 6) {
            gfx.rbox(sx + 10 - w / 2, cy - 6, w, 12, w / 2, C::ORANGE);
            if (w > 4) gfx.rbox(sx + 11 - w / 2, cy - 5, w - 2, 10, (w - 2) / 2, C::YELLOW);
          }
        }
      }
      for (uint8_t i = 0; i < JMP_EN; i++)
        if (jmpE[i].on) {
          col sp[3] = { rgb(170, 70, 200), C::WHITE, C::BLACK };
          gfx.sprite((int16_t)(jmpE[i].x / JMP_Q - cam), jmpE[i].y, JMP_SLIME[(tick / 10) & 1], 6, JMP_SKEYS, sp, 2, jmpE[i].dir > 0);
        }
      if (lives && (!safe || (safe / 4) % 2)) {                   // the runner
        col hp[5] = { C::RED, rgb(255, 200, 150), C::BLACK, C::BLUE, C::BROWN };
        uint8_t f = !ground ? 2 : (vx && (tick / 6) % 2 ? 1 : 0);
        gfx.sprite((int16_t)(px / JMP_Q - cam), (int16_t)(py >> 5), JMP_HERO[f], 9, JMP_HKEYS, hp, 2, faceLeft);
      }
      for (uint8_t i = 0; i < lives; i++) {                       // hearts
        int16_t x = SCR_W - 16 - i * 16, y = TOP_H + 6;
        gfx.disc(x + 3, y + 3, 3, C::RED); gfx.disc(x + 9, y + 3, 3, C::RED);
        gfx.triangle(x, y + 4, x + 12, y + 4, x + 6, y + 10, C::RED);
      }
      char b[12];
      snprintf(b, sizeof(b), "x%u", coins);
      gfx.disc(10, TOP_H + 10, 5, C::YELLOW);
      gfx.textShadow(18, TOP_H + 6, b, C::WHITE, C::BLACK, F_SMALL);
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
