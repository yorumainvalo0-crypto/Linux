// MiniArcade Color: Shooter, a side scroller. Part of console.cpp, which
// includes it. The arrows fly the ship, it fires by itself; OK sets off a
// bomb that clears the screen (three to start with, five at most). Waves
// get closer together every 30 s: drones on a wave line, gunners that aim
// at you, heavy ships that take three hits and drop a power-up (P double
// shot, S shield, B bomb). Drones 10, gunners 25, heavies 60, a power-up
// 20 points. 3 lives.

#define SH_Q      8                    // positions in 1/8 px
#define SH_TICK   10                   // ms per game step
#define SH_EN     12
#define SH_SHOTS  16
#define SH_EBUL   16
#define SH_STARS  40
#define SH_FH     (SCR_H - TOP_H - 14) // the field, above the info line
#define SH_BOT    (TOP_H + SH_FH)
#define SH_PW     20                   // the ship's size
#define SH_PH     14

/* Speeds per step (1/8 px per 10 ms): ship 150 px/s, drones 125, gunners
   88, heavies 62, enemy bullets 150, own shots 375.                     */
enum { SHE_NONE, SHE_DRONE, SHE_GUNNER, SHE_HEAVY };

struct ShEnemy { uint8_t type, hp, flash; int8_t amp, phase; uint16_t t, cool; int16_t x, y, y0; };
struct ShShot  { bool on; int16_t x, y, vx, vy; };
struct ShState {
  ShEnemy e[SH_EN];
  ShShot  s[SH_SHOTS], b[SH_EBUL];
  int16_t starX[SH_STARS];
  uint8_t starY[SH_STARS];
};
static ShState *sh = NULL;             // only while the game runs

static const int8_t SH_SIN[16] = { 0, 3, 6, 7, 8, 7, 6, 3, 0, -3, -6, -7, -8, -7, -6, -3 };

/* Sprites in full resolution (one character = one pixel). The ship faces
   right, the enemies left; k is the dark outline everywhere.           */
static const char *const SH_SHIP[15] = {
  ".kkkk.....................",
  ".kbbbkk...................",
  "..kbBBbkk.................",
  "..kkbBBBbkkk..............",
  "kk.kkkdsslllkkkk..........",
  "kekkdddsslllllllkkkk......",
  "kEekddsssllllllkccwwkkk...",
  "kEEkddsssllllllkcccwwlllkk",
  "kEekddsssllllllkkkkllllkk.",
  "kekkdddssssrrrrrrrrlkkk...",
  "kk.kkkddssssskkkk.........",
  "..kkbBBBbkkk..............",
  "..kbBBbkk.................",
  ".kbbbkk...................",
  ".kkkk....................." };
static const char SH_SHIP_KEYS[] = "kbBwdslcrEe";
static const char *const SH_MINI[7] = {                  // a spare ship in the info line
  "bb........", ".bccc.....", "..cccwwc..", "oocccccccw", "..cccccc..", ".bccc.....", "bb........" };
static const col SH_MINI_PAL[4] = { C::BLUE, rgb(200, 220, 255), C::CYAN, C::ORANGE };

static const char *const SH_GUNNER[18] = {
  "..........kkkk......",
  "........kkoook......",
  ".......kddddkk......",
  ".....kkdrrrrdkk.....",
  "....kdrrllrrrrdk....",
  "...kdrllwwlrrrrdk...",
  "..kdrrlwwlrrrrrrdk..",
  "..kdrrllrrkkkkrrrdk.",
  "kkkkkrrrrkyyyykrrdk.",
  "gggggsrrrkyywykrrdk.",
  "kkkkkrrrrkyyyykrrdk.",
  "..kdrrrrrrkkkkrrrdk.",
  "..kddrrrrrrrrrrrddk.",
  "...kddrrrrrrrrrddk..",
  "....kkddrrrrrrdkk...",
  ".....kkddddddkk.....",
  ".......kddddkk......",
  "........kkoook......" };
static const char SH_GUNNER_KEYS[] = "kdrlwyogs";

