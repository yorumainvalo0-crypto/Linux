/* ------------------------------------------------------------------
   MiniArcade - tiny game launcher for ESP32-C3 + SSD1306 128x64 (I2C)
   Games: Tetris, Snake, Pong           High scores are kept in flash.
   ------------------------------------------------------------------
   Display: laid out for the common two colour panels where the top
   16 pixel rows are yellow and the rest is blue. The yellow band is
   used as a status bar, all gameplay happens in the blue area.

   Buttons: 5 push buttons on any free GPIO. They may be wired to GND
   or to 3V3 - the setup wizard detects the pin AND the polarity, so
   no soldering has to be redone. No external resistors needed.

   Controls: UP/DOWN/LEFT/RIGHT + OK
             OK short = select / action, OK held ~0.7 s = back to menu
             Mine: OK acts on the block in front, DOWN on the one below -
             solid block = dig it, empty space = place one from the bag.

   Wiring:  OLED SDA -> GPIO8   SCL -> GPIO9   VCC 3V3, GND

   Key setup wizard: runs on the first start, when a button is held
   during reset, or from the "Setup keys" entry in the library.

   Library: "U8g2" by olikraus        Board: "ESP32C3 Dev Module"
   ------------------------------------------------------------------ */

#include <U8g2lib.h>
#include <Wire.h>
#include <Preferences.h>
#include <math.h>

// ---------------- configuration ----------------
#define PIN_SDA 8
#define PIN_SCL 9

#define TOP_H  16          // height of the yellow band
#define SCR_W 128
#define SCR_H  64

enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_OK, B_COUNT };
static uint8_t BTN_PIN[B_COUNT] = { 3, 4, 5, 6, 7 };   // learned at runtime
static uint8_t BTN_ACT[B_COUNT] = { 0, 0, 0, 0, 0 };   // 0 = to GND, 1 = to 3V3
static const char *BTN_NAME[B_COUNT] = { "UP", "DOWN", "LEFT", "RIGHT", "OK" };

/* GPIOs the wizard may use. 8/9 are the display, 11..17 the flash and
   18/19 the USB port - those are left out on purpose. */
static const uint8_t CAND[] = { 0, 1, 2, 3, 4, 5, 6, 7, 10, 20, 21 };
#define CAND_N (sizeof(CAND) / sizeof(CAND[0]))

#define HOLD_BACK_MS 700   // hold OK this long to leave a game
#define REPEAT_DELAY 250   // first auto-repeat after ...
#define REPEAT_RATE   80   // ... then every ...

U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

#define FONT   u8g2_font_5x7_tr      // only two fonts -> saves flash
#define FONT_B u8g2_font_7x13B_tr

// ---------------- buttons ----------------
static bool     bDown[B_COUNT], bEdge[B_COUNT], bTap[B_COUNT];
static bool     bIgnore[B_COUNT];    // held while a screen was left: wait for release
static uint32_t bTime[B_COUNT], bNext[B_COUNT];
static bool     okLocked = false;    // long press already handled
static bool     wantExit = false;    // "back to menu" request

void applyPinModes() {
  for (uint8_t i = 0; i < B_COUNT; i++)
    pinMode(BTN_PIN[i], BTN_ACT[i] ? INPUT_PULLDOWN : INPUT_PULLUP);
}

bool rawPressed(uint8_t i) {
  return digitalRead(BTN_PIN[i]) == (BTN_ACT[i] ? HIGH : LOW);
}

void btnUpdate() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < B_COUNT; i++) {
    bool d = rawPressed(i);
    if (d && bIgnore[i]) continue;                     // wait for the release first
    if (d && !bDown[i]) {                              // press
      bDown[i] = true;
      bTap[i] = true;                                  // instant press event
      bTime[i] = now;
      bNext[i] = now + REPEAT_DELAY;
      if (i != B_OK) bEdge[i] = true;                  // directions fire at once
    } else if (!d && bDown[i]) {                       // release
      bDown[i] = false;
      if (bIgnore[i]) { bIgnore[i] = false; okLocked = false; continue; }
      if (i == B_OK) {
        if (!okLocked) bEdge[i] = true;                // short press = select
        okLocked = false;
      }
    } else if (d && !bIgnore[i] && i != B_OK && now >= bNext[i]) {   // auto repeat
      bNext[i] = now + REPEAT_RATE;
      bEdge[i] = true;
    }
  }
  if (bDown[B_OK] && !bIgnore[B_OK] && !okLocked && now - bTime[B_OK] > HOLD_BACK_MS) {
    okLocked = true;
    wantExit = true;
  }
}

bool btn(uint8_t i)     { if (bEdge[i]) { bEdge[i] = false; return true; } return false; }
// fires the moment a button goes down (OK's normal edge only fires on release,
// so that a long press can mean "back to the library")
bool btnTap(uint8_t i)  { if (bTap[i]) { bTap[i] = false; return true; } return false; }
bool btnHeld(uint8_t i) { return bDown[i]; }

/* Called whenever a screen is entered or left. A button that is still held
   down at that moment is muted until it is released - otherwise letting go
   of OK after "hold to leave" would immediately start the game again.   */
void btnClear() {
  for (uint8_t i = 0; i < B_COUNT; i++) {
    bEdge[i] = bTap[i] = false;
    bIgnore[i] = bDown[i] = rawPressed(i);
  }
  okLocked = false;
  wantExit = false;
}

// ---------------- user settings ----------------
static uint8_t  cfgBright = 200;      // display contrast 0..255
static uint8_t  cfgClock  = 160;      // cpu clock in MHz
static uint8_t  cfgSleep  = 5;        // minutes without a key press, 0 = never
static uint32_t lastInput = 0;        // for the sleep timer
static uint8_t  runClock  = 160;      // clock the board actually booted with

#ifdef ARDUINO                        // the IDF build has its own version
#include <esp_sleep.h>
void deepSleepUntilButton(uint8_t pin, uint8_t level) {
  esp_deep_sleep_enable_gpio_wakeup(1ULL << pin,
      level ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
}
#endif

// ---------------- buzzer ----------------
/* A passive piezo cannot be found by measuring - it is a capacitor, not a
   resistor. So the sound wizard plays a tone on every free pin in turn and
   the player confirms the one that beeps. The choice is kept in flash.   */
static uint8_t  sndPin = 255;              // 255 = no buzzer configured
static uint32_t sndUntil = 0;
static bool     sndOn = false;

void sfx(uint16_t freq, uint16_t ms) {
  if (sndPin == 255 || !freq) return;
  tone(sndPin, freq);
  sndOn = true;
  sndUntil = millis() + ms;
}

void sfxUpdate() {
  if (sndOn && millis() >= sndUntil) { noTone(sndPin); sndOn = false; }
}

void centerStr(uint8_t y, const char *s);          // defined further down

/* Goes to deep sleep when no button was touched for a while. Any press on
   the OK button wakes the board - that is a normal reset, so the launcher
   comes back with everything (high scores, mine world) still in flash.  */
void sleepCheck() {
  if (!cfgSleep) return;
  bool anyKey = false;
  for (uint8_t i = 0; i < B_COUNT; i++) if (bDown[i]) anyKey = true;
  if (anyKey) { lastInput = millis(); return; }
  if (millis() - lastInput < (uint32_t)cfgSleep * 60000UL) return;

  noTone(sndPin);
  oled.clearBuffer();
  oled.setFont(FONT_B);
  centerStr(28, "SLEEPING");
  oled.setFont(FONT);
  centerStr(44, "press OK to wake up");
  oled.sendBuffer();
  delay(1200);
  oled.setPowerSave(1);
  deepSleepUntilButton(BTN_PIN[B_OK], BTN_ACT[B_OK]);
}

// ---------------- battery ----------------
/* Needs two resistors from the cell to a free ADC pin (GPIO0..4):
      BAT+ ---[100k]--- pin ---[100k]--- GND
   The pin then sees half the cell voltage. Detected automatically: a pin
   that reads a stable 1.5..2.2 V is treated as the battery sense.        */
static uint8_t batPin = 255;

static uint16_t batMilliVolts() {
  if (batPin > 4) return 0;                  // 255 = none, 254 = not configured yet
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 4; i++) sum += analogReadMilliVolts(batPin);
  return (uint16_t)((sum / 4) * 2);          // undo the 1:2 divider
}

uint8_t batPercent() {                       // 3.30 V = empty, 4.15 V = full
  uint16_t mv = batMilliVolts();
  if (mv < 3300) return 0;
  if (mv > 4150) return 100;
  return (uint8_t)((mv - 3300) * 100 / 850);
}

// ---------------- flash storage (NVS) ----------------
Preferences prefs;

void saveCfg() {
  prefs.begin("arcade", false);
  prefs.putUShort("bri", cfgBright);
  prefs.putUShort("clk", cfgClock);
  prefs.putUShort("slp", cfgSleep);
  prefs.end();
}

void applyCfg() {                 // display only; the clock is set in setup()
  oled.setContrast(cfgBright);
}

static uint8_t  curGame = 0;         // index of the running game
static uint16_t curHigh = 0;         // its high score

uint16_t loadHigh(uint8_t idx) {
  char k[6]; snprintf(k, sizeof(k), "hs%u", idx);
  prefs.begin("arcade", true);
  uint16_t v = prefs.getUShort(k, 0);
  prefs.end();
  return v;
}

void saveHigh(uint8_t idx, uint16_t v) {
  char k[6]; snprintf(k, sizeof(k), "hs%u", idx);
  prefs.begin("arcade", false);
  prefs.putUShort(k, v);
  prefs.end();
}

bool loadPins() {
  prefs.begin("arcade", true);
  sndPin = (uint8_t)prefs.getUShort("snd", 255);
  batPin    = (uint8_t)prefs.getUShort("bat", 254);   // 254 = never configured
  cfgBright = (uint8_t)prefs.getUShort("bri", 200);
  cfgClock  = (uint8_t)prefs.getUShort("clk", 160);
  cfgSleep  = (uint8_t)prefs.getUShort("slp", 5);
  bool ok = prefs.getUShort("pset", 0) == 2;      // version 2 = with polarity
  if (ok)
    for (uint8_t i = 0; i < B_COUNT; i++) {
      char k[4];
      snprintf(k, sizeof(k), "p%u", i); BTN_PIN[i] = (uint8_t)prefs.getUShort(k, BTN_PIN[i]);
      snprintf(k, sizeof(k), "a%u", i); BTN_ACT[i] = (uint8_t)prefs.getUShort(k, 0);
    }
  prefs.end();
  return ok;
}

void savePins() {
  prefs.begin("arcade", false);
  for (uint8_t i = 0; i < B_COUNT; i++) {
    char k[4];
    snprintf(k, sizeof(k), "p%u", i); prefs.putUShort(k, BTN_PIN[i]);
    snprintf(k, sizeof(k), "a%u", i); prefs.putUShort(k, BTN_ACT[i]);
  }
  prefs.putUShort("pset", 2);
  prefs.end();
}

void saveBat() {
  prefs.begin("arcade", false);
  prefs.putUShort("bat", batPin);
  prefs.end();
}

void saveSound() {
  prefs.begin("arcade", false);
  prefs.putUShort("snd", sndPin);
  prefs.end();
}

// ---------------- drawing helpers ----------------
/* Every game is written as:  while (poll()) { ...draw...; oled.sendBuffer(); }
   poll() feeds the buttons, clears the buffer and returns false as soon as
   the player holds OK to get back to the library.                        */
bool poll() {
  delay(5);
  btnUpdate();
  sfxUpdate();
  sleepCheck();
  if (wantExit) { wantExit = false; return false; }
  oled.clearBuffer();
  oled.setFont(FONT);
  return true;
}

void centerStr(uint8_t y, const char *s) {
  oled.drawStr((SCR_W - oled.getStrWidth(s)) / 2, y, s);
}

void rightStr(uint8_t y, const char *s) {
  oled.drawStr(SCR_W - oled.getStrWidth(s) - 2, y, s);
}

// status bar inside the yellow band: title left, score, best right
void statusBar(const char *title, uint16_t score, uint16_t best) {
  char b[24];
  oled.setFont(FONT_B);
  oled.drawStr(2, 12, title);
  uint8_t x = 2 + oled.getStrWidth(title) + 6;
  oled.setFont(FONT);
  snprintf(b, sizeof(b), "%u", score);
  oled.drawStr(x, 12, b);
  snprintf(b, sizeof(b), "BEST %u", best);
  rightStr(12, b);
  oled.drawHLine(0, TOP_H - 1, SCR_W);
}

// ---------------- choice screen ----------------
/* Small vertical menu used for difficulty and player count.
   Returns the chosen index, or 255 when the player leaves with a long OK. */
uint8_t chooseMode(const char *title, const char *const *opts, uint8_t n) {
  uint8_t sel = 0;
  btnClear();
  while (poll()) {
    if (btn(B_UP)   && sel)         { sel--; sfx(700, 15); }
    if (btn(B_DOWN) && sel < n - 1) { sel++; sfx(700, 15); }
    if (btn(B_OK)) { sfx(1200, 60); return sel; }
    oled.setFont(FONT_B);
    oled.drawStr(2, 12, title);
    oled.drawHLine(0, TOP_H - 1, SCR_W);
    oled.setFont(FONT);
    for (uint8_t i = 0; i < n; i++) {
      uint8_t y = TOP_H + 3 + i * 11;
      if (i == sel) { oled.drawBox(0, y, SCR_W, 10); oled.setDrawColor(0); }
      oled.drawStr(6, y + 8, opts[i]);
      oled.setDrawColor(1);
    }
    oled.sendBuffer();
  }
  return 255;
}

