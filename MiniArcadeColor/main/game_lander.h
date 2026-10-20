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

static uint8_t ldHash(uint32_t a) { a *= 2654435761u; a ^= a >> 15; a *= 0x2c1b3c6du; a ^= a >> 12; return (uint8_t)a; }

// a filled ellipse (craters)
static void ldEllipse(int16_t cx, int16_t cy, int16_t rx, int16_t ry, col c) {
  for (int16_t dy = -ry; dy <= ry; dy++) {
    int16_t dx = 0;
    while ((int32_t)(dx + 1) * (dx + 1) * ry * ry + (int32_t)dy * dy * rx * rx <= (int32_t)rx * rx * ry * ry) dx++;
    gfx.hline(cx - dx, cy + dy, 2 * dx + 1, c);
  }
}

// the sky (stars, a faint milky way, the earth) and the moon's ground with its pads; all still
static void ldScene(uint32_t now) {
  gfx.fill(0, TOP_H, SCR_W, SCR_H - TOP_H, C::BLACK);
  for (uint16_t i = 0; i < 260; i++) {                  // the milky way: a faint diagonal band
    uint8_t a = ldHash(i * 3 + 1), b = ldHash(i * 3 + 2);
    int16_t x = (int16_t)(i * 320 / 260), y = (int16_t)(LD_TOP + 100 - x / 4 + (int16_t)(a % 40) - 20 + (int16_t)(b % 16) - 8);
    gfx.pixel(x + (b & 7), y, a & 1 ? GREY1 : rgb(51, 42, 102));
  }
  for (uint8_t i = 0; i < 60; i++) {
    uint32_t h = (uint32_t)(i + 11) * 2654435761u;
    int16_t x = (int16_t)((h >> 8) % SCR_W), y = (int16_t)(LD_TOP + (h >> 18) % 120);
    uint8_t k = i % 6;
    col c = k == 0 ? C::WHITE : (k == 1 ? rgb(204, 212, 255) : (k == 2 ? rgb(255, 212, 153) : (k < 5 ? GREY3 : GREY2)));
    if (k == 0 && (now / 250 + i) % 9 == 0) fxStar(x, y, 2, GREY3);   // a few twinkle
    else gfx.pixel(x, y, c);
  }
  const int16_t EX = 272, EY = 74;                      // the earth: oceans, land, clouds, night side
  gfx.circle(EX, EY, 17, rgb(51, 85, 153));
  gfx.disc(EX, EY, 16, rgb(51, 102, 255));
  gfx.disc(EX - 4, EY - 4, 6, rgb(51, 170, 51));
  gfx.disc(EX - 1, EY - 7, 4, rgb(51, 170, 51));
  gfx.disc(EX + 6, EY + 6, 4, rgb(102, 153, 51));
  gfx.disc(EX - 6, EY + 8, 3, C::WHITE);
  gfx.hline(EX - 9, EY + 1, 7, C::WHITE); gfx.hline(EX + 2, EY - 10, 6, C::WHITE);
  for (int16_t dy = -16; dy <= 16; dy++)                // night: a shadow disc moved down right, a soft edge
    for (int16_t dx = -16; dx <= 16; dx++) {
      if (dx * dx + dy * dy > 16 * 16 + 16) continue;
      int16_t d2 = (dx - 9) * (dx - 9) + (dy - 9) * (dy - 9);
      if (d2 < 19 * 19) gfx.pixel(EX + dx, EY + dy, rgb(0, 0, 51));
      else if (d2 < 22 * 22 && ((dx + dy) & 1)) gfx.pixel(EX + dx, EY + dy, rgb(0, 42, 102));
    }
  gfx.pixel(EX - 9, EY - 9, C::WHITE);
  for (int16_t x = 0; x < SCR_W; x++) {                 // the ground: lit where it faces the sun (up left)
    int16_t h = ldHeight(x), d = ldHeight(x + 1) - h;
    col rim = d < 0 ? GREY4 : (d == 0 ? C::LTGREY : GREY3);
    gfx.pixel(x, h, d < 0 ? C::WHITE : rim);
    gfx.vline(x, h + 1, 2, rim);
    gfx.vline(x, h + 3, 5, d > 1 ? GREY2 : GREY3);
    for (int16_t y = h + 8; y < h + 11; y++) gfx.pixel(x, y, (x + y) & 1 ? GREY3 : GREY2);
    gfx.vline(x, h + 11, 9, GREY2);
    for (int16_t y = h + 20; y < h + 24; y++) gfx.pixel(x, y, (x + y) & 1 ? GREY2 : GREY1);
    gfx.vline(x, h + 24, SCR_H - h - 24, GREY1);
  }
  for (uint8_t k = 0; k < 7; k++) {                     // craters in the ground, from the ground's own numbers
    uint8_t a = ldHash((uint32_t)(ldGround[k * 4] * 7 + k)), b = ldHash((uint32_t)(ldGround[k * 4 + 2] + k * 31));
    int16_t x = (int16_t)(10 + k * 45 + a % 30), rx = (int16_t)(5 + b % 8), ry = (int16_t)(2 + b % 2);
    int16_t y = (int16_t)(ldHeight(x) + 12 + a % 14);
    if (ldPadAt(x) || y + ry >= SCR_H) continue;
    ldEllipse(x, y + 1, rx, ry, GREY3);                  // the lit far rim, then the hollow
    ldEllipse(x, y, rx, ry, GREY1);
    gfx.hline(x - rx / 2, y - ry, rx, GREY1);
  }
  for (uint8_t i = 0; i + 1 < LD_PTS; i++) {            // the pads: plates with hazard stripes on struts
    if (!ldPad[i]) continue;
    int16_t x = i * LD_SEG, y = ldGround[i], len = ldPadLen(ldPad[i]);
    gfx.fill(x + 3, y + 4, 2, 5, GREY3); gfx.fill(x + len - 5, y + 4, 2, 5, GREY3);
    gfx.fill(x, y, len, 4, GREY4);
    for (int16_t k = 0; k < len; k += 6) {
      gfx.fill(x + k, y + 1, 3, 2, C::YELLOW);
      gfx.fill(x + k + 3, y + 1, 3 > len - k - 3 ? len - k - 3 : 3, 2, rgb(51, 42, 0));
    }
    gfx.hline(x, y, len, C::WHITE);
    gfx.hline(x, y + 3, len, GREY3);
    bool on = (now / 400) % 2;
    gfx.fill(x, y - 4, 2, 4, GREY3); gfx.fill(x + len - 2, y - 4, 2, 4, GREY3);
    gfx.disc(x + 1, y - 5, 2, on ? C::RED : C::DKRED);
    gfx.disc(x + len - 2, y - 5, 2, on ? C::GREEN : C::DKGRN);
    if (on) { gfx.pixel(x + 1, y - 6, C::WHITE); gfx.pixel(x + len - 2, y - 6, C::WHITE); }
    char b[6];
    snprintf(b, sizeof(b), "x%u", ldPad[i]);
    int16_t ty = y + 10 + 9 < SCR_H ? y + 10 : y - 16;
    gfx.textShadow(x + len / 2 - 5, ty, b, ldPad[i] == 5 ? C::PINK : C::CYAN, C::BLACK, F_SMALL);
  }
}

