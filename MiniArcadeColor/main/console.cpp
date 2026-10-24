/* ------------------------------------------------------------------
   MiniArcade Color - the colour console: ESP32-C3 SuperMini or
   ESP32-S3 (WROOM-1) with a 2.4" 320 x 240 SPI display (ILI9341).
   ------------------------------------------------------------------
   Four keys UP / DOWN / LEFT / RIGHT on any free GPIO (to GND or to 3V3,
   the wizard finds out). OK = two opposite keys at once (UP+DOWN or
   LEFT+RIGHT), or a fifth key if one is wired. Holding OK = pause in a
   game, back elsewhere.

   Display: SCK, SDI(MOSI), CS, DC on the pins set in menuconfig
   ("MiniArcade Color" -> Display); RESET and LED to 3V3, VCC 3V3, GND.
   Updates come from an own release channel - the OLED console never
   takes this firmware and this one never the OLED firmware.
   ------------------------------------------------------------------ */
#include "Arduino.h"
#include "Preferences.h"
#include "sdkconfig.h"
#include "gfx.h"
#include "gyro.h"
#include "net.h"
#include "link.h"
#include "esp_random.h"

#include "core.h"
#include "wizard.h"
#include "tilt.h"
#include "sd.h"
#include "sdlevels.h"                               // extra levels from the SD card (Sokoban, Pumpkin Maze, Block Puzzle)
#include "tvnet.h"                                   // the TV server client (also the RAM readout on the WLAN page)
#include "wlan.h"
#include "updmode.h"
#include "settings.h"
#include "fx.h"
#include "game_snake.h"
#include "game_tetris.h"
#include "game_pong.h"
#include "game_flappy.h"
#include "game_breakout.h"
#include "game_invaders.h"
#include "game_rocks.h"
#include "game_shooter.h"
#include "game_cave.h"
#include "game_lander.h"
#include "game_doom.h"
#include "game_mine.h"
#include "game_mine3d.h"
#include "game_tunnel.h"
#include "game_pacman.h"
#include "game_bomber.h"
#include "game_2048.h"
#include "game_blocks.h"
#include "game_mines.h"
#include "game_sokoban.h"
#include "game_sudoku.h"
#include "game_lightsout.h"
#include "game_match3.h"
#include "game_fruit.h"
#include "game_dino.h"
#include "game_racer.h"
#include "game_frogger.h"
#include "game_jump.h"
#include "game_stack.h"
#include "game_connect4.h"
#include "game_tictactoe.h"
#include "game_battleship.h"
#include "game_checkers.h"
#include "game_maumau.h"
#include "game_skat.h"
#include "game_minigolf.h"
#include "game_palace.h"
#include "game_chess.h"
#include "qr.h"
#include "game_pumpkin.h"
#include "game_kart.h"

// =========================================================
//  GAME LIBRARY
// =========================================================
typedef void (*GameFn)();
struct Game {
  const char *name;
  const char *key;          // short and fixed: names the high score in flash
  GameFn run;
  const char *const *icon;  // 12 x 12 sprite, drawn with ICON_PAL
  col accent;
  const char *tag;          // NULL = a game with a high score
};

// icon colours: k black, w white, r red, o orange, y yellow, g green, d dark green,
// c cyan, b blue, p purple, m pink, s grey, n brown
static const char ICON_KEYS[] = "kwroygdcbpmsn";
static const col  ICON_PAL[]  = { C::BLACK, C::WHITE, C::RED, C::ORANGE, C::YELLOW, C::GREEN, C::DKGRN,
                                  C::CYAN, C::BLUE, C::PURPLE, C::PINK, C::GREY, C::BROWN };

static const char *const IC_SNAKE[12] = {
  "............", "..ggggg.....", ".gwkgggg....", ".gggggggg...", "......ggg...", "...gggggg...",
  "..ggggggg...", "..ggg.......", "..gggggggg..", "...ggggggg.r", "..........rr", "..........r." };
static const char *const IC_TETRIS[12] = {
  "............", ".cccc.......", ".cccc.......", ".......yy...", ".......yy...", ".......yy...",
  "..pp...yy...", "..pp........", ".pppp..rrrr.", ".pppp..rrrr.", "ggggbbbbbboo", "ggggbbbbbboo" };
static const char *const IC_PONG[12] = {
  "............", "w...........", "w.......s...", "w...........", "w.....s.....", "....w.....ww",
  "..........ww", "..........ww", "..........ww", "............", "............", "............" };