// ---------------- game over screen ----------------
// true -> play another round, false -> back to the library
bool gameOver(uint16_t score) {
  bool record = score > curHigh;
  if (record) { curHigh = score; saveHigh(curGame, curHigh); }
  sfx(180, 350);
  btnClear();
  while (poll()) {
    if (btn(B_OK)) return true;
    oled.setFont(FONT_B);
    centerStr(12, record ? "NEW RECORD!" : "GAME OVER");
    oled.drawHLine(0, TOP_H - 1, SCR_W);
    oled.setFont(FONT);
    char b[24];
    snprintf(b, sizeof(b), "SCORE %u", score);   centerStr(32, b);
    snprintf(b, sizeof(b), "BEST  %u", curHigh); centerStr(42, b);
    centerStr(60, "OK=again   hold OK=menu");
    oled.sendBuffer();
  }
  return false;
}

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

// =========================================================
//  WLAN   (ESP-IDF build only - the Arduino IDE has no net.h)
// =========================================================
/* Joins the saved network and serves the update page, opens the
   "MiniArcade-XXXX" hotspot to set the network up from a phone and
   installs new firmware from the GitHub releases. The radio is only on
   while this page is open.                                              */
#if __has_include("net.h")
#include "net.h"
#define HAVE_NET 1

static void wlanTitle(const char *t) {
  oled.setFont(FONT_B);
  oled.drawStr(2, 12, t);
  oled.drawHLine(0, TOP_H - 1, SCR_W);
  oled.setFont(FONT);
}

static void wlanNeedsClock() {           // the radio does not run below 80 MHz
  btnClear();
  while (poll()) {
    wlanTitle("WLAN");
    centerStr(30, "WLAN needs 80 MHz+");
    centerStr(44, "OK = switch to 80 MHz");
    centerStr(54, "and restart");
    oled.sendBuffer();
    if (btn(B_OK)) { cfgClock = 80; saveCfg(); netRestart(); }
  }
}

static void wlanProgress(NetJob jb) {
  wlanTitle("UPDATE");
  centerStr(30, jb == JOB_DONE ? "done - restarting" : "installing...");
  oled.drawFrame(10, 36, 108, 8);
  oled.drawBox(12, 38, (int16_t)(104 * netProgress() / 100), 4);
  centerStr(60, "do not switch off");
}

static void wlanSetupHelp() {
  wlanTitle("WLAN SETUP");
  centerStr(25, "on the phone join");
  oled.setFont(FONT_B);
  centerStr(37, netApName());
  oled.setFont(FONT);
  centerStr(47, "the setup page opens,");
  centerStr(56, "else go to 192.168.4.1");
  centerStr(64, "hold OK = leave");
}

void wlanRun() {
  if (runClock < 80) { wlanNeedsClock(); return; }
  uint8_t sel = 0;
  bool leaving = false;
  if (netHasConfig()) netConnect();
  btnClear();
  for (;;) {
    bool alive = poll();
    lastInput = millis();                  // no deep sleep while the radio is on
    netTick();
    NetState st = netState();
    NetJob   jb = netJob();
    bool locked = (jb == JOB_UPDATING || jb == JOB_DONE);   // an update is being written
    if (!alive) {                          // hold OK = back
      leaving = true;
      oled.clearBuffer();
      oled.setFont(FONT);
    }
    if (leaving && !locked && jb != JOB_CHECKING) break;    // let a GitHub check finish
    if (locked) { wlanProgress(jb); oled.sendBuffer(); continue; }
    if (st == NET_SETUP) { wlanSetupHelp(); oled.sendBuffer(); continue; }

    bool on = (st == NET_CONNECTING || st == NET_ONLINE);
    if (btn(B_UP)   && sel)     { sel--; sfx(700, 15); }
    if (btn(B_DOWN) && sel < 4) { sel++; sfx(700, 15); }
    if (btn(B_OK)) {
      sfx(1200, 50);
      if (sel == 0) {
        if (on) netStop();
        else if (netHasConfig()) netConnect();
        else netSetup();
      } else if (sel == 1) {
        netSetup();
      } else if (sel == 2) {
        if (st != NET_ONLINE)       sfx(300, 120);
        else if (jb == JOB_NEWER)   netInstallUpdate();
        else if (jb != JOB_CHECKING) netCheckUpdate();
      } else if (sel == 3) {
        netForget();
      } else leaving = true;
      btnClear();
      continue;
    }

    char info[26], item[5][26];
    if (jb == JOB_ERROR)            snprintf(info, sizeof(info), "%s", netError());
    else if (st == NET_ONLINE)      snprintf(info, sizeof(info), "http://%s", netAddress());
    else if (st == NET_CONNECTING)  snprintf(info, sizeof(info), "joining %s", netSsid());
    else if (st == NET_FAILED)      snprintf(info, sizeof(info), "can't join %s", netSsid());
    else if (netHasConfig())        snprintf(info, sizeof(info), "network %s", netSsid());
    else                            snprintf(info, sizeof(info), "no network saved");
    snprintf(item[0], 26, on ? "disconnect" : "connect");
    snprintf(item[1], 26, "set up with phone");
    if (jb == JOB_CHECKING)         snprintf(item[2], 26, "asking GitHub...");
    else if (jb == JOB_NEWER)       snprintf(item[2], 26, "install %s", netRemoteVersion());
    else if (jb == JOB_UPTODATE)    snprintf(item[2], 26, "up to date");
    else                            snprintf(item[2], 26, "check for update");
    snprintf(item[3], 26, "forget network");
    snprintf(item[4], 26, "back");

    wlanTitle("WLAN");
    {
      char v[16];
      snprintf(v, sizeof(v), "v%s", netVersion());
      rightStr(12, v);
    }
    oled.drawStr(2, 23, info);
    for (uint8_t i = 0; i < 5; i++) {
      uint8_t y = 25 + i * 8;
      if (i == sel) { oled.drawBox(0, y, SCR_W, 8); oled.setDrawColor(0); }
      oled.drawStr(3, y + 7, item[i]);
      oled.setDrawColor(1);
    }
    oled.sendBuffer();
  }
  netStop();
}

// ---------------- firmware version ----------------
/* Both update slots: the running firmware and the one before. At start this
   page shows up for 3 s whenever both can be started, so a new version that
   turns out broken never locks the player in. A fresh update is only kept
   for good after a key press (here or in the menu) - switching the board
   off and on before that also brings the previous version back.         */
void versionRun(bool atBoot) {
  FwSlot s[2];
  fwSlots(s);
  if (atBoot && !(s[1].present && s[1].bootable)) return;
  uint8_t  sel = 0;
  bool     waiting = atBoot;                 // count down until a key is pressed
  uint32_t t0 = millis();
  btnClear();
  for (;;) {
    bool alive = poll();
    if (!alive) {
      if (!atBoot) return;                     // hold OK = back to the settings
      oled.clearBuffer();
      oled.setFont(FONT);
    }
    if (btn(B_UP))   { sel = 0; waiting = false; sfx(700, 15); }
    if (btn(B_DOWN)) { sel = 1; waiting = false; sfx(700, 15); }
    bool pressed = btn(B_OK);
    uint32_t left = 3000 - (millis() - t0 < 3000 ? millis() - t0 : 3000);
    if (pressed || (waiting && !left)) {
      if (sel == 0) {
        if (pressed) fwConfirm();              // display and keys work: keep it
        sfx(1200, 50);
        return;
      }
      if (s[1].present && s[1].bootable) {
        oled.clearBuffer();
        oled.setFont(FONT_B);
        centerStr(36, "STARTING");
        oled.setFont(FONT);
        {
          char b[40];
          snprintf(b, sizeof(b), "v%.31s", s[1].version);
          centerStr(50, b);
        }
        oled.sendBuffer();
        sfx(1200, 50);
        delay(400);
        fwStartOther();                        // only returns when it cannot
        s[1].bootable = false;
      }
      sfx(300, 120);
      continue;
    }

    oled.setFont(FONT_B);
    oled.drawStr(2, 12, "VERSION");
    oled.drawHLine(0, TOP_H - 1, SCR_W);
    oled.setFont(FONT);
    if (waiting) {
      char b[8];
      snprintf(b, sizeof(b), "%us", (unsigned)((left + 999) / 1000 % 10));
      rightStr(12, b);
    }
    for (uint8_t i = 0; i < 2; i++) {
      uint8_t y = 18 + i * 14;
      const char *tag;
      if (i == 0)                 tag = s[0].pending ? "new" : "running";
      else if (!s[1].present)     tag = "";
      else if (!s[1].bootable)    tag = "broken";
      else                        tag = "previous";
      char b[40];
      if (s[i].present) snprintf(b, sizeof(b), "v%.31s", s[i].version);
      else              snprintf(b, sizeof(b), "no other version");
      if (i == sel) { oled.drawBox(0, y, SCR_W, 13); oled.setDrawColor(0); }
      oled.setFont(s[i].present ? FONT_B : FONT);
      oled.drawStr(3, y + 11, b);
      oled.setFont(FONT);
      rightStr(y + 10, tag);
      oled.setDrawColor(1);
    }
    centerStr(55, "UP/DOWN, OK = start");
    if (!atBoot) centerStr(63, "hold OK = back");
    oled.sendBuffer();
  }
}
#endif

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
#define SET_ROWS 7                  // lines that fit below the title

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
      uint8_t y = TOP_H + i * 7, idx = top + i;
      if (idx == sel) { oled.drawBox(0, y, SCR_W, 7); oled.setDrawColor(0); }
      oled.drawStr(3, y + 6, line[idx]);
      oled.setDrawColor(1);
    }
    oled.sendBuffer();
  }
}

// =========================================================
//  TETRIS   (10 x 16 field, 3 px cells -> fits the blue area)
// =========================================================
#define T_W 10
#define T_H 16
#define T_CELL 3
#define T_X 1
#define T_Y TOP_H

// 4x4 bitmasks, bit 15 = top left; 7 pieces x 4 rotations
static const uint16_t T_PIECE[7][4] = {
  { 0x0F00, 0x2222, 0x00F0, 0x4444 },  // I
  { 0x44C0, 0x8E00, 0x6440, 0x0E20 },  // J
  { 0x4460, 0x0E80, 0xC440, 0x2E00 },  // L
  { 0xCC00, 0xCC00, 0xCC00, 0xCC00 },  // O
  { 0x06C0, 0x8C40, 0x06C0, 0x8C40 },  // S
  { 0x0E40, 0x4C40, 0x4E00, 0x4640 },  // T
  { 0x0C60, 0x4C80, 0x0C60, 0x4C80 }   // Z
};

static uint16_t tFld[T_H];             // one bit per column

bool tHit(uint8_t p, uint8_t r, int8_t px, int8_t py) {
  uint16_t m = T_PIECE[p][r];
  for (uint8_t i = 0; i < 16; i++) {
    if (!(m & (0x8000 >> i))) continue;
    int8_t x = px + (i & 3), y = py + (i >> 2);
    if (x < 0 || x >= T_W || y >= T_H) return true;
    if (y >= 0 && (tFld[y] & (1 << x))) return true;
  }
  return false;
}

void tLock(uint8_t p, uint8_t r, int8_t px, int8_t py) {
  uint16_t m = T_PIECE[p][r];
  for (uint8_t i = 0; i < 16; i++) {
    if (!(m & (0x8000 >> i))) continue;
    int8_t x = px + (i & 3), y = py + (i >> 2);
    if (y >= 0 && y < T_H && x >= 0 && x < T_W) tFld[y] |= (1 << x);
  }
}

uint8_t tClearLines() {
  uint8_t n = 0;
  for (int8_t y = T_H - 1; y >= 0; y--) {
    if (tFld[y] == 0x03FF) {
      for (int8_t k = y; k > 0; k--) tFld[k] = tFld[k - 1];
      tFld[0] = 0;
      y++;                             // re-check the same row
      n++;
    }
  }
  return n;
}

void tDraw(uint16_t m, int8_t px, int8_t py, uint8_t cell, uint8_t ox, uint8_t oy) {
  for (uint8_t i = 0; i < 16; i++) {
    if (!(m & (0x8000 >> i))) continue;
    int8_t x = px + (i & 3), y = py + (i >> 2);
    if (y >= 0) oled.drawBox(ox + x * cell, oy + y * cell, cell - 1, cell - 1);
  }
}

