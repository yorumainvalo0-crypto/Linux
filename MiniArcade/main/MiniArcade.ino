/* ------------------------------------------------------------------
   MiniArcade - tiny game launcher for ESP32-C3 + SSD1306 128x64 (I2C)
   15 games, one file each (game_*.h). High scores, stats and awards are
   kept in flash.
   ------------------------------------------------------------------
   Display: laid out for the common two colour panels where the top
   16 pixel rows are yellow and the rest is blue. The yellow band is
   used as a status bar, all gameplay happens in the blue area.

   Buttons: 5 push buttons on any free GPIO. They may be wired to GND
   or to 3V3 - the setup wizard detects the pin AND the polarity, so
   no soldering has to be redone. No external resistors needed.

   Controls: UP/DOWN/LEFT/RIGHT + OK
             OK short = select / action
             OK held ~0.7 s = pause menu in a game, back elsewhere
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
static bool     fwUnconfirmed = false; // fresh update, no key pressed yet

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
  /* Waking up is a restart, and an update that was never confirmed would be
     thrown away by that - so a fresh update stays awake until a key press. */
  if (!cfgSleep || fwUnconfirmed) return;
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
static void (*pollHook)() = NULL;          // extra work on every frame (multiplayer radio)

/* Pause: inside a game a long OK opens a small menu instead of leaving at
   once. The games time everything with gameMillis(), a clock that stands
   still while the menu is open - so nothing jumps ahead on "continue".  */
static bool     pauseOk = false;           // a game is running (set by the library)
static uint8_t  pauseBlock = 0;            // >0 on screens inside a game (game over, ...)
static bool     wantRestart = false;       // "restart" chosen: the library starts the game again
static uint32_t pausedMs = 0;              // time spent in the pause menu so far

uint32_t gameMillis() { return millis() - pausedMs; }

static bool pauseMenu();

bool poll() {
  delay(5);
  btnUpdate();
  sfxUpdate();
  if (pollHook) pollHook();
  sleepCheck();
  if (wantExit) {
    wantExit = false;
    if (!pauseOk || pauseBlock || pauseMenu()) return false;
  }
  oled.clearBuffer();
  oled.setFont(FONT);
  return true;
}

void centerStr(uint8_t y, const char *s) {
  oled.drawStr((SCR_W - oled.getStrWidth(s)) / 2, y, s);
}

/* Drawn over the frozen game picture. Returns true when the game should
   end (quit or restart), false to go on playing.                        */
static bool pauseMenu() {
  static const char *const OPT[3] = { "continue", "restart", "quit to menu" };
  uint32_t t0 = millis();
  uint8_t sel = 0;
  bool end = false;
  sfx(600, 40);
  btnClear();                                // the long OK is still held
  for (;;) {
    delay(5);
    btnUpdate();
    sfxUpdate();
    sleepCheck();
    if (wantExit) { wantExit = false; end = true; break; }   // hold OK again = quit
    if (btn(B_UP)   && sel)     { sel--; sfx(700, 15); }
    if (btn(B_DOWN) && sel < 2) { sel++; sfx(700, 15); }
    if (btn(B_OK)) {
      sfx(1200, 50);
      if (sel == 1) wantRestart = true;
      end = sel != 0;
      break;
    }
    oled.setDrawColor(0);
    oled.drawBox(24, 13, 80, 47);
    oled.setDrawColor(1);
    oled.drawFrame(24, 13, 80, 47);
    oled.setFont(FONT_B);
    centerStr(26, "PAUSE");
    oled.setFont(FONT);
    for (uint8_t i = 0; i < 3; i++) {
      uint8_t y = 29 + i * 10;
      if (i == sel) { oled.drawBox(26, y, 76, 9); oled.setDrawColor(0); }
      centerStr(y + 7, OPT[i]);
      oled.setDrawColor(1);
    }
    oled.sendBuffer();
  }
  pausedMs += millis() - t0;
  btnClear();
  return end;
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
  uint8_t end = x + oled.getStrWidth(b) + 4;             // first free column
  snprintf(b, sizeof(b), "BEST %u", best);
  if (SCR_W - 2 - oled.getStrWidth(b) < end) snprintf(b, sizeof(b), "HI %u", best);
  if (SCR_W - 2 - oled.getStrWidth(b) >= end) rightStr(12, b);   // long title: drop it
  oled.drawHLine(0, TOP_H - 1, SCR_W);
}

// ---------------- choice screen ----------------
/* Small vertical menu used for difficulty and player count.
   Returns the chosen index, or 255 when the player leaves with a long OK. */
struct NoPause { NoPause() { pauseBlock++; } ~NoPause() { pauseBlock--; } };   // hold OK leaves at once

