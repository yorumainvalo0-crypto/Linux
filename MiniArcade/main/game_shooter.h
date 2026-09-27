// MiniArcade: side scrolling shooter. Part of MiniArcade.ino, which
// includes it - not compiled on its own.

// =========================================================
//  SHOOTER   (fly right, the ship fires by itself)
// =========================================================
/* Arrows fly the ship, it shoots on its own. OK sets off a bomb that
   clears the screen (three to start with). Waves: drones on a wave line,
   gunners that aim at you, and heavy ships that take three hits and
   sometimes drop a power-up: P = double shot, S = shield, B = bomb.   */
#define SH_Q      8           // positions in 1/8 px
#define SH_EN    12
#define SH_SHOTS 10
#define SH_EBUL  12
#define SH_STARS 16

enum { SE_NONE, SE_DRONE, SE_GUNNER, SE_HEAVY };

struct ShEnemy { uint8_t type, hp, t; int16_t x, y, y0; int8_t amp, phase; uint16_t cool; };
struct ShShot  { bool on; int16_t x, y, vx, vy; };

static ShEnemy shE[SH_EN];
static ShShot  shS[SH_SHOTS], shB[SH_EBUL];
static uint8_t shStar[SH_STARS][2];

static const int8_t SH_SIN[16] = { 0, 3, 6, 7, 8, 7, 6, 3, 0, -3, -6, -7, -8, -7, -6, -3 };

static void shSpawn(uint8_t type, int16_t y, int8_t amp, int8_t phase, int16_t x = SCR_W + 4) {
  for (uint8_t i = 0; i < SH_EN; i++) {
    if (shE[i].type) continue;
    ShEnemy &e = shE[i];
    e.type = type; e.x = x * SH_Q; e.y = e.y0 = y * SH_Q;
    e.amp = amp; e.phase = phase; e.t = 0;
    e.hp = type == SE_HEAVY ? 3 : 1;
    e.cool = 40 + random(60);
    return;
  }
}

static void shFire(ShShot *arr, uint8_t n, int16_t x, int16_t y, int16_t vx, int16_t vy) {
  for (uint8_t i = 0; i < n; i++)
    if (!arr[i].on) { arr[i] = { true, x, y, vx, vy }; return; }
}

static void shShip(int16_t x, int16_t y) {         // 7 x 5, nose to the right
  oled.drawHLine(x, y, 3);
  oled.drawHLine(x + 1, y + 1, 4);
  oled.drawHLine(x + 2, y + 2, 5);
  oled.drawHLine(x + 1, y + 3, 4);
  oled.drawHLine(x, y + 4, 3);
}

static void shEnemyDraw(const ShEnemy &e) {
  int16_t x = e.x / SH_Q, y = e.y / SH_Q;
  switch (e.type) {
  case SE_DRONE:                                    // small diamond
    oled.drawPixel(x + 2, y); oled.drawHLine(x + 1, y + 1, 3); oled.drawHLine(x, y + 2, 5);
    oled.drawHLine(x + 1, y + 3, 3); oled.drawPixel(x + 2, y + 4);
    break;
  case SE_GUNNER:                                   // box with a gun
    oled.drawFrame(x + 2, y, 5, 5); oled.drawHLine(x, y + 2, 2);
    break;
  case SE_HEAVY:                                    // big block, hollow when hurt
    if (e.hp == 3) oled.drawBox(x + 2, y, 7, 7); else oled.drawFrame(x + 2, y, 7, 7);
    oled.drawHLine(x, y + 2, 2); oled.drawHLine(x, y + 4, 2);
    break;
  }
}

static uint8_t shSize(uint8_t type) { return type == SE_HEAVY ? 9 : (type == SE_GUNNER ? 7 : 5); }

