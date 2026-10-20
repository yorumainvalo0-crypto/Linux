// MiniArcade Color: Tunnel 3D. Part of console.cpp, which includes it.
// Fly through a bending tube of square rings: the arrows move the ship
// (you look out of it). Every fourth ring carries a barrier that closes
// one half of it - move to the open side. Touching the tube wall or a
// barrier ends the flight; a point per ring, the speed grows slowly.
// The tube is drawn far to near as solid walls between the rings, every
// ring its own neon colour, fading into the dark.

#define TUN_RINGS 16           // rings ahead
#define TUN_K     38400        // projection: 150 px half size at one ring ahead
#define TUN_R     30           // ring half size in world units
#define TUN_CX    (SCR_W / 2)
#define TUN_CY    (TOP_H + (SCR_H - TOP_H) / 2)
#define TUN_TICK  30           // ms per game step

// sine, 256 steps per turn, Q10 - the OLED game's table, computed by the compiler
constexpr double tunTaylor(double x) {
  double t = x, s = x;
  for (int k = 1; k < 12; k++) { t *= -x * x / ((2 * k) * (2 * k + 1)); s += t; }
  return s;
}
struct TunSinTab {
  int16_t v[256];
  constexpr TunSinTab() : v() {
    for (int i = 0; i < 256; i++) {
      int q = i / 64, r = i % 64;
      double x = ((q & 1) ? 64 - r : r) * 3.14159265358979 / 128;
      int iv = (int)(tunTaylor(x) * 1024 + 0.5);
      v[i] = (int16_t)(q >= 2 ? -iv : iv);
    }
  }
};
static constexpr TunSinTab TUN_SIN{};

static uint32_t tunSeed;                     // new barrier order every run

static uint8_t tunBar(int32_t idx) {         // 0 = free, 1..4 = blocked side (left, right, top, bottom)
  if (idx < 6 || idx % 4) return 0;          // a few free rings to settle in
  uint32_t h = (uint32_t)idx * 2654435761u + tunSeed;
  return 1 + (uint8_t)((h >> 13) & 3);
}

/* centre of ring number idx; the tube bends with two slow sine waves */
static int16_t tunC(int32_t idx, bool vert) {
  if (vert) return (int16_t)(((int32_t)TUN_SIN.v[(uint8_t)(idx * 5 + 30)] * 14) >> 10);
  return (int16_t)(((int32_t)TUN_SIN.v[(uint8_t)(idx * 7)] * 20) >> 10);
}

/* a convex quad, filled scan line by scan line inside the view */
static void tunQuad(const int32_t *qx, const int32_t *qy, col c) {
  int32_t y0 = qy[0], y1 = qy[0];
  for (uint8_t i = 1; i < 4; i++) { if (qy[i] < y0) y0 = qy[i]; if (qy[i] > y1) y1 = qy[i]; }
  if (y0 < TOP_H) y0 = TOP_H;
  if (y1 > SCR_H - 1) y1 = SCR_H - 1;
  if (y0 > y1) return;
  int32_t slope[4];
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t j = (i + 1) & 3;
    slope[i] = qy[j] != qy[i] ? ((qx[j] - qx[i]) << 16) / (qy[j] - qy[i]) : 0;
  }
  uint8_t *fb = gfx.buffer();
  for (int32_t y = y0; y <= y1; y++) {
    int32_t xl = 1 << 20, xr = -(1 << 20);
    for (uint8_t i = 0; i < 4; i++) {
      uint8_t j = (i + 1) & 3;
      int32_t ya = qy[i], yb = qy[j];
      if ((y < ya && y < yb) || (y > ya && y > yb)) continue;
      int32_t x;
      if (ya == yb) { x = qx[i]; if (qx[j] < xl) xl = qx[j]; if (qx[j] > xr) xr = qx[j]; }
      else x = qx[i] + (int32_t)(((int64_t)slope[i] * (y - ya)) >> 16);
      if (x < xl) xl = x;
      if (x > xr) xr = x;
    }
    if (xl < 0) xl = 0;
    if (xr > SCR_W - 1) xr = SCR_W - 1;
    if (xl <= xr) memset(fb + y * SCR_W + xl, c, (size_t)(xr - xl + 1));
  }
}

