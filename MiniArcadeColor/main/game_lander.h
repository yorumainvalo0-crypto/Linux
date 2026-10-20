// MiniArcade Color: Lander. Part of console.cpp, which includes it.
// LEFT/RIGHT turn the lander (16 directions), hold UP for thrust. Gravity
// pulls it down, the engine pushes the way it points. A landing counts on
// a flat pad, upright and slow (the speeds show green when slow enough).
// Every landing gives 50 x the pad's bonus (x2 wide, x5 narrow) plus the
// fuel that is left / 10, and the next ground is rougher with less fuel.
// A crash ends the game.

#define LD_PTS   33                    // ground points, 10 px apart
#define LD_SEG   10
#define LD_HUD   (TOP_H + 2)           // fuel and speeds under the status bar
#define LD_TOP   (TOP_H + 18)          // the sky starts here
#define LD_TICK  20                    // ms per game step
#define LD_FOOT  10                    // feet: centre +- this, ...
#define LD_LEGS  9                     // ... this far below the centre
#define LD_SAFEV 240                   // softest landing: down at 47 px/s ...
#define LD_SAFEH 160                   // ... sideways 31 px/s (1/256 px per step)

/* Positions in 1/256 px, speeds in 1/256 px per 20 ms step: gravity 98
   px/s^2, the engine twice that, the speed capped at 156 px/s.         */
static int16_t ldGround[LD_PTS];       // ground height (y) at x = i * 10
static uint8_t ldPad[LD_PTS];          // a pad starts at point i: its bonus, 0 = none

// sin / cos of the 16 directions, *64 (0 = up, clockwise)
static const int8_t LD_SIN[16] = { 0, 24, 45, 59, 64, 59, 45, 24, 0, -24, -45, -59, -64, -59, -45, -24 };
static int8_t ldSin(uint8_t a) { return LD_SIN[a & 15]; }
static int8_t ldCos(uint8_t a) { return LD_SIN[(a + 4) & 15]; }
static uint8_t ldPadLen(uint8_t bonus) { return bonus == 2 ? 50 : 30; }

static void ldTerrain(uint8_t level) {
  int16_t rough = (int16_t)((6 + level * 2) * 5 / 4);
  if (rough > 20) rough = 20;
  int16_t y = SCR_H - 22;
  for (uint8_t i = 0; i < LD_PTS; i++) {
    y += (int16_t)random(rough * 2 + 1) - rough;
    if (y < 120) y = 120;
    if (y > SCR_H - 8) y = SCR_H - 8;
    ldGround[i] = y;
    ldPad[i] = 0;
  }
  // two pads: a wide one (x2) and a narrow one (x5), one in each half
  uint8_t a, b;
  if (random(2)) { a = (uint8_t)(2 + random(10)); b = (uint8_t)(18 + random(11)); }
  else           { b = (uint8_t)(2 + random(12)); a = (uint8_t)(18 + random(10)); }
  ldPad[a] = 2; for (uint8_t k = 1; k <= 5; k++) ldGround[a + k] = ldGround[a];   // 50 px
  ldPad[b] = 5; for (uint8_t k = 1; k <= 3; k++) ldGround[b + k] = ldGround[b];   // 30 px
}

// ground height under x (linear between the points)
static int16_t ldHeight(int16_t x) {
  if (x < 0) x = 0;
  if (x >= (LD_PTS - 1) * LD_SEG) x = (LD_PTS - 1) * LD_SEG - 1;
  uint8_t i = (uint8_t)(x / LD_SEG), f = (uint8_t)(x % LD_SEG);
  return (int16_t)(ldGround[i] + (ldGround[i + 1] - ldGround[i]) * f / LD_SEG);
}

// the pad under a lander with its centre at x: its bonus, 0 = none
static uint8_t ldPadAt(int16_t x) {
  for (uint8_t i = 0; i + 1 < LD_PTS; i++) {
    if (!ldPad[i]) continue;
    int16_t s = i * LD_SEG, e = s + ldPadLen(ldPad[i]);
    if (x - LD_FOOT >= s - 2 && x + LD_FOOT <= e + 2) return ldPad[i];
  }
  return 0;
}

