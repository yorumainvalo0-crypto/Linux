// MiniArcade Color: key setup, sound setup and battery detection. Part of
// console.cpp, which includes it - not compiled on its own.

// =========================================================
//  KEY SETUP
// =========================================================
/* Works like on the OLED console: every candidate pin is read once with a
   pull-up and once with a pull-down while nothing is pressed; a key press
   is then a pin that differs from its own idle value, which also tells
   whether the key goes to GND or to 3V3. ADC pins are measured in volts
   instead (with the pull-up), so several keys on one pin through
   resistors are told apart by their voltage. Four keys are learned; a
   fifth one for OK is optional (wait a few seconds to skip it - OK is then
   two opposite keys at once).                                          */
static uint8_t  baseU[sizeof(PINS_ALL)], baseD[sizeof(PINS_ALL)];
static uint8_t  liveU[sizeof(PINS_ALL)], liveD[sizeof(PINS_ALL)];
static int16_t  baseMv[sizeof(PINS_ALL)], liveMv[sizeof(PINS_ALL)];
static bool     blocked[sizeof(PINS_ALL)], mvNoisy[sizeof(PINS_ALL)];
static int16_t  seenMv = -1;                 // voltage of the key just found (ladder)
static int16_t  plainAgain = -1;             // a pin without ADC pressed again: no ladder there

static bool candAdc(uint8_t i) { return adcPin(CAND[i]) && CAND[i] != batPin; }   // the battery is no key

static void readAll(uint8_t mode, uint8_t *out) {
  for (uint8_t i = 0; i < CAND_N; i++) pinMode(CAND[i], mode);
  delay(3);
  for (uint8_t i = 0; i < CAND_N; i++) out[i] = digitalRead(CAND[i]);
}
static void readMv(int16_t *out) {            // right after readAll(INPUT_PULLUP)
  for (uint8_t i = 0; i < CAND_N; i++) out[i] = candAdc(i) ? pinMv(CAND[i]) : -1;
}

static void calibrate() {
  uint8_t u1[sizeof(PINS_ALL)], u2[sizeof(PINS_ALL)], d1[sizeof(PINS_ALL)], d2[sizeof(PINS_ALL)];
  int16_t m1[sizeof(PINS_ALL)], m2[sizeof(PINS_ALL)];
  readAll(INPUT_PULLUP, u1); readMv(m1);
  readAll(INPUT_PULLDOWN, d1);
  delay(120);
  readAll(INPUT_PULLUP, u2); readMv(m2);
  readAll(INPUT_PULLDOWN, d2);
  for (uint8_t i = 0; i < CAND_N; i++) {
    baseU[i] = u1[i];
    baseD[i] = d1[i];
    baseMv[i] = m1[i];
    blocked[i] = (u1[i] != u2[i]) || (d1[i] != d2[i]);     // noisy pin
    mvNoisy[i] = m1[i] < 0 || abs(m1[i] - m2[i]) > 150;
  }
}

// a learned ladder key sits at this voltage on this pin already
static bool levelTaken(uint8_t pin, int16_t mv) {
  for (uint8_t b = 0; b < B_COUNT; b++)
    if (BTN_PIN[b] == pin && BTN_MV[b] && abs((int)BTN_MV[b] - 1 - mv) < 25) return true;
  return false;
}