static const char *const SH_HEAVY[24] = {
  "........kkkkkkkk..........",
  "......kkLLLLLLLLkk........",
  ".....kLLHHHHHHHHHhk.......",
  "kkkkkkHHkkkkkkkHHhhk......",
  "ggssssHkhhhhhhhkHhhhkkkkk.",
  "kkkkkkHkhHHHHHhkHHhhhhhhkE",
  "....kLHHkkkkkkkHHHHhhhhhke",
  "...kLHHHHHHHHHHHHHHHhhhhke",
  "..kLHHHHHHkkkkkHHHHHhhhhkE",
  ".kLHHHHHHkcccccckHHHHhhhk.",
  "kLHHHHHHkcccwwccckHHHhhhk.",
  "kHHHHHHHkccwwcccckHHHhhhk.",
  "kHHHHHHHkcccccccckHHHhhhk.",
  "khHHHHHHkcccccccckHHHhhhk.",
  ".khHHHHHHkcccccckHHHHhhhk.",
  "..khHHHHHHkkkkkHHHHHhhhhkE",
  "...khHHHHHHHHHHHHHHHhhhhke",
  "....khHHkkkkkkkHHHHhhhhhke",
  "kkkkkkHkhHHHHHhkHHhhhhhhkE",
  "ggssssHkhhhhhhhkHhhhkkkkk.",
  "kkkkkkHHkkkkkkkHHhhk......",
  ".....khhHHHHHHHHHhk.......",
  "......kkhhhhhhhhkk........",
  "........kkkkkkkk.........." };
static const char SH_HEAVY_KEYS[] = "kHhLcwgsEe";
static uint8_t shSize(uint8_t type) { return type == SHE_HEAVY ? 24 : (type == SHE_GUNNER ? 18 : 14); }

static void shSpawn(uint8_t type, int16_t y, int8_t amp, int8_t phase, int16_t x = SCR_W + 4) {
  for (uint8_t i = 0; i < SH_EN; i++) {
    if (sh->e[i].type) continue;
    ShEnemy &e = sh->e[i];
    e.type = type; e.x = x * SH_Q; e.y = e.y0 = y * SH_Q;
    e.amp = amp; e.phase = phase; e.t = 0; e.flash = 0;
    e.hp = type == SHE_HEAVY ? 3 : 1;
    e.cool = (uint16_t)(80 + random(120));
    return;
  }
}

static void shFire(ShShot *arr, uint8_t n, int16_t x, int16_t y, int16_t vx, int16_t vy) {
  for (uint8_t i = 0; i < n; i++)
    if (!arr[i].on) { arr[i] = { true, x, y, vx, vy }; return; }
}

// an explosion at (x, y) px; big 0..2 (drone, gunner, heavy / the ship)
static void shBoom(int16_t x, int16_t y, uint8_t big) { fxExplode(x, y, (uint8_t)(big + 1)); }

static void shScore(int16_t x, int16_t y, uint16_t pts) {
  char b[8];
  snprintf(b, sizeof(b), "+%u", pts);
  fxText(x, y, b, pts >= 50 ? C::ORANGE : C::YELLOW);
}

