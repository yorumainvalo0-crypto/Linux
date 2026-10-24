// MiniArcade Color: tilt control with the MPU6050 gyroscope. Part of
// console.cpp, which includes it - not compiled on its own.
/* Switched on in the settings. The sensor is searched on every free pair
   of pins (the GY-521 answers on I2C), then the player tilts the console
   to the right, to the left, away from the body and towards it, so the
   mounting of the board does not matter: LEFT must be RIGHT's axis the
   other way round, DOWN the same for UP, else that step is asked again.
   How far each side went is kept, so that a side that reads less (a
   sensor off the middle, a crooked board) answers as much as the others.
   In a game, tilting works like the arrow keys. Level is either the way
   the console is held when a game starts, or a position saved once in
   the settings; the pause menu sets it anew.                           */

static uint8_t tiltAx[2] = { 0xFF, 0xFF };       // axis for left/right and up/down, bit 7 = turned round
#define TILT_FULL_DEF 8192                       // a side's full tilt when not learned (older setups): all the same
static int16_t tiltFull[4] = { TILT_FULL_DEF, TILT_FULL_DEF, TILT_FULL_DEF, TILT_FULL_DEF };   // RIGHT LEFT UP DOWN, saved as "gful"
static int16_t tiltZero[3];                      // level right now
static bool    cfgTiltFix = false;               // level = the saved position (else: at the game start)
static int16_t tiltFix[3];                       // that position
static uint8_t cfgTiltSens = 1;                  // 0 weak, 1 normal, 2 strong

// how far to tilt: about 21, 15 or 10 degrees (16384 = 1 g); let go a bit earlier
static const int16_t TILT_ON[3] = { 5800, 4100, 2800 };
static int16_t tiltOn()  { return TILT_ON[cfgTiltSens % 3]; }
static int16_t tiltOff() { return (int16_t)(tiltOn() * 6 / 10); }

static void tiltSave() {
  prefs.begin("arcade", false);
  prefs.putUShort("tilt", cfgTilt);
  prefs.putUShort("gsda", (uint16_t)(gyroSda + 1));
  prefs.putUShort("gscl", (uint16_t)(gyroScl + 1));
  prefs.putUShort("gax", (uint16_t)(tiltAx[0] | tiltAx[1] << 8));
  prefs.putBytes("gful", tiltFull, sizeof(tiltFull));
  prefs.putUShort("gfix", cfgTiltFix);
  prefs.putUShort("gsen", cfgTiltSens);
  prefs.putBytes("gzero", tiltFix, sizeof(tiltFix));
  prefs.end();
}

static void tiltLoad() {
  prefs.begin("arcade", true);
  cfgTilt = prefs.getUShort("tilt", 0) != 0;
  gyroSda = (int8_t)(prefs.getUShort("gsda", 0) - 1);
  gyroScl = (int8_t)(prefs.getUShort("gscl", 0) - 1);
  uint16_t ax = prefs.getUShort("gax", 0xFFFF);
  cfgTiltFix  = prefs.getUShort("gfix", 0) != 0;
  cfgTiltSens = (uint8_t)prefs.getUShort("gsen", 1);
  if (prefs.getBytes("gzero", tiltFix, sizeof(tiltFix)) != sizeof(tiltFix)) cfgTiltFix = false;
  if (prefs.getBytes("gful", tiltFull, sizeof(tiltFull)) != sizeof(tiltFull))      // set up before the four ways
    for (int16_t &f : tiltFull) f = TILT_FULL_DEF;
  prefs.end();
  tiltAx[0] = (uint8_t)ax;
  tiltAx[1] = (uint8_t)(ax >> 8);
}

static bool gyroPinFree(uint8_t p) { return !pinTaken(p) && p != sndPin && p != batPin; }

// the saved pins, or a search over every free pair
static bool gyroFind() {
  if (gyroSda >= 0 && gyroPinFree(gyroSda) && gyroPinFree(gyroScl) && gyroOpen(gyroSda, gyroScl)) return true;
  if (gyroOpen(CONFIG_ARCADE_I2C_SDA, CONFIG_ARCADE_I2C_SCL)) {   // the pins kept free for it (S3)
    gyroSda = CONFIG_ARCADE_I2C_SDA; gyroScl = CONFIG_ARCADE_I2C_SCL; tiltSave(); return true;
  }
  for (uint8_t i = 0; i < CAND_N; i++)
    for (uint8_t j = 0; j < CAND_N; j++) {
      if (i == j || !gyroPinFree(CAND[i]) || !gyroPinFree(CAND[j])) continue;
      if (gyroOpen(CAND[i], CAND[j])) { gyroSda = CAND[i]; gyroScl = CAND[j]; tiltSave(); return true; }
    }
  gyroSda = gyroScl = -1;
  return false;
}