static void ldScene(uint32_t now) {
  gfx.fill(0, TOP_H, SCR_W, SCR_H - TOP_H, C::BLACK);
  for (uint8_t i = 0; i < 50; i++) {
    uint32_t h = (uint32_t)(i + 11) * 2654435761u;
    gfx.pixel((int16_t)((h >> 8) % SCR_W), (int16_t)(LD_TOP + (h >> 18) % 110), i % 5 ? C::GREY : C::WHITE);
  }
  gfx.disc(272, 74, 16, C::BLUE);                       // the earth
  gfx.disc(268, 70, 6, C::GREEN);
  gfx.disc(278, 80, 4, C::GREEN);
  gfx.disc(266, 82, 3, C::WHITE);
  for (int16_t x = 0; x < SCR_W; x++) {                 // the moon's ground, lit on top
    int16_t h = ldHeight(x);
    gfx.vline(x, h, 2, C::LTGREY);
    gfx.vline(x, h + 2, 6, C::GREY);
    gfx.vline(x, h + 8, 14, rgb(90, 90, 105));
    gfx.vline(x, h + 22, SCR_H - h - 22, rgb(55, 55, 70));
  }
  for (uint8_t i = 0; i + 1 < LD_PTS; i++) {            // the pads
    if (!ldPad[i]) continue;
    int16_t x = i * LD_SEG, y = ldGround[i], len = ldPadLen(ldPad[i]);
    gfx.fill(x, y, len, 4, C::YELLOW);
    gfx.hline(x, y + 4, len, C::ORANGE);
    bool on = (now / 400) % 2;
    gfx.disc(x + 1, y - 2, 2, on ? C::RED : C::DKRED);
    gfx.disc(x + len - 2, y - 2, 2, on ? C::GREEN : C::DKGRN);
    char b[6];
    snprintf(b, sizeof(b), "x%u", ldPad[i]);
    int16_t ty = y + 8 + 9 < SCR_H ? y + 8 : y - 14;
    gfx.text(x + len / 2 - 5, ty, b, ldPad[i] == 5 ? C::PINK : C::CYAN, F_SMALL);
  }
}

// the lander at its centre, turned by ang; flame = the engine burns
static void ldLander(int16_t cx, int16_t cy, uint8_t ang, bool flame) {
  int16_t s = ldSin(ang), c = ldCos(ang);
  int16_t qx[16], qy[16];
  static const int8_t PX[16] = { -5, 5, 8, -8, -7, 7, 7, -7, -6, -10, 6, 10, -2, 2, 4, -4 };
  static const int8_t PY[16] = { -9, -9, -3, -3, -3, -3, 3, 3, 3, 9, 3, 9, 3, 3, 7, 7 };
  for (uint8_t k = 0; k < 16; k++) {                    // turn the outline around the middle
    qx[k] = (int16_t)(cx + (PX[k] * c - PY[k] * s) / 64);
    qy[k] = (int16_t)(cy + (PX[k] * s + PY[k] * c) / 64);
  }
  auto R = [&](int8_t dx, int8_t dy, int16_t &ox, int16_t &oy) {
    ox = (int16_t)(cx + (dx * c - dy * s) / 64); oy = (int16_t)(cy + (dx * s + dy * c) / 64);
  };
  if (flame) {
    int16_t ax, ay, bx, by, fx, fy, gx, gy;
    R(-3, 7, ax, ay); R(3, 7, bx, by);
    R(0, (int8_t)(16 + random(7)), fx, fy);
    R(0, (int8_t)(11 + random(3)), gx, gy);
    gfx.triangle(ax, ay, bx, by, fx, fy, C::ORANGE);
    gfx.triangle(ax, ay, bx, by, gx, gy, C::YELLOW);
  }
  gfx.line(qx[8], qy[8], qx[9], qy[9], C::LTGREY);       // legs and feet
  gfx.line(qx[10], qy[10], qx[11], qy[11], C::LTGREY);
  int16_t fx, fy, gx, gy;
  R(-12, 9, fx, fy); R(-8, 9, gx, gy); gfx.line(fx, fy, gx, gy, C::WHITE);
  R(8, 9, fx, fy);   R(12, 9, gx, gy); gfx.line(fx, fy, gx, gy, C::WHITE);
  gfx.triangle(qx[12], qy[12], qx[13], qy[13], qx[14], qy[14], C::GREY);        // nozzle
  gfx.triangle(qx[12], qy[12], qx[14], qy[14], qx[15], qy[15], C::GREY);
  gfx.triangle(qx[4], qy[4], qx[5], qy[5], qx[6], qy[6], rgb(220, 170, 40));    // gold descent stage
  gfx.triangle(qx[4], qy[4], qx[6], qy[6], qx[7], qy[7], rgb(190, 140, 30));
  gfx.triangle(qx[0], qy[0], qx[1], qy[1], qx[2], qy[2], C::LTGREY);           // cabin
  gfx.triangle(qx[0], qy[0], qx[2], qy[2], qx[3], qy[3], rgb(210, 210, 225));
  R(0, -6, fx, fy);
  gfx.disc(fx, fy, 2, C::CYAN);
}

