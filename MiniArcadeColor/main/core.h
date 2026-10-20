// MiniArcade Color: keys, sound, battery, saved settings and the screens
// every game shares (status bar, pause menu, mode choice, game over).
// Part of console.cpp, which includes it - not compiled on its own.

// =========================================================
//  KEYS
// =========================================================
/* Four keys: UP, DOWN, LEFT, RIGHT. OK is two opposite keys pressed
   together (UP+DOWN or LEFT+RIGHT); a fifth key wired as OK works too.
   So that the first of the two keys does not already move the cursor, a
   direction counts only after CHORD_MS - or at once when it is released
   earlier. Holding OK (both keys) for HOLD_BACK_MS leaves a game.       */
enum { B_UP, B_DOWN, B_LEFT, B_RIGHT, B_OK, B_COUNT };
static uint8_t BTN_PIN[B_COUNT] = { 255, 255, 255, 255, 255 };   // learned; 255 = none
static uint8_t BTN_ACT[B_COUNT] = { 0, 0, 0, 0, 0 };             // 0 = to GND, 1 = to 3V3
static const char *BTN_NAME[B_COUNT] = { "UP", "DOWN", "LEFT", "RIGHT", "OK" };

#define CHORD_MS     70    // second key of an OK pair within this time
#define HOLD_BACK_MS 700   // hold OK this long to leave a game
#define REPEAT_DELAY 250   // first auto-repeat after ...
#define REPEAT_RATE   80   // ... then every ...

/* GPIOs the key wizard may use: all general pins of the chip but the
   display, the I2C pins of the gyroscope, flash, PSRAM and USB.        */
#if CONFIG_IDF_TARGET_ESP32S3
static const uint8_t PINS_ALL[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
                                    21, 38, 39, 40, 41, 42, 45, 46, 47, 48 };
#define ADC_FIRST 1        // ADC1: GPIO1..10
#define ADC_LAST  10
#else
static const uint8_t PINS_ALL[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 21 };
#define ADC_FIRST 0        // ADC1: GPIO0..4
#define ADC_LAST  4
#endif
static uint8_t CAND[sizeof(PINS_ALL)];
static uint8_t CAND_N = 0;

static bool pinIs(uint8_t p, int cfg) { return cfg >= 0 && p == cfg; }   // cfg -1 = not wired

static bool pinReserved(uint8_t p) {
  return p == CONFIG_ARCADE_TFT_SCK || p == CONFIG_ARCADE_TFT_MOSI || p == CONFIG_ARCADE_TFT_CS ||
         p == CONFIG_ARCADE_TFT_DC || pinIs(p, CONFIG_ARCADE_TFT_RST) || pinIs(p, CONFIG_ARCADE_TFT_BL) ||
         p == CONFIG_ARCADE_I2C_SDA || p == CONFIG_ARCADE_I2C_SCL;
}

static void candInit() {
  CAND_N = 0;
  for (uint8_t i = 0; i < sizeof(PINS_ALL); i++) if (!pinReserved(PINS_ALL[i])) CAND[CAND_N++] = PINS_ALL[i];
}

static bool     kDown[B_COUNT], bEdge[B_COUNT], bTap[B_COUNT], bIgnore[B_COUNT];
static bool     kSent[4], kChord[4];       // direction given out / part of an OK pair
static uint32_t kTime[B_COUNT], bNext[B_COUNT];
static bool     okDown = false, okLocked = false, wantExit = false;
static uint32_t okTime = 0;

/* A phone can press the keys too (controller page): each press holds the
   key for a moment and the phone repeats it while the finger stays on.  */
static volatile uint32_t vKeyUntil[B_COUNT];

void applyPinModes() {
  for (uint8_t i = 0; i < B_COUNT; i++)
    if (BTN_PIN[i] != 255) pinMode(BTN_PIN[i], BTN_ACT[i] ? INPUT_PULLDOWN : INPUT_PULLUP);
}

bool rawPressed(uint8_t i) {
  uint32_t u = vKeyUntil[i];
  if (u && (int32_t)(u - millis()) > 0) return true;
  if (BTN_PIN[i] == 255) return false;
  return digitalRead(BTN_PIN[i]) == (BTN_ACT[i] ? HIGH : LOW);
}

