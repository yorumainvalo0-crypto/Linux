// MiniArcade: Lander. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  LANDER   (LEFT/RIGHT turn, UP = thrust, land softly on a pad)
// =========================================================
/* Gravity pulls the lander down; the engine pushes it the way it points.
   A landing counts on a flat pad, upright and slow. Every landing gives
   points (more on the small pads) plus the fuel that is left, and the
   next ground is rougher with less fuel. A crash ends the game.     */
#define LD_TOP    (TOP_H + 1)
#define LD_PTS    17            // ground points, 8 px apart
#define LD_ANG    8             // 16 directions, 0 = up; allowed tilt for landing: 0

static int16_t ldGround[LD_PTS];            // ground height (y) at x = i * 8
static uint8_t ldPad[LD_PTS];               // pad starting at point i: its bonus, 0 = none
static int16_t ldX, ldY, ldVx, ldVy;        // the lander, Q4 pixels
static uint8_t ldA;                         // its direction

// sin / cos of the 16 directions, *64 (0 = up, clockwise)
static const int8_t LD_SIN[16] = { 0, 24, 45, 59, 64, 59, 45, 24, 0, -24, -45, -59, -64, -59, -45, -24 };
static int8_t ldSin(uint8_t a) { return LD_SIN[a & 15]; }
static int8_t ldCos(uint8_t a) { return LD_SIN[(a + 4) & 15]; }

static void ldTerrain(uint8_t level) {
  uint8_t rough = 6 + level * 2;
  if (rough > 16) rough = 16;
  int16_t y = SCR_H - 10;
  for (uint8_t i = 0; i < LD_PTS; i++) {
    y += (int16_t)random(rough * 2 + 1) - rough;
    if (y < 34) y = 34;
    if (y > SCR_H - 3) y = SCR_H - 3;
    ldGround[i] = y;
    ldPad[i] = 0;
  }
  // two pads: a wide one (x2) and a narrow one (x5)
  uint8_t a = 1 + random(6), b = 9 + random(6);
  if (random(2)) { uint8_t t = a; a = b; b = t; }
  ldPad[a] = 2; ldGround[a + 1] = ldGround[a]; ldGround[a + 2] = ldGround[a];   // 16 px
  ldPad[b] = 5; ldGround[b + 1] = ldGround[b];                                 //  8 px
  if (ldPad[a + 1] || ldPad[a + 2]) ldPad[b] = 0;
}

// ground height under x (linear between the points)
int16_t ldHeight(int16_t x) {
  if (x < 0) x = 0;
  if (x >= (LD_PTS - 1) * 8) x = (LD_PTS - 1) * 8 - 1;
  uint8_t i = x / 8, f = x % 8;
  return ldGround[i] + (ldGround[i + 1] - ldGround[i]) * f / 8;
}

// the pad under x: its bonus, 0 = no pad there
uint8_t ldPadAt(int16_t x) {
  for (uint8_t i = 0; i + 1 < LD_PTS; i++) {
    if (!ldPad[i]) continue;
    uint8_t len = ldPad[i] == 2 ? 16 : 8;
    if (x >= i * 8 && x + 5 <= i * 8 + len) return ldPad[i];
  }
  return 0;
}

static void ldBar(uint16_t score) {                         // the band has two lines here
  char b[20];
  snprintf(b, sizeof(b), "LANDER %u", score);
  oled.drawStr(1, 7, b);
  snprintf(b, sizeof(b), "BEST %u", curHigh);
  rightStr(7, b);
}

static void ldDraw(int16_t x, int16_t y, uint8_t ang, bool flame, uint16_t fuel, int16_t vx, int16_t vy) {
  for (uint8_t i = 0; i + 1 < LD_PTS; i++) {
    oled.drawLine(i * 8, ldGround[i], i * 8 + 8, ldGround[i + 1]);
    if (ldPad[i]) {
      uint8_t len = ldPad[i] == 2 ? 16 : 8;
      oled.drawBox(i * 8, ldGround[i], len + 1, 2);
      char b[8]; snprintf(b, sizeof(b), "x%u", ldPad[i]);
      oled.drawStr(i * 8 + len / 2 - 4, ldGround[i] + 9 > SCR_H ? ldGround[i] - 2 : ldGround[i] + 9, b);
    }
  }
  int16_t cx = (x >> 4) + 3, cy = (y >> 4) + 3;             // the lander: a body and two legs
  int8_t s = ldSin(ang), c = ldCos(ang);
  auto P = [&](int8_t dx, int8_t dy, int16_t *ox, int16_t *oy) {   // rotate around the middle
    *ox = cx + (dx * c - dy * s) / 64;
    *oy = cy + (dx * s + dy * c) / 64;
  };
  int16_t ax, ay, bx, by, lx, ly, rx, ry, fx, fy;
  P(-3, -3, &ax, &ay); P(3, -3, &bx, &by);
  P(-4, 3, &lx, &ly);  P(4, 3, &rx, &ry);
  oled.drawLine(ax, ay, bx, by);
  oled.drawLine(ax, ay, lx, ly);
  oled.drawLine(bx, by, rx, ry);
  P(0, -4, &fx, &fy); oled.drawPixel(fx, fy);
  if (flame) { P(0, 6 + (int8_t)random(3), &fx, &fy); int16_t gx, gy; P(0, 3, &gx, &gy); oled.drawLine(gx, gy, fx, fy); }
  char b[24];                                                 // fuel and speed: 2nd line of the band
  snprintf(b, sizeof(b), "fuel %u", fuel / 10);
  oled.drawStr(1, 15, b);
  bool fast = vy > 12 || vx > 8 || vx < -8;                  // too fast to land
  snprintf(b, sizeof(b), "%sspeed %d", fast ? "! " : "", (abs(vx) + abs(vy)) * 10 / 16);
  rightStr(15, b);
}

