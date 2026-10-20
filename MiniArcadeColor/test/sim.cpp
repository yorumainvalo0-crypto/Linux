/* Host test harness for MiniArcade Color.
   Runs the real console code (console.cpp with all games), the real platform
   layer and the real drawing code; only the hardware calls are redirected
   here. The panel driver is replaced: every frame sent lands in a buffer,
   and the frames a scenario asks for are written to frames/ as PPM.
   Usage: ./colorsim <scenario>       (build.sh runs them all)          */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <functional>
#include <unordered_set>

std::map<std::string, uint16_t>    simNvsU16;
std::map<std::string, std::string> simNvsBlob;
struct SimEnd {};

static uint64_t clockUs = 0;
static uint8_t  pinMode_[64], pinPress[64];
static long     frames = 0;
static long     toneCount = 0;
static std::set<std::string> seenTexts;
static std::string scenario;
static const uint8_t KEYS[4] = { 1, 2, 3, 6 };   // UP DOWN LEFT RIGHT, active high
static const uint8_t SND = 7, BAT = 4;
struct KeyEv { uint32_t t; uint8_t mask; };
static std::vector<KeyEv> script;
static std::set<long> captureAt;
static uint32_t simEnd = 0;
static bool autoplay = false;
static uint32_t autoplayFrom = 0;
static int grabNext = 0;
static void (*pzBot)(uint32_t t) = NULL;         // puzzle games: a bot that plays through the keys
static std::vector<uint32_t> captureMs;          // frames to keep, by time (ms)
static bool simSlept = false;
static int8_t  LADDER = -1;                      // pin with three keys through resistors
static uint8_t ladder = 0;                       // which of them is pressed (0 = none)
static const int LADDER_EXT[8] = { 3100, 0, 300, 550 };  // 10k pull-up, keys via 0 / 1k / 2k
// no pull-up resistor, the pin's own ~45k: keys via 10k / 11k / 12k / 13.3k to GND, 5 = UP+DOWN
static const int LADDER_INT[8] = { 3100, 600, 649, 696, 750, 345 };
// five keys: the four arrows and an own OK key (14.6k)
static const int LADDER_5[8] = { 3100, 600, 649, 696, 750, 800 };
static const int *LADDER_MV = LADDER_EXT;
static int16_t simAcc[3] = { 0, 0, 16384 };      // the gyroscope's accelerometer, lying flat
struct AccEv { uint32_t t; int16_t x, y; };
static std::vector<AccEv> accScript;

uint64_t simMicros() { return clockUs; }
void simGpioConfig(uint64_t mask, int pu, int pd) {
  for (int p = 0; p < 64; p++) if (mask >> p & 1) pinMode_[p] = pd ? 3 : (pu ? 2 : 0);
}
int simGpioLevel(int p) {
  if (p == LADDER) return ladder ? 0 : 1;
  if (pinPress[p]) return 1;
  return pinMode_[p] == 3 ? 0 : 1;
}
void simCpuMhz(int) {}
static bool toneOn = false;
void simTone(int f) { if (f) toneCount++; toneOn = f != 0; }
void simFrameSent(const uint8_t *, size_t) {}                   // the OLED's I2C (unused here)
int simAdcRaw(int ch) {
  bool down = pinMode_[ch] == 3;
  if (ch == BAT) return 3900 / 2 * (down ? 62 : 100) / 100 * 4095 / 2500;
  if (ch == LADDER) { int mv = LADDER_MV[ladder] + rand() % 17 - 8 + (toneOn && ladder ? 40 : 0);   // noisy, and the buzzer pulls
    if (mv < 0) mv = 0; return mv * 4095 / 2500 > 4095 ? 4095 : mv * 4095 / 2500; }
  if (pinPress[ch] || pinMode_[ch] == 2) return 4095;
  if (down) return 5;
  return 300 + (int)(clockUs / 1000 % 400);                    // floating pin: drifts
}
void arcadeTraceStr(const char *s) {
  seenTexts.insert(s);
  const char *mark = getenv("GRAB");
  static bool grabbed = false;                                   // only the first time
  if (mark && !grabbed && strstr(s, mark)) { grabNext = 1; grabbed = true; }
}
void simDeepSleep() { simSlept = true; printf("      board went to deep sleep\n"); throw SimEnd{}; }

// bits 0..3: the keys on their own pins, bits 4..5: the ladder key
static void applyMask(uint8_t m) {
  for (int i = 0; i < 4; i++) if (KEYS[i] != LADDER) pinPress[KEYS[i]] = m >> i & 1;
  ladder = m >> 4 & 7;
}
void simDelayUs(uint32_t us) { clockUs += us; }

// a player that looks at the game's state (board games): called every ms
static void (*simBot)(uint32_t t) = NULL;
static uint32_t botFrom = 0, botFree = 0, botUp = 0;
static bool botBusy(uint32_t t) {
  if (botUp && t >= botUp) { applyMask(0); botUp = 0; }
  return t < botFree;
}
static void botPress(uint32_t t, uint8_t mask, uint32_t len) { applyMask(mask); botUp = t + len; botFree = t + len + 90; }

void simDelayMs(uint32_t ms) {
  for (uint32_t i = 0; i < ms; i++) {
    clockUs += 1000;
    uint32_t t = (uint32_t)(clockUs / 1000);
    for (auto &e : script) if (e.t == t) applyMask(e.mask);
    for (auto &e : accScript) if (e.t == t) { simAcc[0] = e.x; simAcc[1] = e.y; }
    if (autoplay && t > autoplayFrom && t % 180 == 0) {
      static const uint8_t o[] = { 0, 0, 1, 2, 4, 8, 4, 8 };
      applyMask(o[rand() % 8]);
    }
    if (pzBot) pzBot(t);
    if (simBot && t >= botFrom) simBot(t);
    if (simEnd && t >= simEnd) throw SimEnd{};
  }
}

#include "Arduino.h"
#include "net.h"                   // pretend network, before the console finds main/net.h
#include "link.h"
#include "../main/console.cpp"

