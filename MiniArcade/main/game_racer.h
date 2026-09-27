// MiniArcade: Racer. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  RACER   (endless road seen from above)
// =========================================================
/* The other cars drive in three lanes and come in rows. A row blocks at most
   two lanes, and the distance to the row before is chosen from the current
   speed so that a free lane can always be reached in time - the road never
   closes up. How far apart the rows are is random beyond that minimum.    */
#define RC_TOP  TOP_H
#define RC_CARY 52            // player car y
#define RC_OPP   3
#define RC_HALF 26            // half road width
#define RC_LANE 17            // lane centres at -17, 0, +17 from the middle
#define RC_STEP  3            // sideways pixels per step

struct RcState {
  uint8_t  spd, curve;
  uint32_t dist;                       // 16 bit ran over after ~4 minutes
  int16_t  opx[RC_OPP], opy[RC_OPP];   // x relative to the road middle, y on screen
  int16_t  topY, prevY;                // newest row of cars and the one below it
  uint8_t  topMask, prevMask;          // lanes they block (bit 0 = left lane)
};
static RcState rc;

static int16_t rcMid(uint8_t curve, int16_t row) {      // road centre for a row
  return SCR_W / 2 + ((DSIN((uint8_t)(curve + row)) * 18) >> 10);
}

// worst case number of lanes to cross from a free lane of one row to the next
static uint8_t rcNeed(uint8_t from, uint8_t to) {
  uint8_t worst = 0;
  for (int8_t a = 0; a < 3; a++) {
    if (from >> a & 1) continue;
    uint8_t best = 3;
    for (int8_t b = 0; b < 3; b++)
      if (!(to >> b & 1)) { uint8_t d = abs(a - b); if (d < best) best = d; }
    if (best > worst) worst = best;
  }
  return worst;
}

// rows must be this far apart (car tops) for the player to switch lanes
static int16_t rcGap(uint8_t lanes, uint8_t spd) {
  int16_t steps = (lanes * RC_LANE + RC_STEP - 1) / RC_STEP + 3;   // + reaction and curve
  return 18 + steps * spd;                                         // 18 = two car lengths
}

static void rcPlace(uint8_t i, uint8_t lane) {
  rc.opx[i] = (lane - 1) * RC_LANE - 3 + random(-1, 2);
}

// moves car i above the screen, in a row that keeps the road passable
static void rcSpawn(uint8_t i) {
  // join the newest row as its second car - only while it is still hidden
  if (rc.topMask && rc.topY < RC_TOP - 12 && __builtin_popcount(rc.topMask) == 1 &&
      random(100) < 20 + rc.spd * 4) {
    uint8_t ok[3], n = 0;
    for (uint8_t l = 0; l < 3; l++) {
      if (rc.topMask >> l & 1) continue;
      uint8_t m = rc.topMask | (1 << l);
      if (rc.prevY - rc.topY >= rcGap(rcNeed(rc.prevMask, m), rc.spd)) ok[n++] = l;
    }
    if (n) {
      uint8_t l = ok[random(n)];
      rc.topMask |= 1 << l;
      rc.opy[i] = rc.topY;
      rcPlace(i, l);
      return;
    }
  }
  // otherwise a new row with one car
  uint8_t l = random(3), m = 1 << l;
  int16_t y = rc.topY - rcGap(rcNeed(rc.topMask, m), rc.spd) - random(0, 36);
  if (y > RC_TOP - 12) y = RC_TOP - 12;         // always appear from above
  rc.prevY = rc.topY;  rc.prevMask = rc.topMask;
  rc.topY  = y;        rc.topMask  = m;
  rc.opy[i] = y;
  rcPlace(i, l);
}

static void rcStart() {
  rc.spd = 3; rc.curve = 0; rc.dist = 0;
  rc.topY = rc.prevY = RC_CARY;                 // an empty row where the player is
  rc.topMask = rc.prevMask = 0;
  for (uint8_t i = 0; i < RC_OPP; i++) rcSpawn(i);
}

// one 30 ms step of the road and the other cars; true when it got faster
static bool rcAdvance() {
  bool faster = false;
  rc.curve += 1;
  rc.dist += rc.spd;
  if (rc.spd < 8 && rc.dist / 20 > (uint32_t)rc.spd * 40) { rc.spd++; faster = true; }
  rc.topY += rc.spd;
  rc.prevY += rc.spd;
  for (uint8_t i = 0; i < RC_OPP; i++) {
    rc.opy[i] += rc.spd;
    if (rc.opy[i] > SCR_H) rcSpawn(i);
  }
  return faster;
}

// the player car at carx is off the road or touches another car
static bool rcCrash(int16_t carx) {
  int16_t mid = rcMid(rc.curve, RC_CARY);
  if (carx + 3 < mid - RC_HALF || carx + 4 > mid + RC_HALF) return true;
  for (uint8_t i = 0; i < RC_OPP; i++) {
    int16_t ox = rcMid(rc.curve, rc.opy[i]) + rc.opx[i];
    if (rc.opy[i] + 9 > RC_CARY && rc.opy[i] < RC_CARY + 9 && ox + 7 > carx && ox < carx + 7)
      return true;
  }
  return false;
}

void racerRun() {
  dInitSin();
  bool again = true;
  while (again) {
    again = false;
    uint16_t score = 0;
    rcStart();
    int16_t  carx = rcMid(0, RC_CARY) - 3;
    uint32_t next = 0;
    btnClear();

    while (poll()) {
      uint32_t now = gameMillis();
      if (now >= next) {
        next = now + 30;
        if (btnHeld(B_LEFT)  && carx > 2)          carx -= RC_STEP;
        if (btnHeld(B_RIGHT) && carx < SCR_W - 9)  carx += RC_STEP;
        if (rcAdvance()) sfx(1000, 60);
        score = (rc.dist / 20 > 65535) ? 65535 : (uint16_t)(rc.dist / 20);
        if (rcCrash(carx)) { again = gameOver(score); break; }
      }

      statusBar("RACER", score, curHigh);
      for (int16_t y = RC_TOP; y < SCR_H; y++) {               // road edges
        int16_t mid = rcMid(rc.curve, y);
        oled.drawPixel(mid - RC_HALF, y);
        oled.drawPixel(mid + RC_HALF, y);
        if (((y + rc.dist / 2) % 12) < 5) oled.drawPixel(mid, y);  // centre line
      }
      for (uint8_t i = 0; i < RC_OPP; i++) {
        if (rc.opy[i] < RC_TOP - 9 || rc.opy[i] > SCR_H) continue;
        int16_t ox = rcMid(rc.curve, rc.opy[i]) + rc.opx[i];
        oled.drawFrame(ox, rc.opy[i], 7, 9);
        oled.drawHLine(ox + 1, rc.opy[i] + 3, 5);
      }
      oled.drawBox(carx, RC_CARY, 7, 9);
      oled.setDrawColor(0);
      oled.drawHLine(carx + 1, RC_CARY + 3, 5);
      oled.setDrawColor(1);
      oled.sendBuffer();
    }
  }
}