static void dirEmit(uint8_t i, uint32_t now) {
  kSent[i] = true;
  bEdge[i] = bTap[i] = true;
  bNext[i] = now + REPEAT_DELAY;
}

void btnUpdate() {
  uint32_t now = millis();
  // directions
  for (uint8_t i = 0; i < 4; i++) {
    bool d = rawPressed(i);
    if (bIgnore[i]) { if (!d) bIgnore[i] = false; continue; }
    if (d && !kDown[i]) { kDown[i] = true; kTime[i] = now; kSent[i] = kChord[i] = false; }
    else if (!d && kDown[i]) {
      kDown[i] = false;
      if (!kSent[i] && !kChord[i]) dirEmit(i, now);          // a quick tap
      kChord[i] = false;
    }
  }
  // OK from a pair: the second key within CHORD_MS of the first, neither given out yet
  static const uint8_t PAIR[2][2] = { { B_UP, B_DOWN }, { B_LEFT, B_RIGHT } };
  for (uint8_t p = 0; p < 2; p++) {
    uint8_t a = PAIR[p][0], b = PAIR[p][1];
    if (kDown[a] && kDown[b] && !kChord[a] && !kChord[b] && !kSent[a] && !kSent[b] &&
        (kTime[a] > kTime[b] ? kTime[a] - kTime[b] : kTime[b] - kTime[a]) <= CHORD_MS)
      kChord[a] = kChord[b] = true;
  }
  for (uint8_t i = 0; i < 4; i++) {
    if (!kDown[i] || kChord[i]) continue;
    if (!kSent[i] && now - kTime[i] >= CHORD_MS) dirEmit(i, now);
    else if (kSent[i] && now >= bNext[i]) { bNext[i] = now + REPEAT_RATE; bEdge[i] = true; }   // auto repeat
  }
  // OK: the pair, or an own key
  bool ok = (kChord[B_UP] && (kDown[B_UP] || kDown[B_DOWN])) || (kChord[B_LEFT] && (kDown[B_LEFT] || kDown[B_RIGHT]));
  bool okRaw = rawPressed(B_OK);
  if (bIgnore[B_OK]) { if (!okRaw) bIgnore[B_OK] = false; okRaw = false; }
  ok = ok || okRaw;
  if (ok && !okDown) { okDown = true; okTime = now; bTap[B_OK] = true; }
  else if (!ok && okDown) {
    okDown = false;
    if (!okLocked) bEdge[B_OK] = true;                        // short press = select
    okLocked = false;
  }
  if (okDown && !okLocked && now - okTime > HOLD_BACK_MS) { okLocked = true; wantExit = true; }
}

bool btn(uint8_t i)     { if (bEdge[i]) { bEdge[i] = false; return true; } return false; }
// fires the moment a key goes down (OK's normal edge only fires on release,
// so that a long press can mean "back")
bool btnTap(uint8_t i)  { if (bTap[i]) { bTap[i] = false; return true; } return false; }
bool btnHeld(uint8_t i) { return i == B_OK ? okDown : (kDown[i] && !kChord[i]); }

/* Called whenever a screen is entered or left: a key still held is muted
   until it is released - letting go of OK after "hold to leave" would
   otherwise start the game again.                                     */
void btnClear() {
  for (uint8_t i = 0; i < B_COUNT; i++) {
    bEdge[i] = bTap[i] = false;
    bIgnore[i] = rawPressed(i);
    if (i < 4) { kDown[i] = false; kSent[i] = kChord[i] = false; }
  }
  okDown = okLocked = wantExit = false;
}

// =========================================================
//  SETTINGS, SOUND, STORAGE
// =========================================================
static uint8_t  cfgBright = 100;      // backlight in %, only with a backlight pin
static uint8_t  cfgClock  = 160;      // cpu clock in MHz
static uint8_t  cfgSleep  = 5;        // minutes without a key press, 0 = never
static bool     cfgMute   = false;
static uint8_t  cfgDisp   = 0;        // 1 = upside down, 2 = red/blue swapped, 4 = inverted
static uint32_t lastInput = 0;        // for the sleep timer
static uint8_t  runClock  = 160;      // the clock this start runs at