// ---------------- the panel ----------------
static void writeFrame(const uint8_t *fb) {
  char fn[96];
  snprintf(fn, sizeof(fn), "frames/%s_%05ld.ppm", scenario.c_str(), frames);
  FILE *f = fopen(fn, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
  for (int i = 0; i < SCR_W * SCR_H; i++) {
    uint8_t rgbv[3];
    colToRgb(fb[i], rgbv[0], rgbv[1], rgbv[2]);
    fwrite(rgbv, 1, 3, f);
  }
  fclose(f);
}

// the school wiring, as if the panel had answered
TftPins tftPins = { 8, 10, 21, 0, 9, 5 };
bool tftFound = true;
bool tftPinUsed(uint8_t p) {
  const TftPins &t = tftPins;
  return p == t.sck || p == t.mosi || p == t.cs || p == t.dc || p == t.miso || p == t.rst;
}

// the gyroscope: a sensor on GPIO20 (SDA) / GPIO7 (SCL) in the "tilt" scenario
static bool    simGyro = false;
bool gyroOpen(int8_t sda, int8_t scl) { return simGyro && sda == 20 && scl == 7; }
void gyroClose() {}
bool gyroAccel(int16_t a[3]) { if (!simGyro) return false; memcpy(a, simAcc, sizeof(simAcc)); return true; }

static uint8_t lastFrame[SCR_W * SCR_H];
static int mine3dCols = 0, mine3dDug = 0, mine3dPut = 0;
void Gfx::begin() {}
void Gfx::setBacklight(uint8_t) {}
void Gfx::setOrientation(bool, bool, bool) {}
void Gfx::sleep(bool) {}
void Gfx::send() {
  frames++;
  memcpy(lastFrame, fb, sizeof(fb));
  if (grabNext) { grabNext = 0; captureAt.insert(frames + 3); }
  if (captureAt.count(frames)) writeFrame(fb);
  if (vox && scenario == "mine3d") {                            // colours in a picture of the game
    bool u[256] = { false }; int n = 0;
    for (int i = 0; i < SCR_W * SCR_H; i++) if (!u[fb[i]]) { u[fb[i]] = true; n++; }
    if (n > mine3dCols) mine3dCols = n;
    if (vox->dug > mine3dDug) mine3dDug = vox->dug;              // a new round starts from 0 again
    if (vox->placed > mine3dPut) mine3dPut = vox->placed;
  }
  for (size_t i = 0; i < captureMs.size(); i++)
    if (clockUs / 1000 >= captureMs[i]) { writeFrame(fb); captureMs.erase(captureMs.begin() + i); break; }
}

// ---------------- scenarios ----------------
static int failures = 0;
static void check(const char *what, bool ok) {
  printf("   %s %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) failures++;
}
static bool seen(const char *s) { return seenTexts.count(s) > 0; }
static void press(uint32_t t, uint8_t mask, uint32_t len = 60) { script.push_back({ t, mask }); script.push_back({ t + len, 0 }); }
static const uint8_t UP = 1, DN = 2, LT = 4, RT = 8, OK = UP | DN;
// from the menu (Snake selected) to game n, started at about t
static uint32_t openGame(uint8_t n, uint32_t t) {
  if (n <= GAME_COUNT / 2) for (uint8_t i = 0; i < n; i++, t += 250) press(t, DN);
  else for (uint8_t i = n; i < GAME_COUNT; i++, t += 250) press(t, UP);   // shorter the other way round
  press(t, OK, 80);
  return t + 400;
}
// the same by the game's name in the library, e.g. openGameNamed("Doom", M)
static uint32_t openGameNamed(const char *name, uint32_t t) {
  for (uint8_t i = 0; i < GAME_COUNT; i++) if (!strcmp(GAMES[i].name, name)) return openGame(i, t);
  printf("no game %s\n", name);
  exit(2);
}

// ---------------- puzzle games: bots and logic checks ----------------
#include "sksolve.h"                                            // the OLED tests' Sokoban solver
static uint32_t pzNext = 0, pzUp = 0, pzStart = 0;
static void pzPress(uint32_t t, uint8_t mask, uint32_t gap) { applyMask(mask); pzUp = t + 40; pzNext = t + gap; }
static bool pzBusy(uint32_t t) {                                // a press still running or not time yet
  if (pzUp && t >= pzUp) { applyMask(0); pzUp = 0; }
  return pzUp || t < pzNext || t < pzStart;
}
static const uint8_t PZ_OK = 3;                                 // UP + DOWN together

// Sudoku: writes the first empty field, the first two times a wrong number
static int suBotWrong = 2;
static void sudokuBot(uint32_t t) {
  if (pzBusy(t) || !suM) return;
  if (!suPicking) {
    int target = -1;
    for (int i = 0; i < 81 && target < 0; i++) if (!suM->grid[i]) target = i;
    if (target < 0) return;
    if (target == suCur) pzPress(t, PZ_OK, 110);
    else if (target / 9 != suCur / 9) pzPress(t, 2, 110);
    else pzPress(t, 8, 110);
  } else {
    uint8_t want = suBotWrong > 0 ? suM->sol[suCur] % 9 + 1 : suM->sol[suCur];
    if (suPick != want) pzPress(t, 1, 110);
    else { pzPress(t, PZ_OK, 110); if (suBotWrong > 0) suBotWrong--; }
  }
}

// Lights Out: light chasing - try all 32 first rows, the rest follows; the shortest solution
static int loSolve(uint32_t b, uint8_t *out) {
  int bestN = 99;
  for (int m = 0; m < 32; m++) {
    uint32_t x = b;
    uint8_t pr[25]; int n = 0;
    for (int c = 0; c < 5; c++) if (m & (1 << c)) { x = loPress(x, 0, (uint8_t)c); pr[n++] = (uint8_t)c; }
    for (int r = 1; r < 5; r++) for (int c = 0; c < 5; c++)
      if (x & (1UL << ((r - 1) * 5 + c))) { x = loPress(x, (uint8_t)r, (uint8_t)c); pr[n++] = (uint8_t)(r * 5 + c); }
    if (!x && n < bestN) { bestN = n; memcpy(out, pr, n); }
  }
  return bestN == 99 ? -1 : bestN;
}
static uint32_t loBotStop = 0;
static void lightsBot(uint32_t t) {
  if (pzBusy(t)) return;
  if (t > loBotStop) { static const uint8_t K[5] = { 1, 2, 4, 8, PZ_OK }; pzPress(t, K[rand() % 5], 140); return; }
  uint8_t pr[25];
  int n = loSolve(loBoard, pr);
  if (n <= 0) return;
  int r = pr[0] / 5, c = pr[0] % 5;
  if (r != loR) pzPress(t, r > loR ? 2 : 1, 100);
  else if (c != loC) pzPress(t, c > loC ? 8 : 4, 100);
  else pzPress(t, PZ_OK, 120);
}

// Match 3: the first swap that makes a row
static void match3Bot(uint32_t t) {
  if (pzBusy(t)) return;
  bool mark[M3_H][M3_W]; uint8_t best;
  for (int r = 0; r < M3_H; r++) for (int c = 0; c < M3_W; c++)
    for (int d = 0; d < 2; d++) {
      int r2 = r + d, c2 = c + 1 - d;
      if (r2 >= M3_H || c2 >= M3_W) continue;
      m3Swap((uint8_t)r, (uint8_t)c, (uint8_t)r2, (uint8_t)c2);
      bool ok = m3Find(mark, &best) > 0;
      m3Swap((uint8_t)r, (uint8_t)c, (uint8_t)r2, (uint8_t)c2);
      if (!ok) continue;
      if (m3Held && r == m3R && c == m3C) { pzPress(t, d ? 2 : 8, 200); return; }   // swap it
      if (m3Held) { pzPress(t, PZ_OK, 120); return; }                                // wrong one: drop it
      if (r != m3R) pzPress(t, r > m3R ? 2 : 1, 90);
      else if (c != m3C) pzPress(t, c > m3C ? 8 : 4, 90);
      else pzPress(t, PZ_OK, 120);
      return;
    }
}

// Minesweeper: knows where the mines are; flags one with a double tap, then opens every free cell
static int  msBotStep = 0;
static bool msBotFlagged = false;
static void minesBot(uint32_t t) {
  if (pzBusy(t) || !ms) return;
  bool placed = false;
  for (int y = 0; y < MS_H; y++) for (int x = 0; x < MS_W; x++) if (ms[y][x] & MS_OPEN) placed = true;
  int cx = msCx, cy = msCy;
  int tx = -1, ty = -1, bestD = 99;
  if (placed && !msBotFlagged) {                                // the flag: to the nearest mine, tap twice
    for (int y = 0; y < MS_H; y++) for (int x = 0; x < MS_W; x++)
      if ((ms[y][x] & MS_MINE) && !(ms[y][x] & MS_OPEN)) {
        int d = abs(x - cx) + abs(y - cy);
        if (d < bestD) { bestD = d; tx = x; ty = y; }
      }
    if (ty != cy) { pzPress(t, ty > cy ? 2 : 1, 160); return; }
    if (tx != cx) { pzPress(t, tx > cx ? 8 : 4, 160); return; }
    pzPress(t, PZ_OK, msBotStep == 0 ? 120 : 600);
    if (++msBotStep == 2) msBotFlagged = true;
    return;
  }
  for (int y = 0; y < MS_H; y++) for (int x = 0; x < MS_W; x++) {
    uint8_t v = ms[y][x];
    if (placed && (v & (MS_MINE | MS_OPEN | MS_FLAG))) continue;
    if (!placed && (x != MS_W / 2 || y != MS_H / 2)) continue;
    int d = abs(x - cx) + abs(y - cy);
    if (d < bestD) { bestD = d; tx = x; ty = y; }
  }
  if (tx < 0) return;
  if (ty != cy) pzPress(t, ty > cy ? 2 : 1, 160);
  else if (tx != cx) pzPress(t, tx > cx ? 8 : 4, 160);
  else pzPress(t, PZ_OK, 600);                                  // one tap, then wait out the double tap time
}

static bool pzLogic() {
  if (scenario == "g2logic") {
    struct Case { uint8_t in[4]; uint8_t out[4]; uint32_t gain; };
    static const Case CS[] = {                                  // one row, moved left (exponents)
      {{1,1,1,1},{2,2,0,0},8}, {{1,1,2,0},{2,2,0,0},4}, {{0,0,0,1},{1,0,0,0},0},
      {{2,1,1,0},{2,2,0,0},4}, {{1,2,1,2},{1,2,1,2},0}, {{3,0,3,3},{4,3,0,0},16} };
    for (auto &c : CS) {
      uint8_t b[16]; memset(b, 0, 16); memcpy(b, c.in, 4);
      uint32_t g = 0; g2Move(b, B_LEFT, &g);
      bool ok = !memcmp(b, c.out, 4) && g == c.gain;
      if (!ok) printf("      %u%u%u%u -> %u%u%u%u gain %u\n", c.in[0], c.in[1], c.in[2], c.in[3], b[0], b[1], b[2], b[3], (unsigned)g);
      check("row slides and merges once per pair", ok);
    }
    uint8_t b[16]; memset(b, 0, 16); b[0] = 1; b[4] = 1; b[12] = 2; uint32_t g = 0;
    int8_t dest[16]; memset(dest, -1, 16); uint16_t merged = 0;
    g2Move(b, B_DOWN, &g, dest, &merged);
    check("column moves down", b[12] == 2 && b[8] == 2 && !b[0] && !b[4] && g == 4);
    check("the slide animation knows where each tile went", dest[0] == 8 && dest[4] == 8 && dest[12] == 12 && merged == (1u << 8));
    uint8_t full[16]; for (int i = 0; i < 16; i++) full[i] = (uint8_t)(1 + (i % 2 + (i / 4) % 2) % 2);
    check("no move left on a checkerboard", !g2CanMove(full));
    full[5] = full[6]; check("a pair is a move", g2CanMove(full));
    return true;
  }
  if (scenario == "mslogic") {
    check("field on the heap", msAlloc());
    int bad = 0, zeroOpen = 0;
    for (int round = 0; round < 300; round++) {
      randomSeed(50 + round);
      uint8_t sx = (uint8_t)random(MS_W), sy = (uint8_t)random(MS_H), mines = (uint8_t)(20 + (round % 3) * 8);
      msPlace(mines, sx, sy);
      int n = 0;
      for (int y = 0; y < MS_H; y++) for (int x = 0; x < MS_W; x++) {
        if (ms[y][x] & MS_MINE) { n++; if (abs(x - sx) <= 1 && abs(y - sy) <= 1) bad++; }
        int c = 0;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) { int X = x + dx, Y = y + dy;
          if ((dx || dy) && X >= 0 && X < MS_W && Y >= 0 && Y < MS_H && (ms[Y][X] & MS_MINE)) c++; }
        if (c != (ms[y][x] & 0x0F)) bad++;
      }
      if (n != mines) bad++;
      if (!msOpen(sx, sy)) bad++;
      for (int y = 0; y < MS_H; y++) for (int x = 0; x < MS_W; x++) {      // flood fill complete, no mine open
        uint8_t c = ms[y][x];
        if ((c & MS_OPEN) && (c & MS_MINE)) bad++;
        if ((c & MS_OPEN) && !(c & 0x0F) && !(c & MS_MINE)) { zeroOpen++;
          for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) { int X = x + dx, Y = y + dy;
            if (X >= 0 && X < MS_W && Y >= 0 && Y < MS_H && !(ms[Y][X] & MS_OPEN)) bad++; } }
      }
    }
    printf("      %d empty cells opened by flood fill\n", zeroOpen);
    check("mines placed right, first cell safe, flood fill complete", bad == 0);
    msFree();
    return true;
  }
  if (scenario == "sklevels") {
    check("map on the heap", skAlloc());
    int bad = 0;
    for (uint8_t l = 0; l < SK_N; l++) {
      SkLevel L = skParse(SK_LEVELS[l]);
      int goals = 0; for (uint8_t g : L.goal) goals += g;
      if (L.w > SK_W || L.h > SK_H || goals != (int)L.boxes.size() || L.boxes.empty()) { bad++; printf("      level %u malformed\n", l + 1); continue; }
      int p = skSolve(L);
      if (!skLoad(l) || skW != L.w || skH != L.h || skX != L.px || skY != L.py) { bad++; printf("      level %u loads differently\n", l + 1); }
      printf("      level %2u: %dx%d, %d boxes, %d pushes, tile %d px\n", l + 1, L.w, L.h, (int)L.boxes.size(), p, skTile());
      if (p <= 0) { bad++; continue; }
      if (skW * skTile() > SCR_W || skH * skTile() > SK_Y1 - SK_Y0) { bad++; printf("      level %u does not fit\n", l + 1); }
      uint8_t before[SK_H * SK_W]; memcpy(before, sk, sizeof(before)); uint8_t bx = skX, by = skY;
      randomSeed(l); int n = 0;
      for (int k = 0; k < 300; k++) if (skStep((uint8_t)random(4))) n++;
      while (skBack()) {}
      if (n > SK_UNDO) skLoad(l);                                // more than the undo holds: fine
      else if (memcmp(before, sk, sizeof(before)) || skX != bx || skY != by) { bad++; printf("      level %u: undo does not restore\n", l + 1); }
    }
    check("all levels well formed, fit the screen and solvable, undo exact", bad == 0);
    skFree();
    return true;
  }
  if (scenario == "pzlogic") {
    // Sudoku: valid, consistent, unique, about as many givens as asked
    suM = (SuMem *)malloc(sizeof(SuMem));
    bool ok = true; int given[3] = { 0, 0, 0 };
    static const uint8_t KEEP[3] = { 40, 32, 26 };
    for (int m = 0; m < 3; m++)
      for (int k = 0; k < 15; k++) {
        int n = suMake(KEEP[m]);
        given[m] += n;
        for (int i = 0; i < 81; i++) {                           // the solution follows the rules
          uint8_t v = suM->sol[i]; suM->sol[i] = 0;
          if (!v || (suMask(suM->sol, (uint8_t)i) & (1 << v))) ok = false;
          suM->sol[i] = v;
        }
        memcpy(suM->tmp, suM->grid, 81);
        if (suSolve(suM->tmp, 2, false) != 1 || memcmp(suM->tmp, suM->grid, 81)) ok = false;   // one solution, grid as it was
        memcpy(suM->tmp, suM->grid, 81);
        if (suSolve(suM->tmp, 1, false) != 1 || memcmp(suM->tmp, suM->sol, 81)) ok = false;    // and it is the right one
        for (int i = 0; i < 81; i++) if (suM->grid[i] && suM->grid[i] != suM->sol[i]) ok = false;
      }
    printf("      sudoku givens: easy %d, medium %d, hard %d (average)\n", given[0] / 15, given[1] / 15, given[2] / 15);
    check("sudoku: 45 puzzles valid with exactly one solution", ok);
    check("sudoku: harder levels give fewer numbers", given[0] > given[1] && given[1] > given[2] && given[2] / 15 <= 32);
    free(suM); suM = NULL;
    // Lights Out: every level solvable
    ok = true;
    for (int par = 3; par <= 15; par++) for (int k = 0; k < 20; k++) { uint8_t pr[25]; uint32_t b = loMake((uint8_t)par); if (!b || loSolve(b, pr) < 0) ok = false; }
    check("lights out: 260 levels, all solvable, none empty", ok);
    // Match 3: fresh boards have no rows and a move
    ok = true;
    for (int k = 0; k < 200; k++) { m3Fill(); bool mark[M3_H][M3_W]; uint8_t best; if (m3Find(mark, &best) || !m3CanMove()) ok = false; }
    check("match 3: 200 new boards without rows, with a move", ok);
    return true;
  }
  return false;
}

// Chess: plays the moves of a list, given as (row, column) of the board: cursor there, OK
static const uint8_t CH_FOOL[8][2] = { {6,5},{5,5}, {1,4},{3,4}, {6,6},{4,6}, {0,3},{4,7} };   // f3 e5 g4 Qh4#
static int chBotStep = 0;
static void chessBot(uint32_t t) {
  if (pzBusy(t) || !ch || chBotStep >= 8) return;
  int r = CH_FOOL[chBotStep][0], c = CH_FOOL[chBotStep][1];
  if (ch->cr != r) pzPress(t, ch->cr < r ? 2 : 1, 120);
  else if (ch->cc != c) pzPress(t, ch->cc < c ? 8 : 4, 120);
  else { pzPress(t, PZ_OK, 250); chBotStep++; }
}

