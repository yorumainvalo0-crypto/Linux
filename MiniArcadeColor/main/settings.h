// MiniArcade Color: settings page. Part of console.cpp, which includes it -
// not compiled on its own. LEFT/RIGHT change the value of a line, OK runs
// the setups, holding OK leaves.

// switching tilt on finds the sensor and learns the axes the first time
static void tiltToggle() {
  if (cfgTilt) { cfgTilt = false; gyroClose(); }
  else if (tiltAx[0] != 0xFF || tiltSetup()) cfgTilt = true;
  tiltSave();
  tiltStart();
  btnClear();
}

void settingsRun() {
#if CONFIG_IDF_TARGET_ESP32S3
  static const uint8_t CLOCKS[3] = { 80, 160, 240 };
#else
  static const uint8_t CLOCKS[3] = { 40, 80, 160 };
#endif
  static const uint8_t MINS[5] = { 0, 1, 5, 15, 30 };
  enum { L_BRIGHT, L_CLOCK, L_SLEEP, L_SOUND, L_FLIP, L_SWAP, L_INVERT, L_TILT, L_KEYS, L_BUZZER, L_BAT, L_WLAN, L_VERSION, L_BACK, L_N };
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
      else if (sel == L_FLIP)   { cfgDisp ^= 1; applyDisplay(); }
      else if (sel == L_SWAP)   { cfgDisp ^= 2; applyDisplay(); }
      else if (sel == L_INVERT) { cfgDisp ^= 4; applyDisplay(); }
      else if (sel == L_TILT)   tiltToggle();
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
      else if (sel == L_BACK)    return;
      else if (sel == L_SOUND)   { cfgMute = !cfgMute; saveCfg(); }
      else if (sel >= L_FLIP && sel <= L_INVERT) { cfgDisp ^= (uint8_t)(1 << (sel - L_FLIP)); saveCfg(); applyDisplay(); }
      btnClear();
    }

    char line[L_N][36];
    if (CONFIG_ARCADE_TFT_BL >= 0) snprintf(line[L_BRIGHT], 36, "brightness   %u %%", cfgBright);
    else snprintf(line[L_BRIGHT], 36, "brightness   (LED on 3V3)");
    snprintf(line[L_CLOCK], 36, "cpu clock    %u MHz%s", cfgClock, cfgClock == runClock ? "" : " *");
    if (cfgSleep) snprintf(line[L_SLEEP], 36, "sleep after  %u min", cfgSleep);
    else snprintf(line[L_SLEEP], 36, "sleep after  never");
    snprintf(line[L_SOUND], 36, "sound        %s", cfgMute ? "off" : "on");
    snprintf(line[L_FLIP], 36, "screen       %s", cfgDisp & 1 ? "turned" : "normal");
    snprintf(line[L_SWAP], 36, "red/blue     %s", cfgDisp & 2 ? "swapped" : "normal");
    snprintf(line[L_INVERT], 36, "colours      %s", cfgDisp & 4 ? "inverted" : "normal");
    snprintf(line[L_TILT], 36, "tilt control %s", !cfgTilt ? "off" : gyroReady ? "on" : "on (no sensor)");
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
    else if (sel <= L_INVERT) gfx.center(SCR_H - 14, "LEFT / RIGHT change", C::GREY, F_SMALL);
    gfx.send();
  }
}