void tetrisRun() {
  bool again = true;
  while (again) {
    again = false;
    uint8_t  p = random(7), nextP = random(7), r = 0, level = 0;
    int8_t   px = 3, py = -1;
    uint16_t score = 0, lines = 0;
    uint32_t nextFall = millis() + 500;
    memset(tFld, 0, sizeof(tFld));
    btnClear();

    while (poll()) {
      // ---- input ----
      if (btn(B_LEFT)  && !tHit(p, r, px - 1, py)) px--;
      if (btn(B_RIGHT) && !tHit(p, r, px + 1, py)) px++;
      if (btn(B_UP)) {                                   // rotate + wall kick
        uint8_t nr = (r + 1) & 3;
        if      (!tHit(p, nr, px, py))     r = nr;
        else if (!tHit(p, nr, px - 1, py)) { r = nr; px--; }
        else if (!tHit(p, nr, px + 1, py)) { r = nr; px++; }
      }
      bool drop = false;
      if (btn(B_OK)) {                                   // hard drop
        while (!tHit(p, r, px, py + 1)) py++;
        drop = true;
      }
      if (btnHeld(B_DOWN)) nextFall = 0;                 // soft drop

      // ---- gravity ----
      uint32_t now = millis();
      if (drop || now >= nextFall) {
        uint16_t step = 500 - (uint16_t)level * 40;
        if (step < 100) step = 100;
        if (!drop && !tHit(p, r, px, py + 1)) {
          py++;
          nextFall = now + (btnHeld(B_DOWN) ? 40 : step);
        } else {
          tLock(p, r, px, py);
          uint8_t n = tClearLines();
          sfx(300, 20);
          if (n) {
            sfx(1000, 90);
            static const uint8_t pts[5] = { 0, 1, 3, 5, 8 };
            score += (uint16_t)pts[n] * 10 * (level + 1);
            lines += n;
            level = lines / 10;
          }
          p = nextP; nextP = random(7); r = 0; px = 3; py = -1;
          nextFall = now + step;
          if (tHit(p, r, px, py)) { again = gameOver(score); break; }
        }
      }

      // ---- draw ----
      statusBar("TETRIS", score, curHigh);
      oled.drawFrame(T_X - 1, T_Y, T_W * T_CELL + 2, T_H * T_CELL + 2);
      for (uint8_t y = 0; y < T_H; y++)
        for (uint8_t x = 0; x < T_W; x++)
          if (tFld[y] & (1 << x)) oled.drawBox(T_X + x * T_CELL, T_Y + 1 + y * T_CELL, 2, 2);
      tDraw(T_PIECE[p][r], px, py, T_CELL, T_X, T_Y + 1);

      char b[16];
      oled.drawStr(40, 24, "NEXT");
      tDraw(T_PIECE[nextP][0], 0, 0, 4, 40, 28);
      snprintf(b, sizeof(b), "LINES %u", lines); oled.drawStr(74, 24, b);
      snprintf(b, sizeof(b), "LEVEL %u", level + 1); oled.drawStr(74, 34, b);
      oled.drawStr(74, 50, "OK = drop");
      oled.sendBuffer();
    }
  }
}

// =========================================================
//  SNAKE   (32 x 12 grid of 4 px cells in the blue area)
// =========================================================
#define S_W   32
#define S_H   12
#define S_MAX 250

void snakeRun() {
  static uint16_t body[S_MAX];        // packed (x<<8)|y, [0] = head
  bool again = true;
  while (again) {
    again = false;
    uint16_t len = 4, score = 0, stepMs = 180;
    int8_t   dx = 1, dy = 0, ndx = 1, ndy = 0;
    uint8_t  fx = random(S_W), fy = random(S_H);
    uint32_t nextStep = 0;
    for (uint16_t i = 0; i < len; i++) body[i] = ((8 - i) << 8) | 6;
    btnClear();

    while (poll()) {
      if (btn(B_LEFT)  && dx == 0) { ndx = -1; ndy =  0; }
      if (btn(B_RIGHT) && dx == 0) { ndx =  1; ndy =  0; }
      if (btn(B_UP)    && dy == 0) { ndx =  0; ndy = -1; }
      if (btn(B_DOWN)  && dy == 0) { ndx =  0; ndy =  1; }

      uint32_t now = millis();
      if (now >= nextStep) {
        nextStep = now + (btnHeld(B_OK) ? stepMs / 3 : stepMs);   // OK = boost
        dx = ndx; dy = ndy;
        int8_t hx = (int8_t)(body[0] >> 8) + dx;
        int8_t hy = (int8_t)(body[0] & 0xFF) + dy;

        bool dead = (hx < 0 || hx >= S_W || hy < 0 || hy >= S_H);
        uint16_t head = ((uint16_t)(uint8_t)hx << 8) | (uint8_t)hy;
        if (!dead)
          for (uint16_t i = 0; i + 1 < len; i++)
            if (body[i] == head) { dead = true; break; }
        if (dead) { again = gameOver(score); break; }

        if (hx == (int8_t)fx && hy == (int8_t)fy) {               // eat
          sfx(1100, 45);
          score += 10;
          if (len < S_MAX) len++;
          if (stepMs > 70) stepMs -= 4;
          while (true) {                                          // respawn food
            fx = random(S_W); fy = random(S_H);
            uint16_t f = ((uint16_t)fx << 8) | fy;
            bool bad = false;
            for (uint16_t i = 0; i < len; i++) if (body[i] == f) { bad = true; break; }
            if (!bad) break;
          }
        }
        memmove(&body[1], &body[0], (len - 1) * sizeof(uint16_t));
        body[0] = head;
      }

      statusBar("SNAKE", score, curHigh);
      for (uint16_t i = 0; i < len; i++)
        oled.drawBox((body[i] >> 8) * 4, TOP_H + (body[i] & 0xFF) * 4, 3, 3);
      oled.drawFrame(fx * 4, TOP_H + fy * 4, 3, 3);
      oled.sendBuffer();
    }
  }
}

// =========================================================
//  PONG   (endless rally against the CPU, one point per return)
// =========================================================
#define P_TOP  TOP_H
#define P_BOT  SCR_H
#define P_PH   13         // paddle height
#define P_STEP 20         // ms per simulation step

void pongRun() {
  bool again = true;
  while (again) {
    again = false;
    int16_t  bx = 64 << 4, by = 40 << 4;      // ball, 1/16 pixel units
    int16_t  vx = -20, vy = 9;
    uint8_t  py = 34, ay = 34;                // paddle tops (player / CPU)
    uint16_t score = 0;
    uint32_t nextStep = 0;
    btnClear();

    while (poll()) {
      uint32_t now = millis();
      if (now >= nextStep) {
        nextStep = now + P_STEP;

        if (btnHeld(B_UP)   && py > P_TOP)        py -= 3;
        if (btnHeld(B_DOWN) && py < P_BOT - P_PH) py += 3;

        uint8_t at = ay + P_PH / 2, bt = by >> 4;      // CPU follows the ball
        uint8_t sp = 2 + score / 20; if (sp > 4) sp = 4;
        if (bt > at + 1 && ay < P_BOT - P_PH) ay += sp;
        if (bt < at - 1 && ay > P_TOP)        ay -= sp;

        bx += vx; by += vy;
        if (by < (P_TOP << 4))       { by = P_TOP << 4;       vy = -vy; }
        if (by > ((P_BOT - 2) << 4)) { by = (P_BOT - 2) << 4; vy = -vy; }

        if (vx < 0 && (bx >> 4) <= 5 && (bx >> 4) >= 2) {        // player paddle
          uint8_t byp = by >> 4;
          if (byp + 2 >= py && byp <= py + P_PH) {
            bx = 5 << 4;
            vx = -vx + 1;                                        // faster each hit
            if (vx > 48) vx = 48;                                // keep it collidable
            vy += ((int16_t)(byp - (py + P_PH / 2))) * 2;        // angle from hit point
            if (vy >  24) vy =  24;
            if (vy < -24) vy = -24;
            sfx(750, 30);
            score++;
          }
        }
        if (vx > 0 && (bx >> 4) >= 121) {                        // CPU paddle
          uint8_t byp = by >> 4;
          if (byp + 2 >= ay && byp <= ay + P_PH) { bx = 121 << 4; vx = -vx; }
        }
        if ((bx >> 4) > 126) { score += 5; bx = 64 << 4; by = 40 << 4; vx = -20; vy = 9; }
        if ((bx >> 4) < 0)   { again = gameOver(score); break; }
      }

      statusBar("PONG", score, curHigh);
      oled.drawBox(2, py, 3, P_PH);
      oled.drawBox(123, ay, 3, P_PH);
      oled.drawBox(bx >> 4, by >> 4, 2, 2);
      oled.sendBuffer();
    }
  }
}

// =========================================================
//  DOOM  (DDA raycaster with 4x4 ordered dithering: walls, floor,
//         ceiling and light/dark wall sides make corridors readable)
// =========================================================
#define D_VY   TOP_H              // viewport top
#define D_VH   (SCR_H - TOP_H)    // viewport height (48)
#define D_COLS 64                 // 64 rays, drawn 2 px wide
#define D_MON  4

/* 16x16 maze, one bit per cell (1 = wall) */
static const uint16_t D_MAP[16] = {
  0xFFFF, 0x8001, 0x8FD1, 0x8811, 0x8BB1, 0x8A21, 0x8AAD, 0x8AA1,
  0x8EA1, 0x82A1, 0xBEAF, 0xA0A1, 0xAFA1, 0xA001, 0xBFFD, 0xFFFF
};
static int16_t  dSin[256];                 // Q10 sine table
static uint16_t dColD[D_COLS];             // wall distance per column (Q8)
static uint8_t  dColS[D_COLS];             // which side was hit (0 = x, 1 = y)

#define DSIN(a) dSin[(uint8_t)(a)]
#define DCOS(a) dSin[(uint8_t)((a) + 64)]

/* 4x4 Bayer matrix: 17 brightness levels on a black and white panel.
   lvl 0 = black, 16 = solid white, everything between is a pattern. */
