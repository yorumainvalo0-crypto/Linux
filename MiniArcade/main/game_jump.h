// MiniArcade: Jump & Run. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  JUMP & RUN   (endless level, made while you run)
// =========================================================
/* LEFT / RIGHT run, UP or OK jumps (let go of UP early for a small jump).
   Jump on the walkers to squash them, collect coins, do not fall into
   the gaps. The level is made column by column ahead of the screen and
   only ever asks for jumps the physics can do (JR_GAP / JR_UP, checked
   by the PC test).                                                     */
#define JR_T     8            // tile size
#define JR_ROWS  6            // 48 px
#define JR_RING 32            // columns kept
#define JR_Q    16            // positions in 1/16 px
#define JR_G     5            // gravity per tick
#define JR_JUMP 60            // jump speed
#define JR_RUN  24            // top running speed
#define JR_GAP   3            // widest gap in tiles
#define JR_UP    2            // highest step up in tiles
#define JR_EN    4

static uint8_t jrH[JR_RING];            // ground height in tiles, 0 = gap
static uint8_t jrP[JR_RING];            // floating platform height, 0 = none
static uint8_t jrC[JR_RING];            // coin height above ground, 0 = none
static uint32_t jrMade;                 // columns made so far
static uint8_t jrCur, jrLeft, jrMode;   // generator: height, columns left in this piece, piece type
static uint8_t jrDiff;                  // grows with the distance

struct JrEn { bool on; int32_t x; int16_t y; int32_t x0, x1; int8_t dir; };
static JrEn jrE[JR_EN];

static uint8_t jrHeight(int32_t col) { return col < 0 ? 1 : jrH[col % JR_RING]; }

// ground top in px (screen y) for a height in tiles
static int16_t jrTop(uint8_t h) { return SCR_H - h * JR_T; }

static void jrEnemy(uint32_t c0, uint8_t len, uint8_t h) {
  for (uint8_t i = 0; i < JR_EN; i++) {
    if (jrE[i].on) continue;
    JrEn &e = jrE[i];
    e.on = true;
    e.x0 = (int32_t)c0 * JR_T * JR_Q;
    e.x1 = (int32_t)(c0 + len) * JR_T * JR_Q - 7 * JR_Q;
    e.x = e.x1; e.dir = -1;
    e.y = jrTop(h) - 5;
    return;
  }
}

// one more column at the right end of the world
void jrMake() {
  uint32_t c = jrMade++;
  uint8_t i = c % JR_RING;
  jrP[i] = jrC[i] = 0;
  if (!jrLeft) {                                   // start the next piece
    uint8_t r = random(10);
    if (c < 12 || jrMode == 1 || jrMode == 3 || r < 4) {     // flat ground (always after a gap)
      jrMode = 0; jrLeft = 3 + random(6);
      if (c >= 12 && r < 2 && jrCur < 3) jrCur++;
      else if (c >= 12 && r < 4 && jrCur > 1) jrCur--;
      if (c >= 16 && jrLeft >= 4 && random(3) < 1 + (jrDiff > 3)) jrEnemy(c + 1, jrLeft - 1, jrCur);
    } else if (r < 7) {                            // a gap
      jrMode = 1;
      uint8_t w = 1 + random(jrDiff < 2 ? 2 : 3);
      jrLeft = w > JR_GAP ? JR_GAP : w;
    } else if (r < 9) {                            // steps up
      jrMode = 2; jrLeft = 2 + random(3);
      jrCur += 1 + (jrCur == 1 && random(2));      // one or two tiles: both within JR_UP
      if (jrCur > 3) jrCur = 3;                    // keeps the jumps below the status bar
    } else {                                       // a wide gap with a platform in the middle
      jrMode = 3; jrLeft = 5;
    }
  }
  jrLeft--;
  switch (jrMode) {
  case 1: jrH[i] = 0; break;
  case 3:
    jrH[i] = 0;
    if (jrLeft == 2 || jrLeft == 1) jrP[i] = jrCur + 1 > 4 ? 4 : jrCur + 1;   // gaps of 2 on each side
    break;
  default:
    jrH[i] = jrCur;
    if (random(4) == 0) jrC[i] = 2;                // a coin two tiles up
    break;
  }
  if (jrP[i] && random(2)) jrC[i] = jrP[i] + 2 - jrH[i];
}