static const char *const IC_FLAPPY[12] = {
  "........gg..", "........gg..", "........gg..", "...yyy..dd..", "..yyywk.....", ".oyyyyy.....",
  "..yyyrrr....", "...yyy......", "........dd..", "........gg..", "........gg..", "nnnnnnnnnnnn" };
static const char *const IC_BREAKOUT[12] = {
  "rr.rr.rr.rr.", "oo.oo.oo.oo.", "yy.yy.yy.yy.", "gg.gg.....g.", "............", "......w.....",
  "............", "............", "............", "....cccc....", "....cccc....", "............" };
static const char *const IC_SETTINGS[12] = {
  ".....ss.....", "..s.ssss.s..", ".ssssssssss.", "..sss..sss..", ".sss....sss.", "ssss....ssss",
  "ssss....ssss", ".sss....sss.", "..sss..sss..", ".ssssssssss.", "..s.ssss.s..", ".....ss....." };

#include "multiplayer.h"           // online play: uses the games above and the icons
#include "tv.h"                    // YouTube through the own server
#include "inet.h"                  // online play over the internet: the TV's server and pairing
#include "kart_online.h"           // Kart GP online: a race of two

static const Game GAMES[] = {
  { "Snake",       "SNK", snakeRun,       IC_SNAKE,       C::GREEN,         NULL },
  { "Tetris",      "TET", tetrisRun,      IC_TETRIS,      C::PURPLE,        NULL },
  { "Pong",        "PNG", pongRun,        IC_PONG,        C::BLUE,          NULL },
  { "Doom",        "DOM", doomRun,        IC_DOOM,        C::RED,           NULL },
  { "Mine",        "MIN", mineRun,        IC_MINE,        C::GREEN,         NULL },
  { "Mine 3D",     "MN3", mine3dRun,      IC_MINE3D,      rgb(40,170,110),  NULL },
  { "Tunnel 3D",   "TUN", tunnelRun,      IC_TUNNEL,      C::CYAN,          NULL },
  { "Flappy",      "FLP", flappyRun,      IC_FLAPPY,      C::ORANGE,        NULL },
  { "Invaders",    "INV", invadersRun,    IC_INVADERS,    C::GREEN,         NULL },
  { "Dino",        "DNO", dinoRun,        IC_DINO,        C::GREEN,         NULL },
  { "Breakout",    "BRK", breakoutRun,    IC_BREAKOUT,    C::RED,           NULL },
  { "Rocks",       "RCK", asteroidsRun,   IC_ROCKS,       C::BROWN,         NULL },
  { "Racer",       "RCR", racerRun,       IC_RACER,       C::RED,           NULL },
  { "Frogger",     "FRG", froggerRun,     IC_FROGGER,     C::LIME,          NULL },
  { "4 wins",      "C4W", connect4Run,    IC_CONNECT4,    C::BLUE,          NULL },
  { "Tic Tac Toe", "TTT", tictactoeRun,   IC_TICTACTOE,   C::PINK,          NULL },
  { "2048",        "2K4", g2048Run,       IC_2048,        C::ORANGE,        NULL },
  { "Block Puzzle", "BLK", blocksRun,     IC_BLOCKS,      rgb(102,85,204),  NULL },
  { "Minesweeper", "MSW", minesRun,       IC_MINES,       C::BLUE,          NULL },
  { "Pac-Man",     "PAC", pacmanRun,      IC_PACMAN,      C::YELLOW,        NULL },
  { "Shooter",     "SHT", shooterRun,     IC_SHOOTER,     C::CYAN,          NULL },
  { "Jump & Run",  "JMP", jumpRun,        IC_JUMP,        C::BLUE,          NULL },
  { "Sokoban",     "SOK", sokobanRun,     IC_SOKOBAN,     C::BROWN,         NULL },
  { "Battleship",  "BSH", battleshipRun,  IC_BATTLESHIP,  rgb(0,102,204),   NULL },
  { "Sudoku",      "SUD", sudokuRun,      IC_SUDOKU,      C::CYAN,          NULL },
  { "Lights Out",  "LGT", lightsOutRun,   IC_LIGHTS,      C::YELLOW,        NULL },
  { "Match 3",     "M3X", match3Run,      IC_MATCH3,      C::PINK,          NULL },
  { "Fruit Pot",   "FRT", fruitRun,       IC_FRUIT,       rgb(255,85,85),   NULL },
  { "Checkers",    "CHK", checkersRun,    IC_CHECKERS,    C::BROWN,         NULL },
  { "Mau-Mau",     "MAU", maumauRun,      IC_MAUMAU,      C::RED,           NULL },
  { "Skat",        "SKT", skatRun,        IC_SKAT,        C::DKGRN,         NULL },
  { "Bomberman",   "BMB", bomberRun,      IC_BOMBER,      C::ORANGE,        NULL },
  { "Cave",        "CAV", caveRun,        IC_CAVE,        C::BROWN,         NULL },
  { "Stack",       "STK", stackRun,       IC_STACK,       C::CYAN,          NULL },
  { "Minigolf",    "GLF", minigolfRun,    IC_MINIGOLF,    C::GREEN,         NULL },
  { "Lander",      "LND", landerRun,      IC_LANDER,      C::GREY,          NULL },
  { "Palace",      "PAL", palaceRun,      IC_PALACE,      rgb(210,150,40),  NULL },
  { "Pumpkin Maze", "PMZ", pumpkinRun,    IC_PUMPKIN,     rgb(255,102,0),   NULL },
  { "Kart GP",     "KGP", kartRun,        IC_KART,        rgb(230,60,30),   NULL },
  // more games come here (before the entries with a tag)
  { "Chess",       "CHS", chessRun,       IC_CHESS,       C::BROWN,         "2P" },
  { "Online",      "ONL", onlineRun,      IC_ONLINE,      MP_ACCENT,        "nearby" },
  { "TV",          "TVY", tvRun,          IC_TV,          TV_ACCENT,        "YouTube" },
  { "Settings", "SET", settingsRun, IC_SETTINGS, C::GREY,   "setup" },
};
static const uint8_t GAME_COUNT = sizeof(GAMES) / sizeof(GAMES[0]);
static uint8_t countReal() { uint8_t n = 0; while (!GAMES[n].tag) n++; return n; }
static const uint8_t REAL_GAMES = countReal();

