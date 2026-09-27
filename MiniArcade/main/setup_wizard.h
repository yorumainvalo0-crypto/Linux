// MiniArcade: key setup wizard, sound setup and battery detection. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  KEY SETUP WIZARD
// =========================================================
/* Key detection without any assumption about the board:

   1. calibrate() records how every candidate pin reads when nothing is
      pressed - once with a pull-up and once with a pull-down. Pins that are
      already driven by the board (external pull-ups on strapping pins, LEDs,
      ...) simply get a fixed reference value instead of confusing us.
   2. A key press is then a pin that DIFFERS from its own reference:
        pull-up phase reads LOW  -> button wires this pin to GND
        pull-down phase reads HIGH -> button wires this pin to 3V3
   Pins whose reading is not stable during calibration are ignored.        */
static uint8_t baseU[CAND_N], baseD[CAND_N];   // idle reference per pin
static uint8_t liveU[CAND_N], liveD[CAND_N];   // most recent reading
static bool    blocked[CAND_N];

static void readAll(uint8_t mode, uint8_t *out) {
  for (uint8_t i = 0; i < CAND_N; i++) pinMode(CAND[i], mode);
  delay(3);
  for (uint8_t i = 0; i < CAND_N; i++) out[i] = digitalRead(CAND[i]);
}

static uint8_t calibrate() {                    // returns number of dead pins
  uint8_t u1[CAND_N], u2[CAND_N], d1[CAND_N], d2[CAND_N], dead = 0;
  readAll(INPUT_PULLUP, u1);
  readAll(INPUT_PULLDOWN, d1);
  delay(120);
  readAll(INPUT_PULLUP, u2);
  readAll(INPUT_PULLDOWN, d2);
  for (uint8_t i = 0; i < CAND_N; i++) {
    baseU[i] = u1[i];
    baseD[i] = d1[i];
    blocked[i] = (u1[i] != u2[i]) || (d1[i] != d2[i]);   // noisy pin
    if (blocked[i]) dead++;
  }
  return dead;
}

static int8_t scanKey(const bool *used, uint8_t *act) {
  readAll(INPUT_PULLUP, liveU);
  readAll(INPUT_PULLDOWN, liveD);
  int8_t hit = -1;
  uint8_t cnt = 0;
  for (uint8_t i = 0; i < CAND_N; i++) {
    if (used[i] || blocked[i]) continue;
    if (liveU[i] == LOW && baseU[i] == HIGH)      { hit = i; *act = 0; cnt++; }
    else if (liveD[i] == HIGH && baseD[i] == LOW) { hit = i; *act = 1; cnt++; }
  }
  return (cnt == 1) ? hit : -1;
}

static uint8_t wizDead = 0;         // candidate pins the board keeps busy

static void wizScreen(const char *msg, uint8_t step, int8_t seen, uint8_t act) {
  char b[28];
  oled.clearBuffer();
  oled.setFont(FONT_B);
  oled.drawStr(2, 12, "KEY SETUP");
  oled.drawHLine(0, TOP_H - 1, SCR_W);
  oled.setFont(FONT);
  oled.drawStr(2, 26, msg);
  for (uint8_t i = 0; i < B_COUNT; i++) {                  // progress boxes
    if (i < step) oled.drawBox(88 + i * 8, 31, 6, 5);
    else          oled.drawFrame(88 + i * 8, 31, 6, 5);
  }
  if (seen >= 0) snprintf(b, sizeof(b), "GPIO%u -> %s", CAND[seen], act ? "3V3" : "GND");
  else           snprintf(b, sizeof(b), "waiting...");
  oled.drawStr(2, 36, b);

  /* live pin map: one column per candidate GPIO
     .  idle    U  pulled to GND    D  pulled to 3V3    x  ignored          */
  oled.drawHLine(0, 43, SCR_W);
  for (uint8_t i = 0; i < CAND_N; i++) {
    uint8_t x = 2 + i * 11;
    if (i) oled.drawVLine(x - 2, 45, 19);                  // column separator
    char n[4], st[2];
    snprintf(n, sizeof(n), "%u", CAND[i]);
    oled.drawStr(x, 52, n);
    st[1] = 0;
    if (blocked[i])                                   st[0] = 'x';
    else if (liveU[i] == LOW  && baseU[i] == HIGH)    st[0] = 'U';
    else if (liveD[i] == HIGH && baseD[i] == LOW)     st[0] = 'D';
    else                                              st[0] = '.';
    oled.drawStr(x + 2, 62, st);
  }
  oled.sendBuffer();
}