static const uint8_t D_BAYER[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
static inline bool dInk(int16_t x, int16_t y, int8_t lvl) {
  return lvl > (int8_t)D_BAYER[((y & 3) << 2) | (x & 3)];
}

static bool dWall(int32_t x, int32_t y) {  // x,y in Q8 map units
  int16_t cx = x >> 8, cy = y >> 8;
  if (cx < 0 || cx > 15 || cy < 0 || cy > 15) return true;
  return (D_MAP[cy] >> cx) & 1;
}

/* classic DDA: walks whole grid cells, so it also tells us which face was
   hit - that is what makes edges and corners visible.                    */
static uint16_t dCast(int32_t px, int32_t py, uint8_t ra, uint8_t *side) {
  int32_t rdx = DCOS(ra), rdy = DSIN(ra);
  int32_t ddx = rdx ? labs(262144L / rdx) : 1L << 16;      // step length per cell
  int32_t ddy = rdy ? labs(262144L / rdy) : 1L << 16;
  int16_t mx = px >> 8, my = py >> 8;
  int8_t  sx = rdx < 0 ? -1 : 1, sy = rdy < 0 ? -1 : 1;
  int32_t dx = rdx < 0 ? (((px & 255) * ddx) >> 8) : ((((256 - (px & 255))) * ddx) >> 8);
  int32_t dy = rdy < 0 ? (((py & 255) * ddy) >> 8) : ((((256 - (py & 255))) * ddy) >> 8);

  *side = 0;
  for (uint8_t i = 0; i < 40; i++) {
    if (dx < dy) { mx += sx; dx += ddx; *side = 0; }
    else         { my += sy; dy += ddy; *side = 1; }
    if (mx < 0 || mx > 15 || my < 0 || my > 15) return 4096;
    if ((D_MAP[my] >> mx) & 1) break;
  }
  int32_t perp = (*side == 0) ? dx - ddx : dy - ddy;
  if (perp < 40) perp = 40;
  return (uint16_t)perp;
}

struct DMon { int32_t x, y; bool alive; };
static DMon dMon[D_MON];          // global: keeps the .ino auto prototypes happy

static void dSpawn(uint8_t i, int32_t px, int32_t py) {
  for (uint8_t t = 0; t < 60; t++) {
    int32_t x = (random(14) + 1) * 256 + 128;
    int32_t y = (random(14) + 1) * 256 + 128;
    if (!dWall(x, y) && abs(x - px) + abs(y - py) > 700) {
      dMon[i].x = x; dMon[i].y = y; dMon[i].alive = true; return;
    }
  }
  dMon[i].alive = false;
}

static void dInitSin() {
  static bool ready = false;
  if (ready) return;
  for (uint16_t i = 0; i < 256; i++) dSin[i] = (int16_t)lroundf(sinf(i * 6.2831853f / 256.0f) * 1024.0f);
  ready = true;
}

void doomRun() {
  dInitSin();

  bool again = true;
  while (again) {
    again = false;
    int32_t px = 2 * 256 + 128, py = 2 * 256 + 128;
    uint8_t ang = 0, hp = 3;
    uint16_t score = 0;
    uint32_t nextStep = 0;
    for (uint8_t i = 0; i < D_MON; i++) dSpawn(i, px, py);
    btnClear();

    while (poll()) {
      uint32_t now = millis();
      if (now >= nextStep) {
        nextStep = now + 50;

        if (btnHeld(B_LEFT))  ang -= 6;
        if (btnHeld(B_RIGHT)) ang += 6;
        int8_t fwd = btnHeld(B_UP) ? 1 : (btnHeld(B_DOWN) ? -1 : 0);
        if (fwd) {
          int32_t nx = px + fwd * (DCOS(ang) >> 5);      // ~1/8 cell per step
          int32_t ny = py + fwd * (DSIN(ang) >> 5);
          if (!dWall(nx, py)) px = nx;
          if (!dWall(px, ny)) py = ny;
        }

        for (uint8_t i = 0; i < D_MON; i++) {
          if (!dMon[i].alive) { dSpawn(i, px, py); continue; }
          dMon[i].x += (px > dMon[i].x) ? 6 : -6;
          dMon[i].y += (py > dMon[i].y) ? 6 : -6;
          if (abs(px - dMon[i].x) + abs(py - dMon[i].y) < 90) {   // it got you
            hp--;
            dMon[i].alive = false;
            if (!hp) break;
          }
        }
        if (!hp) { again = gameOver(score); break; }
      }

      // ---- one pass per column: ceiling, wall, floor - no overdraw ----
      statusBar("DOOM", score, curHigh);
      int16_t hor = D_VY + D_VH / 2;
      int8_t  rowLvl[D_VH];                       // floor / ceiling brightness
      for (int16_t y = 0; y < D_VH; y++) {
        int16_t d = (D_VY + y) - hor;
        int8_t  l = (int8_t)((abs(d) * 9) / (D_VH / 2));
        if (d < 0) l = l / 3;                     // ceiling stays dim
        rowLvl[y] = l > 9 ? 9 : l;
      }

      int16_t wallTop[D_COLS], wallH[D_COLS];
      int8_t  wallLvl[D_COLS];
      for (uint8_t c = 0; c < D_COLS; c++) {
        uint8_t sideHit;
        uint8_t ra = ang + (int8_t)(((int16_t)c - D_COLS / 2) * 40 / D_COLS);
        uint16_t d = dCast(px, py, ra, &sideHit);
        int32_t corr = ((int32_t)d * DCOS(ra - ang)) >> 10;      // no fisheye
        if (corr < 40) corr = 40;
        dColD[c] = corr;
        dColS[c] = sideHit;
        int16_t h = (int16_t)((int32_t)D_VH * 256 / corr);
        if (h > D_VH) h = D_VH;
        wallH[c]   = h;
        wallTop[c] = D_VY + (D_VH - h) / 2;
        int8_t lvl = 15 - (int8_t)(corr / 100);                  // distance shading
        if (sideHit) lvl -= 6;                                   // one face darker
        if (lvl < 2)  lvl = 2;
        if (lvl > 16) lvl = 16;
        wallLvl[c] = lvl;
      }

      for (uint8_t c = 0; c < D_COLS; c++) {
        int16_t y0 = wallTop[c], y1 = y0 + wallH[c];
        for (uint8_t k = 0; k < 2; k++) {
          int16_t x = c * 2 + k;
          for (int16_t y = D_VY; y < SCR_H; y++) {
            int8_t lvl = (y >= y0 && y < y1) ? wallLvl[c] : rowLvl[y - D_VY];
            if (dInk(x, y, lvl)) oled.drawPixel(x, y);
          }
        }
        // solid edge where the wall jumps: makes corners and doorways pop
        if (c && (dColS[c] != dColS[c - 1] || abs((int32_t)dColD[c] - dColD[c - 1]) > 80)) {
          int16_t hh = wallH[c] > wallH[c - 1] ? wallH[c] : wallH[c - 1];
          int16_t ey = D_VY + (D_VH - hh) / 2;
          for (int16_t y = ey; y < ey + hh; y++)
            if (y >= D_VY && y < SCR_H) oled.drawPixel(c * 2, y);
        }
      }

      // ---- monsters (billboards, hidden by nearer walls) ----
      bool fire = btnTap(B_OK);
      int32_t fx = DCOS(ang), fy = DSIN(ang);
      int32_t gx = DCOS(ang + 64), gy = DSIN(ang + 64);
      for (uint8_t i = 0; i < D_MON; i++) {
        if (!dMon[i].alive) continue;
        int32_t rx = dMon[i].x - px, ry = dMon[i].y - py;
        int32_t dep = (rx * fx + ry * fy) >> 10;
        if (dep < 60) continue;
        int32_t sid = (rx * gx + ry * gy) >> 10;
        int16_t cx = 64 + (int16_t)((sid * 120) / dep);
        int16_t sz = (int16_t)((int32_t)D_VH * 160 / dep);
        if (sz < 3) sz = 3;
        if (sz > D_VH) sz = D_VH;
        if (cx < -sz || cx > SCR_W + sz) continue;
        uint8_t col = (cx < 0) ? 0 : (cx > 127 ? 63 : cx / 2);
        if (dColD[col] < dep) continue;
        int16_t y0 = D_VY + (D_VH - sz) / 2;
        oled.setDrawColor(0);                                    // black outline
        oled.drawBox(cx - sz / 4 - 1, y0 + sz / 4 - 1, sz / 2 + 2, sz * 3 / 4 + 2);
        oled.setDrawColor(1);
        oled.drawBox(cx - sz / 4, y0 + sz / 4, sz / 2, sz * 3 / 4);
        oled.drawBox(cx - sz / 6, y0, sz / 3, sz / 4);
        if (sz > 10) {
          oled.setDrawColor(0);
          oled.drawBox(cx - sz / 8, y0 + sz / 12, sz / 12 + 1, sz / 12 + 1);
          oled.drawBox(cx + sz / 16, y0 + sz / 12, sz / 12 + 1, sz / 12 + 1);
          oled.setDrawColor(1);
        }
        if (fire && abs(cx - 64) < (sz / 2 + 4)) { dMon[i].alive = false; score += 10; sfx(900, 60); }
      }

      // ---- crosshair, gun, health ----
      oled.setDrawColor(0);
      oled.drawBox(56, 54, 16, 10);
      oled.drawBox(0, 56, 20, 8);
      oled.setDrawColor(1);
      oled.drawHLine(61, 40, 6);
      oled.drawVLine(64, 37, 6);
      oled.drawBox(58, 56, 12, 8);
      oled.drawBox(62, 52, 4, 5);
      for (uint8_t i = 0; i < hp; i++) oled.drawBox(2 + i * 6, 58, 4, 5);
      oled.sendBuffer();
    }
  }
}

// =========================================================
//  TUNNEL  (wireframe 3D: fly through a bending tube of rings)
// =========================================================
#define TU_RINGS 16           // rings drawn at once
#define TU_K     15360        // projection constant (60 px at the nearest ring)
#define TU_R     30           // ring half size in world units
#define TU_HOR   (TOP_H + (SCR_H - TOP_H) / 2)

/* draws a line but keeps it inside the blue playing area */
static void tuLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  if (y0 < TOP_H && y1 < TOP_H) return;
  if (y0 != y1) {                                   // clip against the status bar
    if (y0 < TOP_H) { x0 = x0 + (int32_t)(x1 - x0) * (TOP_H - y0) / (y1 - y0); y0 = TOP_H; }
    if (y1 < TOP_H) { x1 = x1 + (int32_t)(x0 - x1) * (TOP_H - y1) / (y0 - y1); y1 = TOP_H; }
  }
  oled.drawLine(x0, y0, x1, y1);
}

/* Every third ring carries a barrier that blocks half of the opening, so
   you have to move out of the way instead of coasting down the middle.  */
static uint8_t tuBar(int32_t idx) {          // 0 = free, 1..4 = blocked side
  if (idx < 6 || idx % 4) return 0;          // a few free rings to settle in
  return 1 + (uint8_t)((idx * 7 + (idx >> 2)) & 3);
}

/* centre of ring number idx; the tube bends with two slow sine waves */
static int16_t tuC(int32_t idx, bool vert) {
  uint8_t a = (uint8_t)(idx * 7 + (vert ? 80 : 0));
  int16_t v = (int16_t)(((int32_t)dSin[a] * 20) >> 10);
  if (vert) v = (int16_t)(((int32_t)dSin[(uint8_t)(idx * 5 + 30)] * 14) >> 10);
  return v;
}

void tunnelRun() {
  dInitSin();
  bool again = true;
  while (again) {
    again = false;
    int16_t  shipX = 0, shipY = 0;
    int32_t  travel = 0, ring = 0;          // travel Q8 inside the current ring
    uint16_t score = 0;
    uint8_t  speed = 10;
    uint32_t next = 0;
    btnClear();

    while (poll()) {
      uint32_t now = millis();
      if (now >= next) {
        next = now + 30;
        if (btnHeld(B_LEFT))  shipX -= 3;
        if (btnHeld(B_RIGHT)) shipX += 3;
        if (btnHeld(B_UP))    shipY -= 3;
        if (btnHeld(B_DOWN))  shipY += 3;
        if (btnHeld(B_OK) && speed < 40) speed++;      // boost
        if (shipX >  60) shipX =  60;
        if (shipX < -60) shipX = -60;
        if (shipY >  40) shipY =  40;
        if (shipY < -40) shipY = -40;

        travel += speed;
        if (travel >= 256) {                          // a ring flew past
          travel -= 256;
          ring++;
          score++;
          if (speed < 30 && (score % 10) == 0) speed++;
          int16_t dx = shipX - tuC(ring, false);      // where we sit in the ring
          int16_t dy = shipY - tuC(ring, true);
          bool crash = (abs(dx) > TU_R - 6 || abs(dy) > TU_R - 6);   // tube wall
          uint8_t bar = tuBar(ring);
          if (!crash && bar) {                         // barrier in this ring
            int16_t edge = TU_R / 5;               // the barrier covers ~40 %
            if ((bar == 1 && dx < -edge) || (bar == 2 && dx > edge) ||
                (bar == 3 && dy < -edge) || (bar == 4 && dy > edge)) crash = true;
          }
          if (crash) {
            sfx(160, 250);
            again = gameOver(score);
            break;
          }
          sfx(950, 15);
        }
      }

      statusBar("TUNNEL", score, curHigh);
      int16_t pcx = 0, pcy = 0, psz = -1;             // previous ring, for the struts
      for (int8_t i = TU_RINGS; i >= 1; i--) {        // far rings first
        int32_t z = (int32_t)i * 256 - travel;
        if (z < 40) continue;
        int16_t sz = (int16_t)(TU_K / z);
        if (sz < 4) continue;
        int16_t cx = 64 + (int16_t)(((int32_t)(tuC(ring + i, false) - shipX) * sz) / TU_R);
        int16_t cy = TU_HOR + (int16_t)(((int32_t)(tuC(ring + i, true) - shipY) * sz) / TU_R);
        int8_t  lvl = (int8_t)(16 - i);               // far rings fade out
        if (lvl < 3) lvl = 3;
        for (int16_t k = -sz; k <= sz; k++) {         // dithered wireframe square
          int16_t x1 = cx + k, y1 = cy + k;
          if (x1 >= 0 && x1 < SCR_W) {
            if (cy - sz >= TOP_H && dInk(x1, cy - sz, lvl)) oled.drawPixel(x1, cy - sz);
            if (cy + sz <  SCR_H  && dInk(x1, cy + sz, lvl)) oled.drawPixel(x1, cy + sz);
          }
          if (y1 >= TOP_H && y1 < SCR_H) {
            if (cx - sz >= 0       && dInk(cx - sz, y1, lvl)) oled.drawPixel(cx - sz, y1);
            if (cx + sz <  SCR_W   && dInk(cx + sz, y1, lvl)) oled.drawPixel(cx + sz, y1);
          }
        }
        uint8_t bar = tuBar(ring + i);                // blocked half of the ring
        if (bar) {
          int16_t x0 = cx - sz, x1 = cx + sz, y0 = cy - sz, y1 = cy + sz;
          int16_t edge = sz / 5;
          if (bar == 1) x1 = cx - edge;               // left side closed
          if (bar == 2) x0 = cx + edge;               // right side closed
          if (bar == 3) y1 = cy - edge;               // top closed
          if (bar == 4) y0 = cy + edge;               // bottom closed
          for (int16_t yy = y0; yy <= y1; yy++)
            for (int16_t xx = x0; xx <= x1; xx++)
              if (xx >= 0 && xx < SCR_W && yy >= TOP_H && yy < SCR_H && dInk(xx, yy, lvl))
                oled.drawPixel(xx, yy);
        }
        if (psz > 0 && i <= 6) {                      // struts along the tube
          tuLine(pcx - psz, pcy - psz, cx - sz, cy - sz);
          tuLine(pcx + psz, pcy - psz, cx + sz, cy - sz);
          tuLine(pcx - psz, pcy + psz, cx - sz, cy + sz);
          tuLine(pcx + psz, pcy + psz, cx + sz, cy + sz);
        }
        pcx = cx; pcy = cy; psz = sz;
      }

      // cockpit marker in the middle of the screen
      oled.drawHLine(60, TU_HOR, 3);
      oled.drawHLine(66, TU_HOR, 3);
      oled.drawVLine(64, TU_HOR - 4, 3);
      oled.drawVLine(64, TU_HOR + 2, 3);
      oled.sendBuffer();
    }
  }
}

// =========================================================
//  FLAPPY   (one button, pipes scrolling right to left)
// =========================================================
#define F_TOP  (TOP_H + 1)
#define F_BOT  (SCR_H - 1)
#define F_GAP  22
#define F_PIPES 3
#define F_BX   24              // bird x position