void landerRun() {
  bool again = true;
  while (again) {
    again = false;
    uint16_t score = 0;
    uint8_t  level = 0;
    bool     crashed = false;
    while (!crashed) {                                        // one landing per loop
      ldTerrain(level);
      int16_t &x = ldX, &y = ldY, &vx = ldVx, &vy = ldVy;       // Q4 pixels, Q4 per step
      uint8_t &ang = ldA;
      x = (10 + random(100)) << 4; y = (LD_TOP + 2) << 4;
      vx = (int16_t)random(17) - 8; vy = 0; ang = 0;
      uint16_t fuel = 600 - (level > 5 ? 250 : level * 50);
      uint32_t next = 0;
      bool landed = false, flame = false;
      btnClear();
      while (!landed && !crashed) {
        if (!poll()) return;
        if (btn(B_LEFT))  { ang = (ang + 15) & 15; sfx(500, 8); }
        if (btn(B_RIGHT)) { ang = (ang + 1) & 15;  sfx(500, 8); }
        uint32_t now = gameMillis();
        if (now >= next) {
          next = now + 40;
          flame = (btnHeld(B_UP) || btnHeld(B_OK)) && fuel;
          vy += 1;                                            // gravity
          if (flame) {
            vx += ldSin(ang) / 24;
            vy -= ldCos(ang) / 24;
            fuel -= fuel > 2 ? 2 : fuel;
            if ((now / 40) % 3 == 0) sfx(90 + random(40), 30);
          }
          if (vx > 40) vx = 40;
          if (vx < -40) vx = -40;
          if (vy > 40) vy = 40;
          if (vy < -30) vy = -30;
          x += vx; y += vy;
          if (x < 0) { x = 0; vx = -vx / 2; }
          if (x > (SCR_W - 7) << 4) { x = (SCR_W - 7) << 4; vx = -vx / 2; }
          if (y < LD_TOP << 4) { y = LD_TOP << 4; vy = 0; }
          int16_t px = x >> 4, feet = (y >> 4) + 7;
          int16_t g = ldHeight(px + 3);
          if (feet >= ldHeight(px) || feet >= g || feet >= ldHeight(px + 6)) {   // touched the ground
            uint8_t pad = ldPadAt(px);
            bool soft = vy <= 12 && vx >= -8 && vx <= 8 && ang == 0;
            if (pad && soft) {
              landed = true;
              y = (ldGround[(px + 3) / 8] - 7) << 4;
              score += 50 * pad + fuel / 10;
              level++;
              sfx(1200, 100);
            } else crashed = true;
          }
        }
        ldBar(score);
        ldDraw(x, y, ang, flame, fuel, vx, vy);
        oled.sendBuffer();
      }
      const char *msg = landed ? "LANDED!" : "CRASH";
      if (crashed) sfx(120, 500);
      uint32_t t0 = millis();
      btnClear();
      while (millis() - t0 < 1500) {
        if (!poll()) return;
        ldBar(score);
        ldDraw(x, y, ang, false, fuel, vx, vy);
        if (crashed) for (uint8_t k = 0; k < 6; k++)
          oled.drawPixel((x >> 4) + 3 + (int16_t)random(15) - 7, (y >> 4) + 3 + (int16_t)random(11) - 5);
        oled.setFont(FONT_B);
        uint8_t w = oled.getStrWidth(msg) + 8;
        oled.setDrawColor(0); oled.drawBox((SCR_W - w) / 2, 22, w, 15); oled.setDrawColor(1);
        oled.drawFrame((SCR_W - w) / 2, 22, w, 15);
        oled.drawStr((SCR_W - w) / 2 + 4, 34, msg);
        oled.setFont(FONT);
        oled.sendBuffer();
      }
    }
    again = gameOver(score);
  }
}
