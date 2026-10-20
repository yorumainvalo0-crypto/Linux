// MiniArcade Color: Rocks (Asteroids). Part of console.cpp, which includes
// it. LEFT/RIGHT turn the ship, UP thrusts, OK or DOWN fires (four shots
// at a time). A big rock breaks into two middle ones, those into two small
// ones: 30 / 20 / 10 points, 50 more for a cleared field, and the next
// wave brings more rocks. The field wraps around at the edges. 3 lives;
// after a hit the ship blinks and is safe for 1.5 s.

#define RK_N      9                    // rocks at once
#define RK_SHOTS  4
#define RK_SPARKS 32                   // explosion sparks
#define RK_TOP    TOP_H                // the field: below the status bar
#define RK_FH     (SCR_H - RK_TOP)
#define RK_TICK   15                   // ms per game step
#define RK_SAFE   100                  // steps without collisions after a hit

/* Positions in 1/256 px, speeds in 1/256 px per step: rocks drift at up
   to 50 px/s, the ship flies at up to 156 px/s, shots at 167 px/s plus
   the ship's own speed and live for 0.9 s.                             */
struct RkRock  { int32_t x, y; int16_t vx, vy; uint8_t size, rot, seed; int8_t spin; };
struct RkShot  { int32_t x, y; int16_t vx, vy; uint8_t life; };
struct RkSpark { int16_t x, y, vx, vy; uint8_t life; col c; };   // 1/16 px
struct RkState {
  int16_t sin[256];                    // Q10, 256 steps a turn
  RkRock  rock[RK_N];
  RkShot  shot[RK_SHOTS];
  RkSpark spark[RK_SPARKS];
  uint8_t sparkNext;
};
static RkState *rk = NULL;             // only while the game runs

#define RK_SIN(a) rk->sin[(uint8_t)(a)]
#define RK_COS(a) rk->sin[(uint8_t)((a) + 64)]
static const uint8_t RK_RAD[4] = { 0, 8, 15, 23 };   // radius by size

static void rkWrap(int32_t &x, int32_t &y) {
  if (x < 0)                  x += SCR_W * 256;
  if (x >= SCR_W * 256)       x -= SCR_W * 256;
  if (y < RK_TOP * 256)       y += RK_FH * 256;
  if (y >= SCR_H * 256)       y -= RK_FH * 256;
}

static void rkSpawn(uint8_t i, int32_t x, int32_t y, uint8_t size) {
  RkRock &r = rk->rock[i];
  r.x = x; r.y = y; r.size = size;
  r.vx = (int16_t)(random(-6, 7) * 32);
  r.vy = (int16_t)(random(-6, 7) * 32);
  if (!r.vx && !r.vy) r.vx = 128;
  r.rot = (uint8_t)random(256);
  r.spin = (int8_t)random(-2, 3);
  r.seed = (uint8_t)random(256);
}

// a big rock somewhere not on top of the ship
static void rkSpawnFar(uint8_t i, int32_t sx, int32_t sy) {
  int32_t x = 0, y = 0;
  for (uint8_t tries = 0; tries < 30; tries++) {
    x = random(SCR_W); y = RK_TOP + random(RK_FH);
    int32_t dx = x - sx / 256, dy = y - sy / 256;
    if (dx * dx + dy * dy > 70 * 70) break;
  }
  rkSpawn(i, x * 256, y * 256, 3);
}

static void rkBurst(int32_t x, int32_t y, uint8_t n, col a, col b) {
  for (uint8_t k = 0; k < n; k++) {
    RkSpark &s = rk->spark[rk->sparkNext];
    rk->sparkNext = (uint8_t)((rk->sparkNext + 1) % RK_SPARKS);
    uint8_t ang = (uint8_t)random(256);
    int16_t sp = (int16_t)random(6, 22);
    s = { (int16_t)(x / 16), (int16_t)(y / 16), (int16_t)(RK_COS(ang) * sp / 1024), (int16_t)(RK_SIN(ang) * sp / 1024),
          (uint8_t)random(25, 60), k % 2 ? a : b };
  }
}