void flappyRun() {
  bool again = true;
  while (again) {
    again = false;
    int16_t by = ((F_TOP + F_BOT) / 2) << 4, vy = 0;   // bird y / speed, Q4
    int16_t fx[F_PIPES];
    uint8_t fg[F_PIPES];
    for (uint8_t i = 0; i < F_PIPES; i++) {
      fx[i] = SCR_W + i * 44;
      fg[i] = F_TOP + 4 + random(F_BOT - F_TOP - F_GAP - 8);
    }
    uint16_t score = 0;
    uint32_t next = 0;
    btnClear();

    while (poll()) {
      if (btnTap(B_OK) || btn(B_UP)) { vy = -26; sfx(620, 25); }   // flap

      uint32_t now = millis();
      if (now >= next) {
        next = now + 30;
        vy += 2;
        if (vy > 36) vy = 36;
        by += vy;
        if (by < (F_TOP << 4)) { by = F_TOP << 4; vy = 0; }
        bool dead = (by >> 4) > F_BOT - 5;

        for (uint8_t i = 0; i < F_PIPES; i++) {
          int16_t old = fx[i];
          fx[i] -= 2;
          if (old + 8 > F_BX && fx[i] + 8 <= F_BX) { score++; sfx(1200, 40); }   // passed
          if (fx[i] < -8) {
            fx[i] += F_PIPES * 44;
            fg[i] = F_TOP + 4 + random(F_BOT - F_TOP - F_GAP - 8);
          }
          int16_t bt = by >> 4, bb = bt + 5;                       // bird box
          if (fx[i] < F_BX + 6 && fx[i] + 8 > F_BX &&
              (bt < fg[i] || bb > fg[i] + F_GAP)) dead = true;
        }
        if (dead) { again = gameOver(score); break; }
      }

      statusBar("FLAPPY", score, curHigh);
      for (uint8_t i = 0; i < F_PIPES; i++) {
        if (fx[i] > SCR_W || fx[i] < -8) continue;
        int16_t x = fx[i] < 0 ? 0 : fx[i];
        uint8_t w = 8 - (x - fx[i]);
        if (x + w > SCR_W) w = SCR_W - x;
        oled.drawFrame(x, F_TOP, w, fg[i] - F_TOP);
        oled.drawFrame(x, fg[i] + F_GAP, w, F_BOT - fg[i] - F_GAP + 1);
      }
      oled.drawBox(F_BX, by >> 4, 6, 5);                           // bird
      oled.setDrawColor(0);
      oled.drawPixel(F_BX + 4, (by >> 4) + 1);
      oled.setDrawColor(1);
      oled.sendBuffer();
    }
  }
}

// =========================================================
//  INVADERS
// =========================================================
#define V_ROWS 3
#define V_COLS 7
#define V_CW   12              // cell width
#define V_CH   9               // cell height
#define V_TOP  (TOP_H + 2)

static uint8_t vAlive[V_ROWS];

void invadersRun() {
  bool again = true;
  while (again) {
    again = false;
    uint16_t score = 0;
    uint8_t  lives = 3, wave = 0, ship = 60, stepMs = 90;
    int16_t  ax = 2, ay = V_TOP;
    int8_t   adir = 1;
    int16_t  shotX = -1, shotY = 0;
    int16_t  bombX[3] = { -1, -1, -1 }, bombY[3] = { 0, 0, 0 };
    uint32_t next = 0, nextFrame = 0;
    for (uint8_t r = 0; r < V_ROWS; r++) vAlive[r] = (1 << V_COLS) - 1;
    btnClear();

    while (poll()) {
      uint32_t now = millis();
      if (btnHeld(B_LEFT)  && ship > 1)           ship -= 2;
      if (btnHeld(B_RIGHT) && ship < SCR_W - 10)  ship += 2;
      if (btnTap(B_OK) && shotX < 0) { shotX = ship + 4; shotY = SCR_H - 8; sfx(280, 30); }

      if (now >= nextFrame) {                       // bullets run smoothly
        nextFrame = now + 25;
        if (shotX >= 0) { shotY -= 4; if (shotY < TOP_H) shotX = -1; }
        for (uint8_t b = 0; b < 3; b++)
          if (bombX[b] >= 0) {
            bombY[b] += 2;
            if (bombY[b] > SCR_H) bombX[b] = -1;
            else if (bombY[b] >= SCR_H - 6 && abs(bombX[b] - (ship + 4)) < 5) {
              bombX[b] = -1;
              if (--lives == 0) break;
            }
          }
        if (!lives) { again = gameOver(score); break; }

        if (shotX >= 0)                             // bullet vs aliens
          for (uint8_t r = 0; r < V_ROWS && shotX >= 0; r++)
            for (uint8_t c = 0; c < V_COLS; c++) {
              if (!(vAlive[r] & (1 << c))) continue;
              int16_t x = ax + c * V_CW, y = ay + r * V_CH;
              if (shotX >= x && shotX < x + 8 && shotY >= y && shotY < y + 6) {
                vAlive[r] &= ~(1 << c);
                shotX = -1;
                score += 10;
                sfx(880, 40);
                break;
              }
            }
      }

      if (now >= next) {                            // alien step
        next = now + stepMs;
        ax += adir * 2;
        if (ax < 1 || ax + V_COLS * V_CW > SCR_W - 1) { adir = -adir; ay += 3; }
        if (ay + V_ROWS * V_CH >= SCR_H - 6) { again = gameOver(score); break; }

        for (uint8_t b = 0; b < 3; b++)             // drop a bomb
          if (bombX[b] < 0 && random(100) < 12) {
            uint8_t c = random(V_COLS);
            for (int8_t r = V_ROWS - 1; r >= 0; r--)
              if (vAlive[r] & (1 << c)) {
                bombX[b] = ax + c * V_CW + 4;
                bombY[b] = ay + r * V_CH + 6;
                break;
              }
            break;
          }

        uint8_t left = 0;
        for (uint8_t r = 0; r < V_ROWS; r++)
          for (uint8_t c = 0; c < V_COLS; c++) if (vAlive[r] & (1 << c)) left++;
        if (!left) {                                // next wave
          wave++;
          score += 50;
          ay = V_TOP; ax = 2; adir = 1;
          if (stepMs > 40) stepMs -= 10;
          for (uint8_t r = 0; r < V_ROWS; r++) vAlive[r] = (1 << V_COLS) - 1;
        } else if (left < 6 && stepMs > 40) stepMs = 45;
      }

      statusBar("INVADERS", score, curHigh);
      for (uint8_t r = 0; r < V_ROWS; r++)
        for (uint8_t c = 0; c < V_COLS; c++) {
          if (!(vAlive[r] & (1 << c))) continue;
          int16_t x = ax + c * V_CW, y = ay + r * V_CH;
          oled.drawBox(x + 1, y + 1, 6, 3);
          oled.drawPixel(x, y + 4); oled.drawPixel(x + 7, y + 4);
          oled.drawPixel(x + 2, y); oled.drawPixel(x + 5, y);
        }
      oled.drawBox(ship, SCR_H - 5, 9, 4);
      oled.drawBox(ship + 3, SCR_H - 8, 3, 3);
      if (shotX >= 0) oled.drawVLine(shotX, shotY, 3);
      for (uint8_t b = 0; b < 3; b++)
        if (bombX[b] >= 0) oled.drawVLine(bombX[b], bombY[b], 3);
      for (uint8_t i = 1; i < lives; i++) oled.drawBox(SCR_W - i * 5, SCR_H - 4, 3, 3);
      oled.sendBuffer();
    }
  }
}

// =========================================================
//  MINE  (2D Minecraft: dig, build, world is kept in flash)
// =========================================================
#define M_W    64             // world size in tiles
#define M_H    32
#define M_TS    4             // tile size in pixels
#define M_VIEW (SCR_H - TOP_H)

static uint8_t mWorld[M_W * M_H / 2];      // 4 bit per tile = 1024 byte

/* tile 0 = air, 1 = dirt, 2 = stone, 3 = wood, 4 = ore, 5 = leaves */
static const uint16_t M_TEX[6] = { 0x0000, 0xA5A5, 0xFFFF, 0x6666, 0xF99F, 0x9669 };

static uint8_t mGet(int16_t x, int16_t y) {
  if (x < 0 || x >= M_W || y < 0) return 2;          // world edge = stone
  if (y >= M_H) return 2;
  uint16_t i = (uint16_t)y * M_W + x;
  return (i & 1) ? (mWorld[i >> 1] >> 4) : (mWorld[i >> 1] & 15);
}

static void mSet(int16_t x, int16_t y, uint8_t v) {
  if (x < 0 || x >= M_W || y < 0 || y >= M_H) return;
  uint16_t i = (uint16_t)y * M_W + x;
  if (i & 1) mWorld[i >> 1] = (mWorld[i >> 1] & 0x0F) | (v << 4);
  else       mWorld[i >> 1] = (mWorld[i >> 1] & 0xF0) | v;
}

static void mGenerate() {
  memset(mWorld, 0, sizeof(mWorld));
  int8_t h = 14;
  for (int16_t x = 0; x < M_W; x++) {
    h += (int8_t)random(3) - 1;
    if (h < 9)  h = 9;
    if (h > 22) h = 22;
    for (int16_t y = h; y < M_H; y++)
      mSet(x, y, (y < h + 3) ? 1 : (random(14) ? 2 : 4));
    if (x % 9 == 4 && h > 10) {                       // a tree
      for (uint8_t t = 1; t <= 3; t++) mSet(x, h - t, 3);
      for (int8_t dx = -1; dx <= 1; dx++)
        for (int8_t dy = -5; dy <= -3; dy++) mSet(x + dx, h + dy, 5);
    }
  }
}

static bool mLoad() {
  prefs.begin("arcade", true);
  size_t n = prefs.getBytes("world", mWorld, sizeof(mWorld));
  prefs.end();
  return n == sizeof(mWorld);
}

static void mSave() {
  prefs.begin("arcade", false);
  prefs.putBytes("world", mWorld, sizeof(mWorld));
  prefs.end();
}

static bool mSolid(int16_t x, int16_t y, uint8_t w, uint8_t h) {   // pixel box
  for (int16_t px = x; px < x + w; px += 1)
    for (int16_t py = y; py < y + h; py += 1)
      if (mGet(px / M_TS, py / M_TS)) return true;
  return false;
}

void mineRun() {
  if (!mLoad()) mGenerate();

  int16_t  px = 32 * M_TS, py = 0, vy = 0;      // player, py/vy in Q4 pixels
  int8_t   face = 1;
  uint16_t score = 0, inv = 0;
  uint32_t next = 0;
  while (mGet(px / M_TS, py / M_TS + 1) == 0 && py < (M_H - 3) * M_TS * 16) py += 16;
  btnClear();

  while (poll()) {
    uint32_t now = millis();
    if (now >= next) {
      next = now + 30;

      if (btnHeld(B_LEFT) || btnHeld(B_RIGHT)) {
        face = btnHeld(B_LEFT) ? -1 : 1;
        int16_t nx = px + face;
        if (!mSolid(nx, py >> 4, 3, 7)) px = nx;
        else if (!mSolid(nx, (py >> 4) - M_TS, 3, 7)) { px = nx; py -= M_TS << 4; }
      }
      bool ground = mSolid(px, (py >> 4) + 7, 3, 1);
      if (btn(B_UP) && ground) vy = -42;                     // jump

      vy += 4;
      if (vy > 60) vy = 60;
      int16_t ny = py + vy;
      if (vy > 0) {
        if (mSolid(px, (ny >> 4) + 7, 3, 1)) { ny = ((((ny >> 4) + 7) / M_TS) * M_TS - 7) << 4; vy = 0; }
      } else if (mSolid(px, ny >> 4, 3, 1)) {
        ny = ((((ny >> 4) / M_TS) + 1) * M_TS) << 4;
        vy = 0;
      }
      py = ny;
      if ((py >> 4) > M_H * M_TS) { py = 0; px = 32 * M_TS; }   // fell out

      /* OK works on the tile in front, DOWN on the tile under your feet.
         Solid tile -> dig it, empty tile -> place a block from the bag. */
      bool actF = btn(B_OK), actD = btn(B_DOWN);
      if (actF || actD) {
        int16_t tx, ty;
        if (actD) { tx = px / M_TS;                        ty = ((py >> 4) + 8) / M_TS; }
        else      { tx = (px + (face > 0 ? 4 : -1)) / M_TS; ty = ((py >> 4) + 3) / M_TS; }
        uint8_t t = mGet(tx, ty);
        if (t && ty < M_H)      { mSet(tx, ty, 0); inv++; score++; sfx(320, 25); }   // dig
        else if (!t && inv)     { mSet(tx, ty, 1); inv--; }            // build
      }
    }

    // ---- draw ----
    int16_t camX = px - SCR_W / 2, camY = (py >> 4) - M_VIEW / 2;
    if (camX < 0) camX = 0;
    if (camX > M_W * M_TS - SCR_W) camX = M_W * M_TS - SCR_W;
    if (camY < 0) camY = 0;
    if (camY > M_H * M_TS - M_VIEW) camY = M_H * M_TS - M_VIEW;

    statusBar("MINE", score, curHigh);
    for (int16_t ty = camY / M_TS; ty <= (camY + M_VIEW) / M_TS; ty++)
      for (int16_t tx = camX / M_TS; tx <= (camX + SCR_W) / M_TS; tx++) {
        uint8_t t = mGet(tx, ty);
        if (!t || t > 5) continue;
        uint16_t tex = M_TEX[t];
        for (uint8_t r = 0; r < 4; r++)
          for (uint8_t c = 0; c < 4; c++)
            if (tex & (0x8000 >> (r * 4 + c)))
              oled.drawPixel(tx * M_TS + c - camX, ty * M_TS + r - camY + TOP_H);
      }
    int16_t sx = px - camX, sy = (py >> 4) - camY + TOP_H;
    oled.setDrawColor(0);                                          // outline so the
    oled.drawBox(sx - 1, sy - 1, 5, 9);                            // player stays
    oled.setDrawColor(1);                                          // visible in dirt
    oled.drawBox(sx, sy, 3, 7);

    char b[16];
    snprintf(b, sizeof(b), "BAG %u", inv);
    oled.setDrawColor(0);
    oled.drawBox(0, SCR_H - 8, oled.getStrWidth(b) + 4, 8);
    oled.setDrawColor(1);
    oled.drawStr(2, SCR_H - 1, b);
    oled.sendBuffer();
  }

  mSave();                                    // keep the world for next time
  if (score > curHigh) { curHigh = score; saveHigh(curGame, curHigh); }
}