// move generator against the known counts (perft)
static uint64_t chPerft(ChPos &p, int depth) {
  ChMove m[220];
  int n = chMoves(p, m);
  if (depth == 1) return (uint64_t)n;
  uint64_t sum = 0;
  for (int i = 0; i < n; i++) { ChPos q = p; chPlay(q, m[i]); sum += chPerft(q, depth - 1); }
  return sum;
}
static void chessLogic() {
  struct { const char *fen; int depth; uint64_t want; } T[] = {
    { "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -", 3, 8902 },
    { "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -", 4, 197281 },
    { "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -", 3, 97862 },   // castling, pins
    { "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -", 5, 674624 },                          // en passant
    { "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq -", 3, 9467 },     // promotions
  };
  for (auto &tc : T) {
    ChPos p;
    bool ok = chFromFen(p, tc.fen);
    uint64_t n = ok ? chPerft(p, tc.depth) : 0;
    printf("      perft %d = %llu (want %llu)\n", tc.depth, (unsigned long long)n, (unsigned long long)tc.want);
    check("chess moves counted right", ok && n == tc.want);
  }
  ChPos p;
  chFromFen(p, "7k/5Q2/6K1/8/8/8/8/8 b - -");
  ChMove m[220];
  check("stalemate: no move, not in check", chMoves(p, m) == 0 && !chInCheck(p, false));
  chFromFen(p, "7k/6Q1/6K1/8/8/8/8/8 b - -");
  check("checkmate: no move, in check", chMoves(p, m) == 0 && chInCheck(p, false));
  chFromFen(p, "8/8/4k3/8/8/2B5/4K3/8 w - -");
  check("king and bishop against king is a draw", chTooLittle(p));
}

static uint8_t colsInFrame() {                                  // how many colours one frame shows
  std::set<uint8_t> c(lastFrame, lastFrame + sizeof(lastFrame));
  return (uint8_t)c.size();
}

// ---------------- game logic without the UI (fairness) ----------------
/* Dino: tries every way to press the keys, step by step (jump held or
   not, DOWN or not), with the game's own collision. Two obstacles gap px
   apart (left edges), the dino on the ground well before them.        */
static bool dnoPass(uint8_t ta, uint8_t ha, uint8_t tb, uint8_t hb, int32_t gap, uint16_t spd, bool canJump = true) {
  std::unordered_set<uint64_t> been;
  int32_t step = spd * DNO_MOVE;
  std::function<bool(DnoJump, int32_t)> go = [&](DnoJump j, int32_t ax) -> bool {
    if ((ax >> 7) + gap + 40 < DNO_X) return true;                 // both are behind
    uint64_t key = ((uint64_t)((ax + (1 << 20)) & 0xFFFFF) << 40) | ((uint64_t)((j.y + 32768) & 0xFFFF) << 24) |
                   ((uint64_t)((j.vy + 32768) & 0xFFFF) << 8) | (j.air ? 1 : 0) | (j.reached ? 2 : 0) | (j.drop ? 4 : 0);
    if (!been.insert(key).second) return false;
    for (int k = 0; k < 4; k++) {
      if (!canJump && (k & 1)) continue;
      bool held = k & 1, down = k & 2;
      DnoJump n = j;
      dnoStep(n, held, down, spd);
      bool duck = down && !n.air;
      if (dnoHit(n, duck, ax - step, ta, ha) || dnoHit(n, duck, ax - step + (gap << 7), tb, hb)) continue;
      if (go(n, ax - step)) return true;
    }
    return false;
  };
  return go(DnoJump{ 0, 0, false, false, false }, (DNO_X + 130) << 7);
}

static int logicTest() {
  if (scenario == "dinofair") {
    static const uint8_t KT[6] = { 0, 1, 2, 3, 3, 3 }, KH[6] = { 0, 0, 0, 0, 1, 2 };
    bool ok = true;
    for (uint16_t spd = DNO_VMIN; spd <= DNO_VMAX; spd += 2) {
      int16_t bad = -1;                                              // the game uses dnoGap .. dnoGap + 99
      for (int16_t gap = dnoGap(spd); gap < dnoGap(spd) + 100 && bad < 0; gap++)
        for (int a = 0; a < 6 && bad < 0; a++)
          for (int b = 0; b < 6 && bad < 0; b++)
            if (!dnoPass(KT[a], KH[a], KT[b], KH[b], gap, spd)) bad = gap;
      int16_t need = 0;                                              // the smallest gap from which on all pass
      if (spd % 8 == 0)
        for (int16_t g = dnoGap(spd); g >= 20 && !need; g -= 2)
          for (int a = 0; a < 6 && !need; a++)
            for (int b = 0; b < 6 && !need; b++)
              if (!dnoPass(KT[a], KH[a], KT[b], KH[b], g, spd)) need = g + 2;
      if (spd % 8 == 0 || bad >= 0) printf("      speed %u (%d px/s): the game keeps gaps from %d (needed %d)%s\n", spd,
                                          spd * DNO_MOVE * 1000 / DNO_TICK / 128, dnoGap(spd), need, bad >= 0 ? "  <- too small" : "");
      if (bad >= 0) { ok = false; printf("        first gap that cannot be passed: %d\n", bad); }
    }
    check("dino: every pair of obstacles can be passed at every speed", ok);
    check("dino (the check itself): a cactus needs a jump, a middle bird a duck",
          !dnoPass(2, 0, 2, 0, 150, 40, false) && dnoPass(3, 1, 3, 1, 150, 40, false) && dnoPass(2, 0, 2, 0, 150, 40));
    auto apex = [](int holdSteps, int downAt) {                      // a tap hops, holding jumps high, DOWN drops fast
      DnoJump j = { 0, 0, false, false, false };
      int top = 0, t = 0;
      do { dnoStep(j, t < holdSteps, downAt >= 0 && t >= downAt, 40); if (-j.y > top) top = -j.y; t++; } while (j.air && t < 200);
      return std::make_pair(top / 128, t);
    };
    auto tap = apex(1, -1), hold = apex(60, -1), dropped = apex(60, 8);
    printf("      tap: %d px high, %d ms in the air; held: %d px, %d ms; DOWN after 120 ms: %d ms\n",
           tap.first, tap.second * DNO_TICK, hold.first, hold.second * DNO_TICK, dropped.second * DNO_TICK);
    check("dino: a tap hops, holding jumps high, DOWN drops fast",
          tap.first >= 32 && tap.first <= 43 && hold.first >= 55 && hold.first <= 68 && dropped.second < hold.second - 12);
    return failures ? 1 : 0;
  }
  if (scenario == "racerfair") {
    /* Plays the real car spawning at every speed and keeps the set of all
       positions the player could be in. If that set ever runs empty, the
       road was closed - an impossible situation.                        */
    int stuck = 0; long steps = 0; int top = 0;
    for (int g = 0; g < 60; g++) {
      randomSeed(1000 + g);
      rcrStart();
      std::set<int32_t> can = { (rcrMid(RCR_CARY) - RCR_CW / 2) * RCR_Q };
      for (int t = 0; t < 10000; t++, steps++) {
        std::set<int32_t> nx;
        for (int32_t x : can) {
          nx.insert(x);
          if (x > 4 * RCR_Q) nx.insert(x - RCR_STEP);
          if (x < (SCR_W - RCR_CW - 4) * RCR_Q) nx.insert(x + RCR_STEP);
        }
        rcrAdvance();
        can.clear();
        for (int32_t x : nx) if (!rcrCrash(x)) can.insert(x);
        if (can.empty()) { stuck++; printf("      game %d: no way through after %d steps at speed %u\n", g, t, rcr.spd); break; }
      }
      if (rcr.spd > top) top = rcr.spd;
    }
    printf("      %ld steps checked, top speed %d\n", steps, top);
    check("racer: the road never closes up (60 games up to top speed)", stuck == 0 && top == RCR_VMAX);
    return failures ? 1 : 0;
  }
  if (scenario == "frogroll") {
    /* Rolls lanes for every difficulty and level and checks them: objects
       never overlap, every gap has the promised size, the road always
       leaves room for the frog, and neighbouring lanes move opposite ways. */
    int bad = 0, rolls = 0;
    for (uint8_t diff = 0; diff < 3; diff++)
      for (int round = 0; round < 3000; round++) {
        randomSeed(7 + round * 3 + diff);
        frgRollAll(diff, (uint8_t)(round % 12));
        for (uint8_t l = 0; l < FRG_LANES; l++, rolls++) {
          const FrgLane &L = frgL[l];
          bool water = frgWater(l);
          int glo = water ? 15 : (diff == 0 ? 55 : 40);
          int ghi = water ? (diff == 0 ? 55 : (diff == 1 ? 70 : 85)) : 999;
          if (L.n < 2 || L.n > FRG_MAXO) { bad++; continue; }
          for (int k = 0; k < L.n; k++) {                            // gap after object k, around the ring
            int a = L.x[k] / FRG_Q, b = L.x[(k + 1) % L.n] / FRG_Q;
            int gap = ((b - a + SCR_W) % SCR_W) - L.w[k];
            if (gap < glo || gap > ghi) { bad++; if (bad < 5) printf("      diff %u lane %u: gap %d not in %d..%d\n", diff, l, gap, glo, ghi); }
          }
          if (!L.spd) bad++;
          if (l && (L.spd > 0) == (frgL[l - 1].spd > 0)) bad++;
        }
      }
    printf("      %d lanes rolled\n", rolls);
    check("frogger: lanes always valid (gaps, room for the frog, directions)", bad == 0);
    return failures ? 1 : 0;
  }
  if (scenario == "jrfair") {
    // the jump itself: how high and how far, straight from the constants
    int vy = -JMP_JUMP, y = 0, top = 0, t = 0;
    do { y += vy; vy += JMP_G; if (y < top) top = y; t++; } while (y < 0);
    int high = -top / JMP_Q, far = t * JMP_RUN / JMP_Q;
    printf("      jump: %d px high, %d px far at full speed\n", high, far);
    check("jump & run: steps up are within the jump", high >= JMP_UP * JMP_T + 10);
    check("jump & run: gaps are within the jump", far >= JMP_GAP * JMP_T + 20);
    // and the level: never a wider gap or a higher step than that
    int bad = 0; long cols = 0;
    for (int g = 0; g < 200; g++) {
      randomSeed(300 + g); jmpMade = 0; jmpCur = 1; jmpLeft = 0; jmpMode = 0; jmpDiff = (uint8_t)(g % 6);
      memset(jmpE, 0, sizeof(jmpE));
      int gap = 0, lastLand = 1;
      for (int c = 0; c < 3000; c++, cols++) {
        jmpMake();
        uint8_t i = c % JMP_RING, land = jmpH[i] ? jmpH[i] : jmpP[i];
        for (uint8_t k = 0; k < JMP_EN; k++) jmpE[k].on = false;
        if (!land) { gap++; continue; }
        if (gap > JMP_GAP) { bad++; if (bad < 5) printf("      gap of %d at %d\n", gap, c); }
        if (land > lastLand + JMP_UP || (gap >= 2 && land > lastLand + 1)) { bad++; if (bad < 5) printf("      step %d->%d after gap %d\n", lastLand, land, gap); }
        gap = 0; lastLand = land;
      }
    }
    printf("      %ld columns made\n", cols);
    check("jump & run: the level never asks for an impossible jump", bad == 0);
    return failures ? 1 : 0;
  }
  return -1;
}

// ---------------- players for the board games (they read the game's state) ----------------
// Battleship: aims where the CPU's own logic would
static void bshipBot(uint32_t t) {
  if (botBusy(t) || !bsV.myGo || bsV.aimCell >= 0) return;
  static uint8_t target = 0, forShots = 255;
  if (forShots != bsV.shots || bsShot[target] != BS_UNKNOWN) { target = bsAiPick(bsShot); forShots = bsV.shots; }
  int tr = target / 8, tc = target % 8, cr = bsV.cur / 8, cc = bsV.cur % 8;
  if (tr != cr) botPress(t, tr > cr ? DN : UP, 60);
  else if (tc != cc) botPress(t, tc > cc ? RT : LT, 60);
  else botPress(t, OK, 100);
}