void learnKeys() {
  bool used[CAND_N];
  uint8_t act = 0;
  memset(used, 0, sizeof(used));
  memset(blocked, 0, sizeof(blocked));

  // 1) let go of everything, then measure what "nothing pressed" looks like
  for (uint8_t i = 0; i < 60; i++) { wizScreen("release all buttons", 0, -1, 0); delay(25); }
  wizDead = calibrate();

  // 2) wait until the board is really quiet
  uint8_t quiet = 0;
  uint32_t t0 = millis();
  while (quiet < 8) {
    int8_t s = scanKey(used, &act);
    quiet = (s < 0) ? quiet + 1 : 0;
    wizScreen("release all buttons", 0, s, act);
    if (millis() - t0 > 6000) { wizDead = calibrate(); t0 = millis(); }   // retry
  }

  // 3) learn one button after the other
  for (uint8_t b = 0; b < B_COUNT; b++) {
    char msg[24];
    snprintf(msg, sizeof(msg), "press  %s", BTN_NAME[b]);
    int8_t hit;
    while (true) {
      hit = scanKey(used, &act);
      wizScreen(msg, b, hit, act);
      if (hit < 0) continue;
      uint8_t act2;
      delay(50);                                 // debounce and confirm
      if (scanKey(used, &act2) != hit) continue;
      break;
    }
    BTN_PIN[b] = CAND[hit];
    BTN_ACT[b] = act;
    used[hit] = true;
    applyPinModes();                             // wait for the release
    uint32_t rt = millis();
    while (rawPressed(b) && millis() - rt < 5000) delay(10);
    delay(80);
  }

  savePins();
  applyPinModes();

  oled.clearBuffer();
  oled.setFont(FONT_B);
  oled.drawStr(2, 12, "SAVED");
  oled.drawHLine(0, TOP_H - 1, SCR_W);
  oled.setFont(FONT);
  for (uint8_t i = 0; i < B_COUNT; i++) {
    char l[24];
    snprintf(l, sizeof(l), "%-5s GPIO %-2u  %s", BTN_NAME[i], BTN_PIN[i],
             BTN_ACT[i] ? "3V3" : "GND");
    oled.drawStr(6, 25 + i * 8, l);
  }
  oled.sendBuffer();
  delay(2500);
  btnClear();
}

// =========================================================
//  SOUND SETUP  +  BATTERY SENSE DETECTION
// =========================================================
/* Steps through every GPIO that is not a button, beeps on it and lets the
   player confirm. A passive piezo gives no electrical hint, so this is the
   only reliable way - but the stepping itself runs on its own.          */