static void shEnemyDraw(const ShEnemy &e, uint32_t now) {
  int16_t x = e.x / SH_Q, y = e.y / SH_Q;
  bool white = e.flash > 0;
  switch (e.type) {
  case SHE_DRONE: {                                    // a spinning mine with a glowing eye
    int16_t cx = x + 7, cy = y + 7;
    uint8_t a = (uint8_t)(now / 40 + e.phase * 3);
    for (uint8_t k = 0; k < 4; k++) {                  // four spikes turning
      uint8_t d = (uint8_t)(a + k * 8);
      gfx.line((int16_t)(cx + fxCos(d) * 4 / 64), (int16_t)(cy + fxSin(d) * 4 / 64),
               (int16_t)(cx + fxCos(d) * 8 / 64), (int16_t)(cy + fxSin(d) * 8 / 64), white ? C::WHITE : C::PINK);
      gfx.pixel((int16_t)(cx + fxCos(d) * 8 / 64), (int16_t)(cy + fxSin(d) * 8 / 64), C::WHITE);
    }
    gfx.disc(cx, cy, 6, rgb(51, 0, 51));
    gfx.disc(cx, cy, 5, white ? C::WHITE : rgb(102, 0, 153));
    gfx.disc(cx - 1, cy - 1, 3, white ? C::WHITE : C::PURPLE);
    gfx.fill(cx - 3, cy - 3, 2, 1, rgb(204, 153, 255));
    bool hot = (now / 120 + e.phase) % 3 == 0;
    gfx.disc(cx, cy, 2, hot ? C::WHITE : C::PINK);
    gfx.pixel(cx, cy, hot ? C::PINK : C::WHITE);
    break;
  }
  case SHE_GUNNER: {                                   // a red pod, its eye and gun to the left
    static const col P[9] = { rgb(51, 0, 0), rgb(153, 0, 0), rgb(230, 40, 40), rgb(255, 150, 150), C::WHITE,
                              C::YELLOW, C::ORANGE, GREY4, GREY3 };
    static const col W[9] = { C::WHITE, C::WHITE, C::WHITE, C::WHITE, C::WHITE, C::WHITE, C::WHITE, C::WHITE, C::WHITE };
    col p[9];
    memcpy(p, white ? W : P, sizeof(p));
    if (!white && e.cool < 30) p[5] = (now / 60) % 2 ? C::WHITE : C::ORANGE;   // about to fire: the eye glows
    gfx.sprite(x - 1, y, SH_GUNNER, 18, SH_GUNNER_KEYS, p, 1);
    break;
  }
  case SHE_HEAVY: {                                    // armoured, redder with every hit
    col hull = white ? C::WHITE : (e.hp == 3 ? rgb(130, 140, 170) : (e.hp == 2 ? rgb(180, 130, 90) : rgb(210, 70, 50)));
    col p[10] = { rgb(20, 20, 40), hull, colDark(hull), colLight(hull), C::CYAN, C::WHITE, GREY4, GREY3,
                  (now / 50) % 2 ? C::YELLOW : C::WHITE, C::ORANGE };
    if (e.hp == 1 && !white) p[4] = (now / 100) % 2 ? C::RED : C::ORANGE;   // the core about to blow
    gfx.sprite(x - 1, y, SH_HEAVY, 24, SH_HEAVY_KEYS, p, 1);
    gfx.hline(x + 25, y + 7, 2 + (int16_t)((now / 40) % 3), C::ORANGE);   // engines
    gfx.hline(x + 25, y + 16, 2 + (int16_t)((now / 40 + 1) % 3), C::ORANGE);
    break;
  }
  }
}

static int16_t shSqrt(int32_t v) { int16_t r = 0; while ((int32_t)(r + 1) * (r + 1) <= v) r++; return r; }

// a ring around the planet, the half behind it (back) or in front
static void shRingHalf(int16_t cx, int16_t cy, int16_t rx, int16_t ry, bool back) {
  for (int16_t dx = -rx; dx <= rx; dx++) {
    int16_t dy = (int16_t)(shSqrt((int32_t)ry * ry * (rx * rx - dx * dx) / ((int32_t)rx * rx)));
    int16_t x = cx + dx, y = back ? cy - dy : cy + dy;
    col c = back ? rgb(102, 127, 102) : rgb(230, 212, 153);
    if (!back && (dx & 3) == 0) c = rgb(153, 170, 102);
    gfx.pixel(x, y, c);
    gfx.pixel(x, back ? y - 1 : y + 1, back ? rgb(51, 85, 51) : rgb(153, 127, 102));
  }
}

