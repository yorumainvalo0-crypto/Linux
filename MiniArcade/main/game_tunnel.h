// MiniArcade: Tunnel 3D. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  TUNNEL  (wireframe 3D: fly through a bending tube of rings)
// =========================================================
#define TU_RINGS 16           // rings drawn at once
#define TU_K     15360        // projection constant (60 px at the nearest ring)
#define TU_R     30           // ring half size in world units
#define TU_HOR   (TOP_H + (SCR_H - TOP_H) / 2)

/* draws a line but keeps it inside the blue playing area */
static void tuLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  if (y0 < TOP_H && y1 < TOP_H) return;
  if (y0 != y1) {                                   // clip against the status bar
    if (y0 < TOP_H) { x0 = x0 + (int32_t)(x1 - x0) * (TOP_H - y0) / (y1 - y0); y0 = TOP_H; }
    if (y1 < TOP_H) { x1 = x1 + (int32_t)(x0 - x1) * (TOP_H - y1) / (y0 - y1); y1 = TOP_H; }
  }
  oled.drawLine(x0, y0, x1, y1);
}

/* Every third ring carries a barrier that blocks half of the opening, so
   you have to move out of the way instead of coasting down the middle.  */
static uint32_t tuSeed;                      // new barrier order every run

static uint8_t tuBar(int32_t idx) {          // 0 = free, 1..4 = blocked side
  if (idx < 6 || idx % 4) return 0;          // a few free rings to settle in
  uint32_t h = (uint32_t)idx * 2654435761u + tuSeed;
  return 1 + (uint8_t)((h >> 13) & 3);
}

/* centre of ring number idx; the tube bends with two slow sine waves */
static int16_t tuC(int32_t idx, bool vert) {
  uint8_t a = (uint8_t)(idx * 7 + (vert ? 80 : 0));
  int16_t v = (int16_t)(((int32_t)dSin[a] * 20) >> 10);
  if (vert) v = (int16_t)(((int32_t)dSin[(uint8_t)(idx * 5 + 30)] * 14) >> 10);
  return v;
}

void tunnelRun() {
  dInitSin();
  bool again = true;
  while (again) {
    again = false;
    int16_t  shipX = 0, shipY = 0;
    int32_t  travel = 0, ring = 0;          // travel Q8 inside the current ring
    uint16_t score = 0;
    uint8_t  speed = 10;
    uint32_t next = 0;
    tuSeed = random(0x7FFFFFFF);
    btnClear();

    while (poll()) {
      uint32_t now = gameMillis();
      if (now >= next) {
        next = now + 30;
        if (btnHeld(B_LEFT))  shipX -= 3;
        if (btnHeld(B_RIGHT)) shipX += 3;
        if (btnHeld(B_UP))    shipY -= 3;
        if (btnHeld(B_DOWN))  shipY += 3;
        if (shipX >  60) shipX =  60;
        if (shipX < -60) shipX = -60;
        if (shipY >  40) shipY =  40;
        if (shipY < -40) shipY = -40;

        travel += speed;
        if (travel >= 256) {                          // a ring flew past
          travel -= 256;
          ring++;
          score++;
          if (speed < 30 && (score % 10) == 0) speed++;
          int16_t dx = shipX - tuC(ring, false);      // where we sit in the ring
          int16_t dy = shipY - tuC(ring, true);
          bool crash = (abs(dx) > TU_R - 6 || abs(dy) > TU_R - 6);   // tube wall
          uint8_t bar = tuBar(ring);
          if (!crash && bar) {                         // barrier in this ring
            int16_t edge = TU_R / 5;               // the barrier covers ~40 %
            if ((bar == 1 && dx < -edge) || (bar == 2 && dx > edge) ||
                (bar == 3 && dy < -edge) || (bar == 4 && dy > edge)) crash = true;
          }
          if (crash) {
            sfx(160, 250);
            again = gameOver(score);
            break;
          }
          sfx(950, 15);
        }
      }

      statusBar("TUNNEL", score, curHigh);
      int16_t pcx = 0, pcy = 0, psz = -1;             // previous ring, for the struts
      for (int8_t i = TU_RINGS; i >= 1; i--) {        // far rings first
        int32_t z = (int32_t)i * 256 - travel;
        if (z < 40) continue;
        int16_t sz = (int16_t)(TU_K / z);
        if (sz < 4) continue;
        int16_t cx = 64 + (int16_t)(((int32_t)(tuC(ring + i, false) - shipX) * sz) / TU_R);
        int16_t cy = TU_HOR + (int16_t)(((int32_t)(tuC(ring + i, true) - shipY) * sz) / TU_R);
        int8_t  lvl = (int8_t)(16 - i);               // far rings fade out
        if (lvl < 3) lvl = 3;
        for (int16_t k = -sz; k <= sz; k++) {         // dithered wireframe square
          int16_t x1 = cx + k, y1 = cy + k;
          if (x1 >= 0 && x1 < SCR_W) {
            if (cy - sz >= TOP_H && dInk(x1, cy - sz, lvl)) oled.drawPixel(x1, cy - sz);
            if (cy + sz <  SCR_H  && dInk(x1, cy + sz, lvl)) oled.drawPixel(x1, cy + sz);
          }
          if (y1 >= TOP_H && y1 < SCR_H) {
            if (cx - sz >= 0       && dInk(cx - sz, y1, lvl)) oled.drawPixel(cx - sz, y1);
            if (cx + sz <  SCR_W   && dInk(cx + sz, y1, lvl)) oled.drawPixel(cx + sz, y1);
          }
        }
        uint8_t bar = tuBar(ring + i);                // blocked half of the ring
        if (bar) {
          int16_t x0 = cx - sz, x1 = cx + sz, y0 = cy - sz, y1 = cy + sz;
          int16_t edge = sz / 5;
          if (bar == 1) x1 = cx - edge;               // left side closed
          if (bar == 2) x0 = cx + edge;               // right side closed
          if (bar == 3) y1 = cy - edge;               // top closed
          if (bar == 4) y0 = cy + edge;               // bottom closed
          if (x0 < 0) x0 = 0;                         // near rings are huge: clip first
          if (x1 > SCR_W - 1) x1 = SCR_W - 1;
          if (y0 < TOP_H) y0 = TOP_H;
          if (y1 > SCR_H - 1) y1 = SCR_H - 1;
          for (int16_t yy = y0; yy <= y1; yy++)
            for (int16_t xx = x0; xx <= x1; xx++)
              if (dInk(xx, yy, lvl)) oled.drawPixel(xx, yy);
        }
        if (psz > 0 && i <= 6) {                      // struts along the tube
          tuLine(pcx - psz, pcy - psz, cx - sz, cy - sz);
          tuLine(pcx + psz, pcy - psz, cx + sz, cy - sz);
          tuLine(pcx - psz, pcy + psz, cx - sz, cy + sz);
          tuLine(pcx + psz, pcy + psz, cx + sz, cy + sz);
        }
        pcx = cx; pcy = cy; psz = sz;
      }

      // cockpit marker in the middle of the screen
      oled.drawHLine(60, TU_HOR, 3);
      oled.drawHLine(66, TU_HOR, 3);
      oled.drawVLine(64, TU_HOR - 4, 3);
      oled.drawVLine(64, TU_HOR + 2, 3);
      oled.sendBuffer();
    }
  }
}
