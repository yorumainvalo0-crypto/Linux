// MiniArcade Color: tilt control with the MPU6050 gyroscope. Part of
// console.cpp, which includes it - not compiled on its own.
/* Switched on in the settings. The sensor is searched on every free pair
   of pins (the GY-521 answers on I2C), then the player tilts the console
   once to the right and once away from the body, so the mounting of the
   board does not matter. In a game, tilting works like the arrow keys.
   Level is either the way the console is held when a game starts, or a
   position saved once in the settings; the pause menu sets it anew.     */

static uint8_t tiltAx[2] = { 0xFF, 0xFF };       // axis for left/right and up/down, bit 7 = turned round
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
  if (!tiltGame || !gyroReady) { memset(tiltKey, 0, sizeof(tiltKey)); return; }
  if ((int32_t)(millis() - next) < 0) return;
  next = millis() + 20;
  int16_t a[3];
  if (!gyroAccel(a)) return;
  int16_t x = tiltAxis(a, tiltAx[0]), y = tiltAxis(a, tiltAx[1]);
  const uint8_t K[4] = { B_RIGHT, B_LEFT, B_UP, B_DOWN };
  const int16_t V[4] = { x, (int16_t)-x, y, (int16_t)-y };
  for (uint8_t i = 0; i < 4; i++) tiltKey[K[i]] = V[i] > (tiltKey[K[i]] ? tiltOff() : tiltOn());
}

void tiltStart() {                              // at the start and when switched on
  gyroReady = cfgTilt && tiltAx[0] != 0xFF && gyroFind();
  tiltHook = gyroReady ? tiltUpdate : NULL;
}

static void tiltScreen(const char *l1, const char *l2, col c) {
  gfx.gradient(0, 0, SCR_W, SCR_H, C::DKGRN, C::BLACK);
  gfx.center(10, "TILT SETUP", C::WHITE, F_BOLD);
  gfx.center(90, l1, c, F_BOLD);
  gfx.center(130, l2, C::LTGREY);
  gfx.center(214, "hold OK = cancel", C::GREY, F_SMALL);
  gfx.send();
}

// waits for a clear tilt on an axis other than skip; returns it or 0xFF
static uint8_t tiltLearnAxis(const char *what, uint8_t skip) {
  int16_t a[3];
  tiltLevel();
  uint32_t t0 = millis();
  uint8_t held = 0, last = 0xFF;
  while (poll() && millis() - t0 < 20000) {
    tiltScreen(what, "and hold it there", C::YELLOW);
    if (!gyroAccel(a)) continue;
    uint8_t best = 0xFF;
    int16_t big = 6000;                                       // a good 20 degrees
    for (uint8_t k = 0; k < 3; k++) {
      if (k == (skip & 3) && skip != 0xFF) continue;
      int16_t d = (int16_t)(a[k] - tiltZero[k]);
      if (abs(d) > big) { big = (int16_t)abs(d); best = (uint8_t)(k | (d < 0 ? 0x80 : 0)); }
    }
    held = best != 0xFF && best == last ? held + 1 : 0;
    last = best;
    if (held > 15) { sfx(1200, 60); return best; }            // a quarter of a second
  }
  return 0xFF;
}

// finds the sensor and learns the two axes; false = no sensor or cancelled
bool tiltSetup() {
  tiltScreen("searching...", "MPU6050 on any two free pins", C::WHITE);
  if (!gyroFind()) {
    tiltScreen("NO SENSOR", "wire SDA and SCL to free pins", C::ORANGE);
    delay(2500);
    return false;
  }
  gyroReady = true;
  tiltScreen("hold it level", "", C::WHITE);
  delay(1500);
  uint8_t x = tiltLearnAxis("tilt RIGHT", 0xFF);
  if (x == 0xFF) return false;
  tiltScreen("back to level", "", C::WHITE);
  delay(1500);
  uint8_t y = tiltLearnAxis("tilt AWAY (up)", x);
  if (y == 0xFF) return false;
  tiltAx[0] = x;
  tiltAx[1] = y;
  tiltSave();
  char b[40];
  snprintf(b, sizeof(b), "SDA GPIO%d  SCL GPIO%d", gyroSda, gyroScl);
  tiltScreen("READY", b, C::LIME);
  delay(1500);
  return true;
}
