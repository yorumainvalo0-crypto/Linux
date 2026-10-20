// MiniArcade Color: key setup, sound setup and battery detection. Part of
// console.cpp, which includes it - not compiled on its own.

// =========================================================
//  KEY SETUP
// =========================================================
/* Works like on the OLED console: every candidate pin is read once with a
   pull-up and once with a pull-down while nothing is pressed; a key press
   is then a pin that differs from its own idle value, which also tells
   whether the key goes to GND or to 3V3. Four keys are learned; a fifth
   one for OK is optional (wait a few seconds to skip it - OK is then two
   opposite keys at once).                                              */
static uint8_t baseU[sizeof(PINS_ALL)], baseD[sizeof(PINS_ALL)];
static uint8_t liveU[sizeof(PINS_ALL)], liveD[sizeof(PINS_ALL)];
static bool    blocked[sizeof(PINS_ALL)];

static void readAll(uint8_t mode, uint8_t *out) {
  for (uint8_t i = 0; i < CAND_N; i++) pinMode(CAND[i], mode);
  delay(3);
  for (uint8_t i = 0; i < CAND_N; i++) out[i] = digitalRead(CAND[i]);
}

static void calibrate() {
  uint8_t u1[sizeof(PINS_ALL)], u2[sizeof(PINS_ALL)], d1[sizeof(PINS_ALL)], d2[sizeof(PINS_ALL)];
  readAll(INPUT_PULLUP, u1);
  readAll(INPUT_PULLDOWN, d1);
  delay(120);
  readAll(INPUT_PULLUP, u2);
  readAll(INPUT_PULLDOWN, d2);
  for (uint8_t i = 0; i < CAND_N; i++) {
    baseU[i] = u1[i];
    baseD[i] = d1[i];
    blocked[i] = (u1[i] != u2[i]) || (d1[i] != d2[i]);     // noisy pin
  }
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
  return cnt == 1 ? hit : -1;
}

// an arrow pointing the way of key k (0..3), centred at cx, cy
static void arrow(int16_t cx, int16_t cy, uint8_t k, col c, int16_t s = 18) {
  switch (k) {
    case B_UP:    gfx.triangle(cx, cy - s, cx - s, cy + s / 2, cx + s, cy + s / 2, c); break;
    case B_DOWN:  gfx.triangle(cx, cy + s, cx - s, cy - s / 2, cx + s, cy - s / 2, c); break;
    case B_LEFT:  gfx.triangle(cx - s, cy, cx + s / 2, cy - s, cx + s / 2, cy + s, c); break;
    default:      gfx.triangle(cx + s, cy, cx - s / 2, cy - s, cx - s / 2, cy + s, c); break;
  }
}

static void wizScreen(const char *msg, uint8_t step, int8_t seen, uint8_t act, int8_t secs) {
  gfx.gradient(0, 0, SCR_W, SCR_H, C::DKBLUE, C::BLACK);
  gfx.center(10, "KEY SETUP", C::WHITE, F_BOLD);
  gfx.center(48, msg, C::YELLOW);
  if (step < 4) arrow(SCR_W / 2, 110, step, C::CYAN, 26);
  else if (step == 4) gfx.center(96, "OK", C::CYAN, F_BIG);
  for (uint8_t i = 0; i < 5; i++) {                          // progress
    int16_t x = SCR_W / 2 - 62 + i * 26;
    if (i < step) gfx.rbox(x, 150, 20, 12, 3, C::GREEN);
    else gfx.rframe(x, 150, 20, 12, 3, i == 4 ? C::GREY : C::LTGREY);
  }
  char b[40];
  if (seen >= 0) snprintf(b, sizeof(b), "GPIO%u -> %s", CAND[seen], act ? "3V3" : "GND");
  else if (secs >= 0) snprintf(b, sizeof(b), "no OK key? skipped in %d s", secs);
  else snprintf(b, sizeof(b), "waiting...");
  gfx.center(176, b, C::LTGREY);
  // live pin map
  for (uint8_t i = 0; i < CAND_N; i++) {
    int16_t x = 6 + i * (308 / CAND_N);
    char n[4];
    snprintf(n, sizeof(n), "%u", CAND[i]);
    col c = blocked[i] ? C::DKGREY : (liveU[i] == LOW && baseU[i] == HIGH) || (liveD[i] == HIGH && baseD[i] == LOW) ? C::LIME : C::GREY;
    gfx.text(x, 210, n, c, F_SMALL);
  }
  gfx.send();
}