// fuel, speeds, attitude and level under the status bar
static void ldHud(uint16_t fuel, uint16_t fuel0, int32_t vx, int32_t vy, uint8_t ang, uint8_t level) {
  gfx.fill(0, TOP_H, SCR_W, LD_TOP - TOP_H, rgb(15, 15, 35));
  gfx.text(6, LD_HUD + 3, "FUEL", C::LTGREY, F_SMALL);
  gfx.frame(30, LD_HUD + 2, 62, 10, C::GREY);
  int16_t w = (int16_t)(fuel * 58L / (fuel0 ? fuel0 : 1));
  gfx.fill(32, LD_HUD + 4, w, 6, fuel > fuel0 / 4 ? C::GREEN : C::RED);
  char b[16];
  int32_t sv = vy * 50 / 256, sh = vx * 50 / 256;       // px per second
  snprintf(b, sizeof(b), "DOWN %ld", (long)sv);
  gfx.text(104, LD_HUD + 3, b, vy <= LD_SAFEV ? C::LIME : C::RED, F_SMALL);
  snprintf(b, sizeof(b), "SIDE %ld", (long)(sh < 0 ? -sh : sh));
  gfx.text(166, LD_HUD + 3, b, vx >= -LD_SAFEH && vx <= LD_SAFEH ? C::LIME : C::RED, F_SMALL);
  gfx.text(222, LD_HUD + 3, ang == 0 ? "UPRIGHT" : "TILTED", ang == 0 ? C::LIME : C::RED, F_SMALL);
  snprintf(b, sizeof(b), "PAD %u", level + 1);
  gfx.right(SCR_W - 6, LD_HUD + 3, b, C::YELLOW, F_SMALL);
}

