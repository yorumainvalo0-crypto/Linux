// MiniArcade: Rocks (Asteroids). Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  ASTEROIDS
// =========================================================
#define AS_N     9            // rocks on screen at once
#define AS_SHOTS 4
#define AS_TOP   TOP_H

static int16_t asX[AS_N], asY[AS_N], asVX[AS_N], asVY[AS_N];   // Q4 pixels
static uint8_t asSize[AS_N];                                   // 0 = gone, 1..3

static void asWrap(int16_t *x, int16_t *y) {
  if (*x < 0)              *x += SCR_W << 4;
  if (*x >= (SCR_W << 4))  *x -= SCR_W << 4;
  if (*y < (AS_TOP << 4))  *y += (SCR_H - AS_TOP) << 4;
  if (*y >= (SCR_H << 4))  *y -= (SCR_H - AS_TOP) << 4;
}

static void asSpawn(uint8_t i, int16_t x, int16_t y, uint8_t size);

// a random place for a new rock that is not on top of the ship
static void asSpawnFar(uint8_t i, int16_t sx, int16_t sy) {
  int16_t x, y;
  for (uint8_t tries = 0; tries < 30; tries++) {
    x = random(SCR_W); y = AS_TOP + random(SCR_H - AS_TOP);
    int16_t dx = x - (sx >> 4), dy = y - (sy >> 4);
    if (dx * dx + dy * dy > 28 * 28) break;
  }
  asSpawn(i, x << 4, y << 4, 3);
}

static void asSpawn(uint8_t i, int16_t x, int16_t y, uint8_t size) {
  asX[i] = x; asY[i] = y; asSize[i] = size;
  asVX[i] = (int16_t)random(-6, 7);
  asVY[i] = (int16_t)random(-6, 7);
  if (!asVX[i] && !asVY[i]) asVX[i] = 4;
}

