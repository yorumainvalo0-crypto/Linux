// MiniArcade: Doom. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  DOOM  (DDA raycaster with 4x4 ordered dithering: walls, floor,
//         ceiling and light/dark wall sides make corridors readable)
// =========================================================
#define D_VY   TOP_H              // viewport top
#define D_VH   (SCR_H - TOP_H)    // viewport height (48)
#define D_COLS 64                 // 64 rays, drawn 2 px wide
#define D_MON  4

/* 16x16 maze, one bit per cell (1 = wall) */
static const uint16_t D_MAP[16] = {
  0xFFFF, 0x8001, 0x8FD1, 0x8811, 0x8BB1, 0x8A21, 0x8AAD, 0x8AA1,
  0x8EA1, 0x82A1, 0xBEAF, 0xA0A1, 0xAFA1, 0xA001, 0xBFFD, 0xFFFF
};
static int16_t  dSin[256];                 // Q10 sine table
static uint16_t dColD[D_COLS];             // wall distance per column (Q8)
static uint8_t  dColS[D_COLS];             // which side was hit (0 = x, 1 = y)

#define DSIN(a) dSin[(uint8_t)(a)]
#define DCOS(a) dSin[(uint8_t)((a) + 64)]

/* 4x4 Bayer matrix: 17 brightness levels on a black and white panel.
   lvl 0 = black, 16 = solid white, everything between is a pattern. */