// deep space: a nebula, a ringed planet (both far and slow) and three star layers
static void shSpace(uint32_t tick) {
  gfx.fill(0, TOP_H, SCR_W, SH_FH, C::NAVY);
  static const int16_t NB[9][3] = { { 0, 40, 30 }, { 30, 26, 26 }, { 58, 44, 28 }, { 92, 30, 22 }, { -28, 62, 22 },
                                    { 20, 70, 24 }, { 120, 52, 18 }, { 70, 84, 20 }, { 150, 36, 14 } };
  for (uint8_t g = 0; g < 2; g++) {                    // two gas clouds, 1/16 of the near stars' speed
    int16_t nx = (int16_t)(SCR_W + 60 - (int32_t)((tick / 16 + g * 560) % 1120));
    if (nx < -200 || nx > SCR_W + 60) continue;
    col a = g ? rgb(0, 42, 102) : rgb(51, 0, 102), b = g ? rgb(0, 85, 102) : rgb(51, 0, 153);
    int16_t y0 = (int16_t)(TOP_H + (g ? 80 : 4));
    for (uint8_t k = 0; k < 9; k++) fxDitherDisc(nx + NB[k][0], y0 + NB[k][1], NB[k][2], a);
    for (uint8_t k = 0; k < 9; k += 2) fxDitherDisc(nx + NB[k][0], y0 + NB[k][1], NB[k][2] / 2, b, 1);
  }
  int16_t plx = (int16_t)(380 - (int32_t)(tick / 8 % 560)), ply = 150;   // the planet
  if (plx > -90 && plx < SCR_W + 90) {
    shRingHalf(plx, ply + 6, 68, 11, true);
    gfx.disc(plx, ply, 40, rgb(0, 42, 51));            // night side, lighter towards the sun
    fxDitherDisc(plx - 5, ply - 5, 36, rgb(0, 85, 102));
    gfx.disc(plx - 7, ply - 7, 32, rgb(0, 85, 102));
    fxDitherDisc(plx - 11, ply - 11, 25, rgb(51, 127, 153));
    gfx.disc(plx - 13, ply - 13, 19, rgb(51, 127, 153));
    gfx.hline(plx - 30, ply + 6, 52, rgb(0, 127, 153));  // cloud bands
    gfx.hline(plx - 34, ply + 14, 50, rgb(0, 42, 51));
    gfx.hline(plx - 26, ply - 12, 30, rgb(102, 170, 204));
    gfx.disc(plx - 20, ply - 20, 5, rgb(153, 212, 204));
    shRingHalf(plx, ply + 6, 68, 11, false);
  }
  for (uint8_t i = 0; i < SH_STARS; i++) {
    int16_t x = sh->starX[i], y = (int16_t)(TOP_H + sh->starY[i]);
    switch (i % 3) {
    case 0:                                            // near: bright streaks, some twinkle
      gfx.hline(x, y, 4, C::LTGREY); gfx.hline(x, y, 2, C::WHITE);
      if ((tick / 6 + i * 7) % 40 == 0) fxStar(x, y, 2, C::CYAN);
      break;
    case 1: gfx.pixel(x, y, C::LTGREY); break;
    default: gfx.pixel(x, y, GREY2); break;              // far
    }
  }
}

static void shShip(int16_t x, int16_t y, uint32_t now, uint8_t scale) {
  col p[11] = { rgb(30, 40, 80), C::BLUE, rgb(102, 170, 255), C::WHITE, rgb(60, 80, 110), rgb(120, 140, 170),
                rgb(205, 215, 245), C::CYAN, C::RED, (now / 40) % 2 ? C::WHITE : C::YELLOW, C::ORANGE };
  gfx.sprite(x, y, SH_SHIP, 15, SH_SHIP_KEYS, p, scale);
}