const Game &gameAt(uint8_t i) { return GAMES[i]; }
const char *gameKey(uint8_t i) { return GAMES[i].key; }
const char *gameName(uint8_t i) { return GAMES[i].name; }
uint8_t     realGames()         { return REAL_GAMES; }

// the library's words follow the language; the game names stay as they are
static const char *gameLabel(uint8_t i) { return GAMES[i].run == settingsRun ? TR("Settings", "Einstellungen") : GAMES[i].name; }
static const char *gameTag(const Game &g) {
  if (!strcmp(g.tag, "nearby")) return TR("nearby/web", "nah/Netz");   // the radio or the own server
  if (!strcmp(g.tag, "setup"))  return TR("setup", "einrichten");
  return g.tag;
}

// =========================================================
//  MENU
// =========================================================
/* A list on the left (five rows), the chosen game big on the right with
   its best score. UP from the top wraps to Settings.                  */
static void starfield(uint32_t t) {
  for (uint8_t i = 0; i < 40; i++) {
    uint32_t h = (uint32_t)i * 2654435761u;
    int16_t x = (int16_t)((h >> 8) % SCR_W), y = (int16_t)(((h >> 20) + t / (40 + i % 5 * 20)) % SCR_H);
    gfx.pixel(x, y, i % 3 ? C::GREY : C::WHITE);
  }
}

