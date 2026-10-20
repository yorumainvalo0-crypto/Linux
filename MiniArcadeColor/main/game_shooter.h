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
#define SH_SPARKS 40
#define SH_FH     (SCR_H - TOP_H - 14) // the field, above the info line
#define SH_BOT    (TOP_H + SH_FH)
#define SH_PW     20                   // the ship's size
#define SH_PH     14

/* Speeds per step (1/8 px per 10 ms): ship 150 px/s, drones 125, gunners
   88, heavies 62, enemy bullets 150, own shots 375.                     */
enum { SHE_NONE, SHE_DRONE, SHE_GUNNER, SHE_HEAVY };

struct ShEnemy { uint8_t type, hp, flash; int8_t amp, phase; uint16_t t, cool; int16_t x, y, y0; };
struct ShShot  { bool on; int16_t x, y, vx, vy; };
struct ShSpark { int16_t x, y; int8_t vx, vy; uint8_t life; col c; };
struct ShState {
  ShEnemy e[SH_EN];
  ShShot  s[SH_SHOTS], b[SH_EBUL];
  int16_t starX[SH_STARS];
  uint8_t starY[SH_STARS];
  ShSpark spark[SH_SPARKS];
  uint8_t sparkNext;
};
static ShState *sh = NULL;             // only while the game runs

static const int8_t SH_SIN[16] = { 0, 3, 6, 7, 8, 7, 6, 3, 0, -3, -6, -7, -8, -7, -6, -3 };

static const char *const SH_SHIP[7] = {
  "bb........", ".bccc.....", "..cccwwc..", "oocccccccw", "..cccccc..", ".bccc.....", "bb........" };
static const char *const SH_DRONE[7] = {
  "...p...", "..ppp..", ".ppmpp.", "ppmwmpp", ".ppmpp.", "..ppp..", "...p..." };

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

static void shBurst(int16_t x, int16_t y, uint8_t n, col a, col b) {   // x, y in 1/8 px
  for (uint8_t k = 0; k < n; k++) {
    ShSpark &p = sh->spark[sh->sparkNext];
    sh->sparkNext = (uint8_t)((sh->sparkNext + 1) % SH_SPARKS);
    p = { x, y, (int8_t)random(-14, 15), (int8_t)random(-14, 15), (uint8_t)random(20, 50), k % 2 ? a : b };
  }
}

