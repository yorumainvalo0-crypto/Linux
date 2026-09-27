// MiniArcade: settings page. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  SETTINGS
// =========================================================
/* One page with everything adjustable. LEFT/RIGHT changes the value of the
   selected line, OK runs the wizards, holding OK leaves.                 */
#ifdef HAVE_NET
#define SET_N 9                     // two more lines: WLAN, firmware version
#else
#define SET_N 7
#endif
#define SET_ROWS 6                  // 8 px lines that fit below the title

void settingsRun() {
  static const uint8_t CLOCKS[3] = { 40, 80, 160 };
  uint8_t sel = 0;
  btnClear();
  while (poll()) {
    bool left = btn(B_LEFT), right = btn(B_RIGHT);
    if (btn(B_UP)   && sel)     { sel--; sfx(700, 15); }
    if (btn(B_DOWN) && sel < SET_N - 1) { sel++; sfx(700, 15); }

    if (left || right) {
      sfx(800, 15);
      if (sel == 0) {                                   // brightness
        int16_t v = cfgBright + (right ? 25 : -25);
        cfgBright = (uint8_t)(v < 30 ? 30 : (v > 255 ? 255 : v));
        oled.setContrast(cfgBright);
      } else if (sel == 1) {                            // cpu clock
        uint8_t i = (cfgClock == 40) ? 0 : (cfgClock == 80 ? 1 : 2);
        if (right && i < 2) i++;
        if (left  && i)     i--;
        cfgClock = CLOCKS[i];        // takes effect on the next start
      } else if (sel == 2) {                            // sleep timer
        static const uint8_t MINS[5] = { 0, 1, 5, 15, 30 };
        uint8_t i = 0;
        for (uint8_t k = 0; k < 5; k++) if (MINS[k] == cfgSleep) i = k;
        if (right && i < 4) i++;
        if (left  && i)     i--;
        cfgSleep = MINS[i];
      }
      saveCfg();
    }

    if (btn(B_OK)) {
      sfx(1200, 50);
      if      (sel == 3) learnKeys();
      else if (sel == 4) soundSetup();
      else if (sel == 5) batterySetup();
#ifdef HAVE_NET
      else if (sel == 6) wlanRun();
      else if (sel == 7) versionRun(false);
#endif
      else if (sel == SET_N - 1) return;                // back to the library
      applyCfg();
      btnClear();
    }

    char line[SET_N][26];
    snprintf(line[0], 26, "brightness   %u%%", (cfgBright * 100) / 255);
    snprintf(line[1], 26, "cpu clock    %u MHz%s", cfgClock,
             (cfgClock == runClock) ? "" : "*");        // * = after a restart
    if (cfgSleep) snprintf(line[2], 26, "sleep after  %u min", cfgSleep);
    else          snprintf(line[2], 26, "sleep after  never");
    snprintf(line[3], 26, "set up keys...");
    snprintf(line[4], 26, "set up sound...");
    snprintf(line[5], 26, "set up battery...");
#ifdef HAVE_NET
    snprintf(line[6], 26, "wlan and update...");
    snprintf(line[7], 26, "firmware version...");
#endif
    snprintf(line[SET_N - 1], 26, "back to the games");

    oled.setFont(FONT_B);
    oled.drawStr(2, 12, "SETTINGS");
    oled.drawHLine(0, TOP_H - 1, SCR_W);
    oled.setFont(FONT);
    uint8_t top = (sel >= SET_ROWS) ? sel - SET_ROWS + 1 : 0;   // scroll
    for (uint8_t i = 0; i < SET_ROWS && top + i < SET_N; i++) {
      uint8_t y = TOP_H + i * 8, idx = top + i;       // last baseline: row 63
      if (idx == sel) { oled.drawBox(0, y, SCR_W, 8); oled.setDrawColor(0); }
      oled.drawStr(3, y + 7, line[idx]);
      oled.setDrawColor(1);
    }
    oled.sendBuffer();
  }
}