// =========================================================
//  DINO   (the Chrome offline runner)
// =========================================================
#define DN_GY   61            // ground line
#define DN_X    12            // dino x position
#define DN_OBS   3

#define DN_CLOUDS 3

/* obstacle kinds: 0 = one small cactus, 1 = two small, 2 = big cactus,
   3 = pterodactyl (only once the score got past 30)                     */
static uint8_t dnWidth(uint8_t t)  { return (t == 1) ? 13 : (t == 2 ? 9 : (t == 3 ? 9 : 6)); }
static uint8_t dnHeight(uint8_t t) { return (t == 2) ? 13 : (t == 3 ? 6 : 9); }

void dinoRun() {
  bool again = true;
  while (again) {
    again = false;
    int16_t  y = 0, vy = 0;                    // height above ground, Q4
    int16_t  ox[DN_OBS], cx[DN_CLOUDS];
    uint8_t  ot[DN_OBS], oh[DN_OBS], cy[DN_CLOUDS];
    uint16_t score = 0, dist = 0, spd = 26, blink = 0, nextBlink = 100;
    bool     night = false;
    uint32_t next = 0;
    for (uint8_t i = 0; i < DN_OBS; i++) {
      ox[i] = SCR_W + 30 + i * 60;
      ot[i] = random(3);
      oh[i] = 0;
    }
    for (uint8_t i = 0; i < DN_CLOUDS; i++) { cx[i] = random(SCR_W); cy[i] = 18 + random(9); }
    btnClear();

    while (poll()) {
      bool duck = btnHeld(B_DOWN) && y == 0;
      if ((btnTap(B_UP) || btnTap(B_OK)) && y == 0) { vy = -52; sfx(700, 35); }
      if (btnHeld(B_DOWN) && y < 0) vy += 6;             // dive back down

      uint32_t now = millis();
      if (now >= next) {
        next = now + 30;
        vy += 5;
        y += vy;
        if (y > 0) { y = 0; vy = 0; }
        dist += spd;
        score = dist / 80;
        if (spd < 64) spd = 26 + score / 5;
        if (score >= nextBlink) { nextBlink += 100; blink = 30; sfx(1150, 120); }
        if (blink) blink--;
        if (score && score % 350 == 0 && (dist / 80) != (dist - spd) / 80) night = !night;

        uint8_t dh = duck ? 7 : 14;
        int16_t dt = DN_GY - dh + (y >> 4);
        for (uint8_t i = 0; i < DN_OBS; i++) {
          ox[i] -= spd / 8;
          if (ox[i] < -16) {                              // recycle behind the screen
            int16_t far = ox[0];
            for (uint8_t k = 1; k < DN_OBS; k++) if (ox[k] > far) far = ox[k];
            ox[i] = far + 45 + random(50);
            ot[i] = (score > 30 && random(4) == 0) ? 3 : random(3);
            oh[i] = (ot[i] == 3) ? random(3) : 0;         // three flight heights
          }
          int16_t ow = dnWidth(ot[i]), ohh = dnHeight(ot[i]);
          int16_t oy = (ot[i] == 3) ? (DN_GY - 12 - oh[i] * 8) : (DN_GY - ohh);
          if (ox[i] < DN_X + (duck ? 12 : 8) && ox[i] + ow > DN_X + 1 &&
              oy < dt + dh && oy + ohh > dt) {
            again = gameOver(score);
            goto dinoDone;
          }
        }
        for (uint8_t i = 0; i < DN_CLOUDS; i++) {         // clouds drift slowly
          cx[i] -= 1;
          if (cx[i] < -14) { cx[i] = SCR_W + random(30); cy[i] = 18 + random(9); }
        }
      }

      // ---------------- draw ----------------
      statusBar("DINO", score, curHigh);
      if (!blink || (blink / 5) & 1) {                    // score blinks every 100
        for (uint8_t i = 0; i < DN_CLOUDS; i++) {         // clouds
          oled.drawHLine(cx[i] + 2, cy[i], 8);
          oled.drawHLine(cx[i], cy[i] + 1, 12);
          oled.drawPixel(cx[i] + 3, cy[i] - 1);
          oled.drawPixel(cx[i] + 7, cy[i] - 1);
        }
      }
      oled.drawHLine(0, DN_GY + 1, SCR_W);                // ground
      for (uint8_t i = 0; i < 10; i++) {
        int16_t gx = (i * 13 - (dist / 8) % 13 + SCR_W) % SCR_W;
        oled.drawPixel(gx, DN_GY + 3);
        if (i & 1) oled.drawHLine(gx, DN_GY + 2, 2);
      }

      {                                                   // the dino itself
        uint8_t dh = duck ? 7 : 14;
        int16_t dt = DN_GY - dh + (y >> 4);
        if (duck) {
          oled.drawBox(DN_X, dt + 1, 12, 6);              // stretched body
          oled.drawBox(DN_X + 10, dt, 6, 5);              // head
          oled.setDrawColor(0);
          oled.drawPixel(DN_X + 14, dt + 1);
          oled.setDrawColor(1);
          oled.drawBox(DN_X + 2, dt + 7, 3, 2);
        } else {
          oled.drawBox(DN_X + 1, dt + 4, 6, 8);           // body
          oled.drawBox(DN_X + 5, dt, 7, 6);               // head
          oled.setDrawColor(0);
          oled.drawPixel(DN_X + 10, dt + 2);              // eye
          oled.drawBox(DN_X + 6, dt + 5, 3, 1);           // mouth line
          oled.setDrawColor(1);
          oled.drawBox(DN_X, dt + 6, 2, 3);               // arm
          if (y < 0) {                                    // jumping: legs tucked
            oled.drawBox(DN_X + 2, dt + 12, 2, 2);
            oled.drawBox(DN_X + 5, dt + 12, 2, 2);
          } else if (((dist / 6) & 1)) {                  // running animation
            oled.drawBox(DN_X + 2, dt + 12, 2, 2);
          } else {
            oled.drawBox(DN_X + 5, dt + 12, 2, 2);
          }
        }
      }

      for (uint8_t i = 0; i < DN_OBS; i++) {              // obstacles
        if (ox[i] > SCR_W || ox[i] < -16) continue;
        if (ot[i] == 3) {                                 // pterodactyl
          int16_t by = DN_GY - 12 - oh[i] * 8;
          oled.drawBox(ox[i] + 2, by + 2, 5, 3);
          oled.drawPixel(ox[i] + 7, by + 2);
          bool up = (dist / 50) & 1;
          if (up) {
            oled.drawLine(ox[i] + 2, by + 2, ox[i] - 1, by - 2);
            oled.drawLine(ox[i] + 6, by + 2, ox[i] + 9, by - 2);
          } else {
            oled.drawLine(ox[i] + 2, by + 3, ox[i] - 1, by + 7);
            oled.drawLine(ox[i] + 6, by + 3, ox[i] + 9, by + 7);
          }
        } else {
          uint8_t n = (ot[i] == 1) ? 2 : 1;
          uint8_t h = dnHeight(ot[i]), w = (ot[i] == 2) ? 5 : 4;
          for (uint8_t k = 0; k < n; k++) {
            int16_t x = ox[i] + k * 7;
            oled.drawBox(x + w / 2 - 1, DN_GY - h, 3, h);          // trunk
            oled.drawBox(x, DN_GY - h + 3, 2, 3);                  // left arm
            oled.drawBox(x + 1, DN_GY - h + 3, 1, 1);
            oled.drawBox(x + w - 1, DN_GY - h + 4, 2, 3);          // right arm
          }
        }
      }

      if (night) {                                        // night: invert the scene
        oled.setDrawColor(2);
        oled.drawBox(0, TOP_H, SCR_W, SCR_H - TOP_H);
        oled.setDrawColor(1);
      }
      oled.sendBuffer();
    }
    dinoDone: ;
  }
}

// =========================================================
//  BREAKOUT
// =========================================================
#define BR_COLS 8
#define BR_ROWS 4
#define BR_BW   15            // brick width
#define BR_BH    4
#define BR_TOP  (TOP_H + 3)

static uint8_t brBricks[BR_ROWS];          // one bit per column

void breakoutRun() {
  bool again = true;
  while (again) {
    again = false;
    uint16_t score = 0;
    uint8_t  lives = 3, pad = 52, level = 0;
    int16_t  bx = 64 << 4, by = 50 << 4, vx = 14, vy = -18;
    uint32_t next = 0;
    for (uint8_t r = 0; r < BR_ROWS; r++) brBricks[r] = 0xFF;
    btnClear();

    while (poll()) {
      uint32_t now = millis();
      if (now >= next) {
        next = now + 20;
        if (btnHeld(B_LEFT)  && pad > 1)            pad -= 3;
        if (btnHeld(B_RIGHT) && pad < SCR_W - 25)   pad += 3;

        bx += vx; by += vy;
        if ((bx >> 4) < 0)        { bx = 0;              vx = -vx; }
        if ((bx >> 4) > SCR_W - 3){ bx = (SCR_W-3) << 4; vx = -vx; }
        if ((by >> 4) < TOP_H)    { by = TOP_H << 4;     vy = -vy; }

        int16_t px = bx >> 4, py = by >> 4;
        if (vy > 0 && py >= 58 && py <= 61 && px + 2 >= pad && px <= pad + 24) {
          vy = -vy;
          vx += ((px - (pad + 12)) * 2) / 3;          // steer with the paddle
          if (vx >  26) vx =  26;
          if (vx < -26) vx = -26;
        }
        if (py > SCR_H) {                              // lost the ball
          if (--lives == 0) { again = gameOver(score); break; }
          bx = 64 << 4; by = 50 << 4; vx = 14; vy = -18;
        }

        for (uint8_t r = 0; r < BR_ROWS; r++) {        // brick hits
          int16_t ry = BR_TOP + r * (BR_BH + 2);
          if (py + 2 < ry || py > ry + BR_BH) continue;
          uint8_t c = px / BR_BW;
          if (c < BR_COLS && (brBricks[r] & (1 << c))) {
            brBricks[r] &= ~(1 << c);
            vy = -vy;
            sfx(850, 25);
            score += 10 * (level + 1);
          }
        }
        uint8_t left = 0;
        for (uint8_t r = 0; r < BR_ROWS; r++) for (uint8_t c = 0; c < BR_COLS; c++)
          if (brBricks[r] & (1 << c)) left++;
        if (!left) {                                   // next level
          level++;
          score += 50;
          for (uint8_t r = 0; r < BR_ROWS; r++) brBricks[r] = 0xFF;
          bx = 64 << 4; by = 50 << 4;
          vx = (vx > 0 ? 16 : -16) + level; vy = -20 - level;
        }
      }

      statusBar("BREAKOUT", score, curHigh);
      for (uint8_t r = 0; r < BR_ROWS; r++)
        for (uint8_t c = 0; c < BR_COLS; c++)
          if (brBricks[r] & (1 << c))
            oled.drawBox(c * BR_BW + 1, BR_TOP + r * (BR_BH + 2), BR_BW - 2, BR_BH);
      oled.drawBox(pad, 61, 25, 3);
      oled.drawBox(bx >> 4, by >> 4, 3, 3);
      for (uint8_t i = 1; i < lives; i++) oled.drawBox(SCR_W - i * 5, 18, 3, 3);
      oled.sendBuffer();
    }
  }
}

// =========================================================
//  ASTEROIDS
// =========================================================
#define AS_N     9            // rocks on screen at once
#define AS_SHOTS 4
#define AS_TOP   TOP_H

static int16_t asX[AS_N], asY[AS_N], asVX[AS_N], asVY[AS_N];   // Q4 pixels
static uint8_t asSize[AS_N];                                   // 0 = gone, 1..3

static void asWrap(int16_t *x, int16_t *y) {
  if (*x < 0)              *x += SCR_W << 4;
  if (*x >= (SCR_W << 4))  *x -= SCR_W << 4;
  if (*y < (AS_TOP << 4))  *y += (SCR_H - AS_TOP) << 4;
  if (*y >= (SCR_H << 4))  *y -= (SCR_H - AS_TOP) << 4;
}

static void asSpawn(uint8_t i, int16_t x, int16_t y, uint8_t size) {
  asX[i] = x; asY[i] = y; asSize[i] = size;
  asVX[i] = (int16_t)random(-6, 7);
  asVY[i] = (int16_t)random(-6, 7);
  if (!asVX[i] && !asVY[i]) asVX[i] = 4;
}