// Checkers: the rules' own search, 3 deep, walks the cursor there
static void checkersBot(uint32_t t) {
  if (botBusy(t) || ck.winner || ck.turn != 0 || ckV.fp) return;
  uint32_t r = 12345 + ck.quiet * 7 + ck.count(0) * 131;          // a fixed choice for a given board
  CkStep st = ck.best(3, r);
  int8_t want = (ckSel == st.from) ? Checkers::next(st.from, st.dir, st.jump ? 2 : 1) : (int8_t)st.from;
  int8_t c, rr = Checkers::rc((uint8_t)want, &c);
  int cr = ckCur / 8, cc = ckCur % 8;
  if (rr != cr) botPress(t, rr > cr ? DN : UP, 60);
  else if (c != cc) botPress(t, c > cc ? RT : LT, 60);
  else botPress(t, OK, 120);
}

// Mau-Mau: plays as the CPUs do; after a game OK through "again" and the mode
static uint8_t mmBotWish = 0;
static uint32_t mmBotAgain = 0;
static void maumauBot(uint32_t t) {
  if (botBusy(t)) return;
  if (mmWishing) { botPress(t, mmWishSel == mmBotWish ? OK : RT, 60); return; }
  if (mm.winner) { if (t > mmBotAgain) { botPress(t, OK, 100); mmBotAgain = t + 1500; } return; }
  if (mm.turn != 0 || mmFly.on) return;
  MauMau c = mm;
  uint8_t m = c.cpuMove(), slot = 0;
  if (m != MM_DRAW && m != MM_PASS) {
    uint8_t cards[MM_CARDS], n = mmHand(mm, 0, cards);
    for (uint8_t i = 0; i < n; i++) if (cards[i] == (m & 31)) slot = i + 1;
    mmBotWish = (m >> 5) & 3;
  }
  if (mmCur != slot) botPress(t, slot > mmCur ? RT : LT, 60);
  else botPress(t, OK, 100);
}

// Minigolf: breadth first over resting places for the fewest strokes
struct GolfShot { uint8_t ang, pow; };
static uint8_t golfShoot(int32_t *x, int32_t *y, uint8_t ang, uint8_t pow) {   // 1 hole, 2 water, 0 rest
  int32_t vx = mgCos(ang) * (130 * pow) / 256, vy = mgSin(ang) * (130 * pow) / 256;
  for (int s = 0; s < 3000 && (vx || vy); s++) {
    uint8_t r = mgStep(x, y, &vx, &vy, mgHx, mgHy);
    if (r) return r;
  }
  return 0;
}
static int golfPlan(int32_t x, int32_t y, GolfShot *first) {
  struct Node { int32_t x, y; int depth; GolfShot first; };
  std::vector<Node> q;
  std::set<std::pair<int, int>> seenAt;
  q.push_back({ x, y, 0, { 0, 0 } });
  seenAt.insert({ (int)(x >> 8), (int)(y >> 8) });
  for (size_t h = 0; h < q.size(); h++) {
    Node n = q[h];
    if (n.depth >= 5) break;
    for (uint8_t a = 0; a < 32; a++)
      for (uint8_t p = 1; p <= 10; p++) {
        int32_t ex = n.x, ey = n.y;
        uint8_t r = golfShoot(&ex, &ey, a, p);
        GolfShot f = n.depth ? n.first : GolfShot{ a, p };
        if (r == 1) { *first = f; return n.depth + 1; }
        if (r == 2) continue;
        if (seenAt.insert({ (int)(ex >> 8), (int)(ey >> 8) }).second) q.push_back({ ex, ey, n.depth + 1, f });
      }
  }
  return -1;
}
static GolfShot golfNext = { 255, 0 };
static uint8_t  golfFor = 255;
static int32_t  golfAtX = -1, golfAtY = -1;
static void golfBot(uint32_t t) {
  if (botBusy(t) || mgVx || mgVy) return;
  if (golfFor != mgHole || golfAtX != mgBx || golfAtY != mgBy) {   // a new resting place: plan
    golfFor = mgHole; golfAtX = mgBx; golfAtY = mgBy;
    if (golfPlan(mgBx, mgBy, &golfNext) < 0) golfNext = { (uint8_t)(rand() % 32), 5 };
  }
  if (mgAng != golfNext.ang) botPress(t, ((golfNext.ang - mgAng) & 31) < 16 ? RT : LT, 60);
  else if (mgPow != golfNext.pow) botPress(t, golfNext.pow > mgPow ? UP : DN, 60);
  else { botPress(t, OK, 120); golfFor = 255; botFree = t + 600; }
}

// ---------------- the rules of the board games, without the screen ----------------
static void boardLogic() {
  randomSeed(7);
  // Connect Four: tactics, and depth beats the lower levels
  memset(c4, 0, sizeof(c4)); c4[5][1] = 2; c4[5][2] = 2; c4[5][3] = 2;
  uint8_t m = c4Think(2);
  check("4 wins: the CPU takes a win in one", m == 0 || m == 4);
  memset(c4, 0, sizeof(c4)); c4[5][0] = 1; c4[5][1] = 1; c4[5][2] = 1;
  check("4 wins: the CPU blocks a row", c4Think(2) == 3);
  memset(c4, 0, sizeof(c4)); c4[5][3] = 1; c4[4][3] = 1; c4[3][3] = 1;
  check("4 wins: the CPU blocks a column", c4Think(2) == 3);
  for (int opp = 0; opp < 2; opp++) {
    int hardWins = 0, oppWins = 0;
    for (int g = 0; g < 12; g++) {
      memset(c4, 0, sizeof(c4));
      uint8_t turn = (g & 1) ? 1 : 2;
      for (int ply = 0; ply < 42; ply++) {
        uint8_t col;
        if (turn == 2) col = c4Think(2);
        else {                                                      // the other level plays as 1
          for (int r = 0; r < C4_H; r++) for (int c = 0; c < C4_W; c++) if (c4[r][c]) c4[r][c] = 3 - c4[r][c];
          col = c4Think((uint8_t)opp);
          for (int r = 0; r < C4_H; r++) for (int c = 0; c < C4_W; c++) if (c4[r][c]) c4[r][c] = 3 - c4[r][c];
        }
        if (c4Drop(col, turn) < 0) break;
        if (c4Wins(turn)) { (turn == 2 ? hardWins : oppWins)++; break; }
        if (c4Full()) break;
        turn = 3 - turn;
      }
    }
    printf("      4 wins: hard vs %s: %d wins, %d losses of 12\n", opp ? "normal" : "easy", hardWins, oppWins);
    check(opp ? "4 wins: hard beats normal" : "4 wins: hard beats easy", opp ? (hardWins >= 5 && oppWins <= 2) : hardWins >= 10);
  }
  // Tic Tac Toe: every way to play against "hard", it never loses; easy can be beaten
  {
    long games = 0; int lost = 0, easyLost = 0;
    struct F { uint8_t b[9]; };
    for (int seed = 0; seed < 4; seed++) {
      randomSeed(100 + seed);
      for (uint8_t starter = 1; starter <= 2; starter++) {
        std::vector<F> todo; F f0; memset(f0.b, 0, 9);
        if (starter == 2) { memcpy(xo, f0.b, 9); xo[xoThink(2, 2)] = 2; memcpy(f0.b, xo, 9); }
        todo.push_back(f0);
        while (!todo.empty()) {
          F f = todo.back(); todo.pop_back();
          for (uint8_t i = 0; i < 9; i++) {
            if (f.b[i]) continue;
            memcpy(xo, f.b, 9); xo[i] = 1;
            if (xoLine(1) >= 0) { lost++; games++; continue; }
            if (xoFull()) { games++; continue; }
            xo[xoThink(2, 2)] = 2;
            if (xoLine(2) >= 0 || xoFull()) { games++; continue; }
            F g; memcpy(g.b, xo, 9); todo.push_back(g);
          }
        }
      }
    }
    for (int g = 0; g < 500; g++) {
      randomSeed(9000 + g); memset(xo, 0, 9); uint8_t turn = 1 + (g & 1);
      while (xoLine(1) < 0 && xoLine(2) < 0 && !xoFull()) {
        if (turn == 2) xo[xoThink(0, 2)] = 2;
        else { uint8_t fr[9], n = 0; for (uint8_t i = 0; i < 9; i++) if (!xo[i]) fr[n++] = i; xo[fr[random(n)]] = 1; }
        turn = 3 - turn;
      }
      if (xoLine(1) >= 0) easyLost++;
    }
    printf("      tic tac toe: %ld games against hard, %d lost; easy lost %d of 500 to a random player\n", games, lost, easyLost);
    check("tic tac toe: hard never loses", lost == 0 && games > 1000);
    check("tic tac toe: easy can be beaten", easyLost > 20);
  }
  // Battleship: legal fleets, the CPU always finishes and plays well
  {
    int bad = 0, most = 0; long shots = 0;
    for (int g = 0; g < 500; g++) {
      randomSeed(700 + g);
      bsPlace(bsTheirs);
      int cells = 0, len[6] = { 0 };
      for (int i = 0; i < 64; i++) {
        uint8_t sh = bsTheirs[i]; if (!sh) continue; cells++; len[sh]++;
        int x = i % 8, y = i / 8;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
          int X = x + dx, Y = y + dy;
          if (X >= 0 && Y >= 0 && X < 8 && Y < 8 && bsTheirs[Y * 8 + X] && bsTheirs[Y * 8 + X] != sh) bad++;
        }
      }
      if (cells != 14) bad++;
      for (int k = 0; k < 5; k++) if (len[k + 1] != BS_LEN[k]) bad++;
      memset(bsAiShot, 0, 64);
      int n = 0;
      while (!bsAllSunk(bsTheirs) && n < 64) { uint8_t c = bsAiPick(bsAiShot); uint8_t r = bsFire(bsTheirs, c); if (!r) bad++; bsMark(bsAiShot, c, r); n++; }
      if (!bsAllSunk(bsTheirs)) bad++;
      shots += n; if (n > most) most = n;
    }
    printf("      battleship: the CPU needs %.1f shots on average, at most %d\n", shots / 500.0, most);
    check("battleship: fleets legal (sizes, never touching), the CPU always finishes", bad == 0);
    check("battleship: the CPU plays well", shots / 500.0 < 48);
  }
  // Checkers: forced take, going on with a second jump, crowning, the CPU
  {
    Checkers c; c.begin();
    memset(c.sq, 0, sizeof(c.sq));
    c.sq[Checkers::at(5, 2)] = CK_MAN0; c.sq[Checkers::at(4, 3)] = CK_MAN1; c.sq[Checkers::at(2, 5)] = CK_MAN1;
    c.sq[Checkers::at(6, 7)] = CK_MAN0; c.sq[Checkers::at(0, 1)] = CK_MAN1;
    CkStep st[48]; uint8_t n = c.gen(st);
    check("checkers: taking is a must", n == 1 && st[0].jump && st[0].from == Checkers::at(5, 2));
    bool more = c.apply(st[0].from, st[0].dir);
    check("checkers: a piece that took goes on taking", more && c.chain == Checkers::at(3, 4) && c.turn == 0);
    more = c.apply((uint8_t)c.chain, 1);
    check("checkers: both taken, the turn passes", !more && c.count(1) == 1 && c.turn == 1);
    Checkers k; k.begin(); memset(k.sq, 0, sizeof(k.sq));
    k.sq[Checkers::at(1, 2)] = CK_MAN0; k.sq[Checkers::at(7, 0)] = CK_MAN1;
    k.apply(Checkers::at(1, 2), 1);
    check("checkers: the far row crowns", k.sq[Checkers::at(0, 3)] == CK_KING0);
    int deep = 0;
    for (int g = 0; g < 10; g++) {
      Checkers x; x.begin(); uint32_t r = 99 + g;
      while (!x.winner) {
        CkStep s2[48]; uint8_t n2 = x.gen(s2);
        CkStep s = x.turn == 0 ? x.best(3, r) : s2[bgRand(r) % n2];
        x.apply(s.from, s.dir);
      }
      if (x.winner == 1) deep++;
    }
    printf("      checkers: depth 3 won %d of 10 against a random player\n", deep);
    check("checkers: the CPU plays well", deep >= 9);
    // the game's own search (buffers on the heap) picks exactly what the rules' one picks
    int same = 0, tried = 0;
    for (int g = 0; g < 6; g++) {
      Checkers x; x.begin(); uint32_t r = 5 + g * 13;
      while (!x.winner && tried < 400) {
        uint32_t r1 = r, r2 = r;
        uint8_t d = (uint8_t)(1 + tried % 5);
        CkStep a = x.best(d, r1), b = ckBest(x, d, r2);
        tried++;
        if (a.from == b.from && a.dir == b.dir && r1 == r2) same++;
        CkStep s2[48]; uint8_t n2 = x.gen(s2);
        CkStep s = bgRand(r) % 3 ? a : s2[bgRand(r) % n2];
        x.apply(s.from, s.dir);
      }
    }
    printf("      checkers: heap search agrees in %d of %d positions\n", same, tried);
    check("checkers: the game's search plays as the rules' search", same == tried && tried > 100 && ckPly == NULL);
  }
  // Mau-Mau: no card gets lost, games end, same seed = same game, the 7
  {
    bool keep = true; int ended = 0;
    for (int g = 0; g < 200; g++) {
      MauMau x; x.begin(4242 + g * 31, 2 + g % 3);
      for (int k = 0; k < 2000 && !x.winner; k++) {
        x.move(x.cpuMove());
        int cnt = 0;
        for (int p = 0; p < x.players; p++) cnt += MauMau::count(x.hand[p]);
        if (cnt + x.nDeck + x.nPile != MM_CARDS) keep = false;
      }
      if (x.winner) ended++;
    }
    check("mau-mau: 32 cards all the time", keep);
    check("mau-mau: 200 CPU games all end", ended == 200);
    MauMau a, b; a.begin(99, 2); b.begin(99, 2);
    check("mau-mau: the same seed deals the same", !memcmp(a.hand, b.hand, sizeof(a.hand)) && a.top() == b.top());
    MauMau s; s.begin(1, 2);
    s.pile[s.nPile - 1] = 0;                                        // top: 7 of clubs
    s.hand[0] = (1UL << 8) | (1UL << 3);                            // 7 of spades, 10 of clubs
    s.move(8);
    check("mau-mau: a 7 makes the next one draw 2", s.penalty == 2 && s.turn == 1);
    uint8_t before = MauMau::count(s.hand[1]);
    s.move(MM_DRAW);
    check("mau-mau: ... and ends that turn", MauMau::count(s.hand[1]) == before + 2 && s.turn == 0 && !s.penalty);
  }
  // Minigolf: every hole can be played in par
  {
    bool ok = true;
    for (mgHole = 0; mgHole < 9; mgHole++) {
      int32_t x, y; GolfShot f;
      mgFind('S', &x, &y); mgFind('O', &mgHx, &mgHy);
      int n = golfPlan(x, y, &f);
      printf("      minigolf hole %d: par %d, best %d\n", mgHole + 1, MG_PAR[mgHole], n);
      if (n < 0 || n > MG_PAR[mgHole]) ok = false;
    }
    check("minigolf: every hole can be played in par", ok);
  }
}