static int16_t tiltAxis(const int16_t a[3], uint8_t ax) {
  if (ax == 0xFF) return 0;
  int16_t v = (int16_t)(a[ax & 3] - tiltZero[ax & 3]);
  return (ax & 0x80) ? (int16_t)-v : v;
}

static void tiltLevel() {                        // the way it is held now counts as level
  int16_t a[3];
  if (gyroReady && gyroAccel(a)) memcpy(tiltZero, a, sizeof(tiltZero));
}

void tiltGameStart() {                           // the library starts a game
  if (cfgTiltFix) memcpy(tiltZero, tiltFix, sizeof(tiltZero));
  else tiltLevel();
}

// settings: the position now becomes the fixed level
bool tiltSaveLevel() {
  if (!gyroReady) return false;
  tiltLevel();
  memcpy(tiltFix, tiltZero, sizeof(tiltFix));
  cfgTiltFix = true;
  tiltSave();
  return true;
}

static void tiltUpdate() {
  static uint32_t next = 0;
  if (!tiltGame || !gyroReady) { memset(tiltKey, 0, sizeof(tiltKey)); memset(tiltAn, 0, sizeof(tiltAn)); return; }
  if ((int32_t)(millis() - next) < 0) return;
  next = millis() + 20;
  int16_t a[3];
  if (!gyroAccel(a)) return;
  int16_t x = tiltAxis(a, tiltAx[0]), y = tiltAxis(a, tiltAx[1]);
  const uint8_t K[4] = { B_RIGHT, B_LEFT, B_UP, B_DOWN };
  int16_t V[4] = { x, (int16_t)-x, y, (int16_t)-y };
  // each side scaled by how far it went when learned: its full tilt reads like the mean of all four
  int32_t ref = ((int32_t)tiltFull[0] + tiltFull[1] + tiltFull[2] + tiltFull[3]) / 4;
  for (uint8_t i = 0; i < 4; i++) {
    int32_t v = (int32_t)V[i] * ref / tiltFull[i];
    V[i] = (int16_t)(v > 32000 ? 32000 : v < -32000 ? -32000 : v);
  }
  const int16_t dead = (int16_t)(tiltOn() * 4 / 10), full = (int16_t)(tiltOn() * 2);   // stepless: from about 6 to 30 degrees
  for (uint8_t i = 0; i < 4; i++) {
    tiltKey[K[i]] = V[i] > (tiltKey[K[i]] ? tiltOff() : tiltOn());
    tiltAn[K[i]] = V[i] <= dead ? 0 : V[i] >= full ? 256 : (int16_t)((int32_t)(V[i] - dead) * 256 / (full - dead));
  }
}

void tiltStart() {                              // at the start and when switched on
  gyroReady = cfgTilt && tiltAx[0] != 0xFF && gyroFind();
  tiltHook = gyroReady ? tiltUpdate : NULL;
}

static bool tiltLearned() { return tiltAx[0] != 0xFF; }

// the pause menu: off, or on again with the axes learned before (saved like the settings' switch)
static void tiltSwitch() {
  if (cfgTilt) { cfgTilt = false; gyroClose(); memset(tiltKey, 0, sizeof(tiltKey)); memset(tiltAn, 0, sizeof(tiltAn)); }
  else cfgTilt = true;
  tiltSave();
  tiltStart();
  if (gyroReady) tiltLevel();                     // on again: the way it is held now is level
}

// dir: an arrow for that key's way (B_UP..B_RIGHT), -1 = none
static void tiltScreen(const char *l1, const char *l2, col c, int8_t dir = -1) {
  gfx.gradient(0, 0, SCR_W, SCR_H, C::DKGRN, C::BLACK);
  gfx.center(10, TR("TILT SETUP", "KIPPEN EINRICHTEN"), C::WHITE, F_BOLD);
  gfx.center(90, l1, c, F_BOLD);
  gfx.center(130, l2, C::LTGREY);
  if (dir >= 0) arrow(SCR_W / 2, 176, (uint8_t)dir, C::LIME, 16);
  gfx.center(214, TR("hold OK = cancel", "OK halten = abbrechen"), C::GREY, F_SMALL);
  gfx.send();
}

