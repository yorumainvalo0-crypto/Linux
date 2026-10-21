// MiniArcade Color: settings page. Part of console.cpp, which includes it -
// not compiled on its own. LEFT/RIGHT change the value of a line, OK runs
// the setups, holding OK leaves.

static bool tiltFixKnown() {                     // a level was saved once
  return tiltFix[0] || tiltFix[1] || tiltFix[2];
}

// OK on "tilt level": hold the console as you like, then it is saved
static void tiltLevelScreen() {
  if (!cfgTilt || !gyroReady) {
    gfx.clear(C::NAVY);
    gfx.center(100, "TILT IS OFF", C::ORANGE, F_BOLD);
    gfx.center(140, "switch tilt control on first", C::LTGREY);
    gfx.send();
    delay(1800);
    btnClear();
    return;
  }
  for (int8_t s = 3; s > 0; s--) {                 // three seconds to hold it right
    gfx.gradient(0, 0, SCR_W, SCR_H, C::DKGRN, C::BLACK);
    gfx.center(14, "TILT LEVEL", C::WHITE, F_BOLD);
    gfx.center(70, "hold the console the way", C::LTGREY);
    gfx.center(94, "you want to play", C::LTGREY);
    char b[4];
    snprintf(b, sizeof(b), "%d", s);
    gfx.center(130, b, C::YELLOW, F_BIG);
    gfx.send();
    sfx(900, 40);
    delay(1000);
  }
  tiltSaveLevel();
  sfx(1500, 120);
  gfx.gradient(0, 0, SCR_W, SCR_H, C::DKGRN, C::BLACK);
  gfx.center(100, "LEVEL SAVED", C::LIME, F_BOLD);
  gfx.center(140, "games use this position", C::LTGREY);
  gfx.send();
  delay(1500);
  btnClear();
}

// switching tilt on finds the sensor and learns the axes the first time
static void tiltToggle() {
  if (cfgTilt) { cfgTilt = false; gyroClose(); }
  else if (tiltAx[0] != 0xFF || tiltSetup()) cfgTilt = true;
  tiltSave();
  tiltStart();
  btnClear();
}

// a faster panel clock is kept only when the player can still read this
static void screenSpeed(uint8_t mhz) {
  uint8_t old = cfgSpi;
  gfx.setSpeed(mhz);
  uint32_t t0 = millis();
  bool ok = false;
  btnClear();
  while (millis() - t0 < 10000) {
    if (!poll()) break;
    if (btn(B_OK)) { ok = true; break; }
    uint32_t t = millis() - t0;
    for (int16_t y = 0; y < SCR_H; y += 8)                 // moving bars: every row changes
      gfx.fill(0, y, SCR_W, 8, colLerp(C::DKBLUE, C::PURPLE, (uint8_t)((y * 2 + t / 8) & 0xFF)));
    gfx.rbox(20, 50, SCR_W - 40, 140, 10, C::BLACK);
    char b[32];
    snprintf(b, sizeof(b), "SCREEN %u MHz", mhz);
    gfx.center(62, b, C::WHITE, F_BOLD);
    gfx.center(100, "picture clean? press OK", C::YELLOW);
    snprintf(b, sizeof(b), "back to %u MHz in %lu s", old, (unsigned long)((10999 - t) / 1000));
    gfx.center(130, b, C::LTGREY);
    gfx.center(166, "a key held while switching on resets it", C::GREY, F_SMALL);
    gfx.send();
  }
  if (!ok) gfx.setSpeed(mhz = old);
  cfgSpi = mhz;
  saveCfg();
  sfx(ok ? 1500 : 300, 80);
  btnClear();
}

// the four ways the picture can sit: normal, turned round, mirrored
static void screenTurn(bool fwd) {
  uint8_t o = (uint8_t)((dispOrient() + (fwd ? 1 : 3)) & 3);
  cfgDisp = (uint8_t)((cfgDisp & ~9) | (o & 1) | (o & 2 ? 8 : 0));
}