void menu() {
  static uint8_t sel = 0, top = 0;
  const uint8_t ROWS = 5;
  btnClear();
  while (true) {
    poll();
    if (fwUnconfirmed)
      for (uint8_t i = 0; i < B_COUNT; i++)
        if (rawPressed(i)) { fwConfirm(); fwUnconfirmed = false; break; }
    if (btn(B_UP))   { sel = sel ? sel - 1 : GAME_COUNT - 1; sfx(700, 15); }
    if (btn(B_DOWN)) { sel = (sel + 1) % GAME_COUNT;         sfx(700, 15); }
    if (sel < top) top = sel;
    if (sel >= top + ROWS) top = sel - ROWS + 1;
    if (btn(B_OK)) {
      sfx(1200, 50);
      curGame = sel;
      bool real = sel < REAL_GAMES;
      bool play = real || !strcmp(GAMES[sel].tag, "2P");   // two players: a game too, only without a best
      do {
        wantRestart = false;
        curHigh = real ? loadHigh(sel) : 0;
        pauseOk = play;
        tiltGame = real;
        tiltGameStart();                         // level: as held now, or the saved position
        GAMES[sel].run();
        tiltGame = pauseOk = false;
      } while (wantRestart);
      btnClear();
      continue;
    }

    const Game &g = GAMES[sel];
    gfx.gradient(0, 0, SCR_W, SCR_H, colDark(g.accent, 3), C::BLACK);
    starfield(millis());
    // title
    gfx.textShadow(10, 6, "MiniArcade", C::WHITE, C::BLACK, F_BOLD);
    gfx.textShadow(10 + Gfx::width("MiniArcade ", F_BOLD), 6, "COLOR", C::YELLOW, C::DKRED, F_BOLD);
    if (phoneLink) gfx.text(SCR_W - 70, 12, "WLAN", C::CYAN, F_SMALL);
    batIcon(SCR_W - 30, 11);
    gfx.hline(0, 36, SCR_W, g.accent);
    // list
    for (uint8_t i = 0; i < ROWS && top + i < GAME_COUNT; i++) {
      uint8_t k = top + i;
      int16_t y = 44 + i * 38;
      bool on = k == sel;
      if (on) { gfx.rbox(4, y, 176, 34, 6, GAMES[k].accent); gfx.rframe(4, y, 176, 34, 6, C::WHITE); }
      gfx.sprite(10, y + 5, GAMES[k].icon, 12, ICON_KEYS, ICON_PAL, 2);
      gfx.text(42, y + 9, gameLabel(k), on ? C::WHITE : C::LTGREY);
    }
    // scroll marks
    if (top > 0) gfx.triangle(92, 40, 86, 44, 98, 44, C::WHITE);
    if (top + ROWS < GAME_COUNT) gfx.triangle(92, 238, 86, 234, 98, 234, C::WHITE);
    // preview
    gfx.rbox(190, 46, 124, 186, 10, colDark(g.accent, 2));
    gfx.rframe(190, 46, 124, 186, 10, g.accent);
    gfx.sprite(204, 58, g.icon, 12, ICON_KEYS, ICON_PAL, 8);
    if (!g.tag) {
      char b[20];
      snprintf(b, sizeof(b), "%u", loadHigh(sel));
      gfx.text(204, 168, TR("BEST", "REKORD"), C::LTGREY, F_SMALL);
      gfx.text(204, 182, b, C::YELLOW, F_BOLD);
    } else gfx.text(204, 176, gameTag(g), C::LTGREY);
    char ok[24];
    snprintf(ok, sizeof(ok), TR("%s = start", "%s = Start"), okKeys());
    gfx.text(196, 214, ok, C::GREY, F_SMALL);
    gfx.send();
  }
}

// =========================================================
//  PHONE PAGES   (what this console tells phone.cpp)
// =========================================================
int appStats(char *out, int max) {
  int n = snprintf(out, max, "{\"games\":[");
  for (uint8_t g = 0; g < REAL_GAMES && n < max - 64; g++)
    n += snprintf(out + n, max - n, "%s{\"n\":\"%s\",\"b\":%u,\"p\":0,\"s\":0}", g ? "," : "", GAMES[g].name, loadHigh(g));
  n += snprintf(out + n, max - n, "],\"awards\":[],\"online\":[0,0],\"total\":0}");
  return n < max ? n : max - 1;
}

int appSettings(char *out, int max) {
  tvLoad();
  return snprintf(out, max, "{\"name\":\"%s\",\"bright\":%u,\"sleep\":%u,\"sound\":%u,\"clock\":%u,\"buzzer\":%u,\"tv\":\"%s\",\"maze\":1}",
                  linkName(), cfgBright, cfgSleep, cfgMute ? 0 : 1, cfgClock, sndPin != 255 ? 1 : 0, tvUrl);
}

const char *appSet(const char *key, const char *val) {
  int v = atoi(val);
  if (!strcmp(key, "bright")) {
    if (v < 1 || v > 100) return "brightness 1..100";
    cfgBright = (uint8_t)(v < 10 ? 10 : v);
    gfx.setBacklight(cfgBright);
  } else if (!strcmp(key, "sleep")) {
    if (v < 0 || v > 60) return "sleep 0..60 minutes";
    cfgSleep = (uint8_t)v;
    lastInput = millis();
  } else if (!strcmp(key, "sound")) {
    cfgMute = !v;
    sfx(1000, 60);
  } else if (!strcmp(key, "clock")) {
    if (v != 80 && v != 160 && v != 240) return "clock 80, 160 (or 240 on the S3) MHz";
    cfgClock = (uint8_t)v;
  } else if (!strcmp(key, "tv")) {
    return tvSetUrl(val);                        // saved by tv.h (a new server forgets the pairing)
  } else if (!strcmp(key, "tvpin")) {            // for the next pairing only, never saved
    size_t n = 0;
    for (const char *p = val; *p && n < sizeof(tvPinRam) - 1; p++) if (*p >= '0' && *p <= '9') tvPinRam[n++] = *p;
    tvPinRam[n] = 0;
    return n >= 4 ? NULL : "the PIN has 4 or more digits";
  } else if (!strcmp(key, "name")) {
    char n[LK_NAME + 1];
    uint8_t k = 0;
    for (const char *p = val; *p && k < LK_NAME; p++) {
      char c = *p >= 'a' && *p <= 'z' ? *p - 32 : *p;
      if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == ' ' && k)) n[k++] = c;
    }
    while (k && n[k - 1] == ' ') k--;
    n[k] = 0;
    if (!k) return "name: letters and digits, up to 6";
    linkSetName(n);
    return NULL;
  } else return "unknown setting";
  saveCfg();
  return NULL;
}