static int8_t scanKey(const bool *used, uint8_t *act) {
  readAll(INPUT_PULLUP, liveU); readMv(liveMv);
  readAll(INPUT_PULLDOWN, liveD);
  int8_t hit = -1;
  uint8_t cnt = 0;
  plainAgain = -1;
  for (uint8_t i = 0; i < CAND_N; i++) {
    if (used[i] && !blocked[i] && mvNoisy[i] && ((liveU[i] == LOW && baseU[i] == HIGH) || (liveD[i] == HIGH && baseD[i] == LOW)))
      plainAgain = CAND[i];
    if (used[i] || blocked[i]) continue;
    bool volts = !mvNoisy[i];                                // ADC pin: measured, not just high/low
    if (volts && abs(liveMv[i] - baseMv[i]) > 300) {
      if (!levelTaken(CAND[i], liveMv[i])) { hit = i; *act = 0; seenMv = liveMv[i]; cnt++; }
    }
    else if (!volts && liveU[i] == LOW && baseU[i] == HIGH) { hit = i; *act = 0; seenMv = -1; cnt++; }
    else if (liveD[i] == HIGH && baseD[i] == LOW)          { hit = i; *act = 1; seenMv = -1; cnt++; }
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
  if (seen >= 0 && seenMv >= 0) snprintf(b, sizeof(b), "GPIO%u -> %u.%02u V", CAND[seen], seenMv / 1000, seenMv % 1000 / 10);
  else if (seen >= 0) snprintf(b, sizeof(b), "GPIO%u -> %s", CAND[seen], act ? "3V3" : "GND");
  else if (secs >= 0) snprintf(b, sizeof(b), "no OK key? skipped in %d s", secs);
  else snprintf(b, sizeof(b), "waiting...");
  gfx.center(176, b, C::LTGREY);
  if (plainAgain >= 0) {                                     // several keys on a pin that cannot measure
    snprintf(b, sizeof(b), "GPIO%d has a key already and", plainAgain);
    gfx.center(190, b, C::ORANGE, F_SMALL);
#if CONFIG_IDF_TARGET_ESP32S3
    gfx.center(200, "cannot measure volts: ladder on GPIO1..10", C::ORANGE, F_SMALL);
#else
    gfx.center(200, "cannot measure volts: ladder on GPIO0..3", C::ORANGE, F_SMALL);
#endif
  }
  // live pin map
  for (uint8_t i = 0; i < CAND_N; i++) {
    int16_t x = 6 + i * (308 / CAND_N);
    char n[4];
    snprintf(n, sizeof(n), "%u", CAND[i]);
    bool on = mvNoisy[i] ? liveU[i] == LOW && baseU[i] == HIGH : abs(liveMv[i] - baseMv[i]) > 300;
    col c = blocked[i] ? C::DKGREY : on || (liveD[i] == HIGH && baseD[i] == LOW) ? C::LIME : C::GREY;
    gfx.text(x, 210, n, c, F_SMALL);
  }
  gfx.send();
}

/* Two arrows of one ladder pressed together tell the ladder's supply
   voltage (see keyTolerances), so that every pair of keys can be told
   apart later. Asked only when the ladder has three keys or more and
   some pair stands clear of the other levels; skipped after 8 s.      */
static void learnPairs() {
  int8_t a = -1, b = -1;
  static const uint8_t TRY[4][2] = { { B_UP, B_RIGHT }, { B_UP, B_LEFT }, { B_DOWN, B_RIGHT }, { B_DOWN, B_LEFT } };
  for (uint8_t t = 0; t < 4 && a < 0; t++) {
    uint8_t x = TRY[t][0], y = TRY[t][1];
    if (BTN_MV[x] > 60 && BTN_MV[y] > 60 && BTN_PIN[x] == BTN_PIN[y]) { a = (int8_t)x; b = (int8_t)y; }
  }
  if (a < 0) return;
  uint8_t n = 0;
  bool pairs = false;
  for (uint8_t k = 0; k < B_COUNT; k++) if (BTN_PIN[k] == BTN_PIN[a] && BTN_MV[k] > 60) n++;
  for (uint8_t k = 0; k < ladN; k++) if (ladLv[k].pin == BTN_PIN[a] && (ladLv[k].mask & (ladLv[k].mask - 1))) pairs = true;
  if (n < 3 || !pairs) return;
  int16_t va = (int16_t)(BTN_MV[a] - 1), vb = (int16_t)(BTN_MV[b] - 1), lo = va < vb ? va : vb, last = -1;
  uint32_t t0 = millis();
  char msg[40];
  snprintf(msg, sizeof(msg), "hold %s + %s together", BTN_NAME[a], BTN_NAME[b]);
  while (millis() - t0 < 8000) {
    int16_t mv = pinMv(BTN_PIN[a]);
    gfx.gradient(0, 0, SCR_W, SCR_H, C::DKBLUE, C::BLACK);
    gfx.center(10, "KEY SETUP", C::WHITE, F_BOLD);
    gfx.center(48, msg, C::YELLOW);
    gfx.center(80, "so that two keys at once work", C::LTGREY);
    arrow(SCR_W / 2 - 40, 130, (uint8_t)a, C::CYAN, 22);
    arrow(SCR_W / 2 + 40, 130, (uint8_t)b, C::CYAN, 22);
    char l[40];
    snprintf(l, sizeof(l), "%u.%02u V   skipped in %lu s", mv / 1000, mv % 1000 / 10, (unsigned long)((8999 - (millis() - t0)) / 1000));
    gfx.center(176, l, C::LTGREY);
    gfx.send();
    if (mv < lo - 30 && mv > 30) {                           // lower than either key alone: both are down
      if (last >= 0 && abs(mv - last) <= 15) {
        float inv = 1.0f / va + 1.0f / vb - 1.0f / ((mv + last) / 2);
        uint16_t vs = inv > 0 ? (uint16_t)(1.0f / inv) : 0;
        if (vs >= 2700 && vs <= 3900) { ladVs = vs; sfx(1500, 80); break; }
      }
      last = mv;
    } else last = -1;
    delay(50);
  }
  uint32_t rt = millis();
  while (pinMv(BTN_PIN[a]) < lo + 150 && millis() - rt < 5000) delay(10);   // let go
  delay(80);
}

void learnKeys() {
  bool used[sizeof(PINS_ALL)];
  uint8_t act = 0;
  memset(used, 0, sizeof(used));
  memset(blocked, 0, sizeof(blocked));
  memset(mvNoisy, 1, sizeof(mvNoisy));
  for (uint8_t b = 0; b < B_COUNT; b++) { BTN_PIN[b] = 255; BTN_MV[b] = 0; }
  for (uint8_t i = 0; i < CAND_N; i++) if (candAdc(i)) analogReadMilliVolts(CAND[i]);   // ADC set up before the pulls
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
    snprintf(msg, sizeof(msg), b < 4 ? "press  %s" : "OK key - or two keys together", BTN_NAME[b]);
    int8_t hit;
    uint32_t start = millis();
    bool skipped = false;
    while (true) {
      int8_t secs = -1;
      // OK is optional when two opposite keys sit on different pins (then
      // pressing both is OK); with all keys on one ladder it is needed
      bool pairUD = BTN_PIN[B_UP] != BTN_PIN[B_DOWN] || !BTN_MV[B_UP];
      bool pairLR = BTN_PIN[B_LEFT] != BTN_PIN[B_RIGHT] || !BTN_MV[B_LEFT];
      if (b == 4 && (pairUD || pairLR)) {
        int32_t left = 8000 - (int32_t)(millis() - start);
        if (left <= 0) { skipped = true; break; }
        secs = (int8_t)(left / 1000 + 1);
      }
      hit = scanKey(used, &act);
      wizScreen(msg, b, hit, act, secs);
      if (hit < 0) continue;
      uint8_t act2;
      int16_t mv1 = seenMv;
      delay(50);
      if (scanKey(used, &act2) != hit) continue;
      if (mv1 >= 0 && abs(seenMv - mv1) > 20) continue;      // still settling
      if (mv1 >= 0) seenMv = (int16_t)((seenMv + mv1) / 2);
      break;
    }
    if (skipped) { BTN_PIN[b] = 255; break; }
    BTN_PIN[b] = CAND[hit];
    BTN_ACT[b] = act;
    BTN_MV[b] = seenMv >= 0 ? (uint16_t)(seenMv + 1) : 0;
    used[hit] = !BTN_MV[b];                                  // a ladder pin can take more keys
    applyPinModes();
    uint32_t rt = millis();
    while (rawPressed(b) && millis() - rt < 5000) delay(10);
    delay(80);
  }
  ladVs = 3300;
  learnPairs();
  savePins();
  applyPinModes();

  gfx.gradient(0, 0, SCR_W, SCR_H, C::DKGRN, C::BLACK);
  gfx.center(14, "SAVED", C::WHITE, F_BOLD);
  for (uint8_t i = 0; i < B_COUNT; i++) {
    char l[40];
    if (BTN_PIN[i] == 255) snprintf(l, sizeof(l), "%-5s  = UP+DOWN or LEFT+RIGHT", BTN_NAME[i]);
    else if (BTN_MV[i]) snprintf(l, sizeof(l), "%-5s  GPIO %-2u  %u.%02u V", BTN_NAME[i], BTN_PIN[i], (BTN_MV[i] - 1) / 1000, (BTN_MV[i] - 1) % 1000 / 10);
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
    snprintf(b, sizeof(b), "%s = yes", okKeys());
    gfx.center(150, b, C::LIME);
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

static bool batPinUsed(uint8_t p) { return p == sndPin || pinTaken(p) || pinReserved(p) || p == gyroSda || p == gyroScl; }

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