void learnKeys() {
  bool used[sizeof(PINS_ALL)];
  uint8_t act = 0;
  memset(used, 0, sizeof(used));
  memset(blocked, 0, sizeof(blocked));
  for (uint8_t i = 0; i < 40; i++) { wizScreen("release all keys", 0, -1, 0, -1); delay(25); }
  calibrate();
  uint8_t quiet = 0;
  uint32_t t0 = millis();
  while (quiet < 8) {
    int8_t s = scanKey(used, &act);
    quiet = s < 0 ? quiet + 1 : 0;
    wizScreen("release all keys", 0, s, act, -1);
    if (millis() - t0 > 6000) { calibrate(); t0 = millis(); }
  }
  for (uint8_t b = 0; b < B_COUNT; b++) {
    char msg[32];
    snprintf(msg, sizeof(msg), b < 4 ? "press  %s" : "press an own OK key", BTN_NAME[b]);
    int8_t hit;
    uint32_t start = millis();
    bool skipped = false;
    while (true) {
      int8_t secs = -1;
      if (b == 4) {                                          // the OK key is optional
        int32_t left = 6000 - (int32_t)(millis() - start);
        if (left <= 0) { skipped = true; break; }
        secs = (int8_t)(left / 1000 + 1);
      }
      hit = scanKey(used, &act);
      wizScreen(msg, b, hit, act, secs);
      if (hit < 0) continue;
      uint8_t act2;
      delay(50);
      if (scanKey(used, &act2) != hit) continue;
      break;
    }
    if (skipped) { BTN_PIN[b] = 255; break; }
    BTN_PIN[b] = CAND[hit];
    BTN_ACT[b] = act;
    used[hit] = true;
    applyPinModes();
    uint32_t rt = millis();
    while (rawPressed(b) && millis() - rt < 5000) delay(10);
    delay(80);
  }
  savePins();
  applyPinModes();

  gfx.gradient(0, 0, SCR_W, SCR_H, C::DKGRN, C::BLACK);
  gfx.center(14, "SAVED", C::WHITE, F_BOLD);
  for (uint8_t i = 0; i < B_COUNT; i++) {
    char l[40];
    if (BTN_PIN[i] == 255) snprintf(l, sizeof(l), "%-5s  = UP+DOWN or LEFT+RIGHT", BTN_NAME[i]);
    else snprintf(l, sizeof(l), "%-5s  GPIO %-2u  %s", BTN_NAME[i], BTN_PIN[i], BTN_ACT[i] ? "3V3" : "GND");
    gfx.text(30, 60 + i * 26, l, C::LTGREY);
  }
  gfx.send();
  delay(2500);
  btnClear();
}

// =========================================================
//  SOUND SETUP
// =========================================================
/* Beeps on every free pin in turn; the player says which one was heard.
   RIGHT = next pin, OK (UP+DOWN) = this one.                         */
static bool pinTaken(uint8_t p) {
  for (uint8_t b = 0; b < B_COUNT; b++) if (BTN_PIN[b] == p) return true;
  return false;
}

void soundSetup() {
  uint8_t list[sizeof(PINS_ALL)], n = 0;
  for (uint8_t i = 0; i < CAND_N; i++) if (!pinTaken(CAND[i]) && CAND[i] != batPin) list[n++] = CAND[i];
  uint8_t idx = 0;
  btnClear();
  while (poll()) {
    if (idx >= n) {
      sndPin = 255;
      saveSound();
      gfx.center(100, "NO BUZZER", C::ORANGE, F_BOLD);
      gfx.center(140, "sound stays off", C::LTGREY);
      gfx.send();
      delay(1500);
      return;
    }
    sndPin = list[idx];
    if (!sndOn) sfx(1500, 250);
    gfx.gradient(0, 0, SCR_W, SCR_H, C::PURPLE, C::BLACK);
    gfx.center(14, "SOUND", C::WHITE, F_BOLD);
    char b[32];
    snprintf(b, sizeof(b), "testing GPIO%u", list[idx]);
    gfx.center(70, b, C::YELLOW);
    gfx.center(104, "do you hear a beep?", C::WHITE);
    gfx.center(150, "OK (UP+DOWN) = yes", C::LIME);
    gfx.center(176, "RIGHT = try the next pin", C::LTGREY);
    gfx.send();
    if (btn(B_OK))    { saveSound(); sfx(1200, 120); delay(300); return; }
    if (btn(B_RIGHT)) { noTone(sndPin); sndOn = false; idx++; delay(150); }
  }
  sndPin = 255;
  saveSound();
}

// =========================================================
//  BATTERY DETECTION   (as on the OLED console)
// =========================================================
enum BatProbe : uint8_t { BP_USED, BP_NONE, BP_BAT, BP_HIGH };

static bool batPinUsed(uint8_t p) { return p == sndPin || pinTaken(p) || pinReserved(p); }