static void shEnemyDraw(const ShEnemy &e, uint32_t now) {
  int16_t x = e.x / SH_Q, y = e.y / SH_Q;
  bool white = e.flash > 0;
  switch (e.type) {
  case SHE_DRONE: {                                    // a spinning diamond
    col pal[4] = { C::PURPLE, (now / 120) % 2 ? C::PINK : C::PURPLE, C::WHITE, 0 };
    gfx.sprite(x, y, SH_DRONE, 7, "pmw", pal, 2);
    break;
  }
  case SHE_GUNNER:                                     // a red pod with a gun
    gfx.fill(x, y + 7, 6, 4, C::LTGREY);
    gfx.rbox(x + 4, y, 14, 18, 4, white ? C::WHITE : C::DKRED);
    gfx.rbox(x + 6, y + 2, 10, 14, 3, white ? C::WHITE : C::RED);
    gfx.disc(x + 10, y + 9, 3, C::YELLOW);
    gfx.pixel(x + 9, y + 8, C::WHITE);
    break;
  case SHE_HEAVY: {                                    // armoured, redder with every hit
    col hull = white ? C::WHITE : (e.hp == 3 ? rgb(120, 130, 160) : (e.hp == 2 ? rgb(170, 120, 90) : rgb(200, 60, 50)));
    gfx.fill(x, y + 5, 6, 3, C::LTGREY);
    gfx.fill(x, y + 16, 6, 3, C::LTGREY);
    gfx.fill(x + 21, y + 7, 4, 10, (now / 60) % 2 ? C::ORANGE : C::YELLOW);
    gfx.rbox(x + 4, y, 19, 24, 5, colDark(hull, 2));
    gfx.rbox(x + 5, y + 1, 16, 21, 4, hull);
    gfx.hline(x + 7, y + 4, 12, colLight(hull, 2));
    gfx.hline(x + 7, y + 19, 12, colDark(hull, 1));
    gfx.disc(x + 12, y + 12, 4, C::CYAN);
    gfx.disc(x + 11, y + 11, 1, C::WHITE);
    break;
  }
  }
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
    uint16_t score = 0, dbl = 0, hurt = 0, fireCool = 0, flash = 0;
    int16_t  puX = 0, puY = 0;
    uint8_t  puType = 0;                                 // one power-up at a time: 1 P, 2 S, 3 B
    uint32_t now = gameMillis(), next = now, tick = 0, waveAt = 120;
    bool dead = false;
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
          fireCool = 24;
          shFire(sh->s, SH_SHOTS, px + SH_PW * SH_Q, py + 7 * SH_Q, 30, 0);
          if (dbl) { shFire(sh->s, SH_SHOTS, px + 14 * SH_Q, py + 2 * SH_Q, 28, -4); shFire(sh->s, SH_SHOTS, px + 14 * SH_Q, py + 12 * SH_Q, 28, 4); }
        }
        if (dbl) dbl--;
        if (hurt) hurt--;
        if (flash) flash--;
        if (bomb) {
          bomb = false;
          if (bombs) {                                   // bomb: everything on screen goes
            bombs--; flash = 24; sfx(120, 300);
            for (uint8_t i = 0; i < SH_EN; i++)
              if (sh->e[i].type) {
                score += 10;
                shBurst(sh->e[i].x + 8 * SH_Q, sh->e[i].y + 8 * SH_Q, 4, C::ORANGE, C::YELLOW);
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
            if (--e.hp) { e.flash = 6; sfx(600, 15); break; }
            score += e.type == SHE_HEAVY ? 60 : (e.type == SHE_GUNNER ? 25 : 10);
            sfx(e.type == SHE_HEAVY ? 300 : 900, 40);
            shBurst((int16_t)(e.x + sz / 2 * SH_Q), (int16_t)(e.y + sz / 2 * SH_Q), (uint8_t)(e.type == SHE_HEAVY ? 14 : 7),
                    C::ORANGE, e.type == SHE_DRONE ? C::PINK : C::YELLOW);
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
              shBurst((int16_t)(e.x + sz / 2), (int16_t)(e.y + sz / 2), 6, C::ORANGE, C::YELLOW);
              e.type = SHE_NONE;
            }
          }
          if (hit) {
            if (shield) { shield = 0; hurt = 120; sfx(500, 80); }
            else {
              shBurst((int16_t)(px + 10 * SH_Q), (int16_t)(py + 7 * SH_Q), 12, C::CYAN, C::WHITE);
              if (--lives == 0) dead = true;
              else { hurt = 240; dbl = 0; sfx(200, 300); }
            }
          }
        }
        for (uint8_t i = 0; i < SH_STARS; i++) {         // stars drift left, three speeds
          static const uint8_t EVERY[3] = { 2, 3, 6 };
          if (tick % EVERY[i % 3] == 0 && --sh->starX[i] < 0) { sh->starX[i] = SCR_W - 1; sh->starY[i] = (uint8_t)random(SH_FH); }
        }
        for (uint8_t i = 0; i < SH_SPARKS; i++) {
          ShSpark &p = sh->spark[i];
          if (p.life) { p.x += p.vx; p.y += p.vy; p.life--; }
        }
      }

      // ---- draw ----
      statusBar("SHOOTER", score, curHigh, C::CYAN);
      gfx.fill(0, TOP_H, SCR_W, SH_FH, flash ? colLerp(C::NAVY, C::WHITE, (uint8_t)(flash * 10)) : C::NAVY);
      int16_t plx = (int16_t)(380 - (int32_t)(tick / 8 % 560));      // a far planet, slower than the stars
      gfx.disc(plx, 150, 42, rgb(40, 20, 80));
      gfx.disc(plx - 8, 142, 33, rgb(80, 40, 130));
      gfx.disc(plx - 16, 134, 14, rgb(110, 70, 170));
      gfx.hline(plx - 70, 158, 140, rgb(140, 110, 190));
      gfx.hline(plx - 62, 159, 124, rgb(90, 70, 140));
      for (uint8_t i = 0; i < SH_STARS; i++) {
        static const col SC[3] = { C::WHITE, C::LTGREY, C::GREY };
        int16_t x = sh->starX[i], y = (int16_t)(TOP_H + sh->starY[i]);
        if (i % 3 == 0) gfx.hline(x, y, 3, SC[0]); else gfx.pixel(x, y, SC[i % 3]);
      }
      for (uint8_t i = 0; i < SH_EN; i++) if (sh->e[i].type) shEnemyDraw(sh->e[i], now);
      for (uint8_t s = 0; s < SH_SHOTS; s++)
        if (sh->s[s].on) {
          int16_t x = sh->s[s].x / SH_Q, y = sh->s[s].y / SH_Q;
          gfx.hline(x - 8, y, 9, C::CYAN);
          gfx.hline(x - 4, y, 5, C::WHITE);
          gfx.hline(x - 6, y - 1, 6, C::BLUE);
        }
      for (uint8_t s = 0; s < SH_EBUL; s++)
        if (sh->b[s].on) {
          int16_t x = sh->b[s].x / SH_Q, y = sh->b[s].y / SH_Q;
          gfx.disc(x, y, 3, C::RED);
          gfx.disc(x, y, 1, C::YELLOW);
        }
      for (uint8_t i = 0; i < SH_SPARKS; i++) {
        const ShSpark &p = sh->spark[i];
        if (p.life) gfx.fill(p.x / SH_Q, p.y / SH_Q, 2, 2, p.life > 15 ? p.c : colDark(p.c, 2));
      }
      if (puType) {
        static const char *const PU[4] = { "", "P", "S", "B" };
        static const col PC[4] = { 0, C::ORANGE, C::CYAN, C::RED };
        int16_t x = puX / SH_Q, y = puY / SH_Q;
        col c = (now / 150) % 2 ? PC[puType] : colLight(PC[puType], 1);
        gfx.rbox(x, y, 16, 15, 5, c);
        gfx.rframe(x, y, 16, 15, 5, C::WHITE);
        gfx.text(x + 6, y + 3, PU[puType], C::WHITE, F_SMALL);
      }
      if (!dead && (!hurt || (hurt / 8) % 2)) {
        col pal[4] = { C::BLUE, rgb(200, 220, 255), C::CYAN, (now / 50) % 2 ? C::ORANGE : C::YELLOW };
        gfx.sprite(px / SH_Q, py / SH_Q, SH_SHIP, 7, "bcwo", pal, 2);
      }
      if (shield && !dead) gfx.circle(px / SH_Q + SH_PW / 2, py / SH_Q + SH_PH / 2, (int16_t)(15 + (now / 100) % 2), C::CYAN);
      // info line: spare ships, double shot time, bombs
      gfx.fill(0, SH_BOT, SCR_W, SCR_H - SH_BOT, rgb(0, 30, 50));
      gfx.hline(0, SH_BOT, SCR_W, C::CYAN);
      for (uint8_t i = 1; i < lives; i++) {
        col pal[4] = { C::BLUE, rgb(200, 220, 255), C::CYAN, C::ORANGE };
        gfx.sprite(6 + (i - 1) * 16, SH_BOT + 4, SH_SHIP, 7, "bcwo", pal, 1);
      }
      if (dbl) {
        gfx.text(80, SH_BOT + 3, "P", C::ORANGE, F_SMALL);
        gfx.fill(88, SH_BOT + 5, dbl / 16, 5, C::ORANGE);
      }
      gfx.text(SCR_W - 104, SH_BOT + 3, "BOMBS", C::GREY, F_SMALL);
      for (uint8_t i = 0; i < bombs; i++) {
        int16_t x = SCR_W - 10 - i * 12;
        gfx.disc(x, SH_BOT + 7, 4, C::RED);
        gfx.pixel(x - 1, SH_BOT + 5, C::WHITE);
      }
      gfx.send();
      if (dead) { sfx(150, 500); delay(800); break; }
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