/* The lander, 25 x 22 in full resolution, turned to its direction: k
   outline, l / d light and dark hull, w glints, c / C window, g / G / o
   the gold foil of the descent stage, s struts and antennas.        */
static const char *const LD_SHIP[22] = {
  "............w............",
  "............s.....sss....",
  "........kkkkkkkkk..s.....",
  ".......kkwllllldkk.s.....",
  ".......kwlwwcclddks......",
  ".......kwlCccclddk.......",
  ".......kllccCClddk.......",
  ".......klllllllddk.......",
  ".......kdddddddddk.......",
  "....kkkkkkkkkkkkkkkkk....",
  "....kGGGggGggGggGggGk....",
  "....kGGggggggggggggok....",
  "....kGGggGggGggGggGok....",
  "....kGGoooooooooooook....",
  "....kkkkkkkkkkkkkkkkk....",
  ".....sd.dkssssskd.ds.....",
  "....sd.d..sdddd..d.ds....",
  "....sdd..sdddddd..dds....",
  "...sd...kkkkkkkkk...ds...",
  "...dd...............dd...",
  ".ssss...............ssss.",
  "kkkkk...............kkkkk" };
static const char LD_KEYS[] = "kwldcCgGos";
static const col  LD_PAL[10] = { GREY1, C::WHITE, GREY4, GREY3, C::CYAN, rgb(0, 102, 153),
                                 rgb(220, 170, 40), rgb(255, 230, 120), rgb(190, 110, 20), GREY4 };