/* The phone's screen page shows 128 x 64 in two colours: the picture is
   shrunk and every pixel brighter than dark grey is lit.              */
void appScreen(uint8_t *out) {
  memset(out, 0, 1024);
  const uint8_t *fb = gfx.buffer();
  for (int y = 0; y < 64; y++)
    for (int x = 0; x < 128; x++) {
      col c = fb[(y * SCR_H / 64) * SCR_W + x * SCR_W / 128];
      uint8_t r, g, b;
      colToRgb(c, r, g, b);
      if (r + g + b > 200) out[(y >> 3) * 128 + x] |= (uint8_t)(1 << (y & 7));
    }
}

const char *appLevel(uint8_t, const char *) { return "Sokoban is not on the colour console yet"; }
int appLevelGet(uint8_t, char *out, int) { out[0] = 0; return 0; }

void appKey(uint8_t key, bool down) {
  if (key >= B_COUNT) return;
  vKeyUntil[key] = down ? ((millis() + 700) | 1) : 0;
}

// =========================================================
//  START
// =========================================================
void setup() {
  bool havePins = loadPins();
  prefs.begin("arcade", true);
  batPin = (uint8_t)prefs.getUShort("bat", 254);
  bool soundAsked = prefs.getUShort("sset", 0) != 0;
  prefs.end();
  tftChipRemember();
  runClock = cfgClock;
  setCpuFrequencyMhz(cfgClock);
  randomSeed(esp_random());
  gfx.begin();                     // finds the display's pins first
  candInit();
  tiltLoad();
  // keys, buzzer or battery saved on a pin the display has now: ask again
  for (uint8_t i = 0; i < B_COUNT; i++)
    if (BTN_PIN[i] != 255 && tftPinUsed(BTN_PIN[i])) havePins = false;
  if (!havePins) for (uint8_t i = 0; i < B_COUNT; i++) BTN_PIN[i] = 255;
  if (sndPin != 255 && tftPinUsed(sndPin)) { sndPin = 255; soundAsked = false; }
  if (batPin < BAT_NONE_SET && tftPinUsed(batPin)) batPin = 254;
  applyDisplay();
  applyPinModes();
  lastInput = millis();
  if (uint8_t upd = updTake()) updModeRun(upd);   // asked for before the restart: never returns

  // splash
  for (int16_t f = 0; f <= 16; f++) {
    gfx.gradient(0, 0, SCR_W, SCR_H, C::DKBLUE, C::BLACK);
    starfield((uint32_t)f * 200);
    gfx.center(70, "MiniArcade", colLerp(C::BLACK, C::WHITE, (uint8_t)(f * 15)), F_BIG);
    gfx.center(120, "COLOR", colLerp(C::BLACK, C::YELLOW, (uint8_t)(f * 15)), F_BOLD);
    char v[24];
    snprintf(v, sizeof(v), "v%s", netVersion());
    gfx.center(200, v, C::GREY, F_SMALL);
    gfx.send();
    delay(40);
  }

  // keys: first start, or a key held while starting
  bool held = false;
  if (havePins) { delay(300); for (uint8_t i = 0; i < 4; i++) if (rawPressed(i)) held = true; }
  if (held && cfgSpi != CONFIG_ARCADE_TFT_MHZ) { cfgSpi = CONFIG_ARCADE_TFT_MHZ; saveCfg(); }   // way back from a white screen
  if (!havePins || held) learnKeys();
  if (!soundAsked) {
    soundSetup();
    prefs.begin("arcade", false);
    prefs.putUShort("sset", 1);
    prefs.end();
  }
  batDetect();
  applyPinModes();
  tiltStart();
  sdBoot();
  gfx.setSpeed(cfgSpi);
  fwUnconfirmed = fwPending();
  versionRun(true);
}

void loop() {
  menu();          // never returns
}