// Mine 3D: digs a staircase down (looking steeply down), then a tunnel along the ore (looking down a bit)
static uint32_t m3BotActs = 0;
static void mine3dBot(uint32_t t) {
  if (botBusy(t) || !vox) return;
  bool deep = vox->py < 3 * 256;
  uint8_t want = deep ? 2 : 3;
  if (vox->look != want) { botPress(t, vox->look < want ? DN : UP, 40); botFree = t + 250; return; }
  if (deep && ++m3BotActs % 25 == 0) { botPress(t, RT, 260); return; }   // a bend now and then
  VoxHit h;
  if (voxTarget(&h) && voxGet(h.x, h.y, h.z) != VB_ROCK) { botPress(t, OK, 60); botFree = t + 450; return; }
  botPress(t, UP, 320);
}

// Mine 3D: rays, digging and building, walking and falling, the world - without the screen
static void voxFlat() {                                         // bedrock, three layers of stone, air
  memset(vox->world, 0, sizeof(vox->world));
  for (int z = 0; z < VOX_W; z++) for (int x = 0; x < VOX_W; x++) {
    vox->world[voxIdx(x, 0, z)] = VB_ROCK;
    for (int y = 1; y < 4; y++) vox->world[voxIdx(x, y, z)] = VB_STONE;
  }
  for (int z = 0; z < VOX_W; z++) for (int x = 0; x < VOX_W; x++) voxColumn(x, z);
}
static void voxPlayer(int32_t x, int32_t y, int32_t z, int16_t yaw, int16_t pitch) {
  vox->px = x; vox->py = y; vox->pz = z; vox->vy = 0; vox->yaw = yaw; vox->pitch = pitch; vox->ground = true;
}
static bool voxHitIs(const VoxHit &h, int x, int y, int z, int qx, int qy, int qz) {
  bool ok = h.x == x && h.y == y && h.z == z && h.qx == qx && h.qy == qy && h.qz == qz;
  if (!ok) printf("      hit %d %d %d front %d %d %d\n", h.x, h.y, h.z, h.qx, h.qy, h.qz);
  return ok;
}
static void mine3dLogic() {
  vox = (VoxState *)malloc(sizeof(VoxState));
  check("state on the heap", vox != NULL);
  printf("      state %u bytes\n", (unsigned)sizeof(VoxState));
  voxInit();
  VoxHit h;
  // rays
  voxFlat();
  voxSet(10, 4, 12, VB_STONE);
  check("ray straight ahead hits the block, front face", voxCast(10 * 256 + 128, 4 * 256 + 128, 8 * 256 + 128, 0, 0, 4096, VOX_MAXT, &h) && voxHitIs(h, 10, 4, 12, 10, 4, 11));
  check("ray down hits the ground's top", voxCast(5 * 256 + 128, 6 * 256 + 150, 5 * 256 + 128, 0, -4096, 0, VOX_MAXT, &h) && voxHitIs(h, 5, 3, 5, 5, 4, 5));
  voxSet(6, 4, 4, VB_STONE);
  check("slanted ray: the x face of the right block", voxCast(3 * 256 + 128, 4 * 256 + 128, 3 * 256 + 128, 4096, 0, 2048, VOX_MAXT, &h) && voxHitIs(h, 6, 4, 4, 5, 4, 4));
  voxSet(6, 4, 4, VB_AIR);
  voxSet(10, 4, 11, VB_WATER);
  check("water lets the ray through", voxCast(10 * 256 + 128, 4 * 256 + 128, 8 * 256 + 128, 0, 0, 4096, VOX_MAXT, &h) && voxHitIs(h, 10, 4, 12, 10, 4, 11));
  voxSet(10, 4, 11, VB_AIR); voxSet(10, 4, 12, VB_AIR); voxSet(10, 4, 15, VB_STONE);
  check("out of reach: 6.5 blocks", !voxCast(10 * 256 + 128, 4 * 256 + 128, 8 * 256 + 128, 0, 0, 4096, VOX_REACH, &h));
  check("... seen from 6.5 blocks", voxCast(10 * 256 + 128, 4 * 256 + 128, 8 * 256 + 128, 0, 0, 4096, VOX_MAXT, &h) && h.z == 15);
  voxSet(1, 4, 20, VB_STONE);
  check("the world wraps round", voxCast(31 * 256 + 128, 4 * 256 + 128, 20 * 256 + 128, 4096, 0, 0, VOX_MAXT, &h) && voxHitIs(h, 1, 4, 20, 0, 4, 20));
  voxSet(20, 5, 20, VB_LEAF);
  check("leaves stop the aim", voxCast(20 * 256 + 128, 5 * 256 + 128, 17 * 256 + 128, 0, 0, 4096, VOX_REACH, &h) && voxHitIs(h, 20, 5, 20, 20, 5, 19));
  check("up into the sky: nothing", !voxCast(20 * 256 + 128, 8 * 256, 20 * 256 + 128, 300, 4000, 0, VOX_MAXT, &h));
  // dig and build, looking along +x at eye height
  voxFlat();
  voxStart();
  voxPlayer(8 * 256 + 128, 4 * 256, 8 * 256 + 128, 0, 0);
  uint8_t got = 0;
  voxSet(10, 5, 8, VB_GOLD);
  check("the block in the middle of the view", voxTarget(&h) && voxHitIs(h, 10, 5, 8, 9, 5, 8));
  uint8_t r = voxAct(&got);
  check("gold dug: 5 points, counted", r == 1 && got == VB_GOLD && vox->score == 5 && vox->ores[1] == 1 && !voxGet(10, 5, 8) && vox->dug == 1);
  voxSet(10, 5, 8, VB_STONE);
  r = voxAct(&got);
  check("stone dug into the bag", r == 1 && vox->bag[2] == 1 && vox->score == 5 && !voxGet(10, 5, 8));
  voxSet(10, 5, 8, VB_GRASS); voxAct(&got);
  check("grass gives dirt", vox->bag[1] == 1);
  voxSet(10, 5, 8, VB_ROCK);
  check("bedrock stays", voxAct(&got) == 3 && voxGet(10, 5, 8) == VB_ROCK);
  voxSet(10, 5, 8, VB_AIR);
  voxSet(11, 5, 8, VB_DIRT);
  vox->sel = 2;
  r = voxAct(&got);
  check("stone put on the face aimed at", r == 2 && voxGet(10, 5, 8) == VB_STONE && vox->bag[2] == 0 && vox->placed == 1);
  check("an empty slot puts nothing", voxAct(&got) == 3 && vox->placed == 1);
  vox->sel = 1;                                                  // dirt under the own feet: lifted
  voxPlayer(8 * 256 + 128, 4 * 256, 8 * 256 + 128, 0, -256);
  check("looking down at the ground", voxTarget(&h) && voxHitIs(h, 8, 3, 8, 8, 4, 8));
  r = voxAct(&got);
  check("a block under the feet lifts the player", r == 2 && voxGet(8, 4, 8) == VB_DIRT && vox->py == 5 * 256 && !voxBoxHit(vox->px, vox->py, vox->pz));
  vox->bag[1] = 1;                                               // no room above the head: refused
  voxSet(8, 4, 8, VB_AIR); voxSet(8, 6, 8, VB_STONE);
  voxPlayer(8 * 256 + 128, 4 * 256, 8 * 256 + 128, 0, -256);
  check("no lift under a roof", voxAct(&got) == 3 && vox->py == 4 * 256 && voxGet(8, 4, 8) == VB_AIR);
  // walking, falling, steps, water
  voxFlat();
  voxPlayer(8 * 256 + 128, 9 * 256, 8 * 256 + 128, 0, 0);
  vox->ground = false;
  for (int i = 0; i < 80; i++) voxTick(0);
  check("falls and lands on the ground", vox->py == 4 * 256 && vox->ground && vox->vy == 0);
  for (int z = 0; z < VOX_W; z++) { voxSet(11, 4, z, VB_STONE); voxSet(11, 5, z, VB_STONE); }
  for (int i = 0; i < 60; i++) voxTick(1);
  printf("      at the wall: x %.2f\n", vox->px / 256.0);
  check("a wall two blocks high stops the walk", vox->py == 4 * 256 && vox->px > 11 * 256 - VOX_HW - 26 && vox->px < 11 * 256 - VOX_HW);
  for (int z = 0; z < VOX_W; z++) { voxSet(11, 5, z, VB_AIR); for (int x = 12; x < 16; x++) voxSet(x, 4, z, VB_STONE); }
  for (int i = 0; i < 60; i++) voxTick(1);
  printf("      over the step: x %.2f y %.2f\n", vox->px / 256.0, vox->py / 256.0);
  check("a step of one block is jumped", vox->py == 5 * 256 && vox->px > 12 * 256);
  voxPlayer(31 * 256 + 200, 4 * 256, 3 * 256 + 128, 0, 0);
  for (int i = 0; i < 6; i++) voxTick(1);
  check("walks round the edge of the world", vox->px < 256 && vox->py == 4 * 256);
  voxPlayer(20 * 256 + 128, 4 * 256, 20 * 256 + 128, 512, 0);
  for (int i = 0; i < 20; i++) voxTick(-1);
  check("DOWN walks back", vox->px > 21 * 256);
  for (int z = 0; z < VOX_W; z++) for (int x = 0; x < VOX_W; x++) for (int y = 4; y < 8; y++) voxSet(x, y, z, VB_WATER);
  voxPlayer(5 * 256 + 128, 4 * 256, 5 * 256 + 128, 0, 0);
  for (int i = 0; i < 40; i++) voxTick(1);
  check("UP swims up in water", vox->py > 5 * 256);
  // the world from seeds
  int bad = 0, cnt[13] = { 0 };
  for (uint32_t seed = 1; seed <= 20; seed++) {
    voxGenerate(seed * 7919);
    voxStart();
    for (int i = 0; i < VOX_N; i++) cnt[vox->world[i]]++;
    for (int z = 0; z < VOX_W; z++) for (int x = 0; x < VOX_W; x++) {
      if (vox->world[voxIdx(x, 0, z)] != VB_ROCK) bad++;
      uint8_t t = vox->top[z * VOX_W + x];
      if (t < 1 || t > VOX_H || (t < VOX_H && voxSolid(x, t, z)) || !voxSolid(x, t - 1, z)) bad++;
    }
    if (voxBoxHit(vox->px, vox->py, vox->pz) || !voxBoxHit(vox->px, vox->py - 1, vox->pz)) { bad++; printf("      seed %u: spawn not on the ground\n", seed); }
  }
  printf("      20 worlds: coal %d gold %d crystal %d wood %d leaves %d water %d sand %d flowers %d air %d\n", cnt[VB_COAL] / 20,
         cnt[VB_GOLD] / 20, cnt[VB_GEM] / 20, cnt[VB_WOOD] / 20, cnt[VB_LEAF] / 20, cnt[VB_WATER] / 20, cnt[VB_SAND] / 20, cnt[VB_BLOOM] / 20, cnt[0] / 20);
  check("worlds: bedrock floor, heights right, spawn on the ground", bad == 0);
  check("worlds: every kind of block", cnt[VB_COAL] > 20 * 40 && cnt[VB_GOLD] > 20 * 10 && cnt[VB_GEM] > 20 * 5 && cnt[VB_WOOD] > 20 * 12 &&
        cnt[VB_LEAF] > 20 * 60 && cnt[VB_WATER] > 0 && cnt[VB_SAND] > 0 && cnt[VB_BLOOM] > 0 && cnt[VB_GRASS] > 0 && cnt[VB_DIRT] > 0);
  static uint8_t a[VOX_N];
  voxGenerate(4242); memcpy(a, vox->world, VOX_N); voxGenerate(4243);
  bool differ = memcmp(a, vox->world, VOX_N) != 0;
  voxGenerate(4242);
  check("one seed, one world; another seed, another", differ && !memcmp(a, vox->world, VOX_N));
  // the cost of a picture: DDA steps per frame at the start, all round
  voxStart();
  voxSteps = voxRays = voxFrames = voxStepsMax = 0;
  for (int k = 0; k < 16; k++) for (int p = 0; p < 4; p++) voxRender(vox->px, voxEyeY(), vox->pz, (int16_t)(k * 64), VOX_PITCH[p], -1, 0);
  printf("      64 views at the start: %u DDA steps per frame (max %u), %u rays\n", voxSteps / voxFrames, voxStepsMax, voxRays / voxFrames);
  check("a picture costs less than 30 steps per ray", voxStepsMax < 30u * VOX_RW * VOX_RH);
  free(vox);
  vox = NULL;
}

