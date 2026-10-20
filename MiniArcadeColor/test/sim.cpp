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
void Gfx::begin() {}
void Gfx::setBacklight(uint8_t) {}
void Gfx::setOrientation(bool, bool, bool) {}
void Gfx::sleep(bool) {}
void Gfx::send() {
  frames++;
  memcpy(lastFrame, fb, sizeof(fb));
  if (grabNext) { grabNext = 0; captureAt.insert(frames + 3); }
  if (captureAt.count(frames)) writeFrame(fb);
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

static uint8_t colsInFrame() {                                  // how many colours one frame shows
  std::set<uint8_t> c(lastFrame, lastFrame + sizeof(lastFrame));
  return (uint8_t)c.size();
}

int main(int argc, char **argv) {
  scenario = argc > 1 ? argv[1] : "menu";
  srand(1);
  // a console that was set up before: four keys, a buzzer, a battery
  simNvsU16["pset"] = 3; simNvsU16["sset"] = 1; simNvsU16["snd"] = SND; simNvsU16["bat"] = BAT;
  for (int i = 0; i < 4; i++) { simNvsU16["p" + std::to_string(i)] = KEYS[i]; simNvsU16["a" + std::to_string(i)] = 1; }
  simNvsU16["p4"] = 255;
  const uint32_t M = 1600;                                      // the menu is up by then
  if (scenario == "menu") {
    simEnd = 6000; captureAt = { 60 };
    for (int i = 0; i < 6; i++) press(M + 300 * i, DN);
  } else if (scenario == "snake") {
    uint32_t t = openGame(0, M); simEnd = t + 30000;
    press(t + 200, UP); press(t + 900, RT); press(t + 1500, DN); press(t + 2400, LT);
    autoplay = true; autoplayFrom = t + 3000; captureAt = { 160, 400 };
  } else if (scenario == "snakeover") {
    uint32_t t = openGame(0, M); simEnd = t + 9000;
    press(t + 200, RT);                                          // straight into the wall
    setenv("GRAB", "GAME OVER", 1);
  } else if (scenario == "tetris") {
    uint32_t t = openGame(1, M); simEnd = t + 60000;
    autoplay = true; autoplayFrom = t + 300; captureAt = { 400, 1400 };
  } else if (scenario == "pong") {
    uint32_t t = openGame(2, M); simEnd = t + 40000;
    autoplay = true; autoplayFrom = t + 300; captureAt = { 300 };
  } else if (scenario == "flappy") {
    uint32_t t = openGame(3, M); simEnd = t + 20000;
    for (uint32_t k = t + 300; k < t + 20000; k += 330) press(k, UP, 40);
    captureAt = { 150 };
  } else if (scenario == "breakout") {
    uint32_t t = openGame(4, M); simEnd = t + 40000;
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
  } else if (scenario == "settings") {
    simEnd = M + 3000; press(M, UP); press(M + 400, OK, 80); captureAt = { 200 };
    setenv("GRAB", "SETTINGS", 1);
  } else if (scenario == "pause") {
    uint32_t t = openGame(0, M); simEnd = t + 4000;
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
  } else { printf("unknown scenario %s\n", scenario.c_str()); return 2; }
  printf("== %s\n", scenario.c_str());
  try {
    setup();
    for (;;) loop();
  } catch (SimEnd &) {}

  check("frames were drawn", frames > 20);
  check("the picture uses many colours", colsInFrame() >= 6);
  if (scenario == "menu")      check("menu lists the games", seen("Snake") && seen("Tetris") && seen("Breakout"));
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
  check("sounds played", scenario == "wizard" || scenario == "ladder5" || scenario == "plain2" || scenario == "tilt" || scenario == "menu" || scenario == "settings" || toneCount > 0);
  printf("   %ld frames, %.1f s simulated\n", frames, clockUs / 1e6);
  return failures ? 1 : 0;
}