// solid below the point (x px, y px)?  returns the surface y or -1
static int16_t jrFloor(int32_t xpx, int16_t ypx) {
  int32_t col = xpx / JR_T;
  uint8_t h = jrHeight(col);
  if (h && ypx >= jrTop(h)) return jrTop(h);
  return -1;
}

void jumpRun() {
  bool again = true;
  while (again) {
    again = false;
    memset(jrE, 0, sizeof(jrE));
    jrMade = 0; jrCur = 1; jrLeft = 0; jrMode = 0; jrDiff = 0;
    for (uint8_t k = 0; k < 20; k++) jrMake();
    int32_t  px = 16 * JR_Q * 1, cam = 0;          // world px * JR_Q, camera in px
    int16_t  py = (jrTop(1) - 7) * JR_Q, vx = 0, vy = 0;
    bool     ground = true;
    uint8_t  lives = 3;
    uint16_t coins = 0, stomps = 0, safe = 0;
    uint32_t far = 0, next = gameMillis(), tick = 0;
    btnClear();

    while (poll()) {
      uint32_t now = gameMillis();
      bool dead = false;
      for (uint8_t k = 0; k < 3 && (int32_t)(now - next) >= 0; k++) {
        next += 20; tick++;
        // ---- input ----
        if (btnHeld(B_RIGHT))     { vx += 2; if (vx > JR_RUN) vx = JR_RUN; }
        else if (btnHeld(B_LEFT)) { vx -= 2; if (vx < -JR_RUN) vx = -JR_RUN; }
        else if (vx > 0) vx -= vx > 2 ? 2 : vx;
        else if (vx < 0) vx += -vx > 2 ? 2 : -vx;
        bool jump = btnTap(B_UP) | btnTap(B_OK);
        if (jump && ground) { vy = -JR_JUMP; ground = false; sfx(700, 30); }
        if (!btnHeld(B_UP) && !btnHeld(B_OK) && vy < -JR_JUMP / 3) vy = -JR_JUMP / 3;   // short hop
        // ---- move ----
        int32_t nx = px + vx;
        if (nx / JR_Q < cam) nx = cam * JR_Q;        // no going back past the screen edge
        int16_t feet = (py >> 4) + 7;
        // walls: the next column's ground higher than our feet
        int32_t front = (nx / JR_Q) + (vx > 0 ? 5 : 0);
        uint8_t fh = jrHeight(front / JR_T);
        if (fh && jrTop(fh) < feet - 1) { nx = px; vx = 0; }
        px = nx;
        vy += JR_G; if (vy > 70) vy = 70;
        int16_t ny = py + vy;
        ground = false;
        if (vy >= 0) {                              // landing: ground or a platform
          int16_t nfeet = (ny >> 4) + 7;
          for (uint8_t side = 0; side < 2 && !ground; side++) {
            int32_t xs = px / JR_Q + (side ? 4 : 0);
            int16_t top = jrFloor(xs, nfeet);
            if (top >= 0 && feet <= top + 2) { ny = (top - 7) * JR_Q; vy = 0; ground = true; }
            uint8_t ph = jrP[(xs / JR_T) % JR_RING];
            if (!ground && ph && xs / JR_T < (int32_t)jrMade) {
              int16_t ptop = jrTop(ph);
              if (feet <= ptop && nfeet >= ptop) { ny = (ptop - 7) * JR_Q; vy = 0; ground = true; }
            }
          }
        }
        py = ny;
        if ((py >> 4) > SCR_H) dead = true;          // fell into a gap
        // ---- camera and the world ahead ----
        int32_t want = px / JR_Q - 48;
        if (want > cam) cam = want;
        while ((int32_t)jrMade * JR_T < cam + SCR_W + 2 * JR_T) jrMake();
        if ((uint32_t)(px / JR_Q) > far) far = px / JR_Q;
        jrDiff = far / 800;
        // ---- coins ----
        int32_t pc = (px / JR_Q + 2) / JR_T;
        uint8_t ci = pc % JR_RING;
        if (jrC[ci]) {
          int16_t cy = jrTop(jrH[ci] ? jrH[ci] : 1) - jrC[ci] * JR_T;
          if (abs((py >> 4) + 3 - cy) < 7) { jrC[ci] = 0; coins++; sfx(1500, 25); }
        }
        // ---- walkers ----
        if (safe) safe--;
        for (uint8_t i = 0; i < JR_EN; i++) {
          JrEn &e = jrE[i];
          if (!e.on) continue;
          if (e.x / JR_Q < cam - 16) { e.on = false; continue; }
          e.x += e.dir * 6;
          if (e.x < e.x0 || e.x > e.x1) e.dir = -e.dir;
          int32_t ex = e.x / JR_Q, pxx = px / JR_Q;
          int16_t pyy = py >> 4;
          if (pxx + 5 > ex && pxx < ex + 7 && pyy + 7 > e.y && pyy < e.y + 5) {
            if (vy > 0 && pyy + 7 <= e.y + 3) {      // from above: squashed
              e.on = false; vy = -JR_JUMP / 2; stomps++; sfx(400, 40);
            } else if (!safe) dead = true;
          }
        }
        if (dead) {
          sfx(180, 400);
          if (--lives == 0) break;
          dead = false;                              // back on the ground a bit ahead
          int32_t col = cam / JR_T + 2;
          while ((!jrHeight(col) || !jrHeight(col + 1)) && col + 2 < (int32_t)jrMade) col++;
          px = (int32_t)col * JR_T * JR_Q;
          py = (jrTop(jrHeight(col)) - 7) * JR_Q;
          vx = vy = 0; safe = 100;
        }
      }
      if ((int32_t)(now - next) > 100) next = now;
      uint32_t sc = far / JR_T + coins * 10 + stomps * 50;
      uint16_t score = sc > 65535 ? 65535 : (uint16_t)sc;
      if (!lives) { again = gameOver(score); break; }

      // ---- screen ----
      statusBar("JUMP", score, curHigh);
      int32_t c0 = cam / JR_T;
      for (int32_t c = c0; c <= c0 + SCR_W / JR_T + 1; c++) {
        int16_t sx = c * JR_T - cam;
        uint8_t i = c % JR_RING, h = jrHeight(c);
        if (h) {                                    // ground: outline on top, dots below
          int16_t top = jrTop(h);
          oled.drawHLine(sx, top, JR_T);
          if (!jrHeight(c - 1) || jrHeight(c - 1) < h) oled.drawVLine(sx, top, SCR_H - top);
          if (!jrHeight(c + 1) || jrHeight(c + 1) < h) oled.drawVLine(sx + JR_T - 1, top, SCR_H - top);
          for (int16_t y = top + 3; y < SCR_H; y += 3) oled.drawPixel(sx + (y / 3 % 2) * 4 + 2, y);
        }
        if (jrP[i]) oled.drawBox(sx, jrTop(jrP[i]), JR_T, 2);
        if (jrC[i]) {
          int16_t cy = jrTop(h ? h : 1) - jrC[i] * JR_T;
          if (cy > TOP_H + 1) oled.drawFrame(sx + 2, cy, 4, 4);
        }
      }
      for (uint8_t i = 0; i < JR_EN; i++)
        if (jrE[i].on) {
          int16_t ex = jrE[i].x / JR_Q - cam;
          oled.drawBox(ex + 1, jrE[i].y, 5, 3);
          oled.drawPixel(ex, jrE[i].y + 4); oled.drawPixel(ex + 6, jrE[i].y + 4);
          oled.setDrawColor(0); oled.drawPixel(ex + 2, jrE[i].y + 1); oled.drawPixel(ex + 4, jrE[i].y + 1); oled.setDrawColor(1);
        }
      if (!safe || (safe / 4) % 2) {                // the runner
        int16_t x = px / JR_Q - cam, y = py >> 4;
        oled.drawBox(x + 1, y, 3, 3);               // head
        oled.drawVLine(x + 2, y + 3, 2);            // body
        oled.drawHLine(x, y + 3, 5);                // arms
        bool step = ground && vx && (tick / 5) % 2;
        oled.drawPixel(x + (step ? 1 : 0), y + 6); oled.drawPixel(x + 1, y + 5);
        oled.drawPixel(x + 3, y + 5); oled.drawPixel(x + (step ? 3 : 4), y + 6);
      }
      for (uint8_t i = 1; i < lives; i++) oled.drawBox(SCR_W - 4 - (i - 1) * 5, TOP_H + 2, 3, 3);
      oled.sendBuffer();
    }
  }
}