void landerRun() {
  struct LdSpark { int16_t x, y, vx, vy; uint8_t life; };
  bool again = true;
  while (again) {
    again = false;
    uint16_t score = 0;
    uint8_t  level = 0;
    bool     crashed = false;
    while (!crashed) {                                   // one landing per loop
      ldTerrain(level);
      int32_t x = (30 + random(260)) * 256, y = (LD_TOP + 14) * 256;   // centre
      int32_t vx = random(17) * 20 - 160, vy = 0;
      uint8_t ang = 0;
      uint16_t fuel0 = (uint16_t)(600 - (level > 5 ? 250 : level * 50)), fuel = fuel0;
      uint32_t now = gameMillis(), tick = now;
      bool landed = false, flame = false;
      uint16_t gain = 0;
      btnClear();
      while (!landed && !crashed) {
        if (!poll()) return;
        if (btn(B_LEFT))  { ang = (ang + 15) & 15; sfx(500, 8); }
        if (btn(B_RIGHT)) { ang = (ang + 1) & 15;  sfx(500, 8); }
        now = gameMillis();
        if ((int32_t)(now - tick) > 40) tick = now - 40;  // a slow frame: catch up at most 40 ms
        while ((int32_t)(now - tick) >= 0 && !landed && !crashed) {
          tick += LD_TICK;
          flame = btnHeld(B_UP) && fuel;
          vy += 10;                                       // gravity
          if (flame) {
            vx += ldSin(ang) * 20 / 64;
            vy -= ldCos(ang) * 20 / 64;
            fuel--;
            if ((tick / LD_TICK) % 4 == 0) sfx((uint16_t)(90 + random(40)), 30);
          }
          if (vx > 800) vx = 800;
          if (vx < -800) vx = -800;
          if (vy > 800) vy = 800;
          if (vy < -600) vy = -600;
          x += vx; y += vy;
          if (x < 12 * 256) { x = 12 * 256; vx = -vx / 2; }
          if (x > (SCR_W - 12) * 256) { x = (SCR_W - 12) * 256; vx = -vx / 2; }
          if (y < (LD_TOP + 10) * 256) { y = (LD_TOP + 10) * 256; vy = 0; }
          int16_t px = (int16_t)(x / 256), feet = (int16_t)(y / 256 + LD_LEGS);
          if (feet >= ldHeight(px - LD_FOOT) || feet >= ldHeight(px) || feet >= ldHeight(px + LD_FOOT)) {   // touched down
            uint8_t pad = ldPadAt(px);
            bool soft = vy <= LD_SAFEV && vx >= -LD_SAFEH && vx <= LD_SAFEH && ang == 0;
            if (pad && soft) {
              landed = true;
              y = (int32_t)(ldHeight(px) - LD_LEGS) * 256;
              gain = (uint16_t)(50 * pad + fuel / 10);
              score += gain;
              level++;
              sfx(1200, 100);
            } else crashed = true;
          }
        }
        statusBar("LANDER", score, curHigh, C::GREY);
        ldScene(now);
        ldLander((int16_t)(x / 256), (int16_t)(y / 256), ang, flame);
        ldHud(fuel, fuel0, vx, vy, ang, level);
        gfx.send();
      }

      // a moment to look: the lander on its pad, or the crash
      LdSpark sp[24];
      for (uint8_t k = 0; k < 24; k++)
        sp[k] = { (int16_t)(x / 16), (int16_t)(y / 16), (int16_t)random(-40, 41), (int16_t)random(-60, 10), (uint8_t)random(30, 70) };
      if (crashed) sfx(120, 500);
      uint32_t t0 = gameMillis(), last = t0;
      btnClear();
      while (gameMillis() - t0 < 1600) {
        if (!poll()) return;
        uint32_t t = gameMillis();
        uint32_t dt = t - last;
        if (dt > 40) dt = 40;
        last = t;
        statusBar("LANDER", score, curHigh, C::GREY);
        ldScene(t);
        if (crashed) {
          for (uint8_t k = 0; k < 24; k++) {
            LdSpark &p = sp[k];
            if (!p.life) continue;
            p.x += (int16_t)(p.vx * (int32_t)dt / 16); p.y += (int16_t)(p.vy * (int32_t)dt / 16);
            p.vy += (int16_t)dt / 4;
            p.life = p.life > dt / 16 ? (uint8_t)(p.life - dt / 16) : 0;
            gfx.fill(p.x / 16, p.y / 16, 3, 3, p.life > 30 ? (k % 2 ? C::YELLOW : C::ORANGE) : C::GREY);
          }
          uint32_t age = t - t0;
          if (age < 400) gfx.disc((int16_t)(x / 256), (int16_t)(y / 256), (int16_t)(6 + age / 20), age < 150 ? C::WHITE : C::ORANGE);
        } else ldLander((int16_t)(x / 256), (int16_t)(y / 256), ang, false);
        ldHud(fuel, fuel0, vx, vy, ang, (uint8_t)(landed ? level - 1 : level));
        const char *msg = landed ? "LANDED!" : "CRASH";
        int16_t w = (int16_t)(Gfx::width(msg, F_BOLD) + 24);
        panel((SCR_W - w) / 2, 84, w, landed ? 66 : 44, NULL, landed ? C::GREEN : C::RED);
        gfx.center(94, msg, landed ? C::LIME : C::YELLOW, F_BOLD);
        if (landed) {
          char b[24];
          snprintf(b, sizeof(b), "+%u", gain);
          gfx.center(124, b, C::YELLOW);
        }
        gfx.send();
      }
    }
    again = gameOver(score);
  }
}

static const char *const IC_LANDER[12] = {
  "............", "....ssss....", "...swwccs...", "...sssssss..", "..oooooooo..", "..oooooooo..",
  ".s..yyyy..s.", "s....yy....s", "......y.....", "...........s", "ss.....yyyss", "ssssssssssss" };