// hazard stripes over a rectangle, clipped to the view
static void tunStripes(int32_t x0, int32_t y0, int32_t x1, int32_t y1, col a, col b, int32_t w) {
  if (x0 < 0) x0 = 0;
  if (x1 > SCR_W - 1) x1 = SCR_W - 1;
  if (y0 < TOP_H) y0 = TOP_H;
  if (y1 > SCR_H - 1) y1 = SCR_H - 1;
  if (w < 2) w = 2;
  uint8_t *fb = gfx.buffer();
  for (int32_t y = y0; y <= y1; y++) {
    uint8_t *o = fb + y * SCR_W;
    for (int32_t x = x0; x <= x1; x++) o[x] = ((x + y) / w) & 1 ? a : b;
  }
}

// neon colours along the tube
static col tunHue(int32_t idx) {
  static const col P[6] = { rgb(255, 0, 200), rgb(140, 40, 255), rgb(0, 120, 255), rgb(0, 230, 230), rgb(60, 230, 60), rgb(255, 200, 0) };
  uint32_t i = (uint32_t)(idx < 0 ? 0 : idx);
  return colLerp(P[(i / 6) % 6], P[(i / 6 + 1) % 6], (uint8_t)((i % 6) * 42));
}

void tunnelRun() {
  bool again = true;
  while (again) {
    again = false;
    int16_t  shipX = 0, shipY = 0;
    AxisStep stepX, stepY;                            // tilting: as fast as tilted
    int32_t  travel = 0, ring = 0;          // travel Q8 inside the current ring
    uint16_t score = 0;
    uint8_t  speed = 10;
    uint32_t next = gameMillis();
    bool over = false;
    tunSeed = (uint32_t)random(0x7FFFFFFF);
    btnClear();

    while (poll()) {
      uint32_t now = gameMillis();
      if ((int32_t)(now - next) > 120) next = now - 120;           // a long frame: no huge jump
      while (!over && (int32_t)(now - next) >= 0) {
        next += TUN_TICK;
        shipX = (int16_t)(shipX + stepX(keyAxis(B_LEFT, B_RIGHT), 3));
        shipY = (int16_t)(shipY + stepY(keyAxis(B_UP, B_DOWN), 3));
        shipX = constrain(shipX, -60, 60);
        shipY = constrain(shipY, -40, 40);
        travel += speed;
        if (travel >= 256) {                          // a ring flew past
          travel -= 256;
          ring++;
          score++;
          if (speed < 30 && (score % 10) == 0) speed++;
          int16_t dx = shipX - tunC(ring, false);     // where we sit in the ring
          int16_t dy = shipY - tunC(ring, true);
          bool crash = abs(dx) > TUN_R - 6 || abs(dy) > TUN_R - 6;   // tube wall
          uint8_t bar = tunBar(ring);
          if (!crash && bar) {                        // the barrier covers ~60 %
            int16_t edge = TUN_R / 5;
            if ((bar == 1 && dx < -edge) || (bar == 2 && dx > edge) ||
                (bar == 3 && dy < -edge) || (bar == 4 && dy > edge)) crash = true;
          }
          if (crash) { over = true; sfx(160, 300); break; }
          sfx(950, 15);
        }
      }

      // ---- draw: rings 0..17 projected, then far to near ----
      int32_t rx[TUN_RINGS + 2], ry[TUN_RINGS + 2], rs[TUN_RINGS + 2], rz[TUN_RINGS + 2];
      for (int8_t i = 0; i <= TUN_RINGS + 1; i++) {
        int32_t z = (int32_t)i * 256 - travel;
        if (z < 24) z = 24;
        rz[i] = z;
        rs[i] = TUN_K / z;
        rx[i] = TUN_CX + (int32_t)(tunC(ring + i, false) - shipX) * rs[i] / TUN_R;
        ry[i] = TUN_CY + (int32_t)(tunC(ring + i, true) - shipY) * rs[i] / TUN_R;
      }
      for (int8_t j = TUN_RINGS + 1; j >= 1; j--) {
        int8_t n = j - 1;                             // the wall from ring n to ring j
        uint8_t fog = (uint8_t)(rz[n] * 255 / ((TUN_RINGS + 2) * 256));
        col base = colLerp(tunHue(ring + n), C::BLACK, fog);
        if ((ring + n) & 1) base = colLerp(base, C::BLACK, 50);    // stripes along the tube
        col top = colLight(base), bot = colDark(base), side = colLerp(base, C::BLACK, 30);
        int32_t nl = rx[n] - rs[n], nr = rx[n] + rs[n], nt = ry[n] - rs[n], nb = ry[n] + rs[n];
        int32_t fl = rx[j] - rs[j], fr = rx[j] + rs[j], ft = ry[j] - rs[j], fb = ry[j] + rs[j];
        if (j <= TUN_RINGS && rz[j] >= 40) {          // the barrier on ring j (inside it)
          uint8_t bar = tunBar(ring + j);
          if (bar) {
            int32_t x0 = fl, x1 = fr, y0 = ft, y1 = fb, edge = rs[j] / 5;
            if (bar == 1) x1 = rx[j] - edge;
            if (bar == 2) x0 = rx[j] + edge;
            if (bar == 3) y1 = ry[j] - edge;
            if (bar == 4) y0 = ry[j] + edge;
            uint8_t bf = (uint8_t)(rz[j] * 255 / ((TUN_RINGS + 2) * 256));
            tunStripes(x0, y0, x1, y1, colLerp(C::RED, C::BLACK, bf), colLerp(C::YELLOW, C::BLACK, bf), rs[j] / 4);
          }
        }
        { int32_t qx[4] = { nl, nr, fr, fl }, qy[4] = { nt, nt, ft, ft }; tunQuad(qx, qy, top); }
        { int32_t qx[4] = { nl, fl, fr, nr }, qy[4] = { nb, fb, fb, nb }; tunQuad(qx, qy, bot); }
        { int32_t qx[4] = { nl, fl, fl, nl }, qy[4] = { nt, ft, fb, nb }; tunQuad(qx, qy, side); }
        { int32_t qx[4] = { nr, nr, fr, fr }, qy[4] = { nt, nb, fb, ft }; tunQuad(qx, qy, side); }
        if (j <= TUN_RINGS) {                         // the ring itself, a bright line
          col rc = colLerp(colLight(tunHue(ring + j), 2), C::BLACK, (uint8_t)(rz[j] * 255 / ((TUN_RINGS + 2) * 256)));
          gfx.frame((int16_t)constrain(fl, -2000, 2000), (int16_t)constrain(ft, -2000, 2000),
                    (int16_t)constrain(fr - fl + 1, 0, 4000), (int16_t)constrain(fb - ft + 1, 0, 4000), rc, rs[j] > 60 ? 2 : 1);
        }
      }

      // cockpit: the cross in the middle, the speed below
      gfx.hline(TUN_CX - 12, TUN_CY, 8, C::WHITE);
      gfx.hline(TUN_CX + 5, TUN_CY, 8, C::WHITE);
      gfx.vline(TUN_CX, TUN_CY - 12, 8, C::WHITE);
      gfx.vline(TUN_CX, TUN_CY + 5, 8, C::WHITE);
      gfx.circle(TUN_CX, TUN_CY, 3, C::CYAN);
      gfx.rbox(6, SCR_H - 18, 104, 13, 4, C::BLACK);
      gfx.text(10, SCR_H - 16, "SPEED", C::LTGREY, F_SMALL);
      gfx.fill(40, SCR_H - 14, (int16_t)((speed - 9) * 3), 5, speed < 20 ? C::LIME : C::ORANGE);
      if (over) gfx.frame(0, TOP_H, SCR_W, SCR_H - TOP_H, C::RED, 6);
      statusBar("TUNNEL", score, curHigh, C::CYAN);
      gfx.send();
      if (over) { delay(700); break; }
    }
    if (!over) return;
    again = gameOver(score);
  }
}

// the icon in the library (12 x 12, letters as ICON_KEYS in console.cpp)
static const char *const IC_TUNNEL[12] = {
  "pppppppppppp", "pbbbbbbbbbbp", "pbccccccccbp", "pbcggggggcbp", "pbcgyyyygcbp", "pbcgykkygcbp",
  "pbcgykkygcbp", "pbcgyyyygcbp", "pbcggggggcbp", "pbccccccccbp", "pbbbbbbbbbbp", "pppppppppppp" };