void asteroidsRun() {
  dInitSin();
  bool again = true;
  while (again) {
    again = false;
    int16_t sx = (SCR_W / 2) << 4, sy = ((AS_TOP + SCR_H) / 2) << 4, svx = 0, svy = 0;
    uint8_t ang = 192, lives = 3, wave = 1, safe = 50;   // safe: steps without collisions
    int16_t shx[AS_SHOTS], shy[AS_SHOTS], shvx[AS_SHOTS], shvy[AS_SHOTS];
    uint8_t shl[AS_SHOTS];
    uint16_t score = 0;
    uint32_t next = 0;
    for (uint8_t i = 0; i < AS_SHOTS; i++) shl[i] = 0;
    for (uint8_t i = 0; i < AS_N; i++) asSize[i] = 0;
    for (uint8_t i = 0; i < 4; i++) asSpawnFar(i, sx, sy);
    btnClear();

    while (poll()) {
      uint32_t now = millis();
      if (now >= next) {
        next = now + 30;
        if (btnHeld(B_LEFT))  ang -= 8;
        if (btnHeld(B_RIGHT)) ang += 8;
        if (btnHeld(B_UP)) { svx += DCOS(ang) >> 7; svy += DSIN(ang) >> 7; }
        int16_t sp = svx * svx + svy * svy;                    // simple speed limit
        if (sp > 900) { svx = (svx * 9) / 10; svy = (svy * 9) / 10; }
        svx = (svx * 63) / 64; svy = (svy * 63) / 64;          // drag
        sx += svx; sy += svy;
        asWrap(&sx, &sy);

        if (btnTap(B_OK))
          for (uint8_t i = 0; i < AS_SHOTS; i++)
            if (!shl[i]) {
              shx[i] = sx; shy[i] = sy;
              shvx[i] = (DCOS(ang) >> 5) + svx;
              shvy[i] = (DSIN(ang) >> 5) + svy;
              shl[i] = 30;
              break;
            }

        for (uint8_t i = 0; i < AS_SHOTS; i++) {
          if (!shl[i]) continue;
          shx[i] += shvx[i]; shy[i] += shvy[i];
          asWrap(&shx[i], &shy[i]);
          shl[i]--;
          for (uint8_t a = 0; a < AS_N && shl[i]; a++) {
            if (!asSize[a]) continue;
            int16_t dx = (shx[i] - asX[a]) >> 4, dy = (shy[i] - asY[a]) >> 4;
            int16_t r = asSize[a] * 3;
            if (dx * dx + dy * dy < r * r) {
              shl[i] = 0;
              sfx(260, 45);
              score += 10 * asSize[a];
              uint8_t ns = asSize[a] - 1;
              asSize[a] = 0;
              if (ns)                                         // break into two
                for (uint8_t k = 0, made = 0; k < AS_N && made < 2; k++)
                  if (!asSize[k]) { asSpawn(k, asX[a], asY[a], ns); made++; }
            }
          }
        }

        uint8_t alive = 0;
        for (uint8_t a = 0; a < AS_N; a++) {
          if (!asSize[a]) continue;
          alive++;
          asX[a] += asVX[a]; asY[a] += asVY[a];
          asWrap(&asX[a], &asY[a]);
          int16_t dx = (sx - asX[a]) >> 4, dy = (sy - asY[a]) >> 4;
          int16_t r = asSize[a] * 3 + 2;
          if (!safe && dx * dx + dy * dy < r * r) {            // ship hit
            if (--lives == 0) { again = gameOver(score); goto astDone; }
            sx = (SCR_W / 2) << 4; sy = ((AS_TOP + SCR_H) / 2) << 4;
            svx = svy = 0;
            asSize[a] = 0;
            safe = 50;                                         // 1.5 s to get going
          }
        }
        if (safe) safe--;
        if (!alive) {                                          // next wave
          wave++;
          score += 50;
          for (uint8_t i = 0; i < 3 + (wave > 3 ? 2 : wave / 2); i++) asSpawnFar(i, sx, sy);
        }
      }

      for (uint8_t a = 0; a < AS_N; a++) {                     // rocks as polygons
        if (!asSize[a]) continue;
        int16_t cx = asX[a] >> 4, cy = asY[a] >> 4, r = asSize[a] * 3;
        int16_t px = 0, py = 0;
        for (uint8_t k = 0; k <= 6; k++) {
          uint8_t t = (uint8_t)(k * 43 + a * 20);
          int16_t qx = cx + ((DCOS(t) * r) >> 10);
          int16_t qy = cy + ((DSIN(t) * r) >> 10);
          if (k) oled.drawLine(px, py, qx, qy);
          px = qx; py = qy;
        }
      }
      {                                                        // ship
        int16_t cx = sx >> 4, cy = sy >> 4;
        int16_t nx = cx + ((DCOS(ang) * 5) >> 10),      ny = cy + ((DSIN(ang) * 5) >> 10);
        int16_t lx = cx + ((DCOS(ang + 100) * 4) >> 10), ly = cy + ((DSIN(ang + 100) * 4) >> 10);
        int16_t rx = cx + ((DCOS(ang - 100) * 4) >> 10), ry = cy + ((DSIN(ang - 100) * 4) >> 10);
        if (!safe || (safe / 4) & 1) {                         // blinks while safe
          oled.drawLine(nx, ny, lx, ly);
          oled.drawLine(nx, ny, rx, ry);
          oled.drawLine(lx, ly, rx, ry);
        }
      }
      for (uint8_t i = 0; i < AS_SHOTS; i++)
        if (shl[i]) oled.drawBox(shx[i] >> 4, shy[i] >> 4, 2, 2);
      for (uint8_t i = 1; i < lives; i++) oled.drawBox(SCR_W - i * 5, 18, 3, 3);
      oled.setDrawColor(0);                                    // rocks near the top edge
      oled.drawBox(0, 0, SCR_W, TOP_H);                        // must not cover the bar
      oled.setDrawColor(1);
      statusBar("ROCKS", score, curHigh);
      oled.sendBuffer();
    }
    astDone: ;
  }
}