Preferences prefs;

void saveCfg() {
  prefs.begin("arcade", false);
  prefs.putUShort("bri", cfgBright);
  prefs.putUShort("clk", cfgClock);
  prefs.putUShort("slp", cfgSleep);
  prefs.putUShort("mut", cfgMute);
  prefs.putUShort("dsp", cfgDisp);
  prefs.end();
}

void applyDisplay() {
  gfx.setOrientation(cfgDisp & 1, !(cfgDisp & 2), cfgDisp & 4);
  gfx.setBacklight(cfgBright);
}

static uint8_t  sndPin = 255;          // 255 = no buzzer
static uint32_t sndUntil = 0;
static bool     sndOn = false;

void sfx(uint16_t freq, uint16_t ms) {
  if (sndPin == 255 || !freq || cfgMute) return;
  tone(sndPin, freq);
  sndOn = true;
  sndUntil = millis() + ms;
}
void sfxUpdate() { if (sndOn && millis() >= sndUntil) { noTone(sndPin); sndOn = false; } }

static uint8_t curGame = 0;            // index of the running game
static uint16_t curHigh = 0;           // its high score
struct Game;
const Game &gameAt(uint8_t i);
const char *gameKey(uint8_t i);

uint16_t loadHigh(uint8_t idx) {
  char k[12];
  snprintf(k, sizeof(k), "h%s", gameKey(idx));
  prefs.begin("arcade", true);
  uint16_t v = prefs.getUShort(k, 0);
  prefs.end();
  return v;
}
void saveHigh(uint8_t idx, uint16_t v) {
  char k[12];
  snprintf(k, sizeof(k), "h%s", gameKey(idx));
  prefs.begin("arcade", false);
  prefs.putUShort(k, v);
  prefs.end();
}