uint8_t chooseMode(const char *title, const char *const *opts, uint8_t n) {
  NoPause np;
  uint8_t sel = 0, top = 0;
  btnClear();
  while (poll()) {
    if (btn(B_UP)   && sel)         { sel--; sfx(700, 15); }
    if (btn(B_DOWN) && sel < n - 1) { sel++; sfx(700, 15); }
    if (btn(B_OK)) { sfx(1200, 60); return sel; }
    if (sel < top) top = sel;                         // 4 lines fit, longer lists scroll
    if (sel >= top + 4) top = sel - 3;
    oled.setFont(FONT_B);
    oled.drawStr(2, 12, title);
    oled.drawHLine(0, TOP_H - 1, SCR_W);
    oled.setFont(FONT);
    for (uint8_t i = top; i < n && i < top + 4; i++) {
      uint8_t y = TOP_H + 3 + (i - top) * 11;
      if (i == sel) { oled.drawBox(0, y, SCR_W, 10); oled.setDrawColor(0); }
      oled.drawStr(6, y + 8, opts[i]);
      oled.setDrawColor(1);
    }
    oled.sendBuffer();
  }
  return 255;
}

// ---------------- game over screen ----------------
void awardCheck(bool record);                      // stats.h

// true -> play another round, false -> back to the library
bool gameOver(uint16_t score) {
  NoPause np;
  bool record = score > curHigh;
  if (record) { curHigh = score; saveHigh(curGame, curHigh); }
  awardCheck(record);
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

#include "setup_wizard.h"
#include "wlan.h"
#include "settings.h"
#include "game_tetris.h"
#include "game_snake.h"
#include "game_pong.h"
#include "game_doom.h"
#include "game_tunnel.h"
#include "game_flappy.h"
#include "game_invaders.h"
#include "game_mine.h"
#include "game_dino.h"
#include "game_breakout.h"
#include "game_rocks.h"
#include "game_racer.h"
#include "game_frogger.h"
#include "game_connect4.h"
#include "game_tictactoe.h"
#include "game_2048.h"
#include "game_mines.h"
#include "game_pacman.h"
#include "stats.h"
#include "multiplayer.h"

// =========================================================
//  GAME LIBRARY
// =========================================================
typedef void (*GameFn)();
struct Game { const char *name; GameFn run; const char *tag = NULL; };   // tag: no high score

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
  { "Tic Tac Toe", tictactoeRun },
  { "2048",       g2048Run    },
  { "Minesweeper", minesRun   },
  { "Pac-Man",    pacmanRun   },
  // add new games here (before the entries with a tag), e.g. { "Chess", chessRun },
#ifdef HAVE_LINK
  { "Multiplayer", multiplayerRun, "2P" },
#endif
  { "Stats",      statsRun,    "info" },
  { "Settings",   settingsRun, "setup" }
};
static const uint8_t GAME_COUNT = sizeof(GAMES) / sizeof(GAMES[0]);
static uint8_t countReal() { uint8_t n = 0; while (!GAMES[n].tag) n++; return n; }
static const uint8_t REAL_GAMES = countReal();     // the ones with a high score

const char *gameName(uint8_t i) { return GAMES[i].name; }
uint8_t     realGames()         { return REAL_GAMES; }

#define ROWS 4                       // visible menu rows

void menu() {
  static uint8_t sel = 0, top = 0;
  btnClear();
  while (true) {
    poll();                                   // long press does nothing here
#ifdef HAVE_NET
    if (fwUnconfirmed)                        // fresh update: kept after a key press
      for (uint8_t i = 0; i < B_COUNT; i++)
        if (bDown[i]) { fwConfirm(); fwUnconfirmed = false; break; }
#endif
    if (btn(B_UP))   { sel = sel ? sel - 1 : GAME_COUNT - 1; sfx(700, 15); }   // the list wraps:
    if (btn(B_DOWN)) { sel = (sel + 1) % GAME_COUNT;         sfx(700, 15); }   // UP from the top = Settings
    if (sel < top) top = sel;
    if (sel >= top + ROWS) top = sel - ROWS + 1;
    if (btn(B_OK)) {
      sfx(1200, 50);
      curGame = sel;
      bool real = sel < REAL_GAMES;
      do {                                    // "restart" in the pause menu comes back here
        wantRestart = false;
        curHigh = real ? loadHigh(sel) : 0;
        if (real) statGameStart(sel);
        pauseOk = real;
        GAMES[sel].run();
        pauseOk = false;
        if (real) statGameEnd(sel);
      } while (wantRestart);
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
        oled.drawStr(SCR_W - oled.getStrWidth(GAMES[idx].tag) - 3, y + 8, GAMES[idx].tag);
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
  fwUnconfirmed = fwPending();
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