BatProbe batProbe(uint8_t p, uint16_t *pinMv) {
  if (pinMv) *pinMv = 0;
  if (!adcPin(p) || batPinUsed(p)) return BP_USED;
  pinMode(p, INPUT);
  delay(2);
  uint32_t sum = 0;
  uint16_t lo = 0xFFFF, hi = 0;
  for (uint8_t i = 0; i < 8; i++) {
    uint16_t v = (uint16_t)analogReadMilliVolts(p);
    sum += v;
    if (v < lo) lo = v;
    if (v > hi) hi = v;
    delay(3);
  }
  uint16_t avg = (uint16_t)(sum / 8);
  if (pinMv) *pinMv = avg;
  pinMode(p, INPUT_PULLDOWN);
  delay(2);
  uint32_t down = 0;
  for (uint8_t i = 0; i < 4; i++) down += analogReadMilliVolts(p);
  down /= 4;
  pinMode(p, INPUT);
  if (hi - lo > 80) return BP_NONE;
  if (down < 100 || down < avg / 8) return BP_NONE;
  if (avg >= 2350) return BP_HIGH;
  if (avg >= 1300) return BP_BAT;
  return BP_NONE;
}

uint8_t batSearch() {
  for (uint8_t p = ADC_FIRST; p <= ADC_LAST; p++) if (batProbe(p, NULL) == BP_BAT) return p;
  return 255;
}

void batDetect() {
  if (batValid() || batPin == BAT_NONE_SET) return;
  uint8_t p = batSearch(), was = batPin;
  batPin = p;
  if (batPin != was) saveBat();
  batReset();
  applyPinModes();
}

/* The ADC pins with their voltage; RIGHT searches, UP/DOWN + OK picks one
   by hand, LEFT forgets what was learned about the cell.             */
void batterySetup() {
  uint8_t pins[ADC_LAST - ADC_FIRST + 2], n = 0;
  for (uint8_t p = ADC_FIRST; p <= ADC_LAST; p++) pins[n++] = p;
  pins[n++] = BAT_NONE_SET;
  uint8_t sel = 0;
  for (uint8_t i = 0; i < n; i++) if (pins[i] == batPin) sel = i;
  BatProbe res[ADC_LAST - ADC_FIRST + 2];
  uint16_t mv[ADC_LAST - ADC_FIRST + 2];
  uint32_t probedAt = 0;
  btnClear();
  while (poll()) {
    if (!probedAt || millis() - probedAt > 700) {            // measuring takes a while: not every frame
      for (uint8_t i = 0; i + 1 < n; i++) res[i] = batProbe(pins[i], &mv[i]);
      applyPinModes();
      probedAt = millis();
    }
    if (btn(B_UP))   { sel = sel ? sel - 1 : n - 1; sfx(700, 15); }
    if (btn(B_DOWN)) { sel = (sel + 1) % n; sfx(700, 15); }
    if (btn(B_OK))   {
      if (pins[sel] == BAT_NONE_SET || !batPinUsed(pins[sel])) {
        batPin = pins[sel]; saveBat(); batReset(); sfx(1200, 80); applyPinModes(); return;
      }
      sfx(250, 80);
    }
    if (btn(B_RIGHT)) {
      uint8_t p = batSearch();
      if (p != 255) { batPin = p; saveBat(); batReset(); for (uint8_t i = 0; i < n; i++) if (pins[i] == p) sel = i; sfx(1200, 120); }
      else sfx(250, 200);
      applyPinModes();
    }
    if (btn(B_LEFT)) { batLearnForget(); sfx(900, 80); }
    gfx.gradient(0, 0, SCR_W, SCR_H, C::DKGRN, C::BLACK);
    gfx.text(8, 6, "BATTERY", C::WHITE, F_BOLD);
    gfx.right(SCR_W - 8, 4, "RIGHT find", C::LTGREY, F_SMALL);
    gfx.right(SCR_W - 8, 15, "LEFT new cell", C::LTGREY, F_SMALL);
    uint8_t vis = n < 8 ? n : 8, top = sel >= vis ? sel - vis + 1 : 0;
    for (uint8_t i = 0; i < vis; i++) {
      uint8_t k = top + i, p = pins[k];
      int16_t y = 36 + i * 24;
      char line[40];
      if (p == BAT_NONE_SET) snprintf(line, sizeof(line), "no battery (show USB)");
      else if (res[k] == BP_USED) snprintf(line, sizeof(line), "GPIO%-2u used", p);
      else snprintf(line, sizeof(line), "GPIO%-2u %u.%02u V%s", p, mv[k] * 2 / 1000, (mv[k] * 2 % 1000) / 10,
                    res[k] == BP_BAT ? "  bat" : res[k] == BP_HIGH ? "  high" : "");
      if (k == sel) gfx.rbox(4, y - 2, SCR_W - 8, 22, 4, C::GREEN);
      gfx.text(12, y + 1, line, k == sel ? C::BLACK : (p == batPin ? C::LIME : C::WHITE));
    }
    gfx.send();
  }
}