bool loadPins() {
  prefs.begin("arcade", true);
  sndPin    = (uint8_t)prefs.getUShort("snd", 255);
  cfgBright = (uint8_t)prefs.getUShort("bri", 100);
  cfgClock  = (uint8_t)prefs.getUShort("clk", 160);
  cfgSleep  = (uint8_t)prefs.getUShort("slp", 5);
  cfgMute   = prefs.getUShort("mut", 0) != 0;
  cfgDisp   = (uint8_t)prefs.getUShort("dsp", 0);
  bool ok = prefs.getUShort("pset", 0) == 3;      // version 3 = four keys + optional OK
  if (ok)
    for (uint8_t i = 0; i < B_COUNT; i++) {
      char k[4];
      snprintf(k, sizeof(k), "p%u", i); BTN_PIN[i] = (uint8_t)prefs.getUShort(k, 255);
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
  prefs.putUShort("pset", 3);
  prefs.end();
}
void saveSound() { prefs.begin("arcade", false); prefs.putUShort("snd", sndPin); prefs.end(); }

// =========================================================
//  BATTERY   (two equal resistors from the cell to an ADC pin)
// =========================================================
static uint8_t batPin = 254;   // ADC pin; 255 = none found (searched at every start),
                               // 254 = never searched, 253 = "no battery" chosen by hand
#define BAT_NONE_SET 253
static bool adcPin(int p) { return p >= ADC_FIRST && p <= ADC_LAST; }   // int: ADC_FIRST is 0 on the C3
static bool batValid() { return adcPin(batPin); }
void saveBat() { prefs.begin("arcade", false); prefs.putUShort("bat", batPin); prefs.end(); }

static uint16_t batMilliVolts() {              // the lowest of four quick pairs, x2 for the divider
  if (!batValid()) return 0;
  uint16_t low = 0xFFFF;
  for (uint8_t i = 0; i < 4; i++) {
    uint16_t v = (uint16_t)((analogReadMilliVolts(batPin) + analogReadMilliVolts(batPin)) / 2);
    if (v < low) low = v;
  }
  return (uint16_t)(low * 2);
}

#include "batlearn.h"                          // the curve, and how it learns the own cell (shared)

/* The percentage follows the low points under load: a dip pulls it down
   within a second, it climbs back over half a minute. Learning the own
   cell works as on the OLED console (batlearn.h).                     */
static uint32_t batLow = 0, batAt = 0;
static uint8_t  batShown = 255;
static BatLearn batL;
static bool     batLLoaded = false, batLStarted = false;
static uint16_t batLMv30 = 0, batLPrev = 0;
static int16_t  batLRise = 0;
static uint32_t batLNext = 0;
static uint8_t  batLDirty = 0;
static void (*pollHook)() = NULL;              // extra work on every frame (radio)
bool batRadioOn() { return netState() != NET_OFF || pollHook != NULL; }

static void batLearnLoad() {
  if (batLLoaded) return;
  batLLoaded = true;
  prefs.begin("arcade", true);
  bool ok = prefs.getBytes("bcal", &batL, sizeof(batL)) == sizeof(batL) && batL.ver == BL_VER;
  prefs.end();
  if (!ok) batL.clear();
}
static void batLearnSave() { prefs.begin("arcade", false); prefs.putBytes("bcal", &batL, sizeof(batL)); prefs.end(); batLDirty = 0; }
void batLearnForget() { batL.clear(); batLearnSave(); batShown = 255; }

static void batLearnTick(uint32_t now) {
  uint16_t mv = (uint16_t)(batLow / 16);
  if (!batLStarted) {
    if (now >= 30000 && !batLMv30) batLMv30 = mv;
    if (now < 60000 || !batLMv30) return;
    batLearnLoad();
    batLStarted = true;
    batLRise = (int16_t)(mv - batLMv30);
    if (batLRise >= 8) batL.minute(mv, false, true); else batL.start(mv);
    batLPrev = mv;
    batLNext = now + 60000;
    batLearnSave();
    return;
  }
  if ((int32_t)(now - batLNext) < 0) return;
  batLNext += 60000;
  if ((int32_t)(now - batLNext) > 60000) batLNext = now + 60000;
  int16_t rise = (int16_t)(mv - batLPrev);
  bool charging = rise >= 8 && batLRise >= 8;
  batLRise = rise;
  batLPrev = mv;
  if (batL.minute(mv, batRadioOn(), charging) || ++batLDirty >= 5 || mv < batL.emptyMv() + 250) batLearnSave();
}
void batLearnStop() { if (batLStarted) { batL.stop(); batLearnSave(); } }

void batTick() {
  if (!batValid()) return;
  uint32_t now = millis();
  if (batLow && now - batAt < 250) return;
  batAt = now;
  int32_t mv16 = (int32_t)batMilliVolts() * 16;
  if (!batLow) { batLow = (uint32_t)mv16; return; }
  int32_t d = mv16 - (int32_t)batLow;
  batLow = (uint32_t)((int32_t)batLow + (d < 0 ? d / 2 : d / 128));
  batLearnTick(now);
}
uint16_t batSmoothMv() { batTick(); return (uint16_t)(batLow / 16); }
uint8_t batPercent() {
  batLearnLoad();
  uint8_t p = batL.percent(batSmoothMv());
  if (batShown == 255 || p == 0 || p == 100 || (p > batShown ? p - batShown : batShown - p) >= 3) batShown = p;
  return batShown;
}
void batReset() { batLow = 0; batShown = 255; }

// battery symbol: x, y = top left, 22 x 11
void batIcon(int16_t x, int16_t y) {
  if (!batValid()) { gfx.text(x - 4, y + 1, "USB", C::LTGREY, F_SMALL); return; }
  uint8_t p = batPercent();
  col c = p > 50 ? C::GREEN : (p > 20 ? C::YELLOW : C::RED);
  gfx.frame(x, y, 19, 11, C::WHITE);
  gfx.fill(x + 19, y + 3, 2, 5, C::WHITE);
  gfx.fill(x + 2, y + 2, (int16_t)(15 * p / 100), 7, c);
}

// =========================================================
//  FRAME LOOP, SLEEP, PAUSE
// =========================================================
/* Every game is written as:  while (poll()) { ...draw...; gfx.send(); }
   poll() feeds the keys, clears the picture and returns false as soon as
   the player holds OK to get back to the library. It also keeps the frame
   rate at about 60 per second at most.                                  */
static bool     pauseOk = false;           // a game is running (set by the library)
static uint8_t  pauseBlock = 0;            // >0 on screens inside a game (game over, ...)
static bool     wantRestart = false;
static uint32_t pausedMs = 0;
static bool     fwUnconfirmed = false;     // a fresh update waits for a key press
uint32_t gameMillis() { return millis() - pausedMs; }
struct NoPause { NoPause() { pauseBlock++; } ~NoPause() { pauseBlock--; } };

static bool pauseMenu();

void sleepCheck() {
  if (!cfgSleep || fwUnconfirmed) return;
  for (uint8_t i = 0; i < B_COUNT; i++) if (rawPressed(i)) { lastInput = millis(); return; }
  if (millis() - lastInput < (uint32_t)cfgSleep * 60000UL) return;
  noTone(sndPin);
  batLearnStop();
  gfx.clear(C::NAVY);
  gfx.center(100, "SLEEPING", C::CYAN, F_BOLD);
  gfx.center(140, "press a key to wake up", C::LTGREY);
  gfx.send();
  delay(1200);
  gfx.sleep(true);
  uint64_t mask = 0;                                   // all keys wired the same way as UP
  for (uint8_t i = 0; i < B_COUNT; i++)
    if (BTN_PIN[i] != 255 && BTN_ACT[i] == BTN_ACT[B_UP]) mask |= 1ULL << BTN_PIN[i];
  deepSleepUntilKeys(mask, BTN_ACT[B_UP]);
  // these pins cannot wake the chip: dark screen until a key is pressed
  while (true) {
    delay(50);
    bool any = false;
    for (uint8_t i = 0; i < B_COUNT; i++) if (rawPressed(i)) any = true;
    if (any) break;
  }
  gfx.sleep(false);
  lastInput = millis();
  btnClear();
}

bool poll(bool clear = true) {          // clear = false keeps the picture (dialogs over a game)
  static uint32_t lastFrame = 0;
  uint32_t now = millis();
  if (now - lastFrame < 16) delay(16 - (now - lastFrame));
  else delay(1);
  lastFrame = millis();
  btnUpdate();
  sfxUpdate();
  batTick();
  if (pollHook) pollHook();
  sleepCheck();
  if (wantExit) {
    wantExit = false;
    if (!pauseOk || pauseBlock || pauseMenu()) return false;
  }
  if (clear) gfx.clear();
  return true;
}

// ---------------- shared drawing ----------------
#define TOP_H 26                       // status bar on top of the games

void statusBar(const char *title, uint16_t score, uint16_t best, col accent = C::BLUE) {
  gfx.gradient(0, 0, SCR_W, TOP_H, colLight(accent), colDark(accent, 2));
  gfx.hline(0, TOP_H - 1, SCR_W, colLight(accent, 2));
  gfx.textShadow(6, 5, title, C::WHITE, C::BLACK, F_TEXT);
  char b[24];
  snprintf(b, sizeof(b), "%u", score);
  int16_t x = gfx.text(150, 5, b, C::YELLOW, F_TEXT);
  (void)x;
  snprintf(b, sizeof(b), "BEST %u", best);
  gfx.right(SCR_W - 34, 9, b, C::LTGREY, F_SMALL);
  batIcon(SCR_W - 28, 7);
}

// a box with a title, used by dialogs
void panel(int16_t x, int16_t y, int16_t w, int16_t h, const char *title, col accent) {
  gfx.rbox(x + 3, y + 3, w, h, 8, C::BLACK);
  gfx.rbox(x, y, w, h, 8, colDark(accent, 3));
  gfx.rframe(x, y, w, h, 8, accent);
  if (title) {
    gfx.rbox(x, y, w, 30, 8, accent);
    gfx.fill(x, y + 22, w, 8, accent);
    gfx.text(x + (w - Gfx::width(title, F_BOLD)) / 2, y + 3, title, C::WHITE, F_BOLD);
  }
}

// a list of choices; returns the index or 255 when left with a long OK
uint8_t chooseMode(const char *title, const char *const *opts, uint8_t n, col accent = C::PURPLE) {
  NoPause np;
  uint8_t sel = 0;
  btnClear();
  while (poll()) {
    if (btn(B_UP))   { sel = sel ? sel - 1 : n - 1; sfx(700, 15); }
    if (btn(B_DOWN)) { sel = (sel + 1) % n;          sfx(700, 15); }
    if (btn(B_OK))   { sfx(1200, 60); return sel; }
    gfx.gradient(0, 0, SCR_W, SCR_H, colDark(accent, 3), C::BLACK);
    gfx.center(14, title, C::WHITE, F_BOLD);
    gfx.hline(40, 44, SCR_W - 80, accent);
    uint8_t vis = n < 5 ? n : 5;
    uint8_t top = sel >= vis ? sel - vis + 1 : 0;
    for (uint8_t i = 0; i < vis; i++) {
      uint8_t k = top + i;
      int16_t y = 56 + i * 34;
      bool on = k == sel;
      gfx.rbox(40, y, SCR_W - 80, 28, 6, on ? accent : colDark(accent, 2));
      if (on) gfx.rframe(40, y, SCR_W - 80, 28, 6, C::WHITE);
      gfx.center(y + 5, opts[k], on ? C::WHITE : C::LTGREY);
    }
    gfx.center(SCR_H - 16, "UP/DOWN choose   UP+DOWN = OK   hold = back", C::GREY, F_SMALL);
    gfx.send();
  }
  return 255;
}

static bool pauseMenu() {
  static const char *const OPT[3] = { "continue", "restart", "quit to menu" };
  uint32_t t0 = millis();
  uint8_t sel = 0;
  NoPause np;
  noTone(sndPin);
  gfx.dim(0, 0, SCR_W, SCR_H);                          // the game stays visible behind the menu
  btnClear();
  bool quit = false;
  while (true) {
    delay(16);
    btnUpdate();
    sfxUpdate();
    if (pollHook) pollHook();
    if (wantExit) { wantExit = false; quit = true; break; }  // hold again = quit
    if (btn(B_UP))   { sel = sel ? sel - 1 : 2; sfx(700, 15); }
    if (btn(B_DOWN)) { sel = (sel + 1) % 3;     sfx(700, 15); }
    if (btn(B_OK))   {
      sfx(1200, 50);
      if (sel == 1) { wantRestart = true; quit = true; }
      if (sel == 2) quit = true;
      break;
    }
    panel(80, 50, 160, 140, "PAUSE", C::BLUE);
    for (uint8_t i = 0; i < 3; i++) {
      int16_t y = 92 + i * 30;
      if (i == sel) gfx.rbox(92, y - 4, 136, 26, 5, C::CYAN);
      gfx.center(y, OPT[i], i == sel ? C::BLACK : C::WHITE);
    }
    gfx.send();
  }
  pausedMs += millis() - t0;
  btnClear();
  return quit;
}

// ---------------- game over ----------------
// true -> play another round, false -> back to the library
bool gameOver(uint16_t score) {
  NoPause np;
  bool record = score > curHigh;
  if (record) { curHigh = score; saveHigh(curGame, curHigh); }
  sfx(record ? 1400 : 180, 350);
  gfx.dim(0, 0, SCR_W, SCR_H);                         // the last picture of the game stays behind
  btnClear();
  uint32_t t0 = millis();
  while (poll(false)) {
    if (btn(B_OK)) return true;
    col accent = record ? C::ORANGE : C::RED;
    panel(50, 40, 220, 160, NULL, accent);
    bool blink = record && ((millis() - t0) / 300) % 2;
    gfx.center(54, record ? "NEW RECORD!" : "GAME OVER", blink ? C::YELLOW : C::WHITE, F_BOLD);
    char b[24];
    snprintf(b, sizeof(b), "%u", score);
    gfx.center(92, b, C::YELLOW, F_BIG);
    snprintf(b, sizeof(b), "best %u", curHigh);
    gfx.center(140, b, C::LTGREY);
    gfx.center(172, "OK again - hold OK menu", C::GREY, F_SMALL);
    gfx.send();
  }
  return false;
}