// a rock: a lumpy polygon, dark rim and a lit face, at (cx, cy)
static void rkRockAt(const RkRock &r, int16_t cx, int16_t cy) {
  int16_t rad = RK_RAD[r.size];
  int16_t px[9], py[9];
  for (uint8_t k = 0; k < 9; k++) {
    uint8_t h = (uint8_t)((r.seed + k * 37) * 151 >> 3);
    int16_t len = (int16_t)(rad * (74 + h % 27) / 100);
    uint8_t a = (uint8_t)(k * 256 / 9 + r.rot);
    px[k] = (int16_t)(cx + RK_COS(a) * len / 1024);
    py[k] = (int16_t)(cy + RK_SIN(a) * len / 1024);
  }
  col dark = (r.seed & 1) ? rgb(90, 70, 55) : rgb(80, 80, 90);
  col lit = colLight(dark, 1);
  int16_t ox = -rad / 6, oy = -rad / 6;                         // the light comes from the top left
  for (uint8_t k = 0; k < 9; k++) gfx.triangle(cx, cy, px[k], py[k], px[(k + 1) % 9], py[(k + 1) % 9], dark);
  for (uint8_t k = 0; k < 9; k++)
    gfx.triangle(cx + ox, cy + oy, cx + ox + (px[k] - cx) * 3 / 4, cy + oy + (py[k] - cy) * 3 / 4,
                 cx + ox + (px[(k + 1) % 9] - cx) * 3 / 4, cy + oy + (py[(k + 1) % 9] - cy) * 3 / 4, lit);
  if (r.size > 1) {                                             // two craters
    uint8_t a = (uint8_t)(r.rot + r.seed);
    gfx.disc((int16_t)(cx + RK_COS(a) * rad / 2600), (int16_t)(cy + RK_SIN(a) * rad / 2600), rad / 5, dark);
    gfx.disc((int16_t)(cx + RK_COS(a + 120) * rad / 2200), (int16_t)(cy + RK_SIN(a + 120) * rad / 2200), rad / 7, dark);
  }
  for (uint8_t k = 0; k < 9; k++) gfx.line(px[k], py[k], px[(k + 1) % 9], py[(k + 1) % 9], colLight(lit, 2));
}

// a rock near an edge shows on the other side too
static void rkRock(const RkRock &r) {
  int16_t cx = (int16_t)(r.x / 256), cy = (int16_t)(r.y / 256), rad = RK_RAD[r.size];
  for (int8_t i = -1; i <= 1; i++)
    for (int8_t j = -1; j <= 1; j++) {
      int16_t x = cx + i * SCR_W, y = cy + j * RK_FH;
      if (x + rad < 0 || x - rad >= SCR_W || y + rad < RK_TOP || y - rad >= SCR_H) continue;
      rkRockAt(r, x, y);
    }
}

static void rkShip(int16_t cx, int16_t cy, uint8_t ang, bool flame, uint32_t now) {
  int16_t nx = (int16_t)(cx + RK_COS(ang) * 13 / 1024),       ny = (int16_t)(cy + RK_SIN(ang) * 13 / 1024);
  int16_t lx = (int16_t)(cx + RK_COS(ang + 100) * 10 / 1024), ly = (int16_t)(cy + RK_SIN(ang + 100) * 10 / 1024);
  int16_t rx = (int16_t)(cx + RK_COS(ang - 100) * 10 / 1024), ry = (int16_t)(cy + RK_SIN(ang - 100) * 10 / 1024);
  int16_t bx = (int16_t)(cx - RK_COS(ang) * 5 / 1024),        by = (int16_t)(cy - RK_SIN(ang) * 5 / 1024);
  if (flame) {
    int16_t len = (int16_t)(12 + (now / 40) % 3 * 4);
    int16_t fx = (int16_t)(cx - RK_COS(ang) * len / 1024), fy = (int16_t)(cy - RK_SIN(ang) * len / 1024);
    gfx.triangle((lx + bx) / 2, (ly + by) / 2, (rx + bx) / 2, (ry + by) / 2, fx, fy, C::ORANGE);
    gfx.triangle((lx * 2 + bx * 6) / 8, (ly * 2 + by * 6) / 8, (rx * 2 + bx * 6) / 8, (ry * 2 + by * 6) / 8,
                 (fx + bx) / 2, (fy + by) / 2, C::YELLOW);
  }
  gfx.triangle(nx, ny, lx, ly, bx, by, rgb(170, 200, 255));
  gfx.triangle(nx, ny, rx, ry, bx, by, rgb(110, 140, 220));
  gfx.line(nx, ny, lx, ly, C::WHITE);
  gfx.line(nx, ny, rx, ry, C::WHITE);
  gfx.line(lx, ly, bx, by, C::LTGREY);
  gfx.line(rx, ry, bx, by, C::LTGREY);
  gfx.disc((nx + cx) / 2, (ny + cy) / 2, 2, C::CYAN);           // cockpit
}