// the lander at its centre, turned by ang; flame = the engine burns
static void ldLander(int16_t cx, int16_t cy, uint8_t ang, bool flame) {
  int16_t s = ldSin(ang), c = ldCos(ang);
  auto R = [&](int8_t dx, int8_t dy, int16_t &ox, int16_t &oy) {
    ox = (int16_t)(cx + (dx * c - dy * s) / 64); oy = (int16_t)(cy + (dx * s + dy * c) / 64);
  };
  if (flame) {                                          // the flame's core; the particles do the rest
    int16_t ax, ay, bx, by, fx, fy, gx, gy;
    R(-3, 7, ax, ay); R(3, 7, bx, by);
    R(0, (int8_t)(14 + fxRand(6)), fx, fy);
    R(0, (int8_t)(10 + fxRand(3)), gx, gy);
    gfx.triangle(ax, ay, bx, by, fx, fy, C::ORANGE);
    gfx.triangle(ax, ay, bx, by, gx, gy, C::YELLOW);
    R(0, 8, gx, gy);
    gfx.pixel(gx, gy, C::WHITE);
  }
  fxSpriteTurn(cx, cy, LD_SHIP, 22, 12, 12, LD_KEYS, LD_PAL, s, c);
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
      fxReset();
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
            int16_t s = ldSin(ang), c = ldCos(ang), cx = (int16_t)(x / 256), cy = (int16_t)(y / 256);
            int16_t lvx = (int16_t)(vx * 50 / 256), lvy = (int16_t)(vy * 50 / 256);   // px/s
            for (uint8_t k = 0; k < 2; k++) {             // exhaust: out of the nozzle, away from the lander
              int16_t sp = fxRange(70, 130), side = fxRange(-14, 14);
              fxEmit((int16_t)(cx - 8 * s / 64), (int16_t)(cy + 8 * c / 64), (int16_t)(lvx + (-s * sp + c * side) / 64),
                     (int16_t)(lvy + (c * sp + s * side) / 64), (uint16_t)fxRange(200, 340), k ? C::YELLOW : C::ORANGE, C::DKRED, (uint8_t)(2 + k), 0, FX_DRAG);
            }
            int16_t gx = (int16_t)(cx - 9 * s / 64), gh = ldHeight(gx), dd = (int16_t)(gh - cy - 9);
            if (dd >= 0 && dd < 45) {                     // the blast kicks up dust below
              int8_t side = fxRand(2) ? 1 : -1;
              fxEmit((int16_t)(gx + fxRange(-4, 4)), (int16_t)(gh - 1), (int16_t)(side * fxRange(40, 110) * (50 - dd) / 50),
                     (int16_t)-fxRange(10, 45), (uint16_t)fxRange(350, 600), GREY4, GREY2, 2, 12, FX_DRAG);
            }
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
              for (int8_t side = -1; side <= 1; side += 2)  // a soft touch: dust to both sides
                for (uint8_t k = 0; k < 6; k++)
                  fxEmit((int16_t)(px + side * fxRange(4, 12)), (int16_t)(feet - 1), (int16_t)(side * fxRange(30, 90)), (int16_t)-fxRange(5, 35),
                         (uint16_t)fxRange(400, 700), GREY4, GREY2, 2, 10, FX_DRAG);
            } else {                                      // a crash: fire, gold foil and metal flying, a shock
              crashed = true;
              int16_t cy = (int16_t)(y / 256);
              fxExplode(px, cy, 3);
              fxBurst(px, cy, 6, rgb(255, 230, 120), rgb(190, 110, 20), 150, 900, 40, 2);
              fxBurst(px, cy, 5, GREY4, GREY1, 120, 900, 40, 3);
              fxShake(7, 600); fxFlash(C::WHITE, 250, 140);
            }
          }
        }
        fxUpdate();
        statusBar("LANDER", score, curHigh, C::GREY);
        ldScene(now);
        fxDraw(0, 0, LD_TOP, SCR_H);
        if (!crashed) ldLander((int16_t)(x / 256), (int16_t)(y / 256), ang, flame);
        ldHud(fuel, fuel0, vx, vy, ang, level);
        gfx.send();
      }

      // a moment to look: the lander on its pad, or the crash (the explosion first, then the word)
      if (crashed) sfx(120, 500);
      uint32_t t0 = gameMillis(), smokeAt = 0;
      btnClear();
      while (gameMillis() - t0 < 1600) {
        if (!poll()) return;
        uint32_t t = gameMillis();
        fxUpdate();
        statusBar("LANDER", score, curHigh, C::GREY);
        ldScene(t);
        if (crashed && t - smokeAt > 70 && t - t0 > 200) {   // the wreck smokes
          smokeAt = t;
          fxEmit((int16_t)(x / 256 + fxRange(-6, 6)), (int16_t)(y / 256 + 4), (int16_t)fxRange(-8, 8), -25, 700, GREY3, GREY1, 3, -2, FX_GROW);
        }
        fxDraw(0, 0, LD_TOP, SCR_H);
        if (!crashed) ldLander((int16_t)(x / 256), (int16_t)(y / 256), ang, false);
        fxShakeApply(LD_TOP, SCR_H);
        fxFlashApply(0, LD_TOP, SCR_W, SCR_H - LD_TOP);
        ldHud(fuel, fuel0, vx, vy, ang, (uint8_t)(landed ? level - 1 : level));
        if (crashed && t - t0 < 350) { gfx.send(); continue; }
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