void settingsRun() {
#if CONFIG_IDF_TARGET_ESP32S3
  static const uint8_t CLOCKS[3] = { 80, 160, 240 };
#else
  static const uint8_t CLOCKS[3] = { 40, 80, 160 };
#endif
  static const uint8_t MINS[5] = { 0, 1, 5, 15, 30 };
  static const uint8_t SPEEDS[3] = { 20, 27, 40 };
  enum { L_BRIGHT, L_CLOCK, L_SLEEP, L_SOUND, L_SPEED, L_FPS, L_FLIP, L_SWAP, L_INVERT, L_TILT, L_TLEVEL, L_TSENS, L_KEYS, L_BUZZER, L_BAT, L_WLAN, L_VERSION, L_BACK, L_N };
  uint8_t sel = 0, top = 0;
  const uint8_t ROWS = 7;
  btnClear();
  while (poll()) {
    bool left = btn(B_LEFT), right = btn(B_RIGHT);
    if (btn(B_UP))   { sel = sel ? sel - 1 : L_N - 1; sfx(700, 15); }
    if (btn(B_DOWN)) { sel = (sel + 1) % L_N; sfx(700, 15); }

    if (left || right) {
      sfx(800, 15);
      if (sel == L_BRIGHT) {
        int16_t v = cfgBright + (right ? 10 : -10);
        cfgBright = (uint8_t)(v < 10 ? 10 : (v > 100 ? 100 : v));
        gfx.setBacklight(cfgBright);
      } else if (sel == L_CLOCK) {
        uint8_t i = 0;
        for (uint8_t k = 0; k < 3; k++) if (CLOCKS[k] == cfgClock) i = k;
        if (right && i < 2) i++;
        if (left && i) i--;
        cfgClock = CLOCKS[i];
      } else if (sel == L_SLEEP) {
        uint8_t i = 0;
        for (uint8_t k = 0; k < 5; k++) if (MINS[k] == cfgSleep) i = k;
        if (right && i < 4) i++;
        if (left && i) i--;
        cfgSleep = MINS[i];
      } else if (sel == L_SOUND) cfgMute = !cfgMute;
      else if (sel == L_SPEED) {
        uint8_t i = 0;
        for (uint8_t k = 0; k < 3; k++) if (SPEEDS[k] <= cfgSpi) i = k;
        if (right && i < 2) i++;
        else if (left && i) i--;
        if (SPEEDS[i] < cfgSpi) { gfx.setSpeed(SPEEDS[i]); cfgSpi = SPEEDS[i]; }   // slower is always safe
        else if (SPEEDS[i] > cfgSpi) screenSpeed(SPEEDS[i]);
      }
      else if (sel == L_FPS)    { cfgFps = !cfgFps; applyDisplay(); }
      else if (sel == L_FLIP)   { screenTurn(right); applyDisplay(); }
      else if (sel == L_SWAP)   { cfgDisp ^= 2; applyDisplay(); }
      else if (sel == L_INVERT) { cfgDisp ^= 4; applyDisplay(); }
      else if (sel == L_TILT)   tiltToggle();
      else if (sel == L_TLEVEL) { cfgTiltFix = !cfgTiltFix && tiltFixKnown(); tiltSave(); }
      else if (sel == L_TSENS)  { cfgTiltSens = (uint8_t)((cfgTiltSens + (right ? 1 : 2)) % 3); tiltSave(); }
      saveCfg();
    }
    if (btn(B_OK)) {
      sfx(1200, 50);
      if      (sel == L_KEYS)    learnKeys();
      else if (sel == L_BUZZER)  soundSetup();
      else if (sel == L_BAT)     batterySetup();
      else if (sel == L_WLAN)    wlanRun();
      else if (sel == L_VERSION) versionRun(false);
      else if (sel == L_TILT)    { if (tiltSetup()) { cfgTilt = true; tiltSave(); } tiltStart(); }
      else if (sel == L_TLEVEL)  tiltLevelScreen();
      else if (sel == L_BACK)    return;
      else if (sel == L_SOUND)   { cfgMute = !cfgMute; saveCfg(); }
      else if (sel == L_FPS)     { cfgFps = !cfgFps; saveCfg(); applyDisplay(); }
      else if (sel == L_FLIP)    { screenTurn(true); saveCfg(); applyDisplay(); }
      else if (sel >= L_SWAP && sel <= L_INVERT) { cfgDisp ^= (uint8_t)(1 << (sel - L_FLIP)); saveCfg(); applyDisplay(); }
      btnClear();
    }

    char line[L_N][36];
    if (CONFIG_ARCADE_TFT_BL >= 0) snprintf(line[L_BRIGHT], 36, "brightness   %u %%", cfgBright);
    else snprintf(line[L_BRIGHT], 36, "brightness   (LED on 3V3)");
    snprintf(line[L_CLOCK], 36, "cpu clock    %u MHz%s", cfgClock, cfgClock == runClock ? "" : " *");
    if (cfgSleep) snprintf(line[L_SLEEP], 36, "sleep after  %u min", cfgSleep);
    else snprintf(line[L_SLEEP], 36, "sleep after  never");
    snprintf(line[L_SOUND], 36, "sound        %s", cfgMute ? "off" : "on");
    snprintf(line[L_SPEED], 36, "screen speed %u MHz", cfgSpi);
    snprintf(line[L_FPS], 36, "show fps     %s", cfgFps ? "on" : "off");
    static const char *const ORIENT[4] = { "normal", "turned round", "mirror up/down", "mirror left/right" };
    snprintf(line[L_FLIP], 36, "screen       %s", ORIENT[dispOrient()]);
    snprintf(line[L_SWAP], 36, "red/blue     %s", cfgDisp & 2 ? "swapped" : "normal");
    snprintf(line[L_INVERT], 36, "colours      %s", cfgDisp & 4 ? "inverted" : "normal");
    snprintf(line[L_TILT], 36, "tilt control %s", !cfgTilt ? "off" : gyroReady ? "on" : "on (no sensor)");
    snprintf(line[L_TLEVEL], 36, "tilt level   %s", cfgTiltFix ? "saved" : "game start");
    static const char *const SENS[3] = { "weak", "normal", "strong" };
    snprintf(line[L_TSENS], 36, "tilt         %s", SENS[cfgTiltSens % 3]);
    snprintf(line[L_KEYS], 36, "set up keys...");
    snprintf(line[L_BUZZER], 36, "set up sound...");
    snprintf(line[L_BAT], 36, "set up battery...");
    snprintf(line[L_WLAN], 36, "wlan and update...");
    snprintf(line[L_VERSION], 36, "firmware version...");
    snprintf(line[L_BACK], 36, "back to the games");

    if (sel < top) top = sel;
    if (sel >= top + ROWS) top = sel - ROWS + 1;
    gfx.gradient(0, 0, SCR_W, SCR_H, C::NAVY, C::BLACK);
    gfx.gradient(0, 0, SCR_W, 32, C::BLUE, C::DKBLUE);
    gfx.textShadow(10, 4, "SETTINGS", C::WHITE, C::BLACK, F_BOLD);
    batIcon(SCR_W - 30, 11);
    for (uint8_t i = 0; i < ROWS && top + i < L_N; i++) {
      uint8_t k = top + i;
      int16_t y = 40 + i * 25;
      bool on = k == sel;
      if (on) gfx.rbox(6, y - 3, SCR_W - 12, 23, 5, C::CYAN);
      gfx.text(16, y + 1, line[k], on ? C::BLACK : C::WHITE);
    }
    if (sel == L_TILT) gfx.center(SCR_H - 14, "LEFT / RIGHT on/off - OK set up again", C::GREY, F_SMALL);
    else if (sel == L_TLEVEL) gfx.center(SCR_H - 14, "OK = save how you hold it now as level", C::GREY, F_SMALL);
    else if (sel == L_TSENS) gfx.center(SCR_H - 14, "LEFT / RIGHT: how far to tilt", C::GREY, F_SMALL);
    else if (sel == L_SPEED) gfx.center(SCR_H - 14, "faster = more fps; long wires may not take it", C::GREY, F_SMALL);
    else if (sel <= L_INVERT) gfx.center(SCR_H - 14, "LEFT / RIGHT change", C::GREY, F_SMALL);
    gfx.send();
  }
}