void shooterRun() {
  bool again = true;
  while (again) {
    again = false;
    memset(shE, 0, sizeof(shE)); memset(shS, 0, sizeof(shS)); memset(shB, 0, sizeof(shB));
    for (uint8_t i = 0; i < SH_STARS; i++) { shStar[i][0] = random(SCR_W); shStar[i][1] = TOP_H + random(SCR_H - TOP_H); }
    int16_t  px = 10 * SH_Q, py = 36 * SH_Q;
    uint8_t  lives = 3, bombs = 3, shield = 0;
    uint16_t score = 0, dbl = 0, hurt = 0, fireCool = 0, flash = 0;
    int16_t  puX = 0, puY = 0; uint8_t puType = 0;       // one power-up at a time: 1 P, 2 S, 3 B
    uint32_t next = gameMillis(), tick = 0, waveAt = 60;
    btnClear();

    while (poll()) {
      uint32_t now = gameMillis();
      bool dead = false;
      for (uint8_t k = 0; k < 3 && (int32_t)(now - next) >= 0 && !dead; k++) {
        next += 20;
        tick++;
        // ---- ship ----
        if (btnHeld(B_UP))    py -= 12;
        if (btnHeld(B_DOWN))  py += 12;
        if (btnHeld(B_LEFT))  px -= 12;
        if (btnHeld(B_RIGHT)) px += 12;
        if (py < TOP_H * SH_Q)       py = TOP_H * SH_Q;
        if (py > (SCR_H - 5) * SH_Q) py = (SCR_H - 5) * SH_Q;
        if (px < 0)                  px = 0;
        if (px > 60 * SH_Q)          px = 60 * SH_Q;
        if (!fireCool--) {
          fireCool = 12;
          shFire(shS, SH_SHOTS, px + 7 * SH_Q, py + 2 * SH_Q, 28, 0);
          if (dbl) { shFire(shS, SH_SHOTS, px + 5 * SH_Q, py, 26, -4); shFire(shS, SH_SHOTS, px + 5 * SH_Q, py + 4 * SH_Q, 26, 4); }
        }
        if (dbl) dbl--;
        if (hurt) hurt--;
        if (flash) flash--;
        if (btn(B_OK) && bombs) {                   // bomb: everything on screen goes
          bombs--; flash = 8; sfx(120, 300);
          for (uint8_t i = 0; i < SH_EN; i++) if (shE[i].type) { score += 10; shE[i].type = SE_NONE; }
          memset(shB, 0, sizeof(shB));
        }
        // ---- waves: harder as time goes on ----
        if (tick >= waveAt) {
          uint8_t lvl = tick / 1500;                 // every 30 s
          uint8_t kind = random(10);
          if (kind < 5) {                            // a line of drones
            int16_t y = TOP_H + 8 + random(SCR_H - TOP_H - 20);
            int8_t amp = random(2) ? 0 : 1 + random(2);
            for (uint8_t j = 0; j < 4 + (lvl > 2 ? 2 : lvl); j++) shSpawn(SE_DRONE, y, amp, j * 2, SCR_W + 4 + j * 10);
          } else if (kind < 8) {
            for (uint8_t j = 0; j < 1 + (lvl > 1); j++) shSpawn(SE_GUNNER, TOP_H + 4 + random(SCR_H - TOP_H - 12), 0, 0, SCR_W + 4 + j * 20);
          } else shSpawn(SE_HEAVY, TOP_H + 6 + random(SCR_H - TOP_H - 16), 0, 0);
          uint16_t gap = 110 - (lvl < 8 ? lvl * 10 : 80);
          waveAt = tick + gap + random(40);
        }
        // ---- enemies ----
        for (uint8_t i = 0; i < SH_EN; i++) {
          ShEnemy &e = shE[i];
          if (!e.type) continue;
          e.t++;
          e.x -= e.type == SE_DRONE ? 10 : (e.type == SE_GUNNER ? 6 : 4);
          if (e.amp) e.y = e.y0 + SH_SIN[(e.t / 3 + e.phase) & 15] * e.amp * 2;
          if (e.type != SE_DRONE && e.x < (SCR_W - 10) * SH_Q && !e.cool--) {   // aim at the ship
            e.cool = (e.type == SE_HEAVY ? 70 : 100) + random(40);
            int16_t dx = px - e.x, dy = py - e.y;
            int16_t len = abs(dx) + abs(dy);
            if (len > 0) shFire(shB, SH_EBUL, e.x, e.y + 2 * SH_Q, dx * 14 / len, dy * 14 / len);
          }
          if (e.x < -12 * SH_Q) e.type = SE_NONE;
        }
        // ---- shots ----
        for (uint8_t s = 0; s < SH_SHOTS; s++) {
          ShShot &b = shS[s];
          if (!b.on) continue;
          b.x += b.vx; b.y += b.vy;
          if (b.x > SCR_W * SH_Q || b.y < TOP_H * SH_Q || b.y > SCR_H * SH_Q) { b.on = false; continue; }
          for (uint8_t i = 0; i < SH_EN; i++) {
            ShEnemy &e = shE[i];
            uint8_t sz = shSize(e.type);
            if (!e.type || b.x < e.x || b.x > e.x + sz * SH_Q || b.y < e.y - SH_Q || b.y > e.y + (sz - 1) * SH_Q) continue;
            b.on = false;
            if (--e.hp) { sfx(600, 15); break; }
            score += e.type == SE_HEAVY ? 60 : (e.type == SE_GUNNER ? 25 : 10);
            sfx(e.type == SE_HEAVY ? 300 : 900, 40);
            if (!puType && (e.type == SE_HEAVY || random(25) == 0)) {
              puType = 1 + random(3); puX = e.x; puY = e.y;
            }
            e.type = SE_NONE;
            break;
          }
        }
        for (uint8_t s = 0; s < SH_EBUL; s++) {
          ShShot &b = shB[s];
          if (!b.on) continue;
          b.x += b.vx; b.y += b.vy;
          if (b.x < 0 || b.x > SCR_W * SH_Q || b.y < TOP_H * SH_Q || b.y > SCR_H * SH_Q) b.on = false;
        }
        // ---- power-up ----
        if (puType) {
          puX -= 5;
          if (puX < -6 * SH_Q) puType = 0;
          else if (abs(puX - px) < 6 * SH_Q && abs(puY - py) < 6 * SH_Q) {
            if (puType == 1) dbl = 500;
            if (puType == 2) shield = 1;
            if (puType == 3 && bombs < 5) bombs++;
            puType = 0; sfx(1500, 80); score += 20;
          }
        }
        // ---- hits on the ship ----
        if (!hurt) {
          bool hit = false;
          for (uint8_t s = 0; s < SH_EBUL; s++)
            if (shB[s].on && shB[s].x >= px && shB[s].x <= px + 6 * SH_Q && shB[s].y >= py && shB[s].y <= py + 4 * SH_Q) { shB[s].on = false; hit = true; }
          for (uint8_t i = 0; i < SH_EN; i++) {
            ShEnemy &e = shE[i];
            uint8_t sz = shSize(e.type);
            if (e.type && e.x < px + 6 * SH_Q && e.x + sz * SH_Q > px && e.y < py + 5 * SH_Q && e.y + sz * SH_Q > py) { hit = true; e.type = SE_NONE; }
          }
          if (hit) {
            if (shield) { shield = 0; hurt = 60; sfx(500, 80); }
            else if (--lives == 0) dead = true;
            else { hurt = 120; dbl = 0; sfx(200, 300); }
          }
        }
        for (uint8_t i = 0; i < SH_STARS; i++) {    // stars drift left, two speeds
          if (tick % (i % 2 ? 2 : 4) == 0 && shStar[i][0]-- == 0) { shStar[i][0] = SCR_W - 1; shStar[i][1] = TOP_H + random(SCR_H - TOP_H); }
        }
      }
      if ((int32_t)(now - next) > 100) next = now;
      if (dead) { sfx(150, 500); again = gameOver(score); break; }

      // ---- screen ----
      statusBar("SHOOTER", score, curHigh);
      for (uint8_t i = 0; i < SH_STARS; i++) oled.drawPixel(shStar[i][0], shStar[i][1]);
      for (uint8_t i = 0; i < SH_EN; i++) if (shE[i].type) shEnemyDraw(shE[i]);
      for (uint8_t s = 0; s < SH_SHOTS; s++) if (shS[s].on) oled.drawHLine(shS[s].x / SH_Q, shS[s].y / SH_Q, 3);
      for (uint8_t s = 0; s < SH_EBUL; s++) if (shB[s].on) oled.drawBox(shB[s].x / SH_Q, shB[s].y / SH_Q, 2, 2);
      if (puType) {
        static const char PU[4] = { 0, 'P', 'S', 'B' };
        char c[2] = { PU[puType], 0 };
        oled.drawFrame(puX / SH_Q - 1, puY / SH_Q - 1, 7, 9);
        oled.drawStr(puX / SH_Q, puY / SH_Q + 6, c);
      }
      if (!hurt || (hurt / 4) % 2) shShip(px / SH_Q, py / SH_Q);
      if (shield) oled.drawFrame(px / SH_Q - 2, py / SH_Q - 2, 11, 9);
      for (uint8_t i = 1; i < lives; i++) oled.drawBox(2 + (i - 1) * 5, SCR_H - 3, 3, 3);       // lives
      for (uint8_t i = 0; i < bombs; i++) oled.drawFrame(SCR_W - 5 - i * 5, SCR_H - 4, 4, 4);   // bombs
      if (flash) oled.drawFrame(0, TOP_H, SCR_W, SCR_H - TOP_H);
      oled.sendBuffer();
    }
  }
}