static const uint8_t D_BAYER[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
static inline bool dInk(int16_t x, int16_t y, int8_t lvl) {
  return lvl > (int8_t)D_BAYER[((y & 3) << 2) | (x & 3)];
}

static bool dWall(int32_t x, int32_t y) {  // x,y in Q8 map units
  int16_t cx = x >> 8, cy = y >> 8;
  if (cx < 0 || cx > 15 || cy < 0 || cy > 15) return true;
  return (D_MAP[cy] >> cx) & 1;
}

/* classic DDA: walks whole grid cells, so it also tells us which face was
   hit - that is what makes edges and corners visible.                    */
static uint16_t dCast(int32_t px, int32_t py, uint8_t ra, uint8_t *side) {
  int32_t rdx = DCOS(ra), rdy = DSIN(ra);
  int32_t ddx = rdx ? labs(262144L / rdx) : 1L << 16;      // step length per cell
  int32_t ddy = rdy ? labs(262144L / rdy) : 1L << 16;
  int16_t mx = px >> 8, my = py >> 8;
  int8_t  sx = rdx < 0 ? -1 : 1, sy = rdy < 0 ? -1 : 1;
  int32_t dx = rdx < 0 ? (((px & 255) * ddx) >> 8) : ((((256 - (px & 255))) * ddx) >> 8);
  int32_t dy = rdy < 0 ? (((py & 255) * ddy) >> 8) : ((((256 - (py & 255))) * ddy) >> 8);

  *side = 0;
  for (uint8_t i = 0; i < 40; i++) {
    if (dx < dy) { mx += sx; dx += ddx; *side = 0; }
    else         { my += sy; dy += ddy; *side = 1; }
    if (mx < 0 || mx > 15 || my < 0 || my > 15) return 4096;
    if ((D_MAP[my] >> mx) & 1) break;
  }
  int32_t perp = (*side == 0) ? dx - ddx : dy - ddy;
  if (perp < 40) perp = 40;
  return (uint16_t)perp;
}

/* steps from every free cell to the player, so monsters walk around walls
   instead of through them (breadth first search over the 16x16 maze)     */
static uint8_t dDist[16][16];

static void dFlow(int32_t px, int32_t py) {
  static const int8_t DIR[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
  uint8_t q[256];
  uint16_t head = 0, tail = 0;
  memset(dDist, 255, sizeof(dDist));
  uint8_t sx = px >> 8, sy = py >> 8;
  dDist[sy][sx] = 0;
  q[tail++] = sy * 16 + sx;
  while (head < tail) {
    uint8_t c = q[head++];
    int8_t cx = c & 15, cy = c >> 4;
    for (uint8_t k = 0; k < 4; k++) {
      int8_t nx = cx + DIR[k][0], ny = cy + DIR[k][1];
      if (nx < 0 || nx > 15 || ny < 0 || ny > 15 || ((D_MAP[ny] >> nx) & 1)) continue;
      if (dDist[ny][nx] != 255) continue;
      dDist[ny][nx] = dDist[cy][cx] + 1;
      q[tail++] = ny * 16 + nx;
    }
  }
}

struct DMon { int32_t x, y; bool alive; };
static DMon dMon[D_MON];          // global: keeps the .ino auto prototypes happy

static void dSpawn(uint8_t i, int32_t px, int32_t py) {
  for (uint8_t t = 0; t < 60; t++) {
    int32_t x = (random(14) + 1) * 256 + 128;
    int32_t y = (random(14) + 1) * 256 + 128;
    if (!dWall(x, y) && abs(x - px) + abs(y - py) > 700) {
      dMon[i].x = x; dMon[i].y = y; dMon[i].alive = true; return;
    }
  }
  dMon[i].alive = false;
}

static void dInitSin() {
  static bool ready = false;
  if (ready) return;
  for (uint16_t i = 0; i < 256; i++) dSin[i] = (int16_t)lroundf(sinf(i * 6.2831853f / 256.0f) * 1024.0f);
  ready = true;
}

void doomRun() {
  dInitSin();

  bool again = true;
  while (again) {
    again = false;
    int32_t px = 2 * 256 + 128, py = 2 * 256 + 128;
    uint8_t ang = 0, hp = 3;
    uint16_t score = 0;
    uint32_t nextStep = 0;
    for (uint8_t i = 0; i < D_MON; i++) dSpawn(i, px, py);
    btnClear();

    while (poll()) {
      uint32_t now = gameMillis();
      if (now >= nextStep) {
        nextStep = now + 50;

        if (btnHeld(B_LEFT))  ang -= 6;
        if (btnHeld(B_RIGHT)) ang += 6;
        int8_t fwd = btnHeld(B_UP) ? 1 : (btnHeld(B_DOWN) ? -1 : 0);
        if (fwd) {
          int32_t nx = px + fwd * (DCOS(ang) >> 5);      // ~1/8 cell per step
          int32_t ny = py + fwd * (DSIN(ang) >> 5);
          if (!dWall(nx, py)) px = nx;
          if (!dWall(px, ny)) py = ny;
        }

        dFlow(px, py);
        for (uint8_t i = 0; i < D_MON; i++) {
          if (!dMon[i].alive) { dSpawn(i, px, py); continue; }
          int8_t  mcx = dMon[i].x >> 8, mcy = dMon[i].y >> 8;
          int32_t tx = px, ty = py;                  // same cell: straight at you
          uint8_t best = dDist[mcy][mcx];
          static const int8_t DIR[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
          for (uint8_t k = 0; k < 4; k++) {          // otherwise to the next cell on the way
            int8_t nx = mcx + DIR[k][0], ny = mcy + DIR[k][1];
            if (nx < 0 || nx > 15 || ny < 0 || ny > 15) continue;
            if (dDist[ny][nx] < best) { best = dDist[ny][nx]; tx = nx * 256 + 128; ty = ny * 256 + 128; }
          }
          int32_t mx = dMon[i].x + constrain(tx - dMon[i].x, -6, 6);
          int32_t my = dMon[i].y + constrain(ty - dMon[i].y, -6, 6);
          if (!dWall(mx, dMon[i].y)) dMon[i].x = mx;
          if (!dWall(dMon[i].x, my)) dMon[i].y = my;
          if (abs(px - dMon[i].x) + abs(py - dMon[i].y) < 90) {   // it got you
            hp--;
            dMon[i].alive = false;
            if (!hp) break;
          }
        }
        if (!hp) { again = gameOver(score); break; }
      }

      // ---- one pass per column: ceiling, wall, floor - no overdraw ----
      statusBar("DOOM", score, curHigh);
      int16_t hor = D_VY + D_VH / 2;
      int8_t  rowLvl[D_VH];                       // floor / ceiling brightness
      for (int16_t y = 0; y < D_VH; y++) {
        int16_t d = (D_VY + y) - hor;
        int8_t  l = (int8_t)((abs(d) * 9) / (D_VH / 2));
        if (d < 0) l = l / 3;                     // ceiling stays dim
        rowLvl[y] = l > 9 ? 9 : l;
      }

      int16_t wallTop[D_COLS], wallH[D_COLS];
      int8_t  wallLvl[D_COLS];
      for (uint8_t c = 0; c < D_COLS; c++) {
        uint8_t sideHit;
        uint8_t ra = ang + (int8_t)(((int16_t)c - D_COLS / 2) * 40 / D_COLS);
        uint16_t d = dCast(px, py, ra, &sideHit);
        int32_t corr = ((int32_t)d * DCOS(ra - ang)) >> 10;      // no fisheye
        if (corr < 40) corr = 40;
        dColD[c] = corr;
        dColS[c] = sideHit;
        int16_t h = (int16_t)((int32_t)D_VH * 256 / corr);
        if (h > D_VH) h = D_VH;
        wallH[c]   = h;
        wallTop[c] = D_VY + (D_VH - h) / 2;
        int8_t lvl = 15 - (int8_t)(corr / 100);                  // distance shading
        if (sideHit) lvl -= 6;                                   // one face darker
        if (lvl < 2)  lvl = 2;
        if (lvl > 16) lvl = 16;
        wallLvl[c] = lvl;
      }

      for (uint8_t c = 0; c < D_COLS; c++) {
        int16_t y0 = wallTop[c], y1 = y0 + wallH[c];
        for (uint8_t k = 0; k < 2; k++) {
          int16_t x = c * 2 + k;
          for (int16_t y = D_VY; y < SCR_H; y++) {
            int8_t lvl = (y >= y0 && y < y1) ? wallLvl[c] : rowLvl[y - D_VY];
            if (dInk(x, y, lvl)) oled.drawPixel(x, y);
          }
        }
        // solid edge where the wall jumps: makes corners and doorways pop
        if (c && (dColS[c] != dColS[c - 1] || abs((int32_t)dColD[c] - dColD[c - 1]) > 80)) {
          int16_t hh = wallH[c] > wallH[c - 1] ? wallH[c] : wallH[c - 1];
          int16_t ey = D_VY + (D_VH - hh) / 2;
          for (int16_t y = ey; y < ey + hh; y++)
            if (y >= D_VY && y < SCR_H) oled.drawPixel(c * 2, y);
        }
      }

      // ---- monsters (billboards, hidden by nearer walls) ----
      bool fire = btnTap(B_OK);
      int32_t fx = DCOS(ang), fy = DSIN(ang);
      int32_t gx = DCOS(ang + 64), gy = DSIN(ang + 64);
      for (uint8_t i = 0; i < D_MON; i++) {
        if (!dMon[i].alive) continue;
        int32_t rx = dMon[i].x - px, ry = dMon[i].y - py;
        int32_t dep = (rx * fx + ry * fy) >> 10;
        if (dep < 60) continue;
        int32_t sid = (rx * gx + ry * gy) >> 10;
        int16_t cx = 64 + (int16_t)((sid * 120) / dep);
        int16_t sz = (int16_t)((int32_t)D_VH * 160 / dep);
        if (sz < 3) sz = 3;
        if (sz > D_VH) sz = D_VH;
        if (cx < -sz || cx > SCR_W + sz) continue;
        uint8_t col = (cx < 0) ? 0 : (cx > 127 ? 63 : cx / 2);
        if (dColD[col] < dep) continue;
        int16_t y0 = D_VY + (D_VH - sz) / 2;
        oled.setDrawColor(0);                                    // black outline
        oled.drawBox(cx - sz / 4 - 1, y0 + sz / 4 - 1, sz / 2 + 2, sz * 3 / 4 + 2);
        oled.setDrawColor(1);
        oled.drawBox(cx - sz / 4, y0 + sz / 4, sz / 2, sz * 3 / 4);
        oled.drawBox(cx - sz / 6, y0, sz / 3, sz / 4);
        if (sz > 10) {
          oled.setDrawColor(0);
          oled.drawBox(cx - sz / 8, y0 + sz / 12, sz / 12 + 1, sz / 12 + 1);
          oled.drawBox(cx + sz / 16, y0 + sz / 12, sz / 12 + 1, sz / 12 + 1);
          oled.setDrawColor(1);
        }
        if (fire && abs(cx - 64) < (sz / 2 + 4)) { dMon[i].alive = false; score += 10; sfx(900, 60); }
      }

      // ---- crosshair, gun, health ----
      oled.setDrawColor(0);
      oled.drawBox(56, 54, 16, 10);
      oled.drawBox(0, 56, 20, 8);
      oled.setDrawColor(1);
      oled.drawHLine(61, 40, 6);
      oled.drawVLine(64, 37, 6);
      oled.drawBox(58, 56, 12, 8);
      oled.drawBox(62, 52, 4, 5);
      for (uint8_t i = 0; i < hp; i++) oled.drawBox(2 + i * 6, 58, 4, 5);
      oled.sendBuffer();
    }
  }
}