int main(int argc, char **argv) {
  scenario = argc > 1 ? argv[1] : "menu";
  srand(1);
  if (scenario.find("fair") != std::string::npos || scenario == "frogroll") {
    printf("== %s\n", scenario.c_str());
    int r = logicTest();
    if (r >= 0) return r;
  }
  // a console that was set up before: four keys, a buzzer, a battery
  simNvsU16["pset"] = 3; simNvsU16["sset"] = 1; simNvsU16["snd"] = SND; simNvsU16["bat"] = BAT;
  for (int i = 0; i < 4; i++) { simNvsU16["p" + std::to_string(i)] = KEYS[i]; simNvsU16["a" + std::to_string(i)] = 1; }
  simNvsU16["p4"] = 255;
  const uint32_t M = 1600;                                      // the menu is up by then
  if (scenario == "menu") {
    simEnd = 6000; captureAt = { 60 };
    for (int i = 0; i < 6; i++) press(M + 300 * i, DN);
  } else if (scenario == "snake") {
    uint32_t t = openGameNamed("Snake", M); simEnd = t + 30000;
    press(t + 200, UP); press(t + 900, RT); press(t + 1500, DN); press(t + 2400, LT);
    autoplay = true; autoplayFrom = t + 3000; captureAt = { 160, 400 };
  } else if (scenario == "snakeover") {
    uint32_t t = openGameNamed("Snake", M); simEnd = t + 9000;
    press(t + 200, RT);                                          // straight into the wall
    setenv("GRAB", "GAME OVER", 1);
  } else if (scenario == "tetris") {
    uint32_t t = openGameNamed("Tetris", M); simEnd = t + 60000;
    autoplay = true; autoplayFrom = t + 300; captureAt = { 400, 1400 };
  } else if (scenario == "pong") {
    uint32_t t = openGameNamed("Pong", M); simEnd = t + 40000;
    autoplay = true; autoplayFrom = t + 300; captureAt = { 300 };
  } else if (scenario == "flappy") {
    uint32_t t = openGameNamed("Flappy", M); simEnd = t + 20000;
    for (uint32_t k = t + 300; k < t + 20000; k += 330) press(k, UP, 40);
    captureAt = { 150 };
  } else if (scenario == "breakout") {
    uint32_t t = openGameNamed("Breakout", M); simEnd = t + 40000;
    press(t + 300, UP);
    autoplay = true; autoplayFrom = t + 600; captureAt = { 400 };
  } else if (scenario == "invaders") {
    uint32_t t = openGameNamed("Invaders", M); simEnd = t + 90000;
    autoplay = true; autoplayFrom = t + 300; captureAt = { 400 };
    setenv("GRAB", "OK again - hold OK menu", 1);                // and the game over screen
  } else if (scenario == "rocks") {
    uint32_t t = openGameNamed("Rocks", M); simEnd = t + 90000;
    press(t + 300, UP, 300);
    autoplay = true; autoplayFrom = t + 700; captureAt = { 420 };
    setenv("GRAB", "OK again - hold OK menu", 1);
  } else if (scenario == "shooter") {
    uint32_t t = openGameNamed("Shooter", M); simEnd = t + 90000;
    press(t + 4000, OK, 80);                                     // a bomb
    autoplay = true; autoplayFrom = t + 300; captureAt = { 900 };
    setenv("GRAB", "OK again - hold OK menu", 1);
  } else if (scenario == "lander") {
    uint32_t t = openGameNamed("Lander", M); simEnd = t + 30000;
    for (uint32_t k = t + 600; k < t + 2400; k += 300) press(k, UP, 150);   // brake a little, then fall
    captureAt = { 285 };
    setenv("GRAB", "OK again - hold OK menu", 1);
  } else if (scenario == "cave") {
    uint32_t t = openGameNamed("Cave", M); simEnd = t + 30000;
    captureAt = { 290 };                                         // in flight
    for (uint32_t k = t + 1200; k < t + 4000; k += 200) press(k, UP, 100);
    autoplay = true; autoplayFrom = t + 4000;
    setenv("GRAB", "OK again - hold OK menu", 1);
  } else if (scenario == "doom") {
    uint32_t t = openGameNamed("Doom", M); simEnd = t + 150000;
    press(t + 300, RT, 700); press(t + 1100, UP, 1000);
    for (uint32_t k = t + 2200; k < simEnd; k += 1700) press(k, OK, 80);   // fire now and then
    autoplay = true; autoplayFrom = t + 2300; captureAt = { 245, 1100 };
    setenv("GRAB", "best ", 1);                                  // the game over box
  } else if (scenario == "mine") {
    uint32_t t = openGameNamed("Mine", M); simEnd = t + 20000;
    press(t + 300, RT, 900); press(t + 1400, OK, 80); press(t + 1900, DN); press(t + 2400, UP);
    for (uint32_t k = t + 3000; k < simEnd; k += 1100) press(k, OK, 80);
    autoplay = true; autoplayFrom = t + 3000; captureAt = { 300, 900 };
  } else if (scenario == "mine3d") {
    uint32_t t = openGameNamed("Mine 3D", M); simEnd = t + 186000;
    press(t + 300, UP, 1300);                                    // walk
    press(t + 1800, RT, 450);                                    // turn
    press(t + 2500, DN, 40); press(t + 2900, DN, 40);            // look down, steep down
    for (uint32_t k = t + 3300; k < t + 5000; k += 650) press(k, OK, 60);   // dig
    press(t + 5400, OK, 60); press(t + 5560, OK, 60);            // double OK: the bag
    press(t + 6000, RT); press(t + 6300, RT); press(t + 6700, OK, 60);      // stone, take it
    press(t + 7000, OK, 60); press(t + 7160, OK, 60);            // the bag again
    press(t + 7500, LT); press(t + 7800, OK, 60);                // dirt
    press(t + 8100, UP, 40);                                     // look down less
    press(t + 8400, OK, 60); press(t + 9000, OK, 60);            // build
    press(t + 9700, OK, 60); press(t + 9860, OK, 60); press(t + 10200, LT); press(t + 10500, OK, 60);   // the pick again
    press(t + 10900, UP, 40);                                    // look level
    press(t + 11300, LT, 900); press(t + 12400, UP, 2000);       // turn, walk on
    simBot = mine3dBot; botFrom = t + 14600;                     // then dig for ore
    captureMs = { t + 6100, t + 9600, t + 12000, t + 20000, t + 60000 };   // the bag, building, the view, a pit, ore
    setenv("GRAB", "TIME UP", 1);
  } else if (scenario == "tunnel") {
    uint32_t t = openGameNamed("Tunnel 3D", M); simEnd = t + 90000;
    autoplay = true; autoplayFrom = t + 300; captureAt = { 280 };
    setenv("GRAB", "best ", 1);
  } else if (scenario == "pacman") {
    uint32_t t = openGameNamed("Pac-Man", M); simEnd = t + 150000;
    press(t + 1500, LT);
    autoplay = true; autoplayFrom = t + 2000; captureAt = { 300, 800 };
    setenv("GRAB", "best ", 1);
  } else if (scenario == "bomber") {
    uint32_t t = openGameNamed("Bomberman", M); simEnd = t + 120000;
    press(t + 300, DN); press(t + 600, DN); press(t + 1000, OK, 80);   // vs 3 CPUs
    for (uint32_t k = t + 2000; k < simEnd; k += 2300) press(k, OK, 80);   // a bomb now and then
    autoplay = true; autoplayFrom = t + 1500; captureAt = { 330, 450 };
    setenv("GRAB", "best ", 1);
  } else if (scenario == "dino") {
    uint32_t t = openGameNamed("Dino", M); simEnd = t + 40000;
    autoplay = true; autoplayFrom = t + 300;
    captureMs = { t + 1200, t + 1700 };
    setenv("GRAB", "best ", 1);                              // the game over panel
  } else if (scenario == "racer") {
    uint32_t t = openGameNamed("Racer", M); simEnd = t + 60000;
    press(t + 2500, RT, 100);                                    // then straight on until a car comes
    captureMs = { t + 3000, t + 7000 };
    setenv("GRAB", "best ", 1);                              // the game over panel
  } else if (scenario == "frogger") {
    uint32_t t = openGameNamed("Frogger", M); simEnd = t + 40000;
    press(t + 400, OK, 80);                                      // easy
    press(t + 1200, UP); press(t + 1600, UP); press(t + 2000, UP);
    autoplay = true; autoplayFrom = t + 3000; captureMs = { t + 1100, t + 2300 };
    setenv("GRAB", "best ", 1);                              // the game over panel
  } else if (scenario == "jump") {
    uint32_t t = openGameNamed("Jump & Run", M); simEnd = t + 60000;
    for (uint32_t k = t + 300; k < t + 10000; k += 700) {        // run right, jumping now and then
      script.push_back({ k, RT }); script.push_back({ k + 450, RT | UP });
    }
    script.push_back({ t + 10000, RT });                         // then just run: into the gaps
    captureMs = { t + 2500, t + 5000 };
    setenv("GRAB", "best ", 1);                              // the game over panel
  } else if (scenario == "stack") {
    uint32_t t = openGameNamed("Stack", M); simEnd = t + 60000;
    for (uint32_t k = t + 700; k < t + 60000; k += 730) press(k, DN, 50);
    captureMs = { t + 1600, t + 2600 };
    setenv("GRAB", "best ", 1);                              // the game over panel
  } else if (scenario == "settings") {
    simEnd = M + 3000; press(M, UP); press(M + 400, OK, 80); captureAt = { 200 };
    setenv("GRAB", "SETTINGS", 1);
  } else if (scenario == "pause") {
    uint32_t t = openGameNamed("Snake", M); simEnd = t + 4000;
    press(t + 1000, OK, 900);                                    // hold OK: pause menu
    setenv("GRAB", "PAUSE", 1);
  } else if (scenario == "wizard") {
    simNvsU16.erase("pset");                                     // brand new board: learn the keys
    simEnd = 4000; captureAt = { 40 };
  } else if (scenario == "ladder") {
    // UP, DOWN, LEFT on one pin through resistors, RIGHT on its own pin
    simNvsU16.erase("pset"); LADDER = 2;
    press(2500, 1 << 4, 400); press(3300, 2 << 4, 400); press(4100, 3 << 4, 400); press(4900, RT, 400);
    press(17500, (3 << 4) | RT, 200);                            // OK = LEFT + RIGHT
    simEnd = 19500; captureAt = { 330 };
  } else if (scenario == "ladder4") {
    // all four arrows on one pin, only the pin's own pull-up; OK = UP+DOWN together
    simNvsU16.erase("pset"); LADDER = 2; LADDER_MV = LADDER_INT;
    press(2500, 1 << 4, 400); press(3300, 2 << 4, 400); press(4100, 3 << 4, 400); press(4900, 4 << 4, 400);
    script.push_back({ 5700, 1 << 4 }); script.push_back({ 5720, 5 << 4 }); script.push_back({ 6100, 0 });   // OK: UP, then both
    press(10500, 2 << 4, 120);                                   // DOWN: Snake -> Tetris
    script.push_back({ 11500, 1 << 4 }); script.push_back({ 11520, 5 << 4 }); script.push_back({ 11700, 0 });
    simEnd = 13500;
  } else if (scenario == "ladder5") {
    // five keys on one pin, then the sound setup asks: OK while it beeps
    simNvsU16.erase("pset"); simNvsU16.erase("sset"); LADDER = 2; LADDER_MV = LADDER_5;
    press(2500, 1 << 4, 400); press(3300, 2 << 4, 400); press(4100, 3 << 4, 400); press(4900, 4 << 4, 400);
    press(5700, 5 << 4, 400);
    press(10500, 5 << 4, 200);                                   // OK: yes, I hear it
    simEnd = 12500;
  } else if (scenario == "plain2") {
    // two keys on GPIO6, which has no ADC: the wizard says so
    simNvsU16.erase("pset");
    press(2500, RT, 400); press(3300, RT, 600);
    simEnd = 4500;
  } else if (scenario == "tilt") {
    // settings -> tilt control on: sensor found, axes learned; level saved;
    // then Snake: the pause menu offers a new level, tilting steers
    simGyro = true; simNvsU16["snd"] = 255;                      // GPIO7 is the gyroscope's here
    press(M, UP); press(M + 400, OK, 80);
    for (int i = 0; i < 7; i++) press(M + 800 + 250 * i, DN);
    press(M + 2900, RT);
    accScript = { { M + 5000, 8000, 0 }, { M + 6000, 0, 0 }, { M + 7500, 0, -8000 }, { M + 8500, 0, 0 },
                  { M + 20000, 0, -8000 } };                     // the last one: tilt up in the game
    press(M + 9800, DN); press(M + 10200, OK, 80);               // "tilt level": save it
    press(M + 15200, OK, 900);                                   // leave the settings
    press(M + 16500, DN); press(M + 16900, OK, 80);              // Snake
    press(M + 18000, OK, 900);                                   // pause ...
    press(M + 19500, OK, 80);                                    // ... continue
    simEnd = M + 21000;
    setenv("GRAB", "TILT SETUP", 1);
  } else if (scenario == "g2048") {
    uint32_t t = openGameNamed("2048", M); simEnd = t + 150000;
    autoplay = true; autoplayFrom = t + 300; captureMs = { t + 6000, t + 6070 };
    setenv("GRAB", "NO MOVES", 1);
  } else if (scenario == "mines") {
    uint32_t t = openGameNamed("Minesweeper", M); simEnd = t + 120000;
    press(t + 300, OK, 80);                                      // easy
    pzStart = t + 1000; pzBot = minesBot; captureMs = { t + 9000 };
    setenv("GRAB", "CLEARED!", 1);
  } else if (scenario == "sokoban") {
    uint32_t t = openGameNamed("Sokoban", M); simEnd = t + 9000;
    captureMs = { t + 500 };
    press(t + 700, OK, 80);                                      // level 1
    t += 1300;
    press(t, LT); press(t + 250, OK, 80);                        // a step, taken back
    static const uint8_t SOL[] = { UP, LT, LT, LT, DN, RT, UP, RT, DN, UP, LT, LT, LT, UP, LT, LT, DN, RT, RT };
    t += 600;
    for (uint8_t k = 0; k < sizeof(SOL); k++, t += 200) press(t, SOL[k], 50);
    captureMs.push_back(t - 1430);
    setenv("GRAB", "SOLVED!", 1);
  } else if (scenario == "sudoku") {
    uint32_t t = openGameNamed("Sudoku", M); simEnd = t + 120000;
    press(t + 300, OK, 80);                                      // easy
    pzStart = t + 1200; pzBot = sudokuBot; captureMs = { t + 6000 };
    setenv("GRAB", "SOLVED", 1);
  } else if (scenario == "lightsout") {
    uint32_t t = openGameNamed("Lights Out", M); simEnd = t + 80000;
    pzStart = t + 600; loBotStop = t + 40000; pzBot = lightsBot; captureMs = { t + 5000 };
  } else if (scenario == "match3") {
    uint32_t t = openGameNamed("Match 3", M); simEnd = t + 150000;
    pzStart = t + 900; pzBot = match3Bot; captureMs = { t + 6000 };
    setenv("GRAB", "END", 1);
  } else if (pzLogic()) {
    printf("== %s\n", scenario.c_str());
    printf("%s\n", failures ? "### FAILURES ###" : "   all checks passed");
    return failures ? 1 : 0;
  } else if (scenario == "boardlogic") {                        // the rules alone, no screen
    printf("== %s\n", scenario.c_str());
    boardLogic();
    return failures ? 1 : 0;
  } else if (scenario == "c4") {
    uint32_t t = openGameNamed("4 wins", M); simEnd = t + 90000;
    press(t + 400, DN); press(t + 800, OK, 80);                  // "1P normal"
    for (uint32_t k = t + 1500, i = 0; k < t + 88000; k += 1500, i++) {   // wander over the columns, drop
      uint8_t dir = i % 3 == 0 ? LT : RT;
      press(k, dir);
      if (i % 2) press(k + 200, dir);
      press(k + 500, OK, 80);
    }
    captureMs = { t + 6700 };
    setenv("GRAB", "CPU WINS", 1);
  } else if (scenario == "ttt") {
    uint32_t t = openGameNamed("Tic Tac Toe", M); simEnd = t + 60000;
    press(t + 400, OK, 80);                                      // "1P easy"
    for (uint32_t k = t + 1000; k < t + 58000; k += 400) {       // wander and place
      static const uint8_t keys[4] = { UP, DN, LT, RT };
      press(k, keys[rand() % 4]);
      press(k + 180, OK, 80);
    }
    captureMs = { t + 3000 };
    setenv("GRAB", "WIN", 1);
  } else if (scenario == "bship") {
    uint32_t t = openGameNamed("Battleship", M); simEnd = t + 200000;
    press(t + 500, UP);                                          // mix a new fleet
    press(t + 1000, OK, 80);                                     // and go
    simBot = bshipBot; botFrom = t + 1600;
    captureMs = { t + 400, t + 40000 };
  } else if (scenario == "bshipown") {
    // ships by hand: 4, 3, 3, 2 down the left side, the last one turned (double OK) on the right
    uint32_t t = openGameNamed("Battleship", M); simEnd = t + 12000;
    press(t + 500, DN);
    uint32_t k = t + 1000;
    for (int s = 0; s < 4; s++) {
      if (s) { press(k, DN); press(k + 200, DN); k += 400; }
      press(k, OK, 60); k += 700;
    }
    for (int i = 0; i < 5; i++, k += 200) press(k, RT);
    press(k, OK, 50); press(k + 150, OK, 50);                     // double OK: turn it
    press(k + 800, OK, 60);                                      // set it down
    captureMs = { k + 500 };
    press(k + 2000, OK, 80);                                     // start the battle
  } else if (scenario == "checkers") {
    uint32_t t = openGameNamed("Checkers", M); simEnd = t + 240000;
    press(t + 400, OK, 80);                                      // "1P easy"
    simBot = checkersBot; botFrom = t + 1000;
    captureMs = { t + 30000 };
    setenv("GRAB", "thinking...", 1);
  } else if (scenario == "maumau") {
    uint32_t t = openGameNamed("Mau-Mau", M); simEnd = t + 120000;
    press(t + 400, DN); press(t + 700, DN); press(t + 1000, OK, 80);   // "vs 3 CPUs"
    simBot = maumauBot; botFrom = t + 1600;
    captureMs = { t + 9000 };
    setenv("GRAB", "WISH A SUIT", 1);
  } else if (scenario == "minigolf") {
    uint32_t t = openGameNamed("Minigolf", M); simEnd = t + 300000;
    simBot = golfBot; botFrom = t + 800;
    captureMs = { t + 2500, t + 60000 };
    setenv("GRAB", "HOLE 6/9", 1);
  } else if (scenario == "chess") {
    uint32_t t = openGameNamed("Chess", M); simEnd = t + 20000;
    press(t + 300, DN); press(t + 600, OK, 80);                  // board stays
    pzStart = t + 1400; pzBot = chessBot; captureMs = { t + 5000 };
    setenv("GRAB", "WINS", 1);
  } else if (scenario == "mine3dlogic") {
    printf("== %s\n", scenario.c_str());
    mine3dLogic();
    return failures ? 1 : 0;
  } else if (scenario == "chesslogic") {
    printf("== %s\n", scenario.c_str());
    chessLogic();
    return failures ? 1 : 0;
  } else { printf("unknown scenario %s\n", scenario.c_str()); return 2; }
  printf("== %s\n", scenario.c_str());
  try {
    setup();
    for (;;) loop();
  } catch (SimEnd &) {}

  check("frames were drawn", frames > 20);
  check("the picture uses many colours", colsInFrame() >= 6);
  if (scenario == "menu")      check("menu lists the games", seen("Snake") && seen("Tetris") && seen("Doom"));
  if (scenario == "snake")     check("snake ran", seen("SNAKE"));
  if (scenario == "snakeover") check("game over shown", seen("GAME OVER") || seen("NEW RECORD!"));
  if (scenario == "tetris")    check("tetris ran", seen("TETRIS") && seen("NEXT"));
  if (scenario == "pong")      check("pong ran", seen("PONG"));
  if (scenario == "flappy")    check("flappy ran", seen("FLAPPY"));
  if (scenario == "breakout")  check("breakout ran", seen("BREAKOUT"));
  if (scenario == "pause")     check("pause menu shown", seen("PAUSE"));
  bool overShown = seen("GAME OVER") || seen("NEW RECORD!");
  if (scenario == "invaders")  { check("invaders ran", seen("INVADERS") && seen("WAVE 1")); check("invaders: game over reached", overShown); }
  if (scenario == "rocks")     { check("rocks ran", seen("ROCKS")); check("rocks: game over reached", overShown); }
  if (scenario == "shooter")   { check("shooter ran", seen("SHOOTER") && seen("BOMBS")); check("shooter: game over reached", overShown); }
  if (scenario == "lander")    { check("lander ran", seen("LANDER") && seen("FUEL")); check("lander: crash and game over", seen("CRASH") && overShown); }
  if (scenario == "cave")      { check("cave ran", seen("CAVE") && seen("hold UP to fly")); check("cave: game over reached", overShown); }
  if (scenario == "doom")      check("doom ran to game over", seen("DOOM") && seen("HEALTH") && overShown);
  if (scenario == "mine")      check("mine ran, blocks dug", seen("MINE") && seen("BAG 1"));
  if (scenario == "mine3d") {
    printf("      dug %d, put %d, best %u (y %.1f)\n", mine3dDug, mine3dPut, simNvsU16["hMN3"], vox->py / 256.0);
    printf("      %u frames drawn: %u DDA steps per frame on average, %u at most, %u rays\n", voxFrames, voxSteps / voxFrames, voxStepsMax, voxRays / voxFrames);
    printf("      colours in one picture: up to %d\n", mine3dCols);
    check("mine 3d ran, blocks dug and put", seen("MINE 3D") && mine3dDug >= 3 && mine3dPut >= 1);
    check("the bag was opened", seen("< stone >   UP+DOWN = take") && seen("< dirt >   UP+DOWN = take"));
    check("the picture uses many colours", mine3dCols >= 40);
    check("the time ran out, game over", seen("TIME UP") && overShown);
    check("ore found: points kept as the best", simNvsU16["hMN3"] > 0);
  }
  if (scenario == "tunnel")    check("tunnel ran to game over", seen("TUNNEL") && seen("SPEED") && overShown);
  if (scenario == "pacman")    check("pac-man ran to game over", seen("PAC-MAN") && seen("READY!") && overShown);
  if (scenario == "bomber")    check("bomberman ran to its end", seen("BOMBERMAN") && seen("BOMBER") && overShown &&
                                     (seen("CAUGHT") || seen("YOU WIN") || seen("TIME UP")));
  auto over = []() { return seen("GAME OVER") || seen("NEW RECORD!"); };
  if (scenario == "g2048") {
    printf("   best stored %u\n", simNvsU16["h2K4"]);
    check("2048 ran", seen("BEST TILE") && seen("MOVES"));
    check("no moves left ends it, score kept", seen("NO MOVES") && over() && simNvsU16["h2K4"] > 0);
  }
  if (scenario == "mines") {
    printf("   best stored %u\n", simNvsU16["hMSW"]);
    check("minesweeper ran", seen("MINESWEEPER") && seen("easy - 20 mines"));
    check("double tap set a flag", msBotFlagged && seen("019"));
    check("field cleared, points for the time", seen("CLEARED!") && over() && simNvsU16["hMSW"] > 0);
  }
  if (scenario == "sokoban") {
    printf("   levels solved %u, last level %u\n", simNvsU16["hSOK"], simNvsU16["SOKlv"]);
    check("level list shown", seen("LEVEL 1 / 12"));
    check("level 1 solved (with an undo), progress saved", seen("SOLVED!") && simNvsU16["hSOK"] == 1);
    check("on to level 2", seen("LEVEL 2") && simNvsU16["SOKlv"] == 1);
  }
  if (scenario == "sudoku") {
    printf("   best stored %u\n", simNvsU16["hSUD"]);
    check("sudoku drawn with the number pad", seen("SUDOKU") && seen("OK = write") && seen("OK = place it"));
    check("mistakes counted, puzzle solved", suBotWrong == 0 && seen("SOLVED") && simNvsU16["hSUD"] > 0);
  }
  if (scenario == "lightsout") {
    printf("   best stored %u\n", simNvsU16["hLGT"]);
    check("lights out ran", seen("LIGHTS OUT") && seen("MOVES"));
    check("levels solved", seen("DONE!") && seen("4"));
    check("the move limit ends it", seen("LIMIT") && over() && simNvsU16["hLGT"] > 0);
  }
  if (scenario == "match3") {
    printf("   best stored %u\n", simNvsU16["hM3X"]);
    check("board and moves shown", seen("MATCH 3") && seen("MOVES"));
    check("chains happen", seen("x2"));
    check("game ends when the moves are used up", seen("END") && over() && simNvsU16["hM3X"] > 0);
  }
  if (scenario == "dino")      check("dino ran and ended", seen("DINO") && overShown);
  if (scenario == "racer")     check("racer ran and ended", seen("RACER") && overShown);
  if (scenario == "frogger")   check("frogger: level chosen, ran and ended", seen("easy") && seen("FROGGER") && seen("LEVEL 1") && overShown);
  if (scenario == "jump")      check("jump & run ran and ended", seen("JUMP") && overShown);
  if (scenario == "stack")     check("stack ran and ended", seen("STACK") && overShown);
  if (scenario == "c4") {
    check("4 wins ran, levels offered", seen("4 WINS") && seen("1P normal") && seen("2 players") && seen("OK=drop"));
    check("the CPU takes its turn", seen("thinking..."));
    check("a game ended with game overShown", (seen("CPU WINS") || seen("YOU WIN!")) && overShown);
  }
  if (scenario == "ttt") {
    check("tic tac toe ran, levels offered", seen("TIC TAC TOE") && seen("1P hard") && seen("2 players"));
    check("rounds ended", seen("YOU WIN") || seen("CPU WINS") || seen("DRAW"));
    check("the CPU lost at least once to key mashing", seen("YOU WIN") || seen("DRAW"));
    check("game overShown after a loss", seen("CPU WINS") && overShown);
  }
  if (scenario == "bship") {
    printf("      shots %u, own fleet sunk %d\n", bsV.shots, bsAllSunk(bsMine));
    check("battleship ran", seen("BATTLESHIP") && seen("UP = mix") && seen("your fleet"));
    check("shots hit and missed", seen("hit!") && seen("miss") && seen("sunk!"));
    check("a battle ended with game overShown", (seen("YOU WIN!") || seen("CPU WINS")) && overShown);
  }
  if (scenario == "bshipown") {
    int cells = 0;
    for (int i = 0; i < 64; i++) if (bsMine[i]) cells++;
    check("five ships placed by hand", cells == 14 && seen("ship 2") && seen("2x OK = turn"));
    check("the first ones as set", bsMine[0] == 1 && bsMine[3] == 1 && bsMine[16] == 2 && bsMine[32] == 3 && bsMine[48] == 4);
    check("the last one turned upright", bsMine[6 * 8 + 5] == 5 && bsMine[7 * 8 + 5] == 5);
    check("the battle started", seen("your go"));
  }
  if (scenario == "checkers") {
    printf("      pieces %u : %u, winner %u, score %u\n", ck.count(0), ck.count(1), ck.winner, ckV.score);
    check("checkers ran, levels offered", seen("CHECKERS") && seen("1P hard") && seen("your go"));
    check("the bot (depth 3) beat easy", seen("YOU WIN") && overShown);
  }
  if (scenario == "maumau") {
    check("mau-mau ran with three CPUs", seen("MAU-MAU") && seen("CPU3 5") && seen("DRAW"));
    check("cards were played and drawn", seen("YOU: draw") && seen("CPU1: draw"));
    check("a game ended with game overShown", (seen("MAU-MAU!") || seen("YOU LOST")) && overShown);
  }
  if (scenario == "minigolf") {
    printf("      score %u, card %u %u %u %u %u %u %u %u %u\n", mgScore, mgCard[0], mgCard[1], mgCard[2], mgCard[3],
           mgCard[4], mgCard[5], mgCard[6], mgCard[7], mgCard[8]);
    check("minigolf ran", seen("MINIGOLF") && seen("HOLE 1/9") && seen("POWER"));
    check("all nine holes played", seen("HOLE 9/9") && overShown);
    check("the bot played well", mgScore >= 9 * 40);
  }
  if (scenario == "chess") {
    check("chess drawn for two players", seen("CHESS") && seen("2 players") && seen("WHITE") && seen("BLACK"));
    check("fool's mate: black wins", chBotStep == 8 && seen("WINS") && seen("BLACK"));
  }
  if (scenario == "wizard")    check("key wizard shown", frames > 20);
  if (scenario == "tilt") {
    printf("   tilt: on %d ready %d axes %02x %02x pins %d/%d\n", cfgTilt, gyroReady, tiltAx[0], tiltAx[1], gyroSda, gyroScl);
    check("sensor found on GPIO20 / GPIO7", gyroReady && gyroSda == 20 && gyroScl == 7);
    check("right = x, away = -y", tiltAx[0] == 0x00 && tiltAx[1] == 0x81);
    check("tilt control switched on", cfgTilt);
    check("level saved in the settings", seen("LEVEL SAVED") && cfgTiltFix);
    check("pause menu offers a new level", seen("tilt: level here"));
    check("tilting up presses UP in the game", seen("SNAKE") && tiltKey[B_UP] && !tiltKey[B_DOWN]);
  }
  if (scenario == "ladder5") {
    printf("   OK key %u/%u, buzzer %u\n", BTN_PIN[4], BTN_MV[4], simNvsU16["snd"]);
    check("own OK key learned on the ladder", BTN_PIN[4] == 2 && BTN_MV[4] > 780);
    check("sound setup shows the own OK key", seen("OK = yes"));
    check("OK while beeping picked the buzzer", simNvsU16.count("snd") && simNvsU16["snd"] != 255 && simNvsU16["sset"] == 1);
  }
  if (scenario == "plain2")    check("hint: no ladder on a pin without ADC", seen("cannot measure volts: ladder on GPIO0..3") && BTN_PIN[0] == 6);
  if (scenario == "ladder4") {
    printf("   keys: %u/%u %u/%u %u/%u %u/%u OK %u/%u\n", BTN_PIN[0], BTN_MV[0], BTN_PIN[1], BTN_MV[1],
           BTN_PIN[2], BTN_MV[2], BTN_PIN[3], BTN_MV[3], BTN_PIN[4], BTN_MV[4]);
    bool all = true;
    for (int i = 0; i < 5; i++) all = all && BTN_PIN[i] == 2 && BTN_MV[i];
    check("four arrows and OK learned on one pin", all);
    check("OK (two keys together) opened Tetris", seen("TETRIS") && !seen("SNAKE"));
  }
  if (scenario == "ladder") {
    printf("   keys: UP %u/%u DOWN %u/%u LEFT %u/%u RIGHT %u/%u\n", BTN_PIN[0], BTN_MV[0], BTN_PIN[1], BTN_MV[1],
           BTN_PIN[2], BTN_MV[2], BTN_PIN[3], BTN_MV[3]);
    check("three keys learned on the ladder pin", BTN_PIN[0] == 2 && BTN_PIN[1] == 2 && BTN_PIN[2] == 2);
    check("each with its own voltage", BTN_MV[0] && BTN_MV[1] && BTN_MV[2] && BTN_MV[0] != BTN_MV[1] && BTN_MV[1] != BTN_MV[2]);
    check("RIGHT on its own pin", BTN_PIN[3] == 6 && !BTN_MV[3]);
    check("LEFT+RIGHT opened a game", seen("SNAKE"));
  }
  check("sounds played", scenario == "wizard" || scenario == "chess" || scenario == "ladder5" || scenario == "plain2" || scenario == "tilt" || scenario == "menu" || scenario == "settings" || toneCount > 0);
  printf("   %ld frames, %.1f s simulated\n", frames, clockUs / 1e6);
  return failures ? 1 : 0;
}