static void rkSky() {
  gfx.fill(0, RK_TOP, SCR_W, RK_FH, C::BLACK);
  for (uint8_t i = 0; i < 60; i++) {
    uint32_t h = (uint32_t)(i + 3) * 2654435761u;
    gfx.pixel((int16_t)((h >> 8) % SCR_W), (int16_t)(RK_TOP + (h >> 18) % RK_FH), i % 4 ? C::GREY : C::WHITE);
  }
}

void asteroidsRun() {
  rk = (RkState *)malloc(sizeof(RkState));
  if (!rk) return;
  for (uint16_t i = 0; i < 256; i++) rk->sin[i] = (int16_t)lroundf(sinf(i * 6.2831853f / 256.0f) * 1024.0f);
  bool again = true;
  while (again) {
    again = false;
    int32_t sx = (SCR_W / 2) * 256, sy = ((RK_TOP + SCR_H) / 2) * 256, svx = 0, svy = 0;
    uint8_t ang = 192, lives = 3, wave = 1;                     // 192 = nose up
    uint16_t safe = RK_SAFE, score = 0;
    memset(rk->rock, 0, sizeof(rk->rock));
    memset(rk->shot, 0, sizeof(rk->shot));
    memset(rk->spark, 0, sizeof(rk->spark));
    rk->sparkNext = 0;
    for (uint8_t i = 0; i < 4; i++) rkSpawnFar(i, sx, sy);
    uint32_t now = gameMillis(), tick = now, waveAt = now;
    bool over = false, flame = false;
    btnClear();
    while (poll()) {
      now = gameMillis();
      bool fire = btnTap(B_OK) | btn(B_DOWN);
      if ((int32_t)(now - tick) > 40) tick = now - 40;          // a slow frame: catch up at most 40 ms
      while ((int32_t)(now - tick) >= 0 && !over) {
        tick += RK_TICK;
        if (btnHeld(B_LEFT))  ang -= 4;
        if (btnHeld(B_RIGHT)) ang += 4;
        flame = btnHeld(B_UP);
        if (flame) { svx += RK_COS(ang) * 5 / 64; svy += RK_SIN(ang) * 5 / 64; if ((tick / 60) % 2) sfx(70, 20); }
        if (svx * svx + svy * svy > 600 * 600) { svx = svx * 19 / 20; svy = svy * 19 / 20; }   // speed limit
        svx -= svx / 128; svy -= svy / 128;                     // drag
        sx += svx; sy += svy;
        rkWrap(sx, sy);

        if (fire) {
          fire = false;
          for (uint8_t i = 0; i < RK_SHOTS; i++)
            if (!rk->shot[i].life) {
              RkShot &s = rk->shot[i];
              s.x = sx + RK_COS(ang) * 13 / 4; s.y = sy + RK_SIN(ang) * 13 / 4;   // from the nose
              s.vx = (int16_t)(RK_COS(ang) * 5 / 8 + svx);
              s.vy = (int16_t)(RK_SIN(ang) * 5 / 8 + svy);
              s.life = 60;
              sfx(1200, 20);
              break;
            }
        }

        for (uint8_t i = 0; i < RK_SHOTS; i++) {
          RkShot &s = rk->shot[i];
          if (!s.life) continue;
          s.x += s.vx; s.y += s.vy;
          rkWrap(s.x, s.y);
          s.life--;
          for (uint8_t a = 0; a < RK_N && s.life; a++) {
            RkRock &r = rk->rock[a];
            if (!r.size) continue;
            int32_t dx = (s.x - r.x) / 256, dy = (s.y - r.y) / 256, rad = RK_RAD[r.size];
            if (dx * dx + dy * dy < rad * rad) {
              s.life = 0;
              sfx(260, 45);
              score += 10 * r.size;
              rkBurst(r.x, r.y, (uint8_t)(4 + r.size * 3), C::ORANGE, rgb(160, 140, 120));
              uint8_t ns = r.size - 1;
              r.size = 0;
              if (ns)                                           // break into two
                for (uint8_t k = 0, made = 0; k < RK_N && made < 2; k++)
                  if (!rk->rock[k].size) { rkSpawn(k, r.x, r.y, ns); made++; }
            }
          }
        }

        uint8_t alive = 0;
        for (uint8_t a = 0; a < RK_N; a++) {
          RkRock &r = rk->rock[a];
          if (!r.size) continue;
          alive++;
          r.x += r.vx; r.y += r.vy;
          r.rot = (uint8_t)(r.rot + r.spin);
          rkWrap(r.x, r.y);
          int32_t dx = (sx - r.x) / 256, dy = (sy - r.y) / 256, rad = RK_RAD[r.size] + 5;
          if (!safe && dx * dx + dy * dy < rad * rad) {         // the ship is hit
            rkBurst(sx, sy, 16, C::CYAN, C::WHITE);
            rkBurst(r.x, r.y, 6, C::ORANGE, C::YELLOW);
            sfx(150, 400);
            r.size = 0;
            if (--lives == 0) { over = true; break; }
            sx = (SCR_W / 2) * 256; sy = ((RK_TOP + SCR_H) / 2) * 256;
            svx = svy = 0;
            safe = RK_SAFE;                                     // 1.5 s to get going
          }
        }
        if (safe) safe--;
        if (!alive && !over) {                                  // next wave
          wave++;
          score += 50;
          waveAt = now;
          sfx(1500, 120);
          for (uint8_t i = 0; i < 3 + (wave > 3 ? 2 : wave / 2); i++) rkSpawnFar(i, sx, sy);
        }
        for (uint8_t i = 0; i < RK_SPARKS; i++) {
          RkSpark &p = rk->spark[i];
          if (!p.life) continue;
          p.x += p.vx; p.y += p.vy; p.life--;
        }
      }

      // ---- draw ----
      rkSky();
      for (uint8_t a = 0; a < RK_N; a++) if (rk->rock[a].size) rkRock(rk->rock[a]);
      for (uint8_t i = 0; i < RK_SPARKS; i++) {
        const RkSpark &p = rk->spark[i];
        if (p.life) gfx.fill(p.x / 16, p.y / 16, 2, 2, p.life > 20 ? p.c : colDark(p.c, 2));
      }
      for (uint8_t i = 0; i < RK_SHOTS; i++) {
        const RkShot &s = rk->shot[i];
        if (!s.life) continue;
        int16_t x = (int16_t)(s.x / 256), y = (int16_t)(s.y / 256);
        gfx.line(x, y, (int16_t)(x - s.vx / 128), (int16_t)(y - s.vy / 128), C::ORANGE);
        gfx.disc(x, y, 2, C::YELLOW);
        gfx.pixel(x, y, C::WHITE);
      }
      if (!over && (!safe || (safe / 6) & 1)) {                 // blinks while safe
        rkShip((int16_t)(sx / 256), (int16_t)(sy / 256), ang, flame, now);
        if (safe) gfx.circle((int16_t)(sx / 256), (int16_t)(sy / 256), 17, C::BLUE);
      }
      for (uint8_t i = 1; i < lives; i++) {                     // spare ships
        int16_t x = (int16_t)(8 + (i - 1) * 14), y = RK_TOP + 6;
        gfx.triangle(x + 4, y, x, y + 11, x + 8, y + 11, rgb(170, 200, 255));
      }
      char b[16];
      snprintf(b, sizeof(b), "WAVE %u", wave);
      gfx.text(6, SCR_H - 12, b, C::GREY, F_SMALL);
      if (now - waveAt < 1200 && wave > 1)
        gfx.textShadow((SCR_W - Gfx::width(b, F_BOLD)) / 2, 100, b, C::YELLOW, C::DKRED, F_BOLD);
      statusBar("ROCKS", score, curHigh, C::BROWN);            // last: rocks at the top edge stay below it
      gfx.send();
      if (over) { delay(800); break; }
    }
    if (!over) break;
    again = gameOver(score);
  }
  free(rk);
  rk = NULL;
}

static const char *const IC_ROCKS[12] = {
  "............", "...nnnn.....", "..nnsnnnn...", ".nnnnnnnsn..", ".nsnnnnnnn..", "..nnnnnnn...",
  "...nnnn.....", "........y...", "............", ".........w..", "........www.", ".......ww.ww" };