/* Waits for a clear tilt on any axis; returns it (bit 7 = the negative
   way) or 0xFF, and in *mag how far it went at the most while held.   */
static uint8_t tiltLearnAxis(const char *what, int8_t dir, int16_t *mag) {
  int16_t a[3];
  tiltLevel();
  uint32_t t0 = millis();
  uint8_t held = 0, last = 0xFF;
  int16_t peak = 0;
  while (poll() && millis() - t0 < 20000) {
    tiltScreen(what, TR("and hold it there", "und so halten"), C::YELLOW, dir);
    if (!gyroAccel(a)) continue;
    uint8_t best = 0xFF;
    int16_t big = 6000;                                       // a good 20 degrees
    for (uint8_t k = 0; k < 3; k++) {
      int16_t d = (int16_t)(a[k] - tiltZero[k]);
      if (abs(d) > big) { big = (int16_t)abs(d); best = (uint8_t)(k | (d < 0 ? 0x80 : 0)); }
    }
    held = best != 0xFF && best == last ? held + 1 : 0;
    peak = held ? (big > peak ? big : peak) : big;
    last = best;
    if (held > 15) {                                          // a quarter of a second
      sfx(1200, 60);
      *mag = (int16_t)(peak < 6000 ? 6000 : peak > 12000 ? 12000 : peak);   // 20 to 47 degrees
      return best;
    }
  }
  return 0xFF;
}

// finds the sensor and learns the two axes; false = no sensor or cancelled
bool tiltSetup() {
  tiltScreen(TR("searching...", "suche..."), TR("MPU6050 on any two free pins", "MPU6050 an zwei freien Pins"), C::WHITE);
  if (!gyroFind()) {
    tiltScreen(TR("NO SENSOR", "KEIN SENSOR"), TR("wire SDA and SCL to free pins", "SDA und SCL an freie Pins"), C::ORANGE);
    delay(2500);
    return false;
  }
  gyroReady = true;
  tiltScreen(TR("hold it level", "gerade halten"), "", C::WHITE);
  delay(1500);
  // RIGHT, LEFT, UP (away from the body), DOWN; a step that does not fit is asked again
  const int8_t DIR[4] = { B_RIGHT, B_LEFT, B_UP, B_DOWN };
  uint8_t ax[4];
  int16_t full[4];
  for (uint8_t d = 0; d < 4; d++) {
    const char *const ASK[4] = { TR("tilt RIGHT", "nach RECHTS kippen"), TR("tilt LEFT", "nach LINKS kippen"),
                                 TR("tilt UP (away)", "nach HINTEN kippen"), TR("tilt DOWN (to you)", "nach VORN kippen") };
    if (d) { tiltScreen(TR("back to level", "wieder gerade"), "", C::WHITE); delay(1500); }
    ax[d] = tiltLearnAxis(ASK[d], DIR[d], &full[d]);
    if (ax[d] == 0xFF) return false;
    const char *bad = d == 1 && ax[1] != (ax[0] ^ 0x80) ? TR("LEFT did not match RIGHT", "LINKS passt nicht zu RECHTS")
                    : d == 2 && (ax[2] & 3) == (ax[0] & 3) ? TR("UP matched RIGHT/LEFT", "HINTEN war RECHTS/LINKS")
                    : d == 3 && ax[3] != (ax[2] ^ 0x80) ? TR("DOWN did not match UP", "VORN passt nicht zu HINTEN") : NULL;
    if (bad) {
      sfx(250, 200);
      tiltScreen(TR("TRY AGAIN", "NOCHMAL"), bad, C::ORANGE);
      delay(2000);
      d--;                                                    // the same step once more
    }
  }
  tiltAx[0] = ax[0];
  tiltAx[1] = ax[2];
  memcpy(tiltFull, full, sizeof(tiltFull));
  tiltSave();
  char b[40];
  snprintf(b, sizeof(b), "SDA GPIO%d  SCL GPIO%d", gyroSda, gyroScl);
  tiltScreen(TR("READY", "FERTIG"), b, C::LIME);
  delay(1500);
  return true;
}