void soundSetup() {
  uint8_t list[CAND_N], n = 0;
  for (uint8_t i = 0; i < CAND_N; i++) {
    bool used = false;
    for (uint8_t b = 0; b < B_COUNT; b++) if (BTN_PIN[b] == CAND[i]) used = true;
    if (!used) list[n++] = CAND[i];
  }

  uint8_t idx = 0;
  btnClear();
  while (poll()) {
    if (idx >= n) {                                   // nothing confirmed
      sndPin = 255;
      saveSound();
      oled.setFont(FONT_B);
      centerStr(30, "NO BUZZER");
      oled.setFont(FONT);
      centerStr(46, "sound stays off");
      oled.sendBuffer();
      delay(1500);
      return;
    }
    sndPin = list[idx];                               // beep on the candidate
    sfx(1500, 250);

    char b[26];
    oled.setFont(FONT_B);
    oled.drawStr(2, 12, "SOUND");
    oled.drawHLine(0, TOP_H - 1, SCR_W);
    oled.setFont(FONT);
    snprintf(b, sizeof(b), "testing GPIO%u", list[idx]);
    centerStr(28, b);
    centerStr(40, "do you hear a beep?");
    centerStr(52, "OK = yes");
    centerStr(62, "RIGHT = try next pin");
    oled.sendBuffer();

    if (btn(B_OK))    { saveSound(); sfx(1200, 120); delay(300); return; }
    if (btn(B_RIGHT)) { noTone(sndPin); idx++; delay(150); }
  }
  sndPin = 255;                                       // left with a long press
  saveSound();
}

/* Live view of the five ADC pins so a wired divider can be spotted, plus
   manual selection when the automatic guess picks the wrong pin.        */
void batterySetup() {
  uint8_t sel = (batPin < 5) ? batPin : 5;
  btnClear();
  while (poll()) {
    if (btn(B_UP)   && sel)     { sel--; sfx(700, 15); }
    if (btn(B_DOWN) && sel < 6) { sel++; sfx(700, 15); }
    if (btn(B_OK)) {
      batPin = (sel < 5) ? sel : 255;
      saveBat();
      sfx(1200, 80);
      return;
    }

    oled.setFont(FONT_B);
    oled.drawStr(2, 12, "BATTERY");
    oled.drawHLine(0, TOP_H - 1, SCR_W);
    oled.setFont(FONT);
    for (uint8_t p = 0; p < 6; p++) {
      char line[26];
      uint8_t y = TOP_H + p * 8;
      if (p == 5) snprintf(line, sizeof(line), "no battery (show USB)");
      else {
        const char *why = NULL;
        for (uint8_t b = 0; b < B_COUNT; b++) if (BTN_PIN[b] == p) why = "used by a key";
        if (p == sndPin) why = "used by the buzzer";

        if (why) snprintf(line, sizeof(line), "GPIO%u  %s", p, why);
        else {
          pinMode(p, INPUT);
          uint16_t pv = (uint16_t)analogReadMilliVolts(p);
          uint16_t mv = pv * 2;
          snprintf(line, sizeof(line), "%u:%u.%02u>%u.%02u%s%s", p,
                   pv / 1000, (pv % 1000) / 10, mv / 1000, (mv % 1000) / 10,
                   (mv > 2700 && mv < 4600) ? " ok" : "",
                   (p == 2) ? " strap" : "");
        }
      }
      if (p == sel) { oled.drawBox(0, y, SCR_W, 8); oled.setDrawColor(0); }
      oled.drawStr(3, y + 7, line);
      oled.setDrawColor(1);
    }
    oled.sendBuffer();
    applyPinModes();                       // give the key pins their pull back
  }
}

/* Looks for two resistors from the battery to a free ADC pin. A pin left
   floating drifts, so only a stable reading in the plausible window counts. */
void batDetect() {
  if (batPin != 254) return;                 // the player already decided
  batPin = 255;
  for (uint8_t i = 0; i < CAND_N; i++) {
    uint8_t p = CAND[i];
    if (p > 4) continue;                              // only GPIO0..4 have ADC1
    bool used = (p == sndPin);
    for (uint8_t b = 0; b < B_COUNT; b++) if (BTN_PIN[b] == p) used = true;
    if (used) continue;
    pinMode(p, INPUT);
    uint16_t a = analogReadMilliVolts(p);
    delay(20);
    uint16_t c = analogReadMilliVolts(p);
    uint16_t d = (a > c) ? a - c : c - a;
    if (a > 1300 && a < 2450 && d < 90) { batPin = p; saveBat(); return; }
  }
}