void asteroidsRun() {
  dInitSin();
  bool again = true;
  while (again) {
    again = false;
    int16_t sx = (SCR_W / 2) << 4, sy = ((AS_TOP + SCR_H) / 2) << 4, svx = 0, svy = 0;
    uint8_t ang = 192, lives = 3, wave = 1;
    int16_t shx[AS_SHOTS], shy[AS_SHOTS], shvx[AS_SHOTS], shvy[AS_SHOTS];
    uint8_t shl[AS_SHOTS];
    uint16_t score = 0;
    uint32_t next = 0;
    for (uint8_t i = 0; i < AS_SHOTS; i++) shl[i] = 0;
    for (uint8_t i = 0; i < AS_N; i++) asSize[i] = 0;
    for (uint8_t i = 0; i < 4; i++)
      asSpawn(i, random(SCR_W) << 4, (AS_TOP + random(SCR_H - AS_TOP)) << 4, 3);
    btnClear();

    while (poll()) {
      uint32_t now = millis();
      if (now >= next) {
        next = now + 30;
        if (btnHeld(B_LEFT))  ang -= 8;
        if (btnHeld(B_RIGHT)) ang += 8;
        if (btnHeld(B_UP)) { svx += DCOS(ang) >> 7; svy += DSIN(ang) >> 7; }
        int16_t sp = svx * svx + svy * svy;                    // simple speed limit
        if (sp > 900) { svx = (svx * 9) / 10; svy = (svy * 9) / 10; }
        svx = (svx * 63) / 64; svy = (svy * 63) / 64;          // drag
        sx += svx; sy += svy;
        asWrap(&sx, &sy);

        if (btnTap(B_OK))
          for (uint8_t i = 0; i < AS_SHOTS; i++)
            if (!shl[i]) {
              shx[i] = sx; shy[i] = sy;
              shvx[i] = (DCOS(ang) >> 5) + svx;
              shvy[i] = (DSIN(ang) >> 5) + svy;
              shl[i] = 30;
              break;
            }

        for (uint8_t i = 0; i < AS_SHOTS; i++) {
          if (!shl[i]) continue;
          shx[i] += shvx[i]; shy[i] += shvy[i];
          asWrap(&shx[i], &shy[i]);
          shl[i]--;
          for (uint8_t a = 0; a < AS_N && shl[i]; a++) {
            if (!asSize[a]) continue;
            int16_t dx = (shx[i] - asX[a]) >> 4, dy = (shy[i] - asY[a]) >> 4;
            int16_t r = asSize[a] * 3;
            if (dx * dx + dy * dy < r * r) {
              shl[i] = 0;
              sfx(260, 45);
              score += 10 * asSize[a];
              uint8_t ns = asSize[a] - 1;
              asSize[a] = 0;
              if (ns)                                         // break into two
                for (uint8_t k = 0, made = 0; k < AS_N && made < 2; k++)
                  if (!asSize[k]) { asSpawn(k, asX[a], asY[a], ns); made++; }
            }
          }
        }

        uint8_t alive = 0;
        for (uint8_t a = 0; a < AS_N; a++) {
          if (!asSize[a]) continue;
          alive++;
          asX[a] += asVX[a]; asY[a] += asVY[a];
          asWrap(&asX[a], &asY[a]);
          int16_t dx = (sx - asX[a]) >> 4, dy = (sy - asY[a]) >> 4;
          int16_t r = asSize[a] * 3 + 2;
          if (dx * dx + dy * dy < r * r) {                     // ship hit
            if (--lives == 0) { again = gameOver(score); goto astDone; }
            sx = (SCR_W / 2) << 4; sy = ((AS_TOP + SCR_H) / 2) << 4;
            svx = svy = 0;
            asSize[a] = 0;
          }
        }
        if (!alive) {                                          // next wave
          wave++;
          score += 50;
          for (uint8_t i = 0; i < 3 + (wave > 3 ? 2 : wave / 2); i++)
            asSpawn(i, random(SCR_W) << 4, (AS_TOP + random(SCR_H - AS_TOP)) << 4, 3);
        }
      }

      statusBar("ROCKS", score, curHigh);
      for (uint8_t a = 0; a < AS_N; a++) {                     // rocks as polygons
        if (!asSize[a]) continue;
        int16_t cx = asX[a] >> 4, cy = asY[a] >> 4, r = asSize[a] * 3;
        int16_t px = 0, py = 0;
        for (uint8_t k = 0; k <= 6; k++) {
          uint8_t t = (uint8_t)(k * 43 + a * 20);
          int16_t qx = cx + ((DCOS(t) * r) >> 10);
          int16_t qy = cy + ((DSIN(t) * r) >> 10);
          if (k) oled.drawLine(px, py, qx, qy);
          px = qx; py = qy;
        }
      }
      {                                                        // ship
        int16_t cx = sx >> 4, cy = sy >> 4;
        int16_t nx = cx + ((DCOS(ang) * 5) >> 10),      ny = cy + ((DSIN(ang) * 5) >> 10);
        int16_t lx = cx + ((DCOS(ang + 100) * 4) >> 10), ly = cy + ((DSIN(ang + 100) * 4) >> 10);
        int16_t rx = cx + ((DCOS(ang - 100) * 4) >> 10), ry = cy + ((DSIN(ang - 100) * 4) >> 10);
        oled.drawLine(nx, ny, lx, ly);
        oled.drawLine(nx, ny, rx, ry);
        oled.drawLine(lx, ly, rx, ry);
      }
      for (uint8_t i = 0; i < AS_SHOTS; i++)
        if (shl[i]) oled.drawBox(shx[i] >> 4, shy[i] >> 4, 2, 2);
      for (uint8_t i = 1; i < lives; i++) oled.drawBox(SCR_W - i * 5, 18, 3, 3);
      oled.sendBuffer();
    }
    astDone: ;
  }
}

// =========================================================
//  RACER   (endless road seen from above)
// =========================================================
#define RC_TOP  TOP_H
#define RC_CARY 52            // player car y
#define RC_OPP   3
#define RC_HALF 26            // half road width

static int16_t rcMid(uint8_t curve, int16_t row) {      // road centre for a row
  return SCR_W / 2 + ((DSIN((uint8_t)(curve + row)) * 18) >> 10);
}

void racerRun() {
  dInitSin();
  bool again = true;
  while (again) {
    again = false;
    uint16_t score = 0, dist = 0;
    uint8_t  spd = 3, curve = 0;
    int16_t  carx = rcMid(0, RC_CARY) - 3, opx[RC_OPP], opy[RC_OPP];
    uint32_t next = 0;
    for (uint8_t i = 0; i < RC_OPP; i++) {              // well spaced, off to the side
      opy[i] = RC_TOP - 30 - i * 60;
      opx[i] = random(-RC_HALF + 6, RC_HALF - 12);
    }
    btnClear();

    while (poll()) {
      uint32_t now = millis();
      if (now >= next) {
        next = now + 30;
        if (btnHeld(B_LEFT)  && carx > 2)          carx -= 3;
        if (btnHeld(B_RIGHT) && carx < SCR_W - 9)  carx += 3;
        curve += 1;
        dist += spd;
        score = dist / 20;
        if (spd < 8 && score > (uint16_t)spd * 40) { spd++; sfx(1000, 60); }

        int16_t mid = rcMid(curve, RC_CARY);
        if (carx + 3 < mid - RC_HALF || carx + 4 > mid + RC_HALF) {   // off road
          again = gameOver(score);
          break;
        }
        for (uint8_t i = 0; i < RC_OPP; i++) {
          opy[i] += spd;
          if (opy[i] > SCR_H) {
            opy[i] = RC_TOP - 20 - random(70);
            opx[i] = random(-RC_HALF + 6, RC_HALF - 12);
          }
          int16_t ox = rcMid(curve, opy[i]) + opx[i];
          if (opy[i] + 9 > RC_CARY && opy[i] < RC_CARY + 9 &&
              ox + 7 > carx && ox < carx + 7) {
            again = gameOver(score);
            goto racerDone;
          }
        }
      }

      statusBar("RACER", score, curHigh);
      for (int16_t y = RC_TOP; y < SCR_H; y++) {               // road edges
        int16_t mid = rcMid(curve, y);
        oled.drawPixel(mid - RC_HALF, y);
        oled.drawPixel(mid + RC_HALF, y);
        if (((y + dist / 2) % 12) < 5) oled.drawPixel(mid, y);  // centre line
      }
      for (uint8_t i = 0; i < RC_OPP; i++) {
        if (opy[i] < RC_TOP - 9 || opy[i] > SCR_H) continue;
        int16_t ox = rcMid(curve, opy[i]) + opx[i];
        oled.drawFrame(ox, opy[i], 7, 9);
        oled.drawHLine(ox + 1, opy[i] + 3, 5);
      }
      oled.drawBox(carx, RC_CARY, 7, 9);
      oled.setDrawColor(0);
      oled.drawHLine(carx + 1, RC_CARY + 3, 5);
      oled.setDrawColor(1);
      oled.sendBuffer();
    }
    racerDone: ;
  }
}

// =========================================================
//  FROGGER
// =========================================================
#define FR_ROWH  6            // one lane is 6 px high
#define FR_ROWS  8            // 48 px viewport = 8 lanes
#define FR_LANES 6            // lanes 1..6 are traffic and water

static int16_t frOff[FR_LANES];        // scroll offset per lane

void froggerRun() {
  static const char *const lvl[3] = { "easy", "normal", "hard" };
  uint8_t diff = chooseMode("FROGGER", lvl, 3);
  if (diff == 255) return;
  const uint8_t objs = (diff == 2) ? 4 : 3;          // objects per lane
  const uint8_t logW = (diff == 0) ? 34 : (diff == 1 ? 26 : 20);
  const uint8_t carW = (diff == 0) ? 10 : (diff == 1 ? 14 : 17);
  const uint8_t gap  = SCR_W / objs;
  const int8_t  scale = (diff == 0) ? 70 : (diff == 1 ? 100 : 145);

  bool again = true;
  while (again) {
    again = false;
    int8_t   fx = 60, fy = FR_ROWS - 1;
    uint8_t  lives = 3;
    uint16_t score = 0;
    uint32_t next = 0, step = 0;
    static const int8_t frBase[FR_LANES] = { 2, -3, 2, -2, 3, -2 };
    int8_t frSpd[FR_LANES];
    for (uint8_t i = 0; i < FR_LANES; i++) {
      frSpd[i] = (int8_t)((int16_t)frBase[i] * scale / 100);
      if (!frSpd[i]) frSpd[i] = (frBase[i] > 0) ? 1 : -1;
      frOff[i] = random(gap);
    }
    btnClear();

    while (poll()) {
      uint32_t now = millis();
      if (now >= step) {                              // hopping
        step = now + 90;
        if (btn(B_LEFT)  && fx > 1)            { fx -= 6; sfx(600, 20); }
        if (btn(B_RIGHT) && fx < SCR_W - 7)    { fx += 6; sfx(600, 20); }
        if (btn(B_UP)    && fy > 0)            { fy--;    sfx(800, 20); }
        if (btn(B_DOWN)  && fy < FR_ROWS - 1)  { fy++;    sfx(500, 20); }
      }

      if (now >= next) {
        next = now + 40;
        for (uint8_t i = 0; i < FR_LANES; i++) {
          frOff[i] += frSpd[i];
          if (frOff[i] >  SCR_W) frOff[i] -= SCR_W;
          if (frOff[i] < -SCR_W) frOff[i] += SCR_W;
        }

        if (fy == 0) {                                // reached the far bank
          score += 50;
          sfx(1400, 150);
          fy = FR_ROWS - 1;
          fx = 60;
        } else if (fy >= 1 && fy <= FR_LANES) {
          uint8_t l = fy - 1;
          bool water = (l < 3);
          bool onIt = false;
          for (uint8_t k = 0; k < objs; k++) {
            int16_t ox = ((frOff[l] + k * gap) % SCR_W + SCR_W) % SCR_W;
            int16_t ow = water ? logW : carW;
            if (fx + 4 > ox && fx < ox + ow) onIt = true;
            if (ox + ow > SCR_W && fx < ox + ow - SCR_W) onIt = true;   // wrapped
          }
          bool dead = water ? !onIt : onIt;
          if (water && onIt) fx += frSpd[l];           // ride the log
          if (fx < 0 || fx > SCR_W - 5) dead = true;
          if (dead) {
            sfx(150, 200);
            if (--lives == 0) { again = gameOver(score); break; }
            fy = FR_ROWS - 1; fx = 60;
          }
        }
      }

      statusBar("FROGGER", score, curHigh);
      for (uint8_t l = 0; l < FR_LANES; l++) {
        int16_t ly = TOP_H + (l + 1) * FR_ROWH;
        bool water = (l < 3);
        for (uint8_t k = 0; k < objs; k++) {
          int16_t ox = ((frOff[l] + k * gap) % SCR_W + SCR_W) % SCR_W;
          uint8_t w = water ? logW : carW;
          if (water) {
            oled.drawFrame(ox, ly, w, FR_ROWH - 1);
            if (ox + w > SCR_W) oled.drawFrame(ox - SCR_W, ly, w, FR_ROWH - 1);
          } else {
            oled.drawBox(ox, ly, w, FR_ROWH - 1);
            if (ox + w > SCR_W) oled.drawBox(ox - SCR_W, ly, w, FR_ROWH - 1);
          }
        }
      }
      oled.drawHLine(0, TOP_H + FR_ROWH - 1, SCR_W);
      oled.drawHLine(0, TOP_H + (FR_LANES + 1) * FR_ROWH - 1, SCR_W);
      oled.setDrawColor(0);
      oled.drawBox(fx - 1, TOP_H + fy * FR_ROWH, 7, FR_ROWH);
      oled.setDrawColor(1);
      oled.drawBox(fx, TOP_H + fy * FR_ROWH + 1, 5, 4);
      oled.drawPixel(fx - 1, TOP_H + fy * FR_ROWH);
      oled.drawPixel(fx + 5, TOP_H + fy * FR_ROWH);
      for (uint8_t i = 1; i < lives; i++) oled.drawBox(SCR_W - i * 5, 18, 3, 3);
      oled.sendBuffer();
    }
  }
}