void shooterRun() {
  sh = (ShState *)malloc(sizeof(ShState));
  if (!sh) return;
  bool again = true;
  while (again) {
    again = false;
    memset(sh, 0, sizeof(ShState));
    for (uint8_t i = 0; i < SH_STARS; i++) { sh->starX[i] = (int16_t)random(SCR_W); sh->starY[i] = (uint8_t)random(SH_FH); }
    int16_t  px = 20 * SH_Q, py = (TOP_H + SH_FH / 2) * SH_Q;
    uint8_t  lives = 3, bombs = 3, shield = 0;
    uint16_t score = 0, dbl = 0, hurt = 0, fireCool = 0;
    uint8_t  muzzle = 0;                                 // ticks the muzzle flash still shows
    int16_t  puX = 0, puY = 0;
    uint8_t  puType = 0;                                 // one power-up at a time: 1 P, 2 S, 3 B
    uint32_t now = gameMillis(), next = now, tick = 0, waveAt = 120, deadAt = 0;
    bool dead = false;
    fxReset();
    btnClear();
    while (poll()) {
      now = gameMillis();
      bool bomb = btnTap(B_OK);
      if ((int32_t)(now - next) > 40) next = now - 40;   // a slow frame: catch up at most 40 ms
      while ((int32_t)(now - next) >= 0 && !dead) {
        next += SH_TICK;
        tick++;
        // ---- ship ----
        if (btnHeld(B_UP))    py -= 15;
        if (btnHeld(B_DOWN))  py += 15;
        if (btnHeld(B_LEFT))  px -= 15;
        if (btnHeld(B_RIGHT)) px += 15;
        if (py < (TOP_H + 2) * SH_Q)       py = (TOP_H + 2) * SH_Q;
        if (py > (SH_BOT - SH_PH) * SH_Q)  py = (SH_BOT - SH_PH) * SH_Q;
        if (px < 0)                        px = 0;
        if (px > 150 * SH_Q)               px = 150 * SH_Q;
        if (!fireCool--) {
          fireCool = 24; muzzle = 5;
          shFire(sh->s, SH_SHOTS, px + SH_PW * SH_Q, py + 7 * SH_Q, 30, 0);
          if (dbl) { shFire(sh->s, SH_SHOTS, px + 14 * SH_Q, py + 2 * SH_Q, 28, -4); shFire(sh->s, SH_SHOTS, px + 14 * SH_Q, py + 12 * SH_Q, 28, 4); }
        }
        if (dbl) dbl--;
        if (hurt) hurt--;
        if (muzzle) muzzle--;
        if (tick % 3 == 0)                               // exhaust sparks
          fxEmit((int16_t)(px / SH_Q - 8), (int16_t)(py / SH_Q + 6 + fxRand(3)), (int16_t)-fxRange(60, 110), (int16_t)fxRange(-12, 12),
                 (uint16_t)fxRange(120, 220), C::YELLOW, C::DKRED, 2);
        if (bomb) {
          bomb = false;
          if (bombs) {                                   // bomb: everything on screen goes
            bombs--; sfx(120, 300);
            fxFlash(C::WHITE, 450); fxShake(5, 450);
            fxRing((int16_t)(px / SH_Q + 10), (int16_t)(py / SH_Q + 7), 120, C::WHITE, C::PURPLE, 450);
            for (uint8_t i = 0; i < SH_EN; i++)
              if (sh->e[i].type) {
                score += 10;
                shBoom((int16_t)(sh->e[i].x / SH_Q + 8), (int16_t)(sh->e[i].y / SH_Q + 8), 0);
                sh->e[i].type = SHE_NONE;
              }
            memset(sh->b, 0, sizeof(sh->b));
          }
        }
        // ---- waves: closer together as time goes on ----
        if (tick >= waveAt) {
          uint8_t lvl = (uint8_t)(tick / 3000);          // every 30 s
          uint8_t kind = (uint8_t)random(10);
          if (kind < 5) {                                // a line of drones
            int16_t y = (int16_t)(TOP_H + 20 + random(SH_FH - 50));
            int8_t amp = random(2) ? 0 : (int8_t)(1 + random(2));
            for (uint8_t j = 0; j < 4 + (lvl > 2 ? 2 : lvl); j++) shSpawn(SHE_DRONE, y, amp, (int8_t)(j * 2), (int16_t)(SCR_W + 4 + j * 25));
          } else if (kind < 8) {
            for (uint8_t j = 0; j < 1 + (lvl > 1); j++) shSpawn(SHE_GUNNER, (int16_t)(TOP_H + 10 + random(SH_FH - 30)), 0, 0, (int16_t)(SCR_W + 4 + j * 50));
          } else shSpawn(SHE_HEAVY, (int16_t)(TOP_H + 10 + random(SH_FH - 40)), 0, 0);
          uint16_t gap = (uint16_t)(220 - (lvl < 8 ? lvl * 20 : 160));
          waveAt = tick + gap + (uint32_t)random(80);
        }
        // ---- enemies ----
        for (uint8_t i = 0; i < SH_EN; i++) {
          ShEnemy &e = sh->e[i];
          if (!e.type) continue;
          e.t++;
          if (e.flash) e.flash--;
          e.x -= e.type == SHE_DRONE ? 10 : (e.type == SHE_GUNNER ? 7 : 5);
          if (e.amp) e.y = (int16_t)(e.y0 + SH_SIN[(e.t / 6 + e.phase) & 15] * e.amp * 9);
          uint8_t sz = shSize(e.type);
          if (e.type != SHE_DRONE && e.x < (SCR_W - 25) * SH_Q && !e.cool--) {   // aim at the ship
            e.cool = (uint16_t)((e.type == SHE_HEAVY ? 140 : 200) + random(80));
            int16_t ex = e.x, ey = (int16_t)(e.y + sz / 2 * SH_Q);
            int16_t dx = (int16_t)(px + SH_PW / 2 * SH_Q - ex), dy = (int16_t)(py + SH_PH / 2 * SH_Q - ey);
            int16_t len = (int16_t)(abs(dx) + abs(dy));
            if (len > 0) { shFire(sh->b, SH_EBUL, ex, ey, (int16_t)(dx * 12 / len), (int16_t)(dy * 12 / len)); sfx(400, 15); }
          }
          if (e.x < -30 * SH_Q) e.type = SHE_NONE;
        }
        // ---- shots ----
        for (uint8_t s = 0; s < SH_SHOTS; s++) {
          ShShot &b = sh->s[s];
          if (!b.on) continue;
          b.x += b.vx; b.y += b.vy;
          if (b.x > SCR_W * SH_Q || b.y < TOP_H * SH_Q || b.y > SH_BOT * SH_Q) { b.on = false; continue; }
          for (uint8_t i = 0; i < SH_EN; i++) {
            ShEnemy &e = sh->e[i];
            uint8_t sz = shSize(e.type);
            if (!e.type || b.x < e.x || b.x > e.x + sz * SH_Q || b.y < e.y - 2 * SH_Q || b.y > e.y + sz * SH_Q) continue;
            b.on = false;
            if (--e.hp) { e.flash = 6; sfx(600, 15); fxBurst((int16_t)(b.x / SH_Q), (int16_t)(b.y / SH_Q), 5, C::WHITE, C::ORANGE, 90, 220); break; }
            uint16_t pts = e.type == SHE_HEAVY ? 60 : (e.type == SHE_GUNNER ? 25 : 10);
            score += pts;
            sfx(e.type == SHE_HEAVY ? 300 : 900, 40);
            int16_t ex = (int16_t)(e.x / SH_Q + sz / 2), ey = (int16_t)(e.y / SH_Q + sz / 2);
            shBoom(ex, ey, (uint8_t)(e.type - 1));
            if (e.type == SHE_DRONE) fxBurst(ex, ey, 4, C::PINK, C::PURPLE, 80, 300);
            if (e.type == SHE_HEAVY) fxShake(3, 250);
            shScore(ex, (int16_t)(ey - sz / 2 - 6), pts);
            if (!puType && (e.type == SHE_HEAVY || random(25) == 0)) {
              puType = (uint8_t)(1 + random(3)); puX = e.x; puY = e.y;
            }
            e.type = SHE_NONE;
            break;
          }
        }
        for (uint8_t s = 0; s < SH_EBUL; s++) {
          ShShot &b = sh->b[s];
          if (!b.on) continue;
          b.x += b.vx; b.y += b.vy;
          if (b.x < 0 || b.x > SCR_W * SH_Q || b.y < TOP_H * SH_Q || b.y > SH_BOT * SH_Q) b.on = false;
        }
        // ---- power-up ----
        if (puType) {
          puX -= 6;
          if (puX < -16 * SH_Q) puType = 0;
          else if (abs(puX - px) < 15 * SH_Q && abs(puY - py) < 15 * SH_Q) {
            if (puType == 1) dbl = 1000;
            if (puType == 2) shield = 1;
            if (puType == 3 && bombs < 5) bombs++;
            fxBurst((int16_t)(puX / SH_Q + 8), (int16_t)(puY / SH_Q + 7), 12, C::WHITE, puType == 1 ? C::ORANGE : (puType == 2 ? C::CYAN : C::RED), 120, 400);
            shScore((int16_t)(px / SH_Q + 10), (int16_t)(py / SH_Q - 10), 20);
            puType = 0; sfx(1500, 80); score += 20;
          }
        }
        // ---- hits on the ship ----
        if (!hurt) {
          bool hit = false;
          for (uint8_t s = 0; s < SH_EBUL; s++) {
            ShShot &b = sh->b[s];
            if (b.on && b.x >= px + 2 * SH_Q && b.x <= px + (SH_PW - 2) * SH_Q && b.y >= py + 2 * SH_Q && b.y <= py + (SH_PH - 2) * SH_Q) { b.on = false; hit = true; }
          }
          for (uint8_t i = 0; i < SH_EN; i++) {
            ShEnemy &e = sh->e[i];
            int16_t sz = shSize(e.type) * SH_Q;
            if (e.type && e.x < px + (SH_PW - 2) * SH_Q && e.x + sz > px + 2 * SH_Q && e.y < py + (SH_PH - 2) * SH_Q && e.y + sz > py + 2 * SH_Q) {
              hit = true;
              shBoom((int16_t)((e.x + sz / 2) / SH_Q), (int16_t)((e.y + sz / 2) / SH_Q), 0);
              e.type = SHE_NONE;
            }
          }
          if (hit) {
            if (shield) { shield = 0; hurt = 120; sfx(500, 80); fxRing((int16_t)(px / SH_Q + 10), (int16_t)(py / SH_Q + 7), 26, C::WHITE, C::CYAN, 300); }
            else {
              fxBurst((int16_t)(px / SH_Q + 10), (int16_t)(py / SH_Q + 7), 14, C::WHITE, C::BLUE, 140, 500);
              fxFlash(C::RED, 300, 110); fxShake(4, 300);
              if (--lives == 0) { dead = true; shBoom((int16_t)(px / SH_Q + 10), (int16_t)(py / SH_Q + 7), 2); fxShake(7, 600); }
              else { hurt = 240; dbl = 0; sfx(200, 300); }
            }
          }
        }
        for (uint8_t i = 0; i < SH_STARS; i++) {         // stars drift left, three speeds
          static const uint8_t EVERY[3] = { 2, 3, 6 };
          if (tick % EVERY[i % 3] == 0 && --sh->starX[i] < 0) { sh->starX[i] = SCR_W - 1; sh->starY[i] = (uint8_t)random(SH_FH); }
        }
      }

      // ---- draw ----
      fxUpdate();
      statusBar("SHOOTER", score, curHigh, C::CYAN);
      shSpace(tick);
      for (uint8_t i = 0; i < SH_EN; i++) if (sh->e[i].type) shEnemyDraw(sh->e[i], now);
      for (uint8_t s = 0; s < SH_SHOTS; s++)
        if (sh->s[s].on) {                               // a bolt of light: white core, blue glow
          int16_t x = sh->s[s].x / SH_Q, y = sh->s[s].y / SH_Q;
          gfx.hline(x - 11, y, 12, C::BLUE);
          gfx.hline(x - 7, y - 1, 7, rgb(0, 102, 204));
          gfx.hline(x - 7, y + 1, 7, rgb(0, 102, 204));
          gfx.hline(x - 7, y, 8, C::CYAN);
          gfx.hline(x - 4, y, 5, C::WHITE);
        }
      for (uint8_t s = 0; s < SH_EBUL; s++)
        if (sh->b[s].on) {                               // a hot plasma ball, pulsing
          int16_t x = sh->b[s].x / SH_Q, y = sh->b[s].y / SH_Q;
          bool big = (now / 70 + s) % 2;
          gfx.disc(x, y, big ? 4 : 3, C::DKRED);
          gfx.disc(x, y, 2, C::RED);
          gfx.disc(x, y, 1, C::YELLOW);
          gfx.pixel(x, y, C::WHITE);
        }
      if (puType) {                                      // a capsule in a pulsing glow, a spark around it
        static const char *const PU[4] = { "", "P", "S", "B" };
        static const col PC[4] = { 0, C::ORANGE, C::CYAN, C::RED };
        int16_t x = puX / SH_Q, y = puY / SH_Q, cx = x + 8, cy = y + 7;
        col c = PC[puType];
        fxGlow(cx, cy, (int16_t)(13 + (now / 120) % 3), colDark(c, 3), colDark(c, 1), 3);
        gfx.rbox(x, y, 16, 15, 5, colDark(c));
        gfx.rbox(x + 1, y + 1, 14, 12, 4, c);
        gfx.hline(x + 4, y + 2, 8, colLight(c, 2));
        gfx.rframe(x, y, 16, 15, 5, C::WHITE);
        gfx.textShadow(x + 6, y + 3, PU[puType], C::WHITE, colDark(c, 2), F_SMALL);
        uint8_t a = (uint8_t)(now / 30);
        fxStar((int16_t)(cx + fxCos(a) * 13 / 64), (int16_t)(cy + fxSin(a) * 13 / 64), 2, colLight(c, 2));
      }
      if (dead && !deadAt) { deadAt = now; sfx(150, 500); }
      if (!dead && (!hurt || (hurt / 8) % 2)) {
        int16_t x = px / SH_Q, y = py / SH_Q;
        int16_t fl = (int16_t)(3 + (now / 30) % 4);      // the engine's flame
        gfx.fill(x - 5 - fl, y + 6, fl, 3, C::ORANGE);
        gfx.hline(x - 5 - fl / 2, y + 7, fl / 2, C::YELLOW);
        shShip((int16_t)(x - 5), y, now, 1);
        if (muzzle) {                                    // the gun just fired
          gfx.disc(x + 21, y + 7, muzzle > 2 ? 3 : 2, C::CYAN);
          fxStar(x + 21, y + 7, muzzle > 2 ? 5 : 3, C::WHITE);
        }
      }
      fxDraw(0, 0, TOP_H, SH_BOT);
      if (shield && !dead) {                             // a bubble with a glint going round
        int16_t cx = px / SH_Q + SH_PW / 2, cy = py / SH_Q + SH_PH / 2, r = (int16_t)(15 + (now / 100) % 2);
        gfx.circle(cx, cy, r, C::CYAN);
        gfx.circle(cx, cy, r - 1, rgb(0, 102, 153));
        uint8_t a = (uint8_t)(now / 40);
        for (int8_t k = -2; k <= 2; k++)
          gfx.pixel((int16_t)(cx + fxCos((uint8_t)(a + k)) * r / 64), (int16_t)(cy + fxSin((uint8_t)(a + k)) * r / 64), C::WHITE);
      }
      fxShakeApply(TOP_H, SH_BOT);
      fxFlashApply(0, TOP_H, SCR_W, SH_FH);
      // info line: spare ships, double shot time, bombs
      gfx.fill(0, SH_BOT, SCR_W, SCR_H - SH_BOT, rgb(0, 30, 50));
      gfx.hline(0, SH_BOT, SCR_W, C::CYAN);
      for (uint8_t i = 1; i < lives; i++) gfx.sprite(6 + (i - 1) * 16, SH_BOT + 4, SH_MINI, 7, "bcwo", SH_MINI_PAL, 1);
      if (dbl) {
        gfx.text(80, SH_BOT + 3, "P", C::ORANGE, F_SMALL);
        gfx.fill(88, SH_BOT + 5, dbl / 16, 5, C::ORANGE);
      }
      gfx.text(SCR_W - 104, SH_BOT + 3, "BOMBS", C::GREY, F_SMALL);
      for (uint8_t i = 0; i < bombs; i++) {
        int16_t x = SCR_W - 10 - i * 12;
        gfx.disc(x, SH_BOT + 7, 4, C::DKRED);
        gfx.disc(x - 1, SH_BOT + 6, 3, C::RED);
        gfx.pixel(x - 2, SH_BOT + 5, C::WHITE);
        gfx.pixel(x + 2, SH_BOT + 2, (now / 150 + i) % 2 ? C::YELLOW : C::ORANGE);   // the fuse
      }
      gfx.send();
      if (dead && now - deadAt > 900) break;             // the explosion plays out
    }
    if (!dead) break;
    again = gameOver(score);
  }
  free(sh);
  sh = NULL;
}

static const char *const IC_SHOOTER[12] = {
  "............", ".........pp.", "........pppp", ".........pp.", "............", "bb..........",
  ".bccc...y..y", "oocccccw....", ".bccc...y..y", "bb..........", "..........rr", ".........rrr" };