// =========================================================
//  CONNECT FOUR   (against the ESP32)
// =========================================================
#define C4_W 7
#define C4_H 6
#define C4_CELL 7
#define C4_X 45          // leaves room for the hint text on the left
#define C4_Y (TOP_H + 1)

static uint8_t c4[C4_H][C4_W];         // 0 empty, 1 player, 2 cpu

static int8_t c4Drop(uint8_t col, uint8_t who) {
  for (int8_t r = C4_H - 1; r >= 0; r--)
    if (!c4[r][col]) { c4[r][col] = who; return r; }
  return -1;
}

static bool c4Wins(uint8_t who) {
  for (int8_t r = 0; r < C4_H; r++)
    for (int8_t c = 0; c < C4_W; c++) {
      if (c4[r][c] != who) continue;
      if (c + 3 < C4_W && c4[r][c+1] == who && c4[r][c+2] == who && c4[r][c+3] == who) return true;
      if (r + 3 < C4_H && c4[r+1][c] == who && c4[r+2][c] == who && c4[r+3][c] == who) return true;
      if (r + 3 < C4_H && c + 3 < C4_W && c4[r+1][c+1] == who && c4[r+2][c+2] == who && c4[r+3][c+3] == who) return true;
      if (r + 3 < C4_H && c - 3 >= 0   && c4[r+1][c-1] == who && c4[r+2][c-2] == who && c4[r+3][c-3] == who) return true;
    }
  return false;
}

static bool c4Full() {
  for (uint8_t c = 0; c < C4_W; c++) if (!c4[0][c]) return false;
  return true;
}

/* ---- engine: negamax with alpha-beta pruning ----
   Search depth sets the difficulty. Equally good moves are picked at
   random, so the machine does not repeat the same game every time.      */
#define C4_INF 30000

static int c4Window(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t me) {
  uint8_t mine = 0, opp = 0;
  uint8_t v[4] = { a, b, c, d };
  for (uint8_t i = 0; i < 4; i++) {
    if (v[i] == me) mine++;
    else if (v[i]) opp++;
  }
  if (mine && opp) return 0;                 // blocked window, worth nothing
  if (mine == 3) return 80;
  if (mine == 2) return 10;
  if (opp  == 3) return -95;                 // block a little harder than build
  if (opp  == 2) return -12;
  return 0;
}

static int c4Eval(uint8_t me) {
  int v = 0;
  for (uint8_t r = 0; r < C4_H; r++)
    for (uint8_t c = 0; c < C4_W; c++) {
      if (c + 3 < C4_W) v += c4Window(c4[r][c], c4[r][c+1], c4[r][c+2], c4[r][c+3], me);
      if (r + 3 < C4_H) v += c4Window(c4[r][c], c4[r+1][c], c4[r+2][c], c4[r+3][c], me);
      if (r + 3 < C4_H && c + 3 < C4_W)
        v += c4Window(c4[r][c], c4[r+1][c+1], c4[r+2][c+2], c4[r+3][c+3], me);
      if (r + 3 < C4_H && c >= 3)
        v += c4Window(c4[r][c], c4[r+1][c-1], c4[r+2][c-2], c4[r+3][c-3], me);
    }
  for (uint8_t r = 0; r < C4_H; r++)         // the middle column is worth more
    if (c4[r][3] == me) v += 6;
    else if (c4[r][3]) v -= 6;
  return v;
}

static const uint8_t C4_ORDER[C4_W] = { 3, 2, 4, 1, 5, 0, 6 };

static int c4Neg(uint8_t depth, int alpha, int beta, uint8_t me) {
  uint8_t opp = 3 - me;
  int best = -C4_INF - 1;
  for (uint8_t i = 0; i < C4_W; i++) {
    uint8_t c = C4_ORDER[i];
    int8_t r = c4Drop(c, me);
    if (r < 0) continue;
    int v;
    if (c4Wins(me))     v = C4_INF - (20 - depth);     // win as early as possible
    else if (!depth)    v = c4Eval(me);
    else                v = -c4Neg(depth - 1, -beta, -alpha, opp);
    c4[r][c] = 0;
    if (v > best) best = v;
    if (best > alpha) alpha = best;
    if (alpha >= beta) break;                          // cut off
  }
  return (best == -C4_INF - 1) ? 0 : best;             // board full = draw
}

/* difficulty 0 = easy, 1 = normal, 2 = hard */
static uint8_t c4Think(uint8_t level) {
  uint8_t depth = (level == 0) ? 1 : (level == 1 ? 2 : 5);   // ~0.4 s per move at 5
  if (level == 0 && random(10) < 5) {                  // easy also blunders
    uint8_t pick[C4_W], n = 0;
    for (uint8_t c = 0; c < C4_W; c++) if (!c4[0][c]) pick[n++] = c;
    if (n) return pick[random(n)];
  }
  int bestV = -C4_INF - 1;
  uint8_t best[C4_W], n = 0;
  for (uint8_t i = 0; i < C4_W; i++) {
    uint8_t c = C4_ORDER[i];
    int8_t r = c4Drop(c, 2);
    if (r < 0) continue;
    int v;
    if (c4Wins(2)) v = C4_INF;
    else           v = -c4Neg(depth, -C4_INF, C4_INF, 1);
    c4[r][c] = 0;
    if (v > bestV) { bestV = v; n = 0; best[n++] = c; }
    else if (v == bestV && n < C4_W) best[n++] = c;    // collect equal moves
  }
  return n ? best[random(n)] : 0;
}

static void c4Draw(uint8_t sel, const char *msg, uint16_t score) {
  statusBar("4 WINS", score, curHigh);
  oled.drawFrame(C4_X - 2, C4_Y - 1, C4_W * C4_CELL + 4, C4_H * C4_CELL + 2);
  for (uint8_t r = 0; r < C4_H; r++)
    for (uint8_t c = 0; c < C4_W; c++) {
      int16_t x = C4_X + c * C4_CELL, y = C4_Y + r * C4_CELL;
      if (c4[r][c] == 1)      oled.drawBox(x + 1, y + 1, 5, 5);
      else if (c4[r][c] == 2) oled.drawFrame(x + 1, y + 1, 5, 5);
      else                    oled.drawPixel(x + 3, y + 3);
    }
  oled.drawBox(C4_X + sel * C4_CELL + 2, C4_Y - 4, 3, 2);
  if (msg) oled.drawStr(2, SCR_H - 1, msg);
  oled.sendBuffer();
}

void connect4Run() {
  static const char *const modes[4] = { "1P easy", "1P normal", "1P hard", "2 players" };
  uint8_t mode = chooseMode("4 WINS", modes, 4);
  if (mode == 255) return;
  bool twoPlayers = (mode == 3);

  bool again = true;
  while (again) {
    again = false;
    uint16_t score = 0;
    uint8_t sel = 3, turn = 1;                 // 1 = player one, 2 = cpu / player two
    memset(c4, 0, sizeof(c4));
    btnClear();

    while (poll()) {
      if (btn(B_LEFT)  && sel > 0)         { sel--; sfx(700, 15); }
      if (btn(B_RIGHT) && sel < C4_W - 1)  { sel++; sfx(700, 15); }

      if (btn(B_OK) || btn(B_DOWN)) {
        if (c4Drop(sel, turn) >= 0) {
          sfx(500, 40);
          if (c4Wins(turn)) {                  // somebody got four in a row
            sfx(1400, 250);
            const char *msg = twoPlayers ? (turn == 1 ? "P1 WINS!" : "P2 WINS!") : "YOU WIN!";
            if (turn == 1) score += 100;
            for (uint8_t i = 0; i < 45 && poll(); i++) c4Draw(sel, msg, score);
            if (!twoPlayers && turn == 2) { again = gameOver(score); break; }
            memset(c4, 0, sizeof(c4));
            turn = 1;
          } else if (c4Full()) {
            for (uint8_t i = 0; i < 30 && poll(); i++) c4Draw(sel, "DRAW", score);
            memset(c4, 0, sizeof(c4));
            turn = 1;
          } else if (twoPlayers) {
            turn = 3 - turn;                   // hand the device over
          } else {
            for (uint8_t i = 0; i < 8 && poll(); i++) c4Draw(sel, "thinking...", score);
            uint8_t c = c4Think(mode);
            c4Drop(c, 2);
            sfx(350, 40);
            if (c4Wins(2)) { again = gameOver(score); break; }
            if (c4Full()) { memset(c4, 0, sizeof(c4)); }
          }
        }
      }
      c4Draw(sel, twoPlayers ? (turn == 1 ? "player 1" : "player 2") : "OK=drop", score);
    }
  }
}

// =========================================================
//  GAME LIBRARY
// =========================================================
typedef void (*GameFn)();
struct Game { const char *name; GameFn run; };

static const Game GAMES[] = {
  { "Tetris",     tetrisRun },
  { "Snake",      snakeRun  },
  { "Pong",       pongRun   },
  { "Doom",       doomRun   },
  { "Mine",       mineRun   },
  { "Tunnel 3D",  tunnelRun },
  { "Flappy",     flappyRun },
  { "Invaders",   invadersRun },
  { "Dino",       dinoRun     },
  { "Breakout",   breakoutRun },
  { "Rocks",      asteroidsRun },
  { "Racer",      racerRun    },
  { "Frogger",    froggerRun  },
  { "4 wins",     connect4Run },
  // add new games here:  { "Breakout", breakoutRun },
  { "Settings",   settingsRun }      // must stay last: no high score
};
static const uint8_t GAME_COUNT = sizeof(GAMES) / sizeof(GAMES[0]);
#define REAL_GAMES (GAME_COUNT - 1)
#define ROWS 4                       // visible menu rows

void menu() {
  static uint8_t sel = 0, top = 0;
#ifdef HAVE_NET
  static bool unconfirmed = fwPending();      // fresh update: kept after a key press
#endif
  btnClear();
  while (true) {
    poll();                                   // long press does nothing here
#ifdef HAVE_NET
    if (unconfirmed)
      for (uint8_t i = 0; i < B_COUNT; i++)
        if (bDown[i]) { fwConfirm(); unconfirmed = false; break; }
#endif
    if (btn(B_UP)   && sel > 0)               { sel--; sfx(700, 15); }
    if (btn(B_DOWN) && sel < GAME_COUNT - 1)  { sel++; sfx(700, 15); }
    if (sel < top) top = sel;
    if (sel >= top + ROWS) top = sel - ROWS + 1;
    if (btn(B_OK)) {
      sfx(1200, 50);
      curGame = sel;
      curHigh = (sel < REAL_GAMES) ? loadHigh(sel) : 0;
      GAMES[sel].run();
      btnClear();
      continue;
    }

    oled.setFont(FONT_B);
    oled.drawStr(2, 12, "MiniArcade");
    oled.setFont(FONT);
    {
      char b[8];
      if (batPin > 4) snprintf(b, sizeof(b), "USB");
      else               snprintf(b, sizeof(b), "%u%%", batPercent());
      rightStr(12, b);
    }
    oled.drawHLine(0, TOP_H - 1, SCR_W);
    for (uint8_t i = 0; i < ROWS && top + i < GAME_COUNT; i++) {
      uint8_t idx = top + i, y = TOP_H + 2 + i * 11;
      if (idx == sel) { oled.drawBox(0, y, SCR_W, 10); oled.setDrawColor(0); }
      oled.drawStr(4, y + 8, GAMES[idx].name);
      if (idx < REAL_GAMES) {
        char b[16];
        snprintf(b, sizeof(b), "BEST %u", loadHigh(idx));
        oled.drawStr(SCR_W - oled.getStrWidth(b) - 3, y + 8, b);
      } else {
        oled.drawStr(SCR_W - oled.getStrWidth("setup") - 3, y + 8, "setup");
      }

      oled.setDrawColor(1);
    }
    oled.sendBuffer();
  }
}

void setup() {
  prefs.begin("arcade", true);                 // clock first: peripherals
  runClock = (uint8_t)prefs.getUShort("clk", 160);   // derive their timing from it
  prefs.end();
  setCpuFrequencyMhz(runClock);

  Wire.begin(PIN_SDA, PIN_SCL);
  oled.begin();
  oled.setFontMode(1);
  randomSeed(esp_random());

  oled.clearBuffer();
  oled.setFont(FONT_B);
  oled.drawStr(2, 12, "MiniArcade");
  oled.drawHLine(0, TOP_H - 1, SCR_W);
  oled.setFont(FONT);
  centerStr(40, "Tetris Snake Pong Doom");
  oled.sendBuffer();
  delay(1000);

  bool known = loadPins();
  bool held  = false;                    // button held during reset = re-learn
  if (known) {
    applyPinModes();
    for (uint8_t i = 0; i < B_COUNT; i++) if (rawPressed(i)) held = true;
  }
  if (!known || held) learnKeys();
#ifdef HAVE_NET
  versionRun(true);                      // choose the firmware when two are stored
#endif

  prefs.begin("arcade", true);
  bool sndKnown = prefs.getUShort("sset", 0) == 1;
  prefs.end();
  if (!sndKnown) {
    soundSetup();
    prefs.begin("arcade", false);
    prefs.putUShort("sset", 1);
    prefs.end();
  }
  applyCfg();
  batDetect();
  if (batPin == 254) { batPin = 255; saveBat(); }
  lastInput = millis();
  sfx(900, 80);                          // hello
}

void loop() {
  menu();          // never returns
}
