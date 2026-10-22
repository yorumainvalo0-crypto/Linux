/* Host test harness for MiniArcade Color.
   Runs the real console code (console.cpp with all games), the real platform
   layer and the real drawing code; only the hardware calls are redirected
   here. The panel driver is replaced: every frame sent lands in a buffer,
   and the frames a scenario asks for are written to frames/ as PPM.
   Usage: ./colorsim <scenario>       (build.sh runs them all)
   SPRACHE=de ./colorsim <scenario>: the same in German - then only checked
   that no text runs off the screen; what was drawn goes to frames/lang_de.txt
   for the "sprache" scenario's check over every translation.            */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <functional>
#include <unordered_set>
#include <queue>
#include <regex>
#include <fstream>
#include <sstream>
#include <dirent.h>

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
static long pmbBlocks = 0, pmbFrames = 0;        // Pumpkin Maze: 32-px pieces of rows sent while rolling
static bool simSlept = false;
static int8_t  LADDER = -1;                      // pin with three keys through resistors
static uint8_t ladder = 0;                       // which of them is pressed (0 = none)
static const int LADDER_EXT[8] = { 3100, 0, 300, 550 };  // 10k pull-up, keys via 0 / 1k / 2k
// no pull-up resistor, the pin's own ~45k: keys via 10k / 11k / 12k / 13.3k to GND, 5 = UP+DOWN
static const int LADDER_INT[8] = { 3100, 600, 649, 696, 750, 345 };
// five keys: the four arrows and an own OK key (14.6k)
static const int LADDER_5[8] = { 3100, 600, 649, 696, 750, 800 };
static const int *LADDER_MV = LADDER_EXT;
// five keys through 15k / 10k / 6.8k / 5.1k / 2.7k to GND, 10k pull-up to a 3.24 V supply;
// 1..5 = one key, 6.. = two together (UP+RIGHT, DOWN+LEFT, UP+OK, LEFT+OK)
static bool LADDER_R = false;
static const uint8_t LADDER_R_SET[10] = { 0, 1, 2, 4, 8, 16, 1 | 8, 2 | 4, 1 | 16, 4 | 16 };
static int ladderDrift = 0;                        // mV the supply moves the levels (ladderpair)
static int ladderRmv(uint8_t idx) {
  static const double R[5] = { 15000, 10000, 6800, 5100, 2700 };
  double g = 0;
  for (int k = 0; k < 5; k++) if (LADDER_R_SET[idx] >> k & 1) g += 1 / R[k];
  return g ? (int)((3240 + ladderDrift) / (1 + 10000 * g)) : 3240;
}
static int16_t simAcc[3] = { 0, 0, 16384 };      // the gyroscope's accelerometer, lying flat
struct AccEv { uint32_t t; int16_t x, y; };
static std::vector<AccEv> accScript;
static std::vector<std::pair<uint32_t, std::function<void()>>> simAt;   // things that happen at a time (ms)
static bool simDe = false;                       // SPRACHE=de: the scenario in German
static const char *framePre = "";                // "de_" before the frame files then
static std::set<std::string> deTexts;            // the texts drawn while the console spoke German
static std::set<std::string> deOver;             // ... those that ran off the screen

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
  if (ch == LADDER) { int mv = (LADDER_R ? ladderRmv(ladder) : LADDER_MV[ladder]) + rand() % 17 - 8 + (toneOn && ladder ? 40 : 0);   // noisy, and the buzzer pulls
    if (mv < 0) mv = 0; return mv * 4095 / 2500 > 4095 ? 4095 : mv * 4095 / 2500; }
  if (pinPress[ch] || pinMode_[ch] == 2) return 4095;
  if (down) return 5;
  return 300 + (int)(clockUs / 1000 % 400);                    // floating pin: drifts
}
static std::map<std::string, uint32_t> textAt;   // when a text was drawn last (ms)
void arcadeTraceStr(const char *s) {
  seenTexts.insert(s);
  textAt[s] = (uint32_t)(clockUs / 1000);
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
static void simInetTick(uint32_t t);             // the pretend relay of internet play (inetsrv.h), every ms

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
    for (auto &e : simAt) if (e.first == t) e.second();
    if (autoplay && t > autoplayFrom && t % 180 == 0) {
      static const uint8_t o[] = { 0, 0, 1, 2, 4, 8, 4, 8 };
      applyMask(o[rand() % 8]);
    }
    if (pzBot) pzBot(t);
    simInetTick(t);
    if (simBot && t >= botFrom) simBot(t);
    if (simEnd && t >= simEnd) throw SimEnd{};
  }
}

#include "Arduino.h"
#include "net.h"                   // pretend network, before the console finds main/net.h
#include "link.h"
#include "tvnet.h"                 // pretend MiniArcade server for the TV
#include "../main/console.cpp"

// every text drawn (gfx.cpp); in German also where it lies: nothing may run off the screen
void arcadeTraceText(int16_t x, int16_t y, const char *s, Font f) {
  arcadeTraceStr(s);
  if (!cfgLang || !*s) return;
  deTexts.insert(s);
  if (x < 0 || y < 0 || x + Gfx::width(s, f) > SCR_W || y + Gfx::height(f) > SCR_H) {
    char b[160];
    snprintf(b, sizeof(b), "'%s' at %d,%d, %d px wide", s, x, y, Gfx::width(s, f));
    deOver.insert(b);
  }
}

// ---------------- the panel ----------------
static void writeFrame(const uint8_t *fb) {
  char fn[96];
  snprintf(fn, sizeof(fn), "frames/%s%s_%05ld.ppm", framePre, scenario.c_str(), frames);
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
// an SD card with its CS on GPIO20 in the "sdcard" scenario
static bool simSd = false, simSdIn = false;
bool sdMount(int8_t cs) { simSdIn = simSd && cs == 20; return simSdIn; }
void sdUnmount() { simSdIn = false; }
bool sdReady() { return simSdIn; }
uint32_t sdSizeMB() { return simSdIn ? 7580 : 0; }
uint32_t sdFreeMB() { return simSdIn ? 7000 : 0; }
bool gyroOpen(int8_t sda, int8_t scl) { return simGyro && sda == 20 && scl == 7; }
void gyroClose() {}
bool gyroAccel(int16_t a[3]) { if (!simGyro) return false; memcpy(a, simAcc, sizeof(simAcc)); return true; }

static uint8_t lastFrame[SCR_W * SCR_H];
// the TV: how many sent screens showed a golden picture (the text strip left out)
static std::vector<uint32_t> tvGoldTop;
static long tvGoldSeen = 0, tvSends = 0;
static uint32_t inStalled = 0, inFrames = 0;     // internet play: frames of a real-time game, those that waited
static void tvCheckScreen(const uint8_t *fb) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < SCR_W * (SCR_H - TV_STRIP); i++) { h ^= fb[i]; h *= 16777619u; }
  tvSends++;
  for (uint32_t g : tvGoldTop) if (g == h) { tvGoldSeen++; break; }
}
// PALREC=<file> ./colorsim palace: every second frame, gzipped, for a video:
// 8 bytes (ms, level, phase, the runner's state, 0) and the 320 x 240 colour indices
static FILE *palRec = NULL;
static void palRecEnd() { if (palRec) pclose(palRec); palRec = NULL; }
static void palRecFrame(uint32_t ms, const uint8_t *px) {
  uint8_t hd[8] = { (uint8_t)ms, (uint8_t)(ms >> 8), (uint8_t)(ms >> 16), (uint8_t)(ms >> 24), palLv, palPhase, palHero.st, 0 };
  fwrite(hd, 1, sizeof(hd), palRec);
  fwrite(px, 1, SCR_W * SCR_H, palRec);
}
static int mine3dCols = 0, mine3dDug = 0, mine3dPut = 0;
void Gfx::begin() {}
void Gfx::setBacklight(uint8_t) {}
void Gfx::setOrientation(uint8_t, bool, bool) {}
void Gfx::sleep(bool) {}
void Gfx::setSpeed(uint8_t) {}
void Gfx::send() {
  if (overlay) overlay();
  frames++;
  if (mpRt) { inFrames++; if (mpStalled) inStalled++; }      // real-time online: frames that waited
  if (pm && tiltGame && pm->st == PS_ROLL) {                     // Pumpkin Maze: what goes to the panel (per row: first to last changed block)
    for (int y = 0; y < SCR_H; y++) {
      int b0 = -1, b1 = -1;
      for (int b = 0; b < SCR_W / 32; b++)
        if (memcmp(fb + y * SCR_W + b * 32, lastFrame + y * SCR_W + b * 32, 32)) { if (b0 < 0) b0 = b; b1 = b; }
      if (b0 >= 0) pmbBlocks += b1 - b0 + 1;
    }
    pmbFrames++;
  }
  memcpy(lastFrame, fb, sizeof(fb));
  if (grabNext) { grabNext = 0; captureAt.insert(frames + 3); }
  if (palRec && !(frames & 1)) palRecFrame((uint32_t)(clockUs / 1000), fb);
  if (captureAt.count(frames)) writeFrame(fb);
  if (scenario == "tvui" && tvP.have) tvCheckScreen(fb);
  if (vox && (scenario == "mine3d" || scenario == "mine3dfree")) {                            // colours in a picture of the game
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

// Block Puzzle: how good the board is after a move (more is better), for the bots
static int bkEval(const BkState &k, const BkState &was) {
  int v = (was.gems - k.gems) * 1000 + (was.blocks - k.blocks) * 700 + k.lines * 120 + (int)(k.score - was.score) * (k.mode == BKM_ADV ? 3 : 1);
  for (int i = 0; i < 64; i++) if (was.kind[i] == BK_ICE && k.kind[i] == BK_CRACK) v += 300;
  int edges = 0, holes = 0;
  for (int i = 0; i < 64; i++) {
    bool e = !(k.b >> i & 1);
    int r = i / 8, c = i % 8, nb = 0;
    if (c < 7 && e != !(k.b >> (i + 1) & 1)) edges++;
    if (r < 7 && e != !(k.b >> (i + 8) & 1)) edges++;
    if (!e) continue;
    if (c > 0 && !(k.b >> (i - 1) & 1)) nb++;
    if (c < 7 && !(k.b >> (i + 1) & 1)) nb++;
    if (r > 0 && !(k.b >> (i - 8) & 1)) nb++;
    if (r < 7 && !(k.b >> (i + 8) & 1)) nb++;
    if (!nb) holes++;
  }
  v -= edges * 6 + holes * 40;
  for (int j = 0; j < 8; j++) {                                  // lines with a goal in them: fill them
    const uint64_t LS[2] = { 0xFFull << (j * 8), 0x0101010101010101ull << j };
    for (uint64_t L : LS) {
      bool goal = false;
      for (int i = 0; i < 64; i++) if ((L >> i & 1) && (k.b >> i & 1) && k.kind[i] != BK_PLAIN) goal = true;
      if (goal) { int f = __builtin_popcountll(k.b & L); v += f * f * 4; }
    }
  }
  return v;
}
// the best piece and place (or a random one now and then: rnd in %); false = none fits
static bool bkBest(uint8_t &slot, int8_t &br, int8_t &bc, int rnd = 0) {
  BkState was = *bk;
  int best = -1000000;
  bool any = false, random_ = rnd && rand() % 100 < rnd;
  for (uint8_t s = 0; s < 3; s++) {
    if (was.tray[s] == 255) continue;
    for (int8_t r = 0; r < 8; r++)
      for (int8_t c = 0; c < 8; c++) {
        if (!bkFits(was.b, was.tray[s], r, c)) continue;
        int v;
        if (random_) v = rand() % 1000;
        else { *bk = was; bkPlace(was.tray[s], r, c); v = bkEval(*bk, was); }
        if (v > best) { best = v; slot = s; br = r; bc = c; any = true; }
      }
  }
  *bk = was;
  return any;
}
// plays one round of a mode without the screen; true = goal reached
static bool bkSimRound(uint8_t mode, uint8_t lv, int maxMoves, int rnd = 0) {
  bkStart(mode, lv, 1);
  for (int m = 0; m < maxMoves; m++) {
    if (bkWon()) return true;
    if (mode == BKM_ADV && bk->moves >= BK_LEVELS[lv].moves) return false;
    uint8_t s = 0; int8_t r = 0, c = 0;
    if (!bkBest(s, r, c, rnd)) return false;
    bkPlace(bk->tray[s], r, c);
    bk->tray[s] = 255;
    if (bkTrayEmpty()) bkDeal();
  }
  return bkWon();
}

// Block Puzzle on the screen: the best move (now and then a random one), through the keys
static int8_t  bkBotR = 0, bkBotC = 0;
static int     bkBotRnd = 40, bkBotPlaced = 0, bkBotPlan = -1;
static uint8_t bkBotSlot = 0;
static uint8_t bkBotUsed[BK_SEQ];
static void blocksBot(uint32_t t) {
  if (pzBusy(t) || !bk) return;
  if (bk->st == 0) {
    if (bkBotPlan != bk->moves || bk->tray[bkBotSlot] == 255) {
      bkBotPlan = -1;
      if (bk->mode == BKM_DAILY) {                               // puzzles: every piece to where it was cut out
        if (!bk->moves) memset(bkBotUsed, 0, sizeof(bkBotUsed));
        for (uint8_t s = 0; s < 3 && bkBotPlan < 0; s++)
          for (uint8_t j = 0; j < bk->seqN && bkBotPlan < 0; j++)
            if (bk->tray[s] != 255 && !bkBotUsed[j] && bk->seq[j] == bk->tray[s] && bkFits(bk->b, bk->seq[j], bk->pos[j] / 8, bk->pos[j] % 8)) {
              bkBotUsed[j] = 1; bkBotSlot = s; bkBotR = bk->pos[j] / 8; bkBotC = bk->pos[j] % 8; bkBotPlan = bk->moves;
            }
        if (bkBotPlan < 0) { pzPress(t, PZ_OK, 400); return; }   // over: OK goes on
      } else {
        if (!bkBest(bkBotSlot, bkBotR, bkBotC, bkBotRnd)) { pzPress(t, PZ_OK, 400); return; }
        bkBotPlan = bk->moves;
      }
    }
    if (bk->sel != bkBotSlot) pzPress(t, RT, 110);
    else pzPress(t, PZ_OK, 140);
    return;
  }
  if (bk->cr < bkBotR) pzPress(t, DN, 90);
  else if (bk->cr > bkBotR) pzPress(t, UP, 90);
  else if (bk->cc < bkBotC) pzPress(t, RT, 90);
  else if (bk->cc > bkBotC) pzPress(t, LT, 90);
  else { pzPress(t, PZ_OK, 160); bkBotPlaced++; }
}

static void blocksLogic() {
  bk = (BkState *)calloc(1, sizeof(BkState));
  // fitting
  bool ok = bkFits(0, 7, 0, 3) && !bkFits(0, 7, 0, 4) && !bkFits(0, 8, 4, 0) && bkFits(0, 8, 3, 0) && !bkFits(0, 0, -1, 0);
  ok = ok && !bkFits(1ull << 9, 9, 0, 0) && bkFits(1ull << 9, 9, 0, 2) && bkFits(1ull << 9, 13, 0, 0);   // the corner leaves (1,1) out
  check("blocks: pieces fit only on the board and on free cells", ok);
  ok = true;
  for (uint8_t s = 0; s < BK_NSHAPE; s++) {
    const BkShape &p = BK_SHAPES[s];
    bool rows = true, lastCol = false;
    for (int r = 0; r < p.h; r++) if (!p.r[r] || p.r[r] >> p.w) rows = false;
    for (int r = 0; r < p.h; r++) if (p.r[r] >> (p.w - 1) & 1) lastCol = true;
    if (!rows || !lastCol || bkCells(s) > 9 || !p.wt || p.c > 7) ok = false;
  }
  check("blocks: every shape fills its own box", ok);
  // a row and a column at once, then a streak
  memset(bk, 0, sizeof(*bk));
  bk->b = (0xFFull | 0x8080808080808080ull) & ~(1ull << 7);
  uint32_t p = bkPlace(0, 0, 7);
  printf("      row + column: %u lines, %u points, board %s\n", bk->lines, (unsigned)p, bk->b ? "not empty" : "empty");
  check("blocks: a row and a column clear together (combo x2 = 41)", bk->lines == 2 && p == 41 && !bk->b);
  bk->b = 0xFFull << 8 & ~(1ull << 8);
  p = bkPlace(0, 1, 0);
  check("blocks: a clear right after one doubles (streak 2 = 21)", bk->lines == 1 && p == 21 && bk->streak == 2 && !bk->b);
  p = bkPlace(9, 4, 4);
  check("blocks: a piece without a clear ends the streak", p == 4 && !bk->streak && bk->b == (0x0303ull << 36));
  bk->b = 0xFFull << 16 & ~(1ull << 23);
  bk->kind[17] = BK_ICE; bk->kind[18] = BK_GEM; bk->kind[19] = BK_STONE; bk->gems = 1; bk->blocks = 2;
  bkPlace(0, 2, 7);
  bool crack = bk->b == 1ull << 17 && bk->kind[17] == BK_CRACK && !bk->gems && bk->blocks == 1;
  bk->b |= 0xFFull << 16 & ~(1ull << 23);
  bkPlace(0, 2, 7);
  check("blocks: gem collected, stone gone, ice cracks first and goes at the second clear", crack && !bk->b && !bk->blocks);
  // stuck
  memset(bk, 0, sizeof(*bk));
  bk->b = ~0ull;
  for (int i = 0; i < 64; i += 9) bk->b &= ~(1ull << i);        // only single holes on the diagonal
  bk->tray[0] = 10; bk->tray[1] = 7; bk->tray[2] = 8;
  bool st = bkStuck();
  bk->tray[2] = 0;
  check("blocks: over when no piece of the tray fits, not while one does", st && !bkStuck());
  // fair dealing
  ok = true;
  srand(7);
  for (int k = 0; k < 400; k++) {
    memset(bk, 0, sizeof(*bk));
    int dens = 30 + rand() % 50;
    for (int i = 0; i < 64; i++) if (rand() % 100 < dens) bk->b |= 1ull << i;
    if (bk->b == ~0ull) continue;
    bkDeal();
    for (int s = 0; s < 3; s++) if (!bkFitsAny(bk->b, bk->tray[s])) ok = false;
  }
  check("blocks: every dealt piece fits somewhere when anything can", ok);
  // puzzles: the same every time, all solvable, never empty
  ok = true;
  int pieces = 0;
  for (uint16_t n = 1; n <= 300 && ok; n++) {
    memset(bk, 0, sizeof(*bk)); bk->mode = BKM_DAILY;
    bkMakePuzzle(n);
    BkState a = *bk;
    memset(bk, 0, sizeof(*bk)); bk->mode = BKM_DAILY;
    bkMakePuzzle(n);
    if (memcmp(&a, bk, sizeof(a)) || !bk->b || bk->seqN < 2) ok = false;
    pieces += bk->seqN;
    for (uint8_t j = 0; j < bk->seqN && ok; j++) {
      if (!bkFits(bk->b, bk->seq[j], bk->pos[j] / 8, bk->pos[j] % 8)) ok = false;
      else bkPlace(bk->seq[j], bk->pos[j] / 8, bk->pos[j] % 8);
    }
    if (bk->b) ok = false;
    if (!ok) printf("      puzzle %u fails\n", n);
  }
  printf("      puzzles: %.1f pieces on average\n", pieces / 300.0);
  check("blocks: 300 puzzles the same each time, each one clears with its pieces", ok);
  // classic: a greedy bot plays until no piece fits
  randomSeed(3);
  int score = 0;
  for (int k = 0; k < 3; k++) { bkSimRound(BKM_CLASSIC, 0, 3000, 25); score += (int)bk->score; }
  printf("      classic, greedy bot: %d points on average\n", score / 3);
  check("blocks: the classic bot gets somewhere and the game ends", score / 3 > 300 && bkStuck());
  // every adventure level: the greedy bot wins it within its moves
  ok = true;
  for (uint8_t lv = 0; lv < BK_NLEVEL; lv++) {
    int wins = 0, left = 0;
    for (int seed = 1; seed <= 12; seed++) {
      randomSeed((unsigned long)(seed * 77 + lv));
      if (bkSimRound(BKM_ADV, lv, 200)) wins++;
      left += BK_LEVELS[lv].goal == BKG_GEMS ? bk->gems : BK_LEVELS[lv].goal == BKG_BLOCKS ? bk->blocks : (int)bk->score;
    }
    printf("      level %2u (%s, %2u moves): won %2d of 12 (%s %.1f)\n", lv + 1,
           BK_LEVELS[lv].goal == BKG_GEMS ? "gems  " : BK_LEVELS[lv].goal == BKG_BLOCKS ? "blocks" : "score ", BK_LEVELS[lv].moves, wins,
           BK_LEVELS[lv].goal == BKG_SCORE ? "points" : "left", left / 12.0);
    if (wins < 3) ok = false;
  }
  check("blocks: every adventure level is won by a greedy bot (3+ of 12 tries)", ok && BK_NLEVEL >= 12);
  // a picture: a piece over the board that would clear a row and a column
  memset(bk, 0, sizeof(*bk));
  randomSeed(11);
  bk->b = 0x3Full << 40 | 0x8080000080808080ull | 0x0000001C1C000000ull | 0x0Full << 56 | 0x7;
  for (int i = 0; i < 64; i++) bk->tint[i] = (uint8_t)((i * 3 + i / 8) % 8);
  bk->tray[0] = 9; bk->tray[1] = 21; bk->tray[2] = 33; bk->sel = 0; bk->st = 1; bk->cr = 4; bk->cc = 6;
  bk->score = 1260; bk->cleared = 14; bk->streak = 2;
  gfx.clear();
  bkDraw(0);
  statusBar("BLOCKS", 1260, 3410, rgb(102, 85, 204));
  frames = 1; writeFrame(gfx.buffer());
  free(bk); bk = NULL;
}

// Fruit Pot: drops at random places
static uint16_t frBotMerges = 0;
static int16_t  frBotX = -1;
static void fruitBot(uint32_t t) {
  if (fr && fr->merges > frBotMerges) frBotMerges = fr->merges;
  if (pzBusy(t) || !fr) return;
  if (frBotX < 0) frBotX = (int16_t)(FR_L + 10 + rand() % (FR_R - FR_L - 20));
  int16_t x = (int16_t)(frDropX >> 8);
  if (x < frBotX - 5) pzPress(t, RT, 60);
  else if (x > frBotX + 5) pzPress(t, LT, 60);
  else { pzPress(t, DN, 520); frBotX = -1; }
}

static bool frInside(int32_t tol) {                              // every fruit in the pot (tol 1/256 px)
  for (uint8_t i = 0; i < fr->n; i++) {
    const Fruit &a = fr->f[i];
    if (a.x < (FR_L << 8) + a.rad - tol || a.x > (FR_R << 8) - a.rad + tol || a.y > (FR_FLOOR << 8) - a.rad + tol) return false;
  }
  return true;
}
static int32_t frMaxSpeed() {
  int32_t m = 0;
  for (uint8_t i = 0; i < fr->n; i++) {
    int32_t vx = abs(fr->f[i].x - fr->f[i].ox), vy = abs(fr->f[i].y - fr->f[i].oy);
    m = std::max(m, std::max(vx, vy));
  }
  return m;
}
static int32_t frMaxOverlap() {
  int32_t m = 0;
  for (uint8_t i = 0; i < fr->n; i++)
    for (uint8_t j = i + 1; j < fr->n; j++) {
      const Fruit &a = fr->f[i], &b = fr->f[j];
      double d = sqrt((double)(a.x - b.x) * (a.x - b.x) + (double)(a.y - b.y) * (a.y - b.y));
      m = std::max(m, (int32_t)(a.rad + b.rad - d));
    }
  return m;
}

static void fruitLogic() {
  fr = (FrState *)calloc(1, sizeof(FrState));
  // two cherries side by side on the floor become a strawberry between them
  frAdd(0, 150 << 8, (FR_FLOOR - 7) << 8);
  frAdd(0, 164 << 8, (FR_FLOOR - 7) << 8);
  frStep();
  check("fruit: two cherries touching merge into a strawberry at their middle",
        fr->n == 1 && fr->f[0].kind == 1 && abs((fr->f[0].x >> 8) - 157) <= 1 && fr->score == FR_PTS[1] && fr->merges == 1);
  for (int i = 0; i < 300; i++) frStep();
  check("fruit: the strawberry grows to its size and lands on the floor",
        fr->f[0].rad == FR_RAD[1] << 8 && (fr->f[0].y >> 8) == FR_FLOOR - FR_RAD[1]);
  // two melons vanish
  memset(fr, 0, sizeof(FrState));
  frAdd(9, 120 << 8, (FR_FLOOR - 40) << 8); frAdd(9, 200 << 8, (FR_FLOOR - 40) << 8);
  frStep();
  check("fruit: two melons touching vanish for 100 points", fr->n == 0 && fr->score == 100);
  // a stack settles and stays still
  memset(fr, 0, sizeof(FrState));
  fr->noMerge = true;
  srand(5);
  bool inside = true;
  for (int i = 0; i < 22; i++) frAdd((uint8_t)(rand() % 8), (FR_L + 40 + rand() % (FR_R - FR_L - 80)) << 8, (60 - i * 40) << 8);
  int settle = -1;
  for (int s = 0; s < 3000; s++) {
    frStep();
    if (!frInside(256)) inside = false;
    if (settle < 0 && s > 200 && frMaxSpeed() == 0) settle = s;
  }
  Fruit snap[FR_MAX];
  memcpy(snap, fr->f, sizeof(snap));
  int32_t moved = 0, ke = 0;
  for (int s = 0; s < 300; s++) {
    frStep();
    for (uint8_t i = 0; i < fr->n; i++) {
      int32_t vx = fr->f[i].x - fr->f[i].ox, vy = fr->f[i].y - fr->f[i].oy;
      ke += (vx * vx + vy * vy) * (FR_RAD[fr->f[i].kind] * FR_RAD[fr->f[i].kind]) / 256;
    }
  }
  for (uint8_t i = 0; i < fr->n; i++) moved = std::max(moved, std::max(abs(fr->f[i].x - snap[i].x), abs(fr->f[i].y - snap[i].y)));
  printf("      22 fruits: at rest after %d steps, kinetic energy over 3 s %d, moved %.2f px, overlap %.2f px\n",
         settle, ke, moved / 256.0, frMaxOverlap() / 256.0);
  check("fruit: a stack of 22 settles (kinetic energy ~0) and stays still", settle > 0 && settle < 1500 && ke < 1000 && moved < 64);
  check("fruit: settled fruits barely overlap", frMaxOverlap() < 512);
  check("fruit: nothing leaves the pot", inside);
  // 40 fruits thrown in on top of each other: no explosion, all in, then calm
  memset(fr, 0, sizeof(FrState));
  fr->noMerge = true;
  srand(9);
  for (int i = 0; i < 45; i++) frAdd((uint8_t)(rand() % 5), (FR_L + 10 + rand() % (FR_R - FR_L - 20)) << 8, (110 + rand() % 110) << 8);
  int32_t fast = 0;
  inside = true;
  for (int s = 0; s < 2000; s++) {
    frStep();
    if (!frInside(256)) inside = false;
    if (s > 2) fast = std::max(fast, frMaxSpeed());
  }
  printf("      40 fruits overlapping: fastest %.2f px/step, at the end %.2f px/step, overlap %.2f px\n", fast / 256.0, frMaxSpeed() / 256.0, frMaxOverlap() / 256.0);
  check("fruit: no more than 40 fruits", fr->n == FR_MAX && !frAdd(0, 160 << 8, 100 << 8));
  check("fruit: 40 overlapping fruits do not blow up, stay in and calm down", inside && fast <= 4 * 256 && frMaxSpeed() < 64 && frMaxOverlap() < 768);
  clock_t c0 = clock();
  for (int s = 0; s < 2000; s++) frStep();
  // a picture: one of every kind in the pot, settled
  memset(fr, 0, sizeof(FrState));
  fr->noMerge = true;
  static const uint8_t SHOW[][2] = { {9,110},{8,200},{7,150},{6,222},{5,100},{4,160},{3,205},{2,130},{1,180},{0,95},{4,215},{2,150},{1,120},{0,170} };
  for (int i = 0; i < 14; i++) frAdd(SHOW[i][0], SHOW[i][1] << 8, (200 - i * 30) << 8);
  for (int s = 0; s < 1500; s++) frStep();
  fr->top = 9; fr->score = 2468; frDropX = 150 << 8;
  gfx.clear();
  frDraw(frDropX, 2, 4, 0);
  statusBar("FRUIT POT", 2468, 5120, rgb(255, 85, 85));
  frames = 1; writeFrame(gfx.buffer());
  printf("      host: %.1f us per step with 40 fruits\n", (double)(clock() - c0) * 1e6 / CLOCKS_PER_SEC / 2000);
  free(fr); fr = NULL;
}

static bool pzLogic() {
  if (scenario == "blockslogic") { blocksLogic(); return true; }
  if (scenario == "fruitlogic") { fruitLogic(); return true; }
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

  // the generator for takes only: exactly the takes, en passant and promotions of the full list
  bool same = true;
  srand(7);
  for (int g = 0; g < 40; g++) {
    chStart(p);
    for (int k = 0; k < 120; k++) {
      int n = chMoves(p, m);
      if (!n) break;
      ChMove t[220];
      int nt = chMoves(p, t, true), want = 0;
      for (int i = 0; i < n; i++) {
        bool tk = p.b[m[i].to] || m[i].promo || (m[i].to == p.ep && abs(p.b[m[i].from]) == CH_P);
        if (!tk) continue;
        want++;
        bool found = false;
        for (int j = 0; j < nt; j++) found = found || (t[j].from == m[i].from && t[j].to == m[i].to && t[j].promo == m[i].promo);
        same = same && found;
      }
      same = same && nt == want;
      chPlay(p, m[rand() % n]);
    }
  }
  check("takes-only moves match the full list", same);

  // the CPU
  che = (ChEngine *)malloc(sizeof(ChEngine));
  che->ui = false;
  auto best = [&](const char *fen, uint8_t level) { ChPos q; chFromFen(q, fen); ChMove b = { 0, 0, 0 }; chBest(q, level, b); return b; };
  auto legal = [&](const ChPos &q, const ChMove &b) {
    ChMove l[220]; int n = chMoves(q, l);
    for (int i = 0; i < n; i++) if (l[i].from == b.from && l[i].to == b.to && l[i].promo == b.promo) return true;
    return false;
  };
  bool mate = true;
  for (uint8_t lv = 0; lv < 3; lv++) {
    ChMove b = best("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - -", lv);
    if (b.from != 59 || b.to != 3) { mate = false; printf("      level %u played %d-%d\n", lv, b.from, b.to); }
  }
  check("CPU finds the mate in one (Rd8#) at every level", mate);
  ChMove b = best("rnb1kbnr/pppp1ppp/8/4p3/7q/5N2/PPPPPPPP/RNBQKB1R w KQkq -", 1);
  check("CPU (normal) takes the hanging queen (Nxh4)", b.from == 45 && b.to == 39);
  b = best("7k/8/8/8/8/8/6q1/7K w - -", 2);
  check("CPU plays the only move (Kxg2)", b.from == 63 && b.to == 54);
  b = best("k7/8/1QK5/8/8/8/8/8 w - -", 2);              // mate in one, next to stalemates
  chFromFen(p, "k7/8/1QK5/8/8/8/8/8 w - -");
  { ChPos q = p; chPlay(q, b); check("CPU mates instead of stalemating", legal(p, b) && !chMoves(q, m) && chInCheck(q, false)); }
  // random positions, few moves among them: always a legal move, within the node budget
  bool allLegal = true, inBudget = true;
  int few = 0;
  srand(11);
  for (int g = 0; g < 12; g++) {
    chStart(p);
    for (int k = 0; k < 90; k++) {
      int n = chMoves(p, m);
      if (!n || p.half >= 100) break;
      if (k % 15 == 14 || n <= 3) {
        uint8_t lv = (uint8_t)(k % 3);
        ChMove c = { 0, 0, 0 };
        bool ok = chBest(p, lv, c);
        if (n <= 3) few++;
        if (!ok || !legal(p, c)) { allLegal = false; printf("      no legal move at game %d ply %d\n", g, k); }
        if (che->nodes > CH_CAP[lv] + 4000) { inBudget = false; printf("      %u nodes at level %u\n", (unsigned)che->nodes, lv); }
      }
      chPlay(p, m[rand() % n]);
    }
  }
  printf("      %d positions with three moves or fewer\n", few);
  check("CPU always returns a legal move", allLegal && few > 0);
  check("CPU keeps to its node budget", inBudget);
  // how fast on this machine, and how deep
  static const char *const SPEED[2] = { "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
                                        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -" };
  for (uint8_t lv = 0; lv < 3; lv++)
    for (int f = 0; f < 2; f++) {
      chFromFen(p, SPEED[f]);
      ChMove c;
      clock_t c0 = clock();
      chBest(p, lv, c);
      double ms = (double)(clock() - c0) * 1000.0 / CLOCKS_PER_SEC;
      printf("      level %u %s: depth %u, %6u nodes, %.1f ms here (%.0f nodes/s)\n", lv, f ? "middle game" : "start      ",
             che->depth, (unsigned)che->nodes, ms, ms > 0 ? che->nodes / ms * 1000 : 0.0);
    }
  // a few games: normal against easy (easy's random moves too), normal should win them
  int won = 0, lost = 0;
  srand(3);
  for (int g = 0; g < 4; g++) {
    chStart(p);
    bool normalWhite = g % 2 == 0;
    int r = 0;
    for (int k = 0; k < 300; k++) {
      int n = chMoves(p, m);
      if (!n) { r = chInCheck(p, p.white) ? (p.white ? -1 : 1) : 0; break; }
      if (p.half >= 100 || chTooLittle(p)) break;
      bool normal = (p.white != 0) == normalWhite;
      ChMove c;
      chBest(p, normal ? 1 : 0, c);
      if (!normal && rand() % 4 == 0) c = m[rand() % n];
      chPlay(p, c);
    }
    int me = normalWhite ? r : -r;
    if (me > 0) won++;
    if (me < 0) lost++;
  }
  printf("      normal against easy: %d won, %d lost, %d drawn\n", won, lost, 4 - won - lost);
  check("normal beats easy", won >= 3 && !lost);
  free(che); che = NULL;
}

// Chess against the CPU: the bot plays random moves through the keys (takes preferred)
static int chBotMax = 1000, chBotDone = 0, chCpuMoves = 0;
static uint32_t chCpuFrom = 0, chCpuMaxMs = 0, chCpuMaxNodes = 0, chCpuDepths = 0, chCpuOver = 0;
static uint16_t chBotPly = 0xFFFF;
static ChMove chBotMv;
static void chBotGo(uint32_t t, int sq) {                       // cursor to the square, then OK
  int r = sq / 8, c = sq % 8;
  if (chFlipped()) { r = 7 - r; c = 7 - c; }
  if (ch->cr != r) pzPress(t, ch->cr < r ? 2 : 1, 110);
  else if (ch->cc != c) pzPress(t, ch->cc < c ? 8 : 4, 110);
  else pzPress(t, PZ_OK, 220);
}
static void chessCpuBot(uint32_t t) {
  if (!ch || !che) return;
  if (chCpuGo() && !chEnd) { if (!chCpuFrom) chCpuFrom = t; return; }
  if (chCpuFrom) {                                              // the CPU has moved
    chCpuMoves++;
    uint32_t d = t - chCpuFrom;
    if (d > chCpuMaxMs) chCpuMaxMs = d;
    if (che->nodes > chCpuMaxNodes) chCpuMaxNodes = che->nodes;
    if (che->nodes > CH_CAP[ch->level] + 4000 || d > CH_MS[ch->level] + 600u) chCpuOver++;
    chCpuDepths += che->depth;
    chCpuFrom = 0;
  }
  if (pzBusy(t)) return;
  if (chEnd) { pzPress(t, PZ_OK, 1500); return; }               // a new game
  if (ch->plies != chBotPly) {                                  // a new turn: pick a move
    if (chBotPly != 0xFFFF) chBotDone++;
    chBotPly = ch->plies;
    if (chBotDone >= chBotMax) return;
    int caps[220], nc = 0;
    for (int i = 0; i < ch->nMoves; i++) if (ch->pos.b[ch->moves[i].to]) caps[nc++] = i;
    chBotMv = nc && rand() % 2 ? ch->moves[caps[rand() % nc]] : ch->moves[rand() % ch->nMoves];
  }
  if (chBotDone >= chBotMax) return;
  chBotGo(t, ch->sel == chBotMv.from ? chBotMv.to : chBotMv.from);
}

// ---------------- online (multiplayer.h) against ANNA, the bot console of link.h ----------------
static bool recent(const char *s) {                             // on the screen right now
  auto it = textAt.find(s);
  return it != textAt.end() && it->second + 60 >= clockUs / 1000;
}
static bool mpInList() { return recent("OK = your name   hold OK = back") || recent("OK = challenge   RIGHT = friend   hold OK = back"); }

// ANNA's side of Chess: random legal moves, the same two byte moves as ours
static ChPos    cpPos;
static ChMove   cpMoves[220];
static int      cpN = 0, cpPlies = 0, cpBad = 0;
static int16_t  cpFirst = -1;
static uint16_t cpSid = 0;
static uint8_t  cpQ[2], cpQn = 0;
static bool     cpDone = false;
static uint32_t cpNext = 0;
static void chessPeer(uint32_t now) {
  LinkCore &B = simPeer;
  if (B.game != LKG_CHESS) return;
  if (B.session() != cpSid) {                                   // a new game
    cpSid = B.session(); chStart(cpPos); cpN = chMoves(cpPos, cpMoves);
    cpFirst = -1; cpQn = 0; cpPlies = 0; cpDone = false; cpNext = now + 600;
  }
  uint8_t m;
  if (B.moveIn(&m)) {
    if (cpFirst < 0) cpFirst = m;
    else {
      ChMove mv;
      if (!mpChessDec(cpMoves, cpN, (uint8_t)cpFirst, m, &mv)) { printf("      bot: chess move %d %u not legal\n", cpFirst, m); cpBad++; }
      else { chPlay(cpPos, mv); cpN = chMoves(cpPos, cpMoves); cpPlies++; }
      cpFirst = -1; cpNext = now + 400;
    }
  }
  bool mine = (bool)cpPos.white == B.iStart;
  if (!cpDone && cpN && mine && !cpQn && now >= cpNext) {
    ChMove mv = cpMoves[rand() % cpN];
    mpChessEnc(mv, cpQ); cpQn = 2;
    chPlay(cpPos, mv); cpN = chMoves(cpPos, cpMoves); cpPlies++;
  }
  if (cpQn && B.sendMove(cpQ[0], now)) { cpQ[0] = cpQ[1]; cpQn--; }
  if (!cpDone && !cpQn && (!cpN || cpPos.half >= 100 || chTooLittle(cpPos))) { cpDone = true; simBotGames++; B.finish(); }
}

// our side of each game, through the keys
static void bshipBot(uint32_t t);                               // the local games' bots, below
static void maumauBot(uint32_t t);
static int  mpChSame = -1;                                     // chess: both boards the same when we left
static void mpPlayRandom(uint32_t t, bool ok) {                 // arrows (and OK) at random
  static const uint8_t K[5] = { UP, DN, LT, RT, OK };
  botPress(t, K[rand() % (ok ? 5 : 4)], 60);
}
static void mpPlayCheckers(uint32_t t) {
  uint8_t me = ckV.flip ? 1 : 0;
  if (ck.winner || ck.turn != me || ckV.fp) return;
  uint32_t r = 12345 + ck.quiet * 7 + ck.count(0) * 131;
  CkStep st = ck.best(1, r);
  int8_t want = (ckSel == st.from) ? Checkers::next(st.from, st.dir, st.jump ? 2 : 1) : (int8_t)st.from;
  int8_t c, rr = Checkers::rc((uint8_t)want, &c);
  if (ckV.flip) { rr = 7 - rr; c = 7 - c; }
  int cr = ckCur / 8, cc = ckCur % 8;
  if (rr != cr) botPress(t, rr > cr ? DN : UP, 60);
  else if (c != cc) botPress(t, c > cc ? RT : LT, 60);
  else botPress(t, OK, 120);
}
static void mpPlayChess(uint32_t t) {
  if (!mpCh) return;
  if (recent("PROMOTE")) { botPress(t, OK, 80); return; }
  MpChess &g = *mpCh;
  if ((bool)g.pos.white == g.black || !g.n) return;             // not our go
  if (cpPlies >= 40 && mpChSame < 0) {                          // enough: both boards the same? then leave
    mpChSame = !memcmp(g.pos.b, cpPos.b, 64) && g.pos.white == cpPos.white && g.pos.castle == cpPos.castle && g.pos.ep == cpPos.ep;
    botPress(t, OK, 1000);
    return;
  }
  static ChMove pick;
  static int pickAt = -1;
  if (pickAt != cpPlies) { pick = g.moves[rand() % g.n]; pickAt = cpPlies; }
  int sq = g.sel == pick.from ? pick.to : pick.from, r = sq / 8, c = sq % 8;
  if (g.black) { r = 7 - r; c = 7 - c; }
  if (r != g.cr) botPress(t, r > g.cr ? DN : UP, 60);
  else if (c != g.cc) botPress(t, c > g.cc ? RT : LT, 60);
  else botPress(t, OK, 100);
}
static void mpPlay(uint32_t t) {
  switch (linkCore().game) {
  case LKG_C4: case LKG_TTT: mpPlayRandom(t, true); break;
  case LKG_SHIP: if (recent("UP = mix")) botPress(t, OK, 80); else bshipBot(t); break;
  case LKG_DAME: mpPlayCheckers(t); break;
  case LKG_MAU:  maumauBot(t); break;
  case LKG_CHESS: mpPlayChess(t); break;
  case LKG_BOMB: mpPlayRandom(t, rand() % 6 == 0); break;
  default: mpPlayRandom(t, false); break;                       // Pong, Snake, Pac-Man: arrows
  }
}

/* The driver: does what a player in the lobby does, one step after the
   other, each when the screen for it is up. mpSteps is the plan:
   'n' rename, 'f' ANNA as a friend, 'c' + game: challenge ANNA (and play),
   'i' + game: ANNA challenges us, 'a' accept it (and play), 'd' decline,
   'x' / 'y' ANNA will say no / yes, 'q' leave the lobby. A game is
   '0' + its number (Chess ':').                                        */
static std::string mpSteps;
static size_t   mpStep = 0;
static uint32_t mpWait = 0;
static int      mpInvited = 0;                                  // ANNA's challenges we saw
static void onlineDriver(uint32_t t) {
  if (mpStep && mpStep >= mpSteps.size() && !simOpen) {         // all done and left: the end
    if (!simEnd || simEnd > t + 800) simEnd = t + 800;
    return;
  }
  if (mpChoosing && !botBusy(t) && t >= mpWait) { botPress(t, OK, 80); mpWait = t + 600; return; }   // nearby
  if (botBusy(t) || t < mpWait || !simOpen) return;
  LinkCore &L = linkCore();
  if (L.state == LK_PLAYING) { mpPlay(t); return; }
  if (mpStep >= mpSteps.size()) return;
  char s = mpSteps[mpStep];
  uint8_t game = mpStep + 1 < mpSteps.size() ? (uint8_t)(mpSteps[mpStep + 1] - '0') : 0;
  if (s == 'a' || s == 'd') {                                   // ANNA's challenge
    if (L.state != LK_INVITED || !recent("wants to play")) return;
    mpInvited++;
    botPress(t, s == 'a' ? OK : LT, 80);
    mpStep++; mpWait = t + 1500;
    return;
  }
  if (!mpInList() || L.state != LK_LOBBY || !L.peerCount()) return;
  if (s == 'x' || s == 'y') { simBotAnswer = s == 'y'; mpStep++; return; }
  if (s == 'i') { simBotGame = game; simBotInviteAt = linkNow() + 300; mpStep += 2; return; }
  bool own = recent("OK = your name   hold OK = back");
  if (s == 'n') {                                               // own row: OK, one letter up, OK
    if (!own) { botPress(t, UP, 60); return; }
    press(t + 1, OK, 80); press(t + 600, UP); press(t + 1000, OK, 80);
    mpStep++; mpWait = t + 2000;
  } else if (s == 'f' || s == 'c') {
    if (own) { botPress(t, DN, 60); return; }
    if (s == 'f') { botPress(t, RT, 60); mpStep++; mpWait = t + 400; return; }
    press(t + 1, OK, 80);                                       // the game list, then the game
    uint32_t k = t + 700;
    for (uint8_t i = 1; i < game; i++, k += 250) press(k, DN);
    press(k, OK, 80);
    mpStep += 2; mpWait = k + 1200;
  } else if (s == 'q') {
    botPress(t, OK, 1100);                                      // hold OK: back to the games
    mpStep++;
  }
}

// one game online from the lobby to its end
static void onlineGame(uint8_t game, uint32_t t) {
  mpSteps = std::string("c") + (char)('0' + game) + "q";
  simBot = onlineDriver; botFrom = t;
  simBotOther = chessPeer;
  simPeer.caps = LK_CAPS | LK_CAP_CHESS;                        // ANNA is a colour console here
}

// ---------------- online over the internet (inet.h): ANNA, BOB and CARL on the pretend relay (inetsrv.h) ----------------
/* The plan, one step after the other when the lobby is up: 'I' Internet
   in the first list, 'c' + game: challenge ANNA (and play), 'i' + game:
   ANNA challenges us, 'a' / 'd' accept / decline, 'x' / 'y' / 'n' ANNA will
   say no / yes / nothing, 'b' challenge CARL (busy), 'm' the own code
   shown, 'f' BOB added by his code, 'u' a code nobody has, 's' our own
   code, 'o' challenge BOB (a friend, offline), 'r' ANNA a friend with
   RIGHT, 'D' the server goes away in the next game, 'U' back when the
   lobby says so, 'g' ANNA is switched off in the next game, 'q' leave. */
static std::string inSteps;
static size_t   inStep = 0;
static uint32_t inWait = 0, inNoteT = 0, inGameT = 0, inEvent = 0;
static bool     inChose = false, inDownNext = false, inGoneNext = false, inDownSeen = false, inBack = false;
static int      inGames = 0;
static uint32_t inTicks = 0, inGameMs = 0, inLastGame = 0;
static std::vector<int> inResults;                               // ours, as ANNA counts them: 1 she won, 2 she lost, 3 draw
static std::string inTyping;                                     // the code being typed
static const char *inTarget(char s) { return s == 'f' ? "BQB234" : s == 'u' ? "ZZZZZZ" : "MY3CDE"; }

// the row of the lobby with this code ("" = own): moves there, true when it is picked
static bool inPick(uint32_t t, const char *code) {
  InRow rows[IN_LIST * 2];
  uint8_t n = inRows(rows, IN_LIST * 2);
  int want = code[0] ? -1 : 0, cur = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (!strcmp(rows[i].p.code, code)) want = i + 1;
    if (inSelCode[0] && !strcmp(rows[i].p.code, inSelCode)) cur = i + 1;
  }
  if (want < 0) return false;
  if (cur == want) return true;
  botPress(t, cur < want ? DN : UP, 50);
  return false;
}

static void inetDriver(uint32_t t) {
  if (inStep >= inSteps.size() && !inS && pollHook == NULL && inChose) {   // all done and left: the end
    if (!simEnd || simEnd > t + 800) simEnd = t + 800;
    return;
  }
  if (botBusy(t) || t < inWait) return;
  if (mpChoosing) {                                             // nearby or internet: the second
    if (inStep < inSteps.size() && inSteps[inStep] == 'I') { press(t + 1, DN); press(t + 300, OK, 80); inStep++; inChose = true; inWait = t + 800; }
    return;
  }
  if (!inS) {                                                   // pairing first: OK on its message
    if (tvWhere == TVW_SAY && t > inEvent + 400) { botPress(t, OK, 80); inEvent = t; }
    return;
  }
  NetLink &N = inS->n;
  LinkCore &L = linkCore();
  if (mpNoting) {                                               // a message: read, then OK
    if (!inNoteT) inNoteT = t;
    if (t > inNoteT + 500) { botPress(t, OK, 80); inNoteT = 0; inWait = t + 300; }
    return;
  }
  inNoteT = 0;
  if (L.state == LK_PLAYING) {
    if (!inGameT) { inGameT = t; inGames++; }
    if (inDownNext && t > inGameT + 4000) { inDownNext = false; simNetDown = true; printf("      the server goes away at %u ms\n", t); }
    if (inGoneNext && t > inGameT + 4000) { inGoneNext = false; annaStop(); printf("      ANNA is switched off at %u ms\n", t); }
    if (LinkCore::realtime(L.game)) { inTicks = L.syncTick(); inGameMs = t - inGameT; }
    mpPlay(t);
    return;
  }
  if (inGameT) inLastGame = inGameT;
  inGameT = 0;
  if (inLastGame && inWhere == IW_LOBBY) {                     // back from a game: what we saw
    auto after = [](const char *x) { auto it = textAt.find(x); return it != textAt.end() && it->second >= inLastGame; };
    inResults.push_back(after("YOU WIN") ? 2 : after("YOU LOST") ? 1 : after("DRAW") ? 3 : 0);
    inLastGame = 0;
  }
  if (simNetDown && inWhere == IW_LOBBY && inDown()) {         // the lobby says it: the server comes back
    inDownSeen = true;
    if (inStep < inSteps.size() && inSteps[inStep] == 'U') {
      if (!inEvent) inEvent = t;
      if (t > inEvent + 3000) { simNetDown = false; inStep++; inEvent = 0; inBack = true; printf("      the server is back at %u ms\n", t); }
    }
    return;
  }
  if (inWhere == IW_CODE) {                                     // typing a code: letter by letter
    static const char CH[] = "-23456789ABCDEFGHJKMNPQRSTUVWXYZ";
    const char *want = inTyping.c_str();
    static bool shot = false;
    if (!shot && inCodePos == 4) { shot = true; captureMs.push_back(t); }   // a picture half typed
    if (!strcmp(inCode, want)) { botPress(t, OK, 80); inWait = t + 600; return; }
    uint8_t p = inCodePos;
    if (inCode[p] == want[p]) { botPress(t, RT, 40); return; }
    int a = (int)(strchr(CH, inCode[p]) - CH), b = (int)(strchr(CH, want[p]) - CH), n = (int)strlen(CH);
    botPress(t, ((b - a + n) % n) <= n / 2 ? UP : DN, 40);
    return;
  }
  if (inWhere == IW_MYCODE) {
    if (t > inEvent + 1200) { botPress(t, OK, 80); inEvent = t; captureMs.push_back(t - 100); }
    return;
  }
  if (inWhere == IW_INVITED) {
    if (inStep < inSteps.size() && (inSteps[inStep] == 'a' || inSteps[inStep] == 'd') && t > inEvent + 1500) {
      captureMs.push_back(t);
      botPress(t, inSteps[inStep] == 'a' ? OK : LT, 80);
      inStep++; inWait = t + 1000;
    } else if (!inEvent || t > inEvent + 5000) inEvent = t;
    return;
  }
  if (inWhere != IW_LOBBY || N.out != INO_NONE || N.inv || !N.haveLists || inDown() || inStep >= inSteps.size()) return;
  char s = inSteps[inStep];
  uint8_t game = inStep + 1 < inSteps.size() ? (uint8_t)(inSteps[inStep + 1] - '0') : 0;
  switch (s) {
  case 'x': case 'y': case 'n': simBotAnswer = s == 'y' ? 1 : (s == 'x' ? 0 : -1); inStep++; break;
  case 'i': annaInviteGame = game; annaInviteAt = t + 300; inStep += 2; inEvent = t; break;
  case 'D': inDownNext = true; inStep++; break;
  case 'g': inGoneNext = true; inStep++; break;
  case 'a': case 'd': break;                                    // waits for ANNA's challenge
  case 'c': case 'b': case 'o': case 'r': {
    const char *code = s == 'b' ? "CRL567" : (s == 'o' ? "BQB234" : "AN4K7Q");
    if (!inPick(t, code)) break;
    if (s == 'r') { botPress(t, RT, 60); inStep++; inWait = t + 1500; break; }
    press(t + 1, OK, 80);
    if (s != 'c') { inStep++; inWait = t + 1500; break; }
    uint32_t k = t + 700;                                       // the game list, then the game
    for (uint8_t i = 1; i < game; i++, k += 250) press(k, DN);
    press(k, OK, 80);
    inStep += 2; inWait = k + 1200;
    break;
  }
  case 'm': case 'f': case 'u': case 's':                       // the own row's menu
    if (!inPick(t, "")) break;
    press(t + 1, OK, 80);
    if (s != 'm') press(t + 700, DN);
    press(t + 1000, OK, 80);
    inTyping = inTarget(s);
    inStep++; inWait = t + 2000; inEvent = t + 1500;
    break;
  case 'q': captureMs.push_back(t); botPress(t + 20, OK, 1100); inStep++; break;   // the lobby once more, hold OK: back
  default: inStep++; break;
  }
}

// a scenario of internet play: the relay with its delay, ANNA online from the start
static void inetScenario(const char *steps, uint32_t t) {
  inSteps = steps;
  simBot = inetDriver; botFrom = t;
  simBotOther = chessPeer;
  simPeer.caps = LK_CAPS | LK_CAP_CHESS;
  fsSetup();
  annaStart(0);
  simNvsBlob["url"] = std::string("https://arcade.example.de") + '\0';
  simNvsBlob["tok"] = std::string(SIM_ME_TOK) + '\0';           // paired for the TV: ready for the internet too
  if (getenv("SIM_NETMS")) simNetMs = (uint32_t)atoi(getenv("SIM_NETMS"));   // experiments: another relay
  if (getenv("SIM_NETJIT")) simNetJit = (uint32_t)atoi(getenv("SIM_NETJIT"));
}

/* The chess moves on the protocol: two consoles (LinkCore) on a radio that
   loses a third of the packets; a random game, each move as its two
   bytes. Every move must arrive whole and in order: the boards stay equal. */
struct LkNode { LinkCore c; uint8_t mac[6]; std::vector<std::vector<uint8_t>> in; };
static LkNode   lkA, lkB;
static uint32_t lkNow = 0;
static void lkSend(void *ctx, const uint8_t *, const uint8_t *d, uint8_t n) {
  LkNode *to = (LkNode *)ctx == &lkA ? &lkB : &lkA;
  if (rand() % 100 < 33) return;                                // lost on the way
  to->in.push_back(std::vector<uint8_t>(d, d + n));
}
static void lkRun(uint32_t ms) {
  for (uint32_t k = 0; k < ms; k += 10) {
    lkNow += 10;
    LkNode *n[2] = { &lkA, &lkB }, *o[2] = { &lkB, &lkA };
    for (int i = 0; i < 2; i++) {
      std::vector<std::vector<uint8_t>> in;
      in.swap(n[i]->in);
      for (auto &p : in) n[i]->c.onPacket(o[i]->mac, p.data(), (int)p.size(), -50, lkNow);
    }
    lkA.c.tick(lkNow); lkB.c.tick(lkNow);
  }
}
static void chessProtocol() {
  // every legal move of some positions (castling, en passant, promotions) there and back
  const char *FEN[] = { "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -",
                        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq -",
                        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq -",
                        "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 b kq -",
                        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -" };
  int moves = 0, promos = 0, bad = 0;
  for (const char *f : FEN) {
    ChPos p;
    ChMove m[220], got;
    if (!chFromFen(p, f)) { bad++; continue; }
    int n = chMoves(p, m);
    for (int i = 0; i < n; i++) {
      uint8_t b[2];
      mpChessEnc(m[i], b);
      bool ok = b[0] < 64 && (m[i].promo || b[1] < 64) && mpChessDec(m, n, b[0], b[1], &got) &&
                got.from == m[i].from && got.to == m[i].to && got.promo == m[i].promo;
      if (!ok) bad++;
      moves++;
      if (m[i].promo) promos++;
    }
    if (n) {                                                    // what is no move is refused
      uint8_t b[2];
      mpChessEnc(m[0], b);
      if (mpChessDec(m, n, 64 + b[0], b[1], &got)) bad++;       // from off the board
      if (!m[0].promo && mpChessDec(m, n, b[0], b[1] | 0x40, &got)) bad++;   // a promotion that is none
      if (mpChessDec(m, n, b[0], b[0], &got)) bad++;                     // to the square it stands on
    }
  }
  printf("      %d moves (%d promotions) coded and back, %d wrong\n", moves, promos, bad);
  check("every chess move survives the two bytes", moves > 100 && promos >= 8 && !bad);

  // two consoles: the chess flag in the beacon, the challenge, a game on a lossy radio
  static const uint8_t MA[6] = { 0x24, 0x6F, 0x28, 0x10, 0x20, 1 }, MB[6] = { 0x24, 0x6F, 0x28, 0x10, 0x20, 2 };
  memcpy(lkA.mac, MA, 6); memcpy(lkB.mac, MB, 6);
  lkA.c.begin(MA, "AAA", lkSend, &lkA, 0, 11);
  lkB.c.begin(MB, "BBB", lkSend, &lkB, 0, 22);
  lkRun(3000);
  check("a console without Chess does not offer it", lkA.c.peerCount() == 1 && !(lkA.c.peer(0).caps & LK_CAP_CHESS) &&
                                                      (lkA.c.peer(0).caps & LK_CAPS) == LK_CAPS);
  lkB.c.caps = LK_CAPS | LK_CAP_CHESS;
  lkA.c.caps = LK_CAPS | LK_CAP_CHESS;
  lkRun(3000);
  check("the colour console's beacon says Chess", lkA.c.peer(0).caps & LK_CAP_CHESS);
  lkA.c.invite(0, LKG_CHESS, lkNow);
  for (int k = 0; k < 100 && lkB.c.state != LK_INVITED; k++) lkRun(50);
  bool asked = lkB.c.state == LK_INVITED && lkB.c.game == LKG_CHESS;
  lkB.c.answer(true, lkNow);
  for (int k = 0; k < 100 && lkA.c.state != LK_PLAYING; k++) lkRun(50);
  check("challenged for Chess, accepted", asked && lkA.c.state == LK_PLAYING && lkB.c.state == LK_PLAYING &&
                                          lkA.c.iStart != lkB.c.iStart);
  ChPos pa, pb;
  chStart(pa); chStart(pb);
  ChMove *ma = (ChMove *)malloc(sizeof(ChMove) * 220), *mb = (ChMove *)malloc(sizeof(ChMove) * 220);
  int plies = 0, wrong = 0;
  for (; plies < 80; plies++) {
    int na = chMoves(pa, ma), nb = chMoves(pb, mb);
    if (!na || pa.half >= 100) break;
    bool aMoves = (bool)pa.white == lkA.c.iStart;               // whoever begins plays white
    LkNode &from = aMoves ? lkA : lkB, &to = aMoves ? lkB : lkA;
    ChPos &pf = aMoves ? pa : pb, &pt = aMoves ? pb : pa;
    ChMove *lt = aMoves ? mb : ma;
    int nt = aMoves ? nb : na;
    ChMove mv = (aMoves ? ma : mb)[rand() % (aMoves ? na : nb)];
    uint8_t b[2], got[2];
    mpChessEnc(mv, b);
    chPlay(pf, mv);
    int sent = 0, in = 0;
    for (int k = 0; k < 400 && in < 2; k++) {                   // the two bytes, one after the other
      if (sent < 2 && from.c.sendMove(b[sent], lkNow)) sent++;
      uint8_t m;
      if (to.c.moveIn(&m)) got[in++] = m;
      lkRun(20);
    }
    ChMove back;
    if (in < 2 || !mpChessDec(lt, nt, got[0], got[1], &back) || back.from != mv.from || back.to != mv.to || back.promo != mv.promo) { wrong++; break; }
    chPlay(pt, back);
    if (memcmp(pa.b, pb.b, 64) || pa.white != pb.white) { wrong++; break; }
  }
  free(ma); free(mb);
  printf("      %d plies over the lossy radio, %d wrong\n", plies, wrong);
  check("a chess game over a lossy radio: both boards stay the same", plies >= 20 && !wrong);
}

// the shared effects (fx.h): the pool fills, never overflows, empties again, at any frame rate
static void fxLogic() {
  fxReset();
  fxBurst(160, 120, 20, C::YELLOW, C::RED, 200, 400);
  check("fx: a burst takes 20 particles", fxCount() == 20);
  fxBurst(160, 120, 200, C::YELLOW, C::RED, 200, 400);
  check("fx: a full pool hands out its oldest, no overflow", fxCount() == FX_N);
  for (int i = 0; i < 40; i++) { clockUs += 16000; fxUpdate(); }
  check("fx: all particles die after their life", fxCount() == 0);
  int16_t xs[2];                                               // 600 ms in 10 ms or in 30 ms frames
  for (int run = 0; run < 2; run++) {
    fxReset();
    fxEmit(10, 100, 100, -150, 2000, C::WHITE, C::WHITE, 2, 30, 0);
    for (int t = 0; t < 600; t += run ? 30 : 10) { clockUs += (run ? 30 : 10) * 1000; fxUpdate(); }
    for (int i = 0; i < FX_N; i++) if (fxP[i].life) xs[run] = (int16_t)(fxP[i].y >> 8);
    if (!run) check("fx: moves 60 px in 600 ms at 100 px/s", fxP[0].life && abs((int)(fxP[0].x >> 8) - 70) <= 1);
  }
  printf("      a falling particle after 600 ms: y %d (10 ms frames), %d (30 ms frames)\n", xs[0], xs[1]);
  check("fx: the same path at any frame rate", abs(xs[0] - xs[1]) <= 2);
  fxReset();
  fxShake(6, 300);
  bool moved = false;
  for (int i = 0; i < 10; i++) { clockUs += 16000; fxUpdate(); if (fxSX || fxSY) moved = true; if (abs(fxSX) > 6 || abs(fxSY) > 6) moved = false; }
  for (int i = 0; i < 20; i++) { clockUs += 16000; fxUpdate(); }
  check("fx: a shake moves the picture, then dies away", moved && !fxSX && !fxSY);
  fxText(100, 100, "+10", C::YELLOW);
  fxFlash(C::WHITE, 200);
  gfx.clear(C::NAVY);
  fxBurst(100, TOP_H + 2, 40, C::YELLOW, C::RED, 300, 800);
  for (int i = 0; i < 5; i++) { clockUs += 16000; fxUpdate(); }
  fxDraw();
  bool clean = true;
  for (int i = 0; i < SCR_W * TOP_H; i++) if (gfx.buffer()[i] != C::NAVY) clean = false;
  check("fx: drawing keeps out of the status bar", clean);
  for (int i = 0; i < 80; i++) { clockUs += 16000; fxUpdate(); }
  check("fx: texts and the flash end", !fxT[0].s[0] && !fxFlashT && fxCount() == 0);
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
  if (mm.turn != mmMe || mmFly.on) return;                       // mmMe: the seat online
  MauMau c = mm;
  uint8_t m = c.cpuMove(), slot = 0;
  if (m != MM_DRAW && m != MM_PASS) {
    uint8_t cards[MM_CARDS], n = mmHand(mm, mmMe, cards);
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

// ---------------- Palace: a planner over the tiles, the rules alone, and a player ----------------
/* The planner searches the tiles (cheapest first): (tile, gates open,
   exit open) with the moves the runner has - a step to the next tile, a
   jump of 2..4 tiles (4 needs a tile of run-up), down into a hole (one
   floor, or up to three when climbing down first), through a loose tile,
   up to a ledge over a hole. Plates change the gates on the way. Walking
   is cheap, the rest dearer, so the way found is the natural one. A
   scythe is walked past (the player waits for its beat), never jumped
   over, landed on or stood on to jump.                                */
enum { PL_WALK, PL_JUMP, PL_DROP, PL_HANG, PL_LOOSE, PL_CLIMB, PL_EXIT };
struct PlMove { uint8_t type; int8_t dir; uint8_t len; int16_t c, r; };
static const char *const PL_NAME[] = { "walk", "jump", "drop", "hang", "loose", "climb", "exit" };

static uint8_t plApply(uint8_t m, char ch) {                    // bits 0..3 gates open, bit 4 exit open
  if (ch >= '1' && ch <= '4') m |= (uint8_t)(1 << (ch - '1'));
  if (ch >= '5' && ch <= '8') m &= (uint8_t)~(1 << (ch - '5'));
  if (ch == 'X') m |= 16;
  return m;
}
static bool plOpen(char ch, uint8_t m) { return !(ch >= 'a' && ch <= 'd') || (m >> (ch - 'a') & 1); }
static bool plStand(char ch, uint8_t m) { return palIsFloor(ch) && plOpen(ch, m); }

static std::vector<PlMove> plPlan(int16_t c0, int16_t r0, uint8_t m0) {
  static const int COST[6] = { 2, 7, 4, 8, 5, 6 };                // walk jump drop hang loose climb
  const int N = PAL_MAXR * PAL_MAXC * 32;
  static std::vector<int> from, dist; static std::vector<PlMove> how;
  from.assign(N, -2); dist.assign(N, 1 << 30); how.resize(N);
  auto id = [](int c, int r, int m) { return (r * PAL_MAXC + c) * 32 + m; };
  std::priority_queue<std::pair<int, int>, std::vector<std::pair<int, int>>, std::greater<std::pair<int, int>>> q;
  int s0 = id(c0, r0, plApply(m0, palAt(c0, r0)));
  from[s0] = -1; dist[s0] = 0; q.push({ 0, s0 });
  int goal = -1;
  while (!q.empty() && goal < 0) {
    int ds = q.top().first, s = q.top().second;
    q.pop();
    if (ds > dist[s]) continue;
    int m = s % 32, cell = s / 32, r = cell / PAL_MAXC, c = cell % PAL_MAXC;
    char here = palAt(c, r);
    if (here == 'E' && (m & 16)) { goal = s; break; }
    auto go = [&](int c1, int r1, PlMove mv) {
      int t = id(c1, r1, plApply((uint8_t)m, palAt(c1, r1))), dt = ds + COST[mv.type] + (mv.type == PL_JUMP ? mv.len : 0);
      if (dt >= dist[t]) return;
      dist[t] = dt; from[t] = s; how[t] = mv; q.push({ dt, t });
    };
    bool loose = here == '~';
    for (int d = -1; d <= 1; d += 2) {
      char n = palAt(c + d, r);
      if (plStand(n, m)) go(c + d, r, { PL_WALK, (int8_t)d, 1, (int16_t)(c + d), (int16_t)r });
      if (loose || here == 'K') continue;                       // nobody stops on a loose tile or a scythe
      for (int L = 2; L <= 4; L++) {
        char x = palAt(c + d * (L - 1), r);
        if (x == '#' || x == 'K' || !plOpen(x, m)) break;
        if (L == 4 && (!plStand(palAt(c - d, r), m) || strchr("~^K", palAt(c - d, r)))) continue;
        char t = palAt(c + d * L, r);
        if (plStand(t, m) && t != '^' && t != 'K') go(c + d * L, r, { PL_JUMP, (int8_t)d, (uint8_t)L, (int16_t)(c + d * L), (int16_t)r });
      }
      if (n == ' ') {
        int rr = r + 1;
        while (rr < palH && palAt(c + d, rr) == ' ') rr++;
        char t = palAt(c + d, rr);
        if (rr < palH && palIsFloor(t) && t != '^' && t != 'K' && plOpen(t, m) && rr - r <= 3)
          go(c + d, rr, { (uint8_t)(rr - r == 1 ? PL_DROP : PL_HANG), (int8_t)d, (uint8_t)(rr - r), (int16_t)(c + d), (int16_t)rr });
      }
      if (r > 0 && palAt(c, r - 1) == ' ' && plStand(palAt(c + d, r - 1), m) && palAt(c + d, r - 1) != 'K')
        go(c + d, r - 1, { PL_CLIMB, (int8_t)d, 1, (int16_t)(c + d), (int16_t)(r - 1) });
    }
    if (loose) {
      int rr = r + 1;
      while (rr < palH && palAt(c, rr) == ' ') rr++;
      char t = palAt(c, rr);
      if (rr < palH && palIsFloor(t) && t != '^' && t != 'K' && rr - r <= 2) go(c, rr, { PL_LOOSE, 0, (uint8_t)(rr - r), (int16_t)c, (int16_t)rr });
    }
  }
  std::vector<PlMove> path;
  if (goal < 0) return path;
  for (int s = goal; from[s] >= 0; s = from[s]) path.insert(path.begin(), how[s]);
  int gc = goal / 32 % PAL_MAXC, gr = goal / 32 / PAL_MAXC;
  path.push_back({ PL_EXIT, 0, 0, (int16_t)gc, (int16_t)gr });
  return path;
}

static PalIn plIn(bool l, bool r, bool u, bool d, bool uT = false, bool dT = false, bool okT = false) {
  PalIn in; in.l = l; in.r = r; in.u = u; in.d = d; in.uT = uT; in.dT = dT; in.okT = okT; return in;
}
static void plRun(PalIn in, int n) { for (int i = 0; i < n; i++) { palStep(in); in.uT = in.dT = in.okT = false; } }
static bool plGround() { uint8_t s = palHero.st; return s == PH_STAND || s == PH_RUN || s == PH_STEP || s == PH_LAND; }

static void palaceLogic() {
  int bad = 0, mech[PAL_LEVELS][6] = {};
  // ---- the levels: well formed, and the exit can be reached ----
  for (uint8_t lv = 0; lv < PAL_LEVELS; lv++) {
    const PalLevel &L = PAL_LEVEL[lv];
    int W = L.w * 10, H = L.h * 3, starts = 0, doors = 0, xs = 0, guards = 0, bosses = 0;
    if (W > PAL_MAXC || H > PAL_MAXR) { bad++; printf("      level %u too big\n", lv + 1); continue; }
    for (int r = 0; r < H; r++) {
      if ((int)strlen(L.rows[r]) != W) { bad++; printf("      level %u row %d: %d tiles\n", lv + 1, r, (int)strlen(L.rows[r])); continue; }
      for (int c = 0; c < W; c++) {
        char ch = L.rows[r][c], below = r + 1 < H ? L.rows[r + 1][c] : ' ';
        if (!strchr(" _#t|FR^~Kabcd12345678XEhHSGCW@", ch)) { bad++; printf("      level %u: tile '%c'\n", lv + 1, ch); }
        starts += ch == '@'; doors += ch == 'E'; xs += ch == 'X'; guards += ch == 'G' || ch == 'C' || ch == 'W'; bosses += ch == 'W';
        mech[lv][0] += ch == 'K'; mech[lv][1] += ch == '~'; mech[lv][2] += ch == '^'; mech[lv][3] += ch >= 'a' && ch <= 'd';
        mech[lv][4] += ch == 'h' || ch == 'H'; mech[lv][5] += ch == 'G' || ch == 'C' || ch == 'W';
        if (ch == 'K' && (c == 0 || !strchr("_t|F", L.rows[r][c - 1]) || c + 1 >= W || !strchr("_t|F", L.rows[r][c + 1]))) {
          bad++; printf("      level %u (%d,%d): a scythe needs plain floor on both sides\n", lv + 1, c, r);
        }
        if ((ch == ' ' || ch == '~') && below == '#') { bad++; printf("      level %u (%d,%d): a hole or loose tile on a wall\n", lv + 1, c, r); }
        if (ch >= 'a' && ch <= 'd') {
          bool plate = false;
          for (int rr = 0; rr < H; rr++) if (strchr(L.rows[rr], '1' + (ch - 'a'))) plate = true;
          if (!plate) { bad++; printf("      level %u: gate %c has no plate\n", lv + 1, ch); }
        }
      }
    }
    if (starts != 1 || doors != 1 || xs < 1 || guards > PAL_NG) { bad++; printf("      level %u: %d starts, %d doors, %d exit plates, %d guards\n", lv + 1, starts, doors, xs, guards); }
    if ((bosses == 1) != (lv == PAL_LEVELS - 1)) { bad++; printf("      level %u: %d Wardens\n", lv + 1, bosses); }
    palStartHp = palFlasksAt(lv); palStartSword = lv > 0;
    palLoad(lv);
    std::vector<PlMove> p = plPlan(palColOf(palHero.x), palRowOf(palHero.y), 0);
    int n[7] = { 0, 0, 0, 0, 0, 0, 0 };
    for (auto &m : p) n[m.type]++;
    printf("      level %u %-17s %dx%d rooms, %d guards: %s in %d moves (%d jumps, %d drops, %d climbs, %d hangs)\n", lv + 1,
           L.name, L.w, L.h, guards, p.empty() ? "NO WAY OUT" : "exit", (int)p.size(), n[PL_JUMP], n[PL_DROP] + n[PL_LOOSE], n[PL_CLIMB], n[PL_HANG]);
    if (p.empty()) bad++;
    if (getenv("PALPATH")) for (auto &m : p) printf("        %s %d,%d\n", PL_NAME[m.type], m.c, m.r);
    uint8_t mm = 0;                                             // the moves of the way, by the state of the gates on it
    for (auto &m : p) mm = plApply(mm, palAt(m.c, m.r));
    if (lv == PAL_LEVELS - 1) {                                 // the way out leads past the Warden
      bool past = false;
      for (uint8_t g = 0; g < palNG; g++)
        if (palG[g].sk == PAL_BOSS) for (auto &m : p) past |= m.r == palG[g].row && m.c > palColOf(palG[g].x0) && m.c >= palColOf(palG[g].x1);
      if (!past) { bad++; printf("      level %u: the exit is not behind the Warden\n", lv + 1); }
    }
    if (lv == 0) {                                              // the sword lies on the way, before the guard
      bool sword = false, ok = true;
      for (auto &m : p) {
        if (palAt(m.c, m.r) == 'S') sword = true;
        for (uint8_t g = 0; g < palNG; g++) if (palG[g].row == m.r && abs(palColOf(palG[g].x) - m.c) <= 1 && !sword) ok = false;
      }
      if (!sword || !ok) { bad++; printf("      level 1: the sword is not found before the guard\n"); }
    }
  }
  check("palace: levels well formed, every exit can be reached", bad == 0);
  {                                                             // the later levels: harder, with more of everything
    int prev = 0;
    bool more = true;
    for (uint8_t lv = 0; lv < PAL_LEVELS; lv++) {
      int traps = mech[lv][0] * 3 + mech[lv][1] + mech[lv][2] + mech[lv][3] * 2 + mech[lv][5] * 2 + (PAL_LEVEL[lv].timed ? 4 : 0);
      printf("      level %u: %d scythes, %d loose, %d spikes, %d gates%s, %d potions, %d foes (skill %u/%u%s): %d\n", lv + 1,
             mech[lv][0], mech[lv][1], mech[lv][2], mech[lv][3], PAL_LEVEL[lv].timed ? " (timed)" : "", mech[lv][4], mech[lv][5],
             PAL_SKILL[lv].block, PAL_SKILL[lv].wind, lv == PAL_LEVELS - 1 ? " + the Warden" : "", traps);
      if (lv >= 4 && traps < prev) more = false;
      if (lv >= 4) prev = traps;
      if (lv && (PAL_SKILL[lv].block < PAL_SKILL[lv - 1].block || PAL_SKILL[lv].wind > PAL_SKILL[lv - 1].wind)) more = false;
    }
    bool mix = mech[6][0] && mech[7][0] && PAL_LEVEL[4].timed && PAL_LEVEL[6].timed && PAL_LEVEL[7].timed && mech[5][1] >= 3;
    check("palace: eight levels, the guards better each time, the new ones use scythes, timed gates, loose chains", PAL_LEVELS == 8 && more && mix);
    printf("      going on from a level: flasks %u %u %u %u %u %u %u %u, %lu s each\n", palFlasksAt(0), palFlasksAt(1), palFlasksAt(2),
           palFlasksAt(3), palFlasksAt(4), palFlasksAt(5), palFlasksAt(6), palFlasksAt(7), (unsigned long)(PAL_TIME / PAL_LEVELS / 1000));
    check("palace: twenty minutes, going on later gives the flasks of the big draughts before", PAL_TIME == 1200000UL &&
          palFlasksAt(0) == 3 && palFlasksAt(2) == 4 && palFlasksAt(7) >= palFlasksAt(4) && palFlasksAt(7) <= 8);
  }

  // ---- falling: one floor is safe, two hurt, three kill; climbing down first saves one ----
  {
    static const char *const R0 = "#@_ ##", *const RM = "### ##", *const RL = "###_##", *const RB = "######";
    int got[2][4];
    for (int hang = 0; hang < 2; hang++)
      for (int k = 1; k <= 4; k++) {
        const char *rows[7];
        int n = 0;
        rows[n++] = R0;
        for (int i = 1; i < k; i++) rows[n++] = RM;
        rows[n++] = RL; rows[n++] = RB;
        palStartHp = 3; palStartSword = false;
        palLoadRows(rows, 6, (uint8_t)n, 0);
        if (hang) {
          plRun(plIn(false, true, false, true), 60);           // careful steps to the edge
          plRun(plIn(false, false, false, false, false, true), 25);   // climb down ...
          plRun(plIn(false, false, false, false, false, true), 1);    // ... and let go
        } else plRun(plIn(false, true, false, false), 40);    // run off
        plRun(plIn(false, false, false, false), 150);
        got[hang][k - 1] = palHero.st == PH_DEAD ? -1 : palHero.hp;
      }
    printf("      flasks left after a fall of 1..4 floors: run off %d %d %d %d, climbed down %d %d %d %d\n",
           got[0][0], got[0][1], got[0][2], got[0][3], got[1][0], got[1][1], got[1][2], got[1][3]);
    check("palace: one floor safe, two hurt, three kill", got[0][0] == 3 && got[0][1] == 2 && got[0][2] == -1);
    check("palace: climbing down first: two floors safe, three hurt", got[1][0] == 3 && got[1][1] == 3 && got[1][2] == 2 && got[1][3] == -1);
  }

  // ---- jumps: a running jump clears three tiles, a standing jump one or two ----
  {
    static const char *const RJ[2] = { "#@_____   ______#", "#_______________#" };
    int lo = -1, hi = -1;
    for (int xp = 60; xp <= 230; xp += 2) {
      palStartHp = 3; palLoadRows(RJ, 17, 2, 0);
      bool pressed = false;
      for (int i = 0; i < 400; i++) {
        bool now = !pressed && palHero.st == PH_RUN && (palHero.x >> 8) >= xp;
        if (now) pressed = true;
        palStep(plIn(false, !pressed || now, palHero.st == PH_HANG || palHero.st == PH_CLIMB, false, now));
        if (pressed && plGround() && palHero.st != PH_LAND) break;
      }
      bool ok = pressed && palRowOf(palHero.y) == 0 && palColOf(palHero.x) >= 10 && palHero.st != PH_DEAD;
      if (ok && lo < 0) lo = xp;
      if (ok) hi = xp;
    }
    printf("      running jump over three tiles works when UP comes at x %d..%d (the edge at 224)\n", lo, hi);
    check("palace: a running jump clears three tiles, with room for timing", lo > 0 && hi - lo >= 24);
    static const char *const R2[2] = { "#@____ ______#", "#____________#" }, *const R3[2] = { "#@____  _____#", "#____________#" };
    palLoadRows(R2, 14, 2, 0);
    palHero.x = (int32_t)(palEdge(5, 1) - 26) << 8;
    plRun(plIn(false, true, false, false, true), 1); plRun(plIn(false, false, false, false), 80);
    int c2 = palColOf(palHero.x), r2 = palRowOf(palHero.y);
    palLoadRows(R3, 14, 2, 0);
    palHero.x = (int32_t)(palEdge(5, 1) - 6) << 8;
    plRun(plIn(false, true, false, false, true), 1); plRun(plIn(false, false, false, false), 80);
    int c3 = palColOf(palHero.x), r3 = palRowOf(palHero.y);
    printf("      standing jumps: from the back of a tile to tile %d (row %d), from the edge to tile %d (row %d)\n", c2, r2, c3, r3);
    check("palace: a standing jump clears one tile from the back, two from the edge", c2 == 7 && r2 == 0 && c3 == 8 && r3 == 0);
  }

  // ---- loose floor, spikes, a gate and its plates, climbing up ----
  {
    static const char *const RL[2] = { "#__@__#", "#_____#" }, *const RR[2] = { "#@_~__#", "#_____#" };
    palLoadRows(RL, 7, 2, 0);
    palMap[0][3] = '~';
    plRun(plIn(false, false, false, false), 15);
    bool still = palMap[0][3] == '~' && palRowOf(palHero.y) == 0;
    plRun(plIn(false, false, false, false), 80);
    check("palace: a loose tile holds a moment, then falls with the runner onto rubble",
          still && palMap[0][3] == ' ' && palMap[1][3] == '-' && palRowOf(palHero.y) == 1 && palHero.hp == 3 && plGround());
    palLoadRows(RR, 7, 2, 0);
    plRun(plIn(false, true, false, false), 60);
    plRun(plIn(false, false, false, false), 60);
    check("palace: running over it is fast enough", palRowOf(palHero.y) == 0 && palMap[0][3] == ' ');

    static const char *const RS[1] = { "#@_^___#" };
    palLoadRows(RS, 8, 1, 0);
    plRun(plIn(false, true, false, false), 60);
    bool runDead = palHero.st == PH_DEAD && palHero.how == 1;
    palLoadRows(RS, 8, 1, 0);
    plRun(plIn(false, true, false, true), 150);
    check("palace: spikes kill a running runner, a careful step passes", runDead && palHero.st != PH_DEAD && palColOf(palHero.x) >= 5);

    static const char *const RG[1] = { "#@__a____#" }, *const RP[1] = { "#@1_a_5__#" };
    palLoadRows(RG, 10, 1, 0);
    plRun(plIn(false, true, false, false), 80);
    int blocked = (int)(palHero.x >> 8);
    palLoadRows(RP, 10, 1, 0);
    plRun(plIn(false, true, false, false), 55);
    bool through = (palHero.x >> 8) > 4 * PAL_TW + 16;
    plRun(plIn(false, true, false, false), 60);
    printf("      a shut gate stops the runner at x %d (gate at %d)\n", blocked, 4 * PAL_TW + 16);
    check("palace: a shut gate stops, its plate opens it, the other plate shuts it again",
          blocked <= 4 * PAL_TW + 7 && through && palGateWant == 0 && palGate[0] == 0 && palColOf(palHero.x) >= 7);

    static const char *const RC[2] = { "#__ __#", "#__@__#" };
    palLoadRows(RC, 7, 2, 0);
    plRun(plIn(false, false, true, false, true), 1); plRun(plIn(false, false, true, false), 60);
    check("palace: UP under a hole jumps, catches the ledge and climbs on", palRowOf(palHero.y) == 0 && palColOf(palHero.x) == 4 && plGround());
  }

  // ---- a sword fight: hits cost a flask, a parry none; the level 1 guard can be beaten ----
  {
    static const char *const RF[1] = { "#@_____G__#" };
    palStartHp = 3; palStartSword = true;
    palLoadRows(RF, 11, 1, 0);
    plRun(plIn(false, false, false, false), 300);              // just stand there
    bool hurt = palHero.hp < 3, fought = palHero.st == PH_FIGHT || palHero.st == PH_DEAD;
    palLoadRows(RF, 11, 1, 0);
    int parries = 0, strikes = 0;
    uint8_t ghp = palG[0].hp;
    for (int i = 0; i < 3000 && palG[0].st < PG_DYING && palHero.st != PH_DEAD; i++) {
      PalGuard &g = palG[0];
      bool block = g.st == PG_STRIKE && g.t + 4 >= PAL_SKILL[g.sk].wind && palHero.act == PA_NONE;
      bool hit = !block && palHero.st == PH_FIGHT && palHero.act == PA_NONE && g.st != PG_STRIKE && abs((g.x - palHero.x) >> 8) <= 46;
      parries += block; strikes += hit;
      palStep(plIn(false, palHero.st != PH_FIGHT, false, false, block, false, hit));
    }
    printf("      fight: %d strikes, %d parries, guard %u -> %u, runner %u flasks\n", strikes, parries, ghp, palG[0].hp, palHero.hp);
    check("palace: a guard's blow hurts an idle runner", hurt && fought);
    check("palace: parrying and striking beat the level 1 guard", palG[0].st >= PG_DYING && palHero.hp == 3);
  }
  // ---- scythes: they come out in a beat; a run at the right moment passes, at the wrong one it does not ----
  {
    static const char *const RK[1] = { "#@_K___#" };
    palStartHp = 3; palStartSword = false;
    palLoadRows(RK, 8, 1, 0);
    plRun(plIn(false, false, false, false), 200);
    bool idle = palHero.st != PH_DEAD;                           // a tile away it does nothing
    int pass = 0, die = 0;
    for (int ph = 0; ph < PAL_SCYP; ph++) {                     // a run from the tile before it, started at each moment of the beat
      palLoadRows(RK, 8, 1, 0);
      palHero.x = (int32_t)(2 * PAL_TW + PAL_TW / 2) << 8;
      while (palScy(3, 0) != ph) palTicks++;
      plRun(plIn(false, true, false, false), 50);
      if (palHero.st == PH_DEAD && palHero.how == 2) die++;
      else if (palColOf(palHero.x) >= 5) pass++;
    }
    palLoadRows(RK, 8, 1, 0);                                   // careful steps from just before it, after it went in
    palHero.x = (int32_t)(3 * PAL_TW + PAL_TW / 2 - PAL_SCYW - 1) << 8;
    while (palScy(3, 0) != PAL_SCYK) palTicks++;
    plRun(plIn(false, true, false, true), 70);
    bool careful = palHero.st != PH_DEAD && palColOf(palHero.x) >= 4;
    printf("      a run past a scythe: passes when started at %d of %d moments, cut down at %d\n", pass, PAL_SCYP, die);
    check("palace: a scythe cuts down who runs into its beat, a run or careful steps after it went in pass",
          idle && pass >= 24 && die >= 8 && pass + die == PAL_SCYP && careful);
  }
  // ---- timed gates: open while the plate is held and a while after; a tile lying on the plate holds it for good ----
  {
    static const char *const RT[1] = { "#@1__a___#" };
    palLoadRows(RT, 10, 1, 0); palTimed = 1; palGt = 20;        // 2 s
    plRun(plIn(false, true, false, true), 40);                  // careful steps onto the plate
    plRun(plIn(false, false, false, false), 200);
    bool held = palColOf(palHero.x) == 2 && palGate[0] == 64;
    plRun(plIn(true, false, false, false), 30);                 // off it, and wait
    plRun(plIn(false, false, false, false), 80);
    bool still = palGate[0] == 64;
    plRun(plIn(false, false, false, false), 60);
    bool shut = palGate[0] == 0 && palGateWant == 0;
    plRun(plIn(false, true, false, false), 90);                 // over the plate and straight on: through
    bool through = palColOf(palHero.x) >= 7;
    plRun(plIn(false, false, false, false), 200);
    bool after = palGate[0] == 0;
    palLoadRows(RT, 10, 1, 0); palTimed = 1; palGt = 20;
    palPlate('1', true);                                        // rubble on the plate
    plRun(plIn(false, false, false, false), 600);
    printf("      a timed gate: held %d, open a while %d, then shut %d, run through %d, shut behind %d, held by rubble %d\n",
           held, still, shut, through, after, palGate[0] == 64);
    check("palace: a timed gate stays open while its plate is held, falls shut soon after, rubble holds it",
          held && still && shut && through && after && palGate[0] == 64);
  }
  // ---- pools: the deepest pits of the baths and the forges hold water or iron ----
  {
    static const char *const RW[2] = { "#@_ ___#", "### ####" };
    palLoadRows(RW, 8, 2, 0); palPool = 1;
    plRun(plIn(false, true, false, false), 40);
    plRun(plIn(false, false, false, false), 60);
    bool drown = palHero.st == PH_DEAD && palHero.how == 3 && (palHero.y >> 8) < 2 * PAL_TH + 80;
    check("palace: a fall into a pool is the end", drown);
  }
  // ---- the Warden: more flasks, he rages at half of them, but parries and blows beat him ----
  {
    static const char *const RB[1] = { "#@_____W__#" };
    palStartHp = 8; palStartSword = true;
    palLoadRows(RB, 11, 1, 0);
    bool rage = false;
    uint8_t ghp = palG[0].hp;
    for (int i = 0; i < 6000 && palG[0].st < PG_DYING && palHero.st != PH_DEAD; i++) {
      PalGuard &g = palG[0];
      bool block = g.st == PG_STRIKE && g.t + 4 >= PAL_SKILL[g.sk].wind && palHero.act == PA_NONE;
      bool hit = !block && palHero.st == PH_FIGHT && palHero.act == PA_NONE && g.st != PG_STRIKE && abs((g.x - palHero.x) >> 8) <= 46;
      palStep(plIn(false, palHero.st != PH_FIGHT, false, false, block, false, hit));
      rage |= palMsgT && palMsg && !strcmp(palMsg, "THE WARDEN RAGES!");
    }
    printf("      the Warden: %u flasks -> %u, raged %d, runner %u of %u flasks\n", ghp, palG[0].hp, rage, palHero.hp, palHero.maxHp);
    check("palace: the Warden is the strongest, rages, and can be beaten", ghp == 9 && rage && palG[0].st >= PG_DYING && palHero.st != PH_DEAD &&
          PAL_SKILL[PAL_BOSS].hp > PAL_SKILL[PAL_CAPT].hp && PAL_SKILL[PAL_BOSS].block >= PAL_SKILL[PAL_CAPT].block);
  }
  palStartSword = false;
}

// ---- a player for the real game: plans with plPlan, presses the keys ----
static uint8_t  pbMask = 0, pbTap = 0;
static uint32_t pbTapStart = 0, pbTapEnd = 0, pbBusy = 0;
static uint8_t  pbPre = 0;
static bool     pbClimb = false, pbDrop = false;
static int      pbShots = 0;
static std::set<std::string> pbShot;
static void pbWrite(const char *name, const uint8_t *px) {      // frames/<scenario>_<name>.ppm
  char fn[128];
  snprintf(fn, sizeof(fn), "frames/%s_%s.ppm", scenario.c_str(), name);
  FILE *f = fopen(fn, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
  for (int i = 0; i < SCR_W * SCR_H; i++) { uint8_t v[3]; colToRgb(px[i], v[0], v[1], v[2]); fwrite(v, 1, 3, f); }
  fclose(f);
}
static void pbSnap(const char *name) {                          // a named screenshot of the last frame, the first time
  if (pbShot.count(name)) return;
  pbShot.insert(name);
  pbWrite(name, lastFrame);
  pbShots++;
  static FILE *ev = NULL;                                       // when, for the video's highlights
  if (!ev) { char fn[96]; snprintf(fn, sizeof(fn), "frames/%s_events.txt", scenario.c_str()); ev = fopen(fn, "w"); }
  if (ev) { fprintf(ev, "%u %s\n", (unsigned)(clockUs / 1000), name); fflush(ev); }
}
// a press of k for len ms; a key that is down already is let go first (a new press is what counts)
static void pbPress(uint32_t t, uint8_t k, uint32_t len) {
  uint8_t fresh = k == OK ? OK : (uint8_t)(k & (UP | DN));
  pbPre = (uint8_t)(pbMask & ~fresh);
  pbTap = k; pbTapStart = (pbMask & fresh) ? t + 40 : t; pbTapEnd = pbTapStart + len; pbBusy = pbTapEnd + 40;
}

static int pbDeaths = 0;
static uint8_t palaceStart() { return scenario == "palacecont" ? 5 : 0; }
static void palaceBot(uint32_t t) {
  PalHero &h = palHero;
  static bool wasDead = false;
  if (palPhase == 1 && h.st == PH_DEAD && !wasDead) {
    pbDeaths++;
    printf("      %6u level %u: the runner died (%s) at %d,%d\n", t, palLv + 1, h.how == 1 ? "spikes" : h.how == 2 ? "scythe" :
           h.how == 3 ? "pool" : "fall or blow", palColOf(h.x), palRowOf(h.y));
  }
  wasDead = h.st == PH_DEAD;
  uint8_t hold = 0;
  if (t < pbTapEnd) {}                                          // a press runs
  else if (palPhase == 0) { if (t >= pbBusy) { pbPress(t, OK, 90); pbBusy = t + 1800; } }   // the title, the choice
  else if (palPhase == 1 && t >= pbBusy) {
    int16_t c = palColOf(h.x), r = palRowOf(h.y);
    int16_t x = (int16_t)(h.x >> 8);
    PalGuard *g = h.st == PH_FIGHT ? palFoe(150) : NULL;
    if (h.st == PH_FIGHT && g) {
      int16_t dist = (int16_t)abs((g->x - h.x) >> 8);
      if (g->st == PG_STRIKE && g->t + 7 >= PAL_SKILL[g->sk].wind && h.act == PA_NONE) pbPress(t, UP, 80);
      else if (h.act == PA_NONE && dist <= 46 && g->st != PG_STRIKE && g->st != PG_BLOCK) pbPress(t, OK, 80);
      else if (dist > 46 && h.act == PA_NONE) hold = g->x > h.x ? RT : LT;
    } else if (h.st == PH_HANG) {
      if (pbDrop) { pbDrop = false; pbPress(t, DN, 90); }
      else hold = UP;
    } else if (h.st == PH_JUP || h.st == PH_CLIMB) {
      hold = pbClimb ? UP : 0;
    } else if (h.st == PH_STAND || h.st == PH_RUN || h.st == PH_STEP || h.st == PH_FIGHT) {
      pbClimb = false;
      uint8_t m = palGateWant | (palExitOn ? 16 : 0);
      std::vector<PlMove> p = plPlan(c, r, m);
      if (!p.empty()) {
        PlMove mv = p[0];
        uint8_t dk = mv.dir > 0 ? RT : LT;
        if (mv.type == PL_WALK) {
          // on or onto spikes: careful steps to the first tile past them; else run up to the next spikes
          bool spikes = palAt(c, r) == '^' || palAt(mv.c, mv.r) == '^';
          size_t k = 0;
          for (; k < p.size() && p[k].type == PL_WALK && p[k].dir == mv.dir; k++) {
            char ch = palAt(p[k].c, p[k].r);
            if (spikes ? ch != '^' : (ch == '^' && k > 0)) { k += spikes; break; }
          }
          if (k > p.size()) k = p.size();
          // a scythe on the way: wait on the tile before it until it has just gone in, then run past
          bool wait = false;
          for (size_t j = 0; j < k; j++)
            if (palAt(p[j].c, p[j].r) == 'K') {
              uint8_t ph = palScy(p[j].c, p[j].r);
              bool atIt = j == 0 && abs(c * PAL_TW + PAL_TW / 2 - x) <= 6;
              bool going = h.st == PH_RUN && h.vx * mv.dir > 0 && ph >= PAL_SCYK && ph <= PAL_SCYK + 30;   // on its way
              if (j > 0) k = j;
              else if (!going && (!atIt || ph < PAL_SCYK || ph > PAL_SCYK + 20)) wait = true;
              else for (size_t j2 = 1; j2 < k; j2++) if (palAt(p[j2].c, p[j2].r) == 'K') { k = j2; break; }
              break;
            }
          while (k > 1 && strchr("~K", palAt(p[k - 1].c, p[k - 1].r)) && !(k < p.size() && p[k].type == PL_LOOSE)) k--;
          int16_t tx = (int16_t)(p[k - 1].c * PAL_TW + PAL_TW / 2), dx = (int16_t)(tx - x);
          if (wait) dx = (int16_t)(c * PAL_TW + PAL_TW / 2 - x);
          if (h.st == PH_FIGHT) hold = dx > 0 ? RT : LT;
          else if (abs(dx) <= 5) hold = 0;
          else hold = (uint8_t)((dx > 0 ? RT : LT) | (spikes || abs(dx) < 14 ? DN : 0));
        } else if (mv.type == PL_JUMP) {
          int16_t b = palEdge(c, mv.dir);
          if (mv.len < 4) {
            int16_t tx = (int16_t)(b - mv.dir * (mv.len == 2 ? 26 : 6)), dx = (int16_t)(tx - x);
            if (abs(dx) > 2) hold = (uint8_t)((dx > 0 ? RT : LT) | DN);
            else if (h.vx == 0) { hold = dk; pbPress(t, (uint8_t)(UP | dk), 150); pbBusy = t + 400; }
          } else {
            int16_t start = (int16_t)(b - mv.dir * 50), press = (int16_t)(b - mv.dir * 30);
            if (h.st == PH_RUN && h.dir == mv.dir && mv.dir * (x - (b - mv.dir * 56)) > 0) {
              hold = dk;
              if (mv.dir * (x - press) >= 0) { pbPress(t, (uint8_t)(UP | dk), 150); pbBusy = t + 400; }
            } else if (mv.dir * (x - start) > 3) hold = (uint8_t)((mv.dir > 0 ? LT : RT) | (abs(x - start) < 14 ? DN : 0));
            else hold = dk;
          }
        } else if (mv.type == PL_DROP) hold = dk;
        else if (mv.type == PL_HANG) {
          int16_t b = palEdge(c, mv.dir);
          if (abs(b - mv.dir * 4 - x) > 1) hold = (uint8_t)(dk | DN);
          else if (h.vx == 0) { pbDrop = true; pbPress(t, DN, 90); pbBusy = t + 300; }
        } else if (mv.type == PL_LOOSE) {
          int16_t dx = (int16_t)(c * PAL_TW + PAL_TW / 2 - x);
          if (abs(dx) > 3) hold = (uint8_t)((dx > 0 ? RT : LT) | DN);
        } else if (mv.type == PL_CLIMB) {
          int16_t dx = (int16_t)(c * PAL_TW + PAL_TW / 2 - x);
          if (abs(dx) > 6) hold = (uint8_t)((dx > 0 ? RT : LT) | DN);
          else if (h.dir != mv.dir) { pbPress(t, dk, 30); pbBusy = t + 120; }
          else if (h.vx == 0) { pbClimb = true; pbPress(t, UP, 150); }
        } else if (mv.type == PL_EXIT) { pbPress(t, UP, 90); pbBusy = t + 600; }
      }
    }
    if (pbClimb && (h.st == PH_JUP || h.st == PH_HANG || h.st == PH_CLIMB)) hold = UP;
  } else hold = palPhase == 1 && pbClimb ? UP : 0;              // waiting for the game to answer
  uint8_t m = t < pbTapStart ? pbPre : (t < pbTapEnd ? pbTap : hold);
  if (m != pbMask) { applyMask(m); pbMask = m; }
  // PALDBG=<from ms> [PALSTEP=<every ms>] ./colorsim palace: a trace of the runner and the plan
  static const bool dbg = getenv("PALDBG") != NULL;
  static uint32_t dbgFrom = getenv("PALDBG") ? (uint32_t)atoi(getenv("PALDBG")) : 0, dbgStep = getenv("PALSTEP") ? (uint32_t)atoi(getenv("PALSTEP")) : 250;
  if (dbg && t >= dbgFrom && t % dbgStep == 0 && palPhase == 1) {
    std::vector<PlMove> p = plPlan(palColOf(h.x), palRowOf(h.y), palGateWant | (palExitOn ? 16 : 0));
    printf("      %6u L%u st %2u x %4d y %4d (%2d,%d) vx %4d hp %u keys %02x  %s %d,%d\n", t, palLv + 1, h.st, (int)(h.x >> 8),
           (int)(h.y >> 8), palColOf(h.x), palRowOf(h.y), (int)h.vx, h.hp, pbMask, p.empty() ? "-" : PL_NAME[p[0].type],
           p.empty() ? 0 : p[0].c, p.empty() ? 0 : p[0].r);
  }

  static bool ending = false;                                   // free (or out of time): see the score, then stop
  if (!ending && palPhase == 2 && (seen("FREE AT LAST!") || seen("TIME IS UP"))) { ending = true; simEnd = t + 9000; }
  // pictures of the moments worth showing, the first time each happens: L<level>_<what>
  char n[48];
  auto snap = [&](const char *what) { snprintf(n, sizeof(n), "L%u_%s", palLv + 1, what); pbSnap(n); };
  static uint8_t lastPhase = 0;
  static uint32_t phaseT = 0, endT = 0;
  if (palPhase != lastPhase) { lastPhase = palPhase; phaseT = t; }
  if (!endT && seen("FREE AT LAST!")) endT = t;
  if (palPhase == 0 && t + 60 == pbBusy) pbSnap(seen("new game") ? "menu_continue" : "title");   // before OK
  if (palPhase != 1) {
    if (palPhase == 2 && t - phaseT == 500 && !endT) snap(h.st == PH_EXIT ? "level_complete" : "banner");
    if (endT && t == endT + 2600) pbSnap("ending");
    return;
  }
  const char *msg = palMsgT >= 100 ? palMsg : NULL;
  int16_t c = palColOf(h.x), r = palRowOf(h.y);
  if (palTicks >= 50 && palTicks < 60 && h.st != PH_DEAD) snap("look");
  if (palLv == 0 && h.st == PH_RUN && abs(h.vx) == PAL_RUN && c >= 3) snap("run");
  if (h.st == PH_JUMP && h.t == 17) snap(abs(h.vx) > PAL_JSTAND ? "jump_running" : "jump_standing");
  if (h.st == PH_JUMP && h.t == 15 && palPool && palAt(c, palH - 1) == ' ') snap(palPool == 1 ? "jump_over_the_water" : "jump_over_molten_iron");
  if (h.st == PH_JUP && h.t == 9) snap("jump_up");
  if (h.st == PH_HANG && h.t == 3) snap("hang");
  if (h.st == PH_CLIMB && h.t == 16) snap("climb");
  if (h.st == PH_DOWN && h.t == 10) snap("climb_down");
  if (h.st == PH_FALL && h.t == 10) snap("drop");
  if (h.st == PH_LAND && h.lt == 24 && h.t == 4) snap("hard_landing");
  for (uint8_t k = 0; k < PAL_NF; k++) if (palF[k].on && palF[k].vy >= 575) snap("loose_floor_falling");
  for (uint8_t k = 0; k < PAL_NL; k++) if (palSh[k].on && palSh[k].t == 8 && palSh[k].r == r) snap("loose_floor_shaking");
  if (h.st == PH_STEP && palAt(c, r) == '^') snap("spikes_careful_step");
  for (int16_t cc = (int16_t)(c / 10 * 10); cc < c / 10 * 10 + 10; cc++) {
    char g = palAt(cc, r);
    if (g >= 'a' && g <= 'd' && (palGateWant >> (g - 'a') & 1) && palGate[g - 'a'] >= 30 && palGate[g - 'a'] < 34) snap("gate_opening");
    if (g == 'K' && h.st == PH_STAND && abs(cc - c) == 1 && palScy(cc, r) == 7) snap("scythe_waiting");
    if (g == 'K' && cc == c && h.st == PH_RUN && palScy(cc, r) > PAL_SCYK + 6) snap("scythe_run_past");
  }
  for (uint8_t i = 0; i < 4; i++)
    if ((palTimed >> i & 1) && palGateT[i] && palGateT[i] != 0xFFFF && palGateT[i] + 40 < palGt * 5 && h.st == PH_JUMP && h.t == 12)
      snap("timed_gate_race");
  if (msg && !strcmp(msg, "THE EXIT DOOR OPENS")) snap("exit_plate");
  if (msg && !strcmp(msg, "A HEALING DRAUGHT")) snap("potion_small");
  if (msg && !strcmp(msg, "A DRAUGHT OF LIFE!")) snap("potion_big");
  if (msg && !strcmp(msg, "A SWORD! OK DRAWS IT")) snap("sword_found");
  if (msg && !strcmp(msg, "THE WARDEN RAGES!")) snap("warden_rages");
  if (h.hurt == 16 && h.st != PH_DEAD) snap("hurt");
  if (h.st == PH_EXIT && h.t == 22) snap("exit_door");
  PalGuard *fg = h.st == PH_FIGHT ? palFoe(80) : NULL;
  const char *who = fg ? (fg->sk == PAL_BOSS ? "warden" : fg->sk == PAL_CAPT ? "captain" : "guard") : "";
  char w[32];
  if (fg && !h.hurt && fg->st == PG_STRIKE && fg->t + 3 == PAL_SKILL[fg->sk].wind) { snprintf(w, sizeof(w), "%s_winds_up", who); snap(w); }
  if (fg && !h.hurt && h.act == PA_STRIKE && h.at == 4) { snprintf(w, sizeof(w), "%s_lunge", who); snap(w); }
  if (fg && h.act == PA_BLOCK && h.at == 3) { snprintf(w, sizeof(w), "%s_parry", who); snap(w); }
  if (fg && fg->st == PG_BLOCK && fg->t == 3) { snprintf(w, sizeof(w), "%s_blocks", who); snap(w); }
  for (uint8_t i = 0; i < palNG; i++)
    if (palG[i].st == PG_DYING && palG[i].t == 18) snap(palG[i].sk == PAL_BOSS ? "warden_falls" : palG[i].sk == PAL_CAPT ? "captain_falls" : "guard_falls");
}

// ---- pictures of what the player above never does: the ways to die, and a few more ----
static void palaceShots() {
  // how: the death to show; 254 a timed gate about to fall shut, 255 just `ticks` steps
  struct Shot { const char *name; uint8_t lv; int16_t c, r; int8_t dir; bool sword; uint8_t hp, scy, keys, how, ticks; const char *msg; };
  static const Shot SH[] = {
    { "death_spikes",       5, 10, 7,  1, true,  3, 0,  RT, 1,   0,  NULL },  // a run into the spikes of the library
    { "death_scythe",       6,  5, 1,  1, true,  3, 1,  RT, 2,   0,  NULL },  // a run into a scythe's beat
    { "death_drowned",      4,  6, 1,  1, false, 3, 0,  0,  3,   0,  NULL },  // standing on the loose chain over the water
    { "death_molten_iron",  6, 41, 1,  1, true,  3, 0,  0,  3,   0,  NULL },  // the same over the forge's pit
    { "death_guard",        7, 20, 7,  1, false, 1, 0,  0,  0,   0,  NULL },  // no sword drawn, a guard's blow
    { "L5_potion_big",      4, 37, 1,  1, true,  3, 0,  RT, 255, 30, "A DRAUGHT OF LIFE!" },
    { "L5_timed_gate_shuts", 4, 21, 2, 1, true,  3, 0,  0,  254, 34, NULL },
  };
  int ok = 0;
  for (const Shot &S : SH) {
    palStartHp = S.hp; palStartSword = S.sword;
    palLoad(S.lv);
    PalHero &h = palHero;
    h.x = (int32_t)(S.c * PAL_TW + PAL_TW / 2) << 8; h.y = (int32_t)palLine(S.r) << 8; h.dir = S.dir;
    if (S.scy) while (palScy((int16_t)(S.c + 1), S.r) != PAL_SCYP - 9) palTicks++;
    if (S.how == 254) { palGateWant = 1; palGate[0] = 64; palGateT[0] = 50; }
    static uint32_t ms = 0;
    palPhase = 1;
    for (int i = 0; i < 900 && (S.how >= 254 ? i < S.ticks : !(h.st == PH_DEAD && h.t >= (S.how == 3 ? 7 : 26))); i++) {
      palStep(plIn(S.keys == LT, S.keys == RT, false, false));
      if (palRec) { palDraw(600000); palRecFrame(ms += PAL_TICK, gfx.buffer()); }
    }
    palDraw(600000);
    pbWrite(S.name, gfx.buffer());
    for (int i = 0; palRec && i < 70; i++) {                    // a while longer, for the video
      palStep(plIn(false, false, false, false));
      palDraw(600000); palRecFrame(ms += PAL_TICK, gfx.buffer());
    }
    bool died = S.how == 255 ? palMsg && !strcmp(palMsg, S.msg) && h.st != PH_DEAD :
                S.how == 254 ? (palGateWant == 0 || palGateT[0] < 20) && h.st != PH_DEAD : h.st == PH_DEAD && h.how == S.how;
    printf("      %-20s level %u: %s\n", S.name, S.lv + 1, died ? "shown" : "NOT AS PLANNED");
    ok += died;
  }
  check("palace: pictures of the ways to die, a big potion, a timed gate falling shut", ok == (int)(sizeof(SH) / sizeof(SH[0])));
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
  memset(vox, 0, sizeof(VoxState));
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

// ---------------- Skat: the rules and the CPU without the screen, and a player for the UI ----------------
static uint32_t sktMaskOf(std::initializer_list<int> cs) { uint32_t m = 0; for (int c : cs) m |= 1UL << c; return m; }
static int sktC(int suit, int rank) { return suit * 8 + rank; }
static uint8_t sktWin3(uint8_t g, int a, int b, int c) {
  Skat s; memset(&s, 0, sizeof(s)); s.game = g;
  s.trick[0] = (uint8_t)a; s.trick[1] = (uint8_t)b; s.trick[2] = (uint8_t)c; s.nTrick = 3;
  return s.trickWin();
}
static uint32_t sktLegal1(uint8_t g, int lead, uint32_t hand) {
  Skat s; memset(&s, 0, sizeof(s)); s.game = g; s.trick[0] = (uint8_t)lead; s.nTrick = 1; s.hand[1] = hand;
  return s.legal(1);
}
static void skatLogic() {
  enum { K = 0, P = 1, H = 2, D = 3 };                          // Kreuz Pik Herz Karo
  // the bid values
  static const uint16_t WANT[] = { 18, 20, 22, 23, 24, 27, 30, 33, 35, 36, 40, 44, 45, 46, 48, 50, 54, 55, 59, 60, 63, 66, 70, 72 };
  uint16_t v = 0; bool ok = true;
  for (uint16_t w : WANT) { v = sktNextBid(v); if (v != w) ok = false; }
  check("skat: bid values 18, 20, 22, 23, 24, 27, 30 ...", ok && sktPrevBid(23) == 22 && sktNextBid(240) == 264 && sktNextBid(264) == 0);
  // the deal
  ok = true;
  for (int i = 0; i < 3000; i++) {
    Skat s; s.deal(1234 + i * 7919, (uint8_t)(i % 3));
    uint32_t all = s.skat;
    if (sktCount(s.skat) != 2) ok = false;
    for (int p = 0; p < 3; p++) { if (sktCount(s.hand[p]) != 10 || (all & s.hand[p])) ok = false; all |= s.hand[p]; }
    if (all != 0xFFFFFFFFUL || s.bidActor() != (i + 2) % 3) ok = false;
  }
  check("skat: 3000 deals, each the 32 cards as 10 + 10 + 10 + Skat 2, Mittelhand says first", ok);
  // following
  uint32_t m = sktMaskOf({ sktC(K, SR_B), sktC(H, SR_A), sktC(P, SR_7) });
  check("skat: Herz led in a Herz game: a Bube or Herz (trumps)", sktLegal1(SG_HERZ, sktC(H, SR_7), m) == sktMaskOf({ sktC(K, SR_B), sktC(H, SR_A) }));
  m = sktMaskOf({ sktC(P, SR_B), sktC(P, SR_A), sktC(H, SR_7) });
  check("skat: Pik led in a Herz game: the Pik-Bube is no Pik", sktLegal1(SG_HERZ, sktC(P, SR_7), m) == sktMaskOf({ sktC(P, SR_A) }));
  m = sktMaskOf({ sktC(H, SR_B), sktC(H, SR_A), sktC(P, SR_7) });
  check("skat: Grand, Herz led: Herz-A, not the Herz-Bube", sktLegal1(SG_GRAND, sktC(H, SR_7), m) == sktMaskOf({ sktC(H, SR_A) }));
  check("skat: Grand, a Bube led: only Buben", sktLegal1(SG_GRAND, sktC(D, SR_B), m) == sktMaskOf({ sktC(H, SR_B) }));
  check("skat: Null, Herz led: the Herz-Bube is a Herz", sktLegal1(SG_NULL, sktC(H, SR_7), sktMaskOf({ sktC(H, SR_B), sktC(P, SR_7) })) == sktMaskOf({ sktC(H, SR_B) }));
  m = sktMaskOf({ sktC(P, SR_7), sktC(K, SR_B) });
  check("skat: no card of the suit led: anything", sktLegal1(SG_GRAND, sktC(H, SR_7), m) == m);
  // who takes the trick
  check("skat: Pik game: the Karo-Bube trumps the Pik-A", sktWin3(SG_PIK, sktC(P, SR_7), sktC(D, SR_B), sktC(P, SR_A)) == 1);
  check("skat: Kreuz game: a Kreuz-7 trumps Herz", sktWin3(SG_KREUZ, sktC(H, SR_A), sktC(H, SR_10), sktC(K, SR_7)) == 2);
  check("skat: Buben order Kreuz Pik Herz Karo", sktWin3(SG_HERZ, sktC(H, SR_A), sktC(D, SR_B), sktC(K, SR_B)) == 2 &&
        sktWin3(SG_HERZ, sktC(H, SR_A), sktC(P, SR_B), sktC(H, SR_B)) == 1);
  check("skat: A 10 K D 9 8 7", sktWin3(SG_KARO, sktC(D, SR_A), sktC(D, SR_10), sktC(D, SR_K)) == 0 &&
        sktWin3(SG_KARO, sktC(D, SR_K), sktC(D, SR_10), sktC(D, SR_D)) == 1 && sktWin3(SG_GRAND, sktC(H, SR_7), sktC(H, SR_8), sktC(H, SR_K)) == 2);
  check("skat: Grand: the Herz-A takes the 10, a Bube takes the A", sktWin3(SG_GRAND, sktC(H, SR_10), sktC(H, SR_A), sktC(K, SR_7)) == 1 &&
        sktWin3(SG_GRAND, sktC(H, SR_A), sktC(D, SR_B), sktC(P, SR_A)) == 1);
  check("skat: a card of another suit never takes it", sktWin3(SG_GRAND, sktC(P, SR_7), sktC(H, SR_A), sktC(K, SR_A)) == 0);
  check("skat: Null: B over 10, 10 over 9, no trumps", sktWin3(SG_NULL, sktC(H, SR_10), sktC(H, SR_B), sktC(H, SR_9)) == 1 &&
        sktWin3(SG_NULL, sktC(H, SR_A), sktC(H, SR_K), sktC(K, SR_B)) == 0 && sktWin3(SG_NULL, sktC(H, SR_7), sktC(K, SR_A), sktC(H, SR_8)) == 2);
  // game values
  uint32_t mit3 = sktMaskOf({ sktC(K, SR_B), sktC(P, SR_B), sktC(H, SR_B), sktC(K, SR_A) });
  SkResult r = sktValue(mit3, SG_KREUZ, 0, 61, 5, 5, 18);
  check("skat: Kreuz mit 3 = 4 x 12 = 48, won with 61", r.won && r.spitzen == 3 && r.value == 48 && r.score == 48);
  r = sktValue(mit3, SG_KREUZ, 0, 60, 5, 5, 18);
  check("skat: 60 Augen lose: -96", !r.won && r.score == -96);
  r = sktValue(sktMaskOf({ sktC(P, SR_B), sktC(H, SR_B) }), SG_PIK, 0, 70, 6, 4, 18);
  check("skat: Pik ohne 1 = 2 x 11 = 22", r.won && r.spitzen == -1 && r.value == 22);
  r = sktValue(sktMaskOf({ sktC(H, SR_10), sktC(H, SR_K) }), SG_HERZ, SKF_HAND, 70, 6, 4, 18);
  check("skat: Herz ohne 5 Hand = 7 x 10 = 70", r.won && r.spitzen == -5 && r.value == 70);
  uint32_t mit4 = sktMaskOf({ sktC(K, SR_B), sktC(P, SR_B), sktC(H, SR_B), sktC(D, SR_B) });
  r = sktValue(mit4, SG_GRAND, SKF_HAND | SKF_SCHN, 92, 8, 2, 18);
  check("skat: Grand mit 4 Hand Schneider angesagt = 8 x 24 = 192", r.won && r.value == 192);
  r = sktValue(0, SG_GRAND, 0, 61, 5, 5, 18);
  check("skat: Grand ohne 4 = 5 x 24 = 120", r.won && r.spitzen == -4 && r.value == 120);
  uint32_t mit1 = sktMaskOf({ sktC(K, SR_B), sktC(H, SR_B) });
  r = sktValue(mit1, SG_KREUZ, SKF_HAND | SKF_SCHN | SKF_SCHW, 120, 10, 0, 18);
  check("skat: Kreuz mit 1 Hand Schwarz angesagt = 7 x 12 = 84", r.won && r.value == 84);
  r = sktValue(mit1, SG_KREUZ, SKF_HAND | SKF_SCHN | SKF_SCHW, 110, 9, 1, 18);
  check("skat: Schwarz angesagt, one trick lost: -168", !r.won && r.score == -168);
  r = sktValue(sktMaskOf({ sktC(K, SR_B), sktC(P, SR_B) }), SG_KREUZ, SKF_HAND | SKF_SCHN | SKF_SCHW | SKF_OUV, 120, 10, 0, 18);
  check("skat: Kreuz mit 2 Ouvert = 9 x 12 = 108", r.won && r.value == 108);
  r = sktValue(mit1, SG_PIK, 0, 95, 9, 1, 18);
  check("skat: Pik mit 1 Schneider = 3 x 11 = 33", r.won && r.schneider && r.value == 33);
  r = sktValue(mit1, SG_PIK, 0, 120, 10, 0, 18);
  check("skat: Pik mit 1 Schwarz = 4 x 11 = 44", r.won && r.schwarz && r.value == 44);
  r = sktValue(mit1, SG_HERZ, 0, 25, 2, 8, 18);
  check("skat: lost Schneider: Herz mit 1 = 3 x 10, -60", !r.won && r.schneider && r.score == -60);
  r = sktValue(mit1, SG_HERZ, SKF_HAND | SKF_SCHN, 80, 7, 3, 18);
  check("skat: Schneider angesagt, 80 Augen: lost 5 x 10, -100", !r.won && r.score == -100);
  static const uint8_t NFL[4] = { 0, SKF_HAND, SKF_OUV, SKF_HAND | SKF_OUV };
  static const uint16_t NV[4] = { 23, 35, 46, 59 };
  ok = true;
  for (int i = 0; i < 4; i++) {
    r = sktValue(0, SG_NULL, NFL[i], 0, 0, 10, 18);
    if (!r.won || r.value != NV[i]) ok = false;
    r = sktValue(0, SG_NULL, NFL[i], 0, 1, 0, 18);
    if (r.won || r.score != -2 * NV[i]) ok = false;
  }
  check("skat: Null 23, Hand 35, Ouvert 46, Ouvert Hand 59; a trick loses", ok);
  r = sktValue(mit1, SG_KREUZ, 0, 70, 6, 4, 30);
  check("skat: overbid (24 for 30): lost with 3 x 12 = 36, -72", !r.won && r.overbid && r.value == 36 && r.score == -72);
  r = sktValue(mit1, SG_KREUZ, 0, 92, 8, 2, 36);
  check("skat: ... but Schneider makes 36 enough", r.won && !r.overbid && r.value == 36);
  r = sktValue(0, SG_NULL, 0, 0, 0, 10, 30);
  check("skat: Null for a bid of 30: overbid, -70", !r.won && r.overbid && r.score == -70);
  // bidding: who plays
  {
    Skat s; s.deal(5, 0);                                       // Vorhand 1, Mittelhand 2, Hinterhand 0
    s.bidAct(true); s.bidAct(true); s.bidAct(true); s.bidAct(false);   // M 18, V ja, M 20, V passe
    bool a = s.bidActor() == 0 && s.bidAsk() == 22;
    s.bidAct(true, 30); s.bidAct(false);                        // H 30, M passe
    check("skat: Reizen: M against V, then H against the one left", a && s.decl == 0 && s.bid == 30 && s.stage == 3);
    Skat t; t.deal(5, 0);
    t.bidAct(false); t.bidAct(false);                           // M passe, H passe
    bool b = t.bidActor() == 1 && t.bidAsk() == 18;
    t.bidAct(false);
    Skat u; u.deal(5, 0); u.bidAct(false); u.bidAct(false); u.bidAct(true);
    check("skat: all passe = new deal; Vorhand may still play for 18", b && t.decl == SKT_NONE && t.stage == 3 && u.decl == 1 && u.bid == 18);
  }
  // CPU games: always legal, 120 Augen, bidding ends
  long games = 0, passed = 0, declWon = 0, played = 0, illegal = 0, bad120 = 0, badVoid = 0, overbid = 0, cards = 0;
  long byType[6] = { 0 }, wonType[6] = { 0 };
  long forced = 0;
  uint32_t rr = 77;
  for (int gi = 0; gi < 6000; gi++) {
    Skat s; s.deal(99991u * gi + 17, (uint8_t)(gi % 3));
    bool randomPlay = gi % 5 == 4;                              // sometimes a player that plays any legal card
    int guard = 0;
    while (s.stage != 3 && guard++ < 300) { uint8_t a = s.bidActor(); uint16_t ask = s.bidAsk(); s.bidAct(ask && ask <= sktMaxBid(s.hand[a], a ? 0 : -4)); }
    games++;
    if (guard >= 300) { illegal++; continue; }
    if (s.decl == SKT_NONE) { passed++; continue; }
    uint8_t d = s.decl;
    SkPlan pl;
    if (gi % 11 == 3) {                                         // announcements the CPU never makes itself
      static const uint8_t F[6] = { SKF_HAND, SKF_HAND | SKF_SCHN, SKF_HAND | SKF_SCHW, SKF_HAND | SKF_OUV, SKF_OUV, SKF_HAND | SKF_OUV };
      pl.game = (uint8_t)(bgRand(rr) % 6); pl.flags = F[bgRand(rr) % 6];
      if (pl.game == SG_NULL) pl.flags &= SKF_HAND | SKF_OUV;
      if (!(pl.flags & SKF_HAND)) { s.takeSkat(); pl.push = sktCpuPick(s.hand[d], s.bid).push; s.push(pl.push); }
      forced++;
    } else if (!sktCpuHand(s.hand[d], s.bid, pl)) { s.takeSkat(); pl = sktCpuPick(s.hand[d], s.bid); s.push(pl.push); }
    if (sktCount(s.hand[d]) != 10 || sktCount(s.skat) != 2 || (s.hand[d] | s.skat) != s.cards) illegal++;
    s.declare(pl.game, pl.flags);
    while (!s.over()) {
      if (s.nTrick == 3) { s.collect(); continue; }
      uint8_t p = s.turn;
      uint32_t L = s.legal(p);
      uint8_t c = randomPlay && p == (gi / 5) % 3 ? 255 : sktCpuCard(s, p);
      if (c == 255) { uint8_t k = (uint8_t)(bgRand(rr) % sktCount(L)); for (uint8_t q = 0; q < 32; q++) if ((L >> q & 1) && !k--) { c = q; break; } }
      if (c >= 32 || !(L >> c & 1) || !(s.hand[p] >> c & 1)) { illegal++; break; }
      if (s.nTrick) {                                            // following, checked once more by hand
        uint8_t e = sktEff(s.game, s.trick[0]);
        bool has = false;
        for (uint8_t q = 0; q < 32; q++) if ((s.hand[p] >> q & 1) && sktEff(s.game, q) == e) has = true;
        if (has && sktEff(s.game, c) != e) illegal++;
      }
      s.play(c);
      cards++;
      for (int q = 0; q < 3; q++) for (int e = 0; e < 5; e++)
        if ((s.voids[q] >> e & 1) && (s.hand[q] & sktEffMask(s.game, (uint8_t)e))) badVoid++;
    }
    if (s.done == 10 && sktPoints(s.won[0] | s.won[1] | s.won[2] | s.skat) != 120) bad120++;
    if (s.done == 10 && s.tricks[0] + s.tricks[1] + s.tricks[2] != 10) bad120++;
    SkResult res = s.result();
    played++; byType[s.game]++;
    if (res.won) { declWon++; wonType[s.game]++; }
    if (res.overbid) overbid++;
  }
  printf("      skat: %ld deals, %ld passed in, %ld played (%ld with forced announcements), %ld cards\n", games, passed, played, forced, cards);
  printf("      skat: declarer won %.1f%%, overbid %ld; won/played Kreuz %ld/%ld Pik %ld/%ld Herz %ld/%ld Karo %ld/%ld Grand %ld/%ld Null %ld/%ld\n",
         100.0 * declWon / played, overbid, wonType[0], byType[0], wonType[1], byType[1], wonType[2], byType[2],
         wonType[3], byType[3], wonType[4], byType[4], wonType[5], byType[5]);
  check("skat: thousands of CPU games: only legal cards", illegal == 0 && cards > 100000);
  check("skat: the card points always add up to 120, 10 tricks", bad120 == 0);
  check("skat: a player marked void never holds that kind", badVoid == 0);
  check("skat: every game type gets played", byType[0] && byType[1] && byType[2] && byType[3] && byType[4] && byType[5]);
  // the CPU's own games (no forced announcements, no random player): the declarer wins most of them
  long own = 0, ownWon = 0, ownOver = 0;
  for (int gi = 0; gi < 2000; gi++) {
    Skat s; s.deal(424243u * gi + 5, (uint8_t)(gi % 3));
    while (s.stage != 3) { uint8_t a = s.bidActor(); uint16_t ask = s.bidAsk(); s.bidAct(ask && ask <= sktMaxBid(s.hand[a])); }
    if (s.decl == SKT_NONE) continue;
    SkPlan pl;
    if (!sktCpuHand(s.hand[s.decl], s.bid, pl)) { s.takeSkat(); pl = sktCpuPick(s.hand[s.decl], s.bid); s.push(pl.push); }
    s.declare(pl.game, pl.flags);
    while (!s.over()) { if (s.nTrick == 3) s.collect(); else s.play(sktCpuCard(s, s.turn)); }
    SkResult res = s.result();
    own++; if (res.won) ownWon++; if (res.overbid) ownOver++;
  }
  printf("      skat: CPU against CPU: the declarer won %ld of %ld (%ld overbid)\n", ownWon, own, ownOver);
  check("skat: the CPU bids sensibly (the declarer wins 55..92%)", own > 800 && ownWon * 100 >= own * 55 && ownWon * 100 <= own * 92);
  // the defenders put points on a trick the partner surely has, the last one takes it cheaply
  {
    Skat s; memset(&s, 0, sizeof(s)); s.game = SG_GRAND; s.decl = 0;
    s.trick[0] = (uint8_t)sktC(P, SR_7); s.who[0] = 0; s.trick[1] = (uint8_t)sktC(P, SR_A); s.who[1] = 1; s.nTrick = 2; s.turn = 2;
    s.played = sktMaskOf({ sktC(P, SR_7), sktC(P, SR_A) });
    s.hand[2] = sktMaskOf({ sktC(H, SR_10), sktC(H, SR_7), sktC(K, SR_8) });
    check("skat: a defender schmiert the 10 on the partner's trick", sktCpuCard(s, 2) == sktC(H, SR_10));
    s.trick[0] = (uint8_t)sktC(P, SR_D); s.trick[1] = (uint8_t)sktC(P, SR_8);
    s.played = sktMaskOf({ sktC(P, SR_D), sktC(P, SR_8) });
    s.hand[2] = sktMaskOf({ sktC(P, SR_K), sktC(P, SR_10), sktC(H, SR_7) });
    check("skat: ... and takes the declarer's trick with the cheapest card", sktCpuCard(s, 2) == sktC(P, SR_K));
  }
}

// Skat through the keys: bids, declares and plays as the CPU would (a bit bolder)
static SkPlan sktBotPlan;
static uint8_t sktBotPhase = 255, sktBotSeenPhase = 255;
static uint32_t sktBotAt = 0;
static int sktBotDeclared = 0, sktBotPushed = 0, sktBotBid = 0, sktBotCards = 0;
static bool sktEndSet = false, sktCapBid = false, sktCapPlay = false, sktCapRes = false, sktCapList = false;
static void skatBot(uint32_t t) {
  if (botBusy(t)) return;
  if (sktRows == sktGames && sktGames && seen("FINAL LIST") && sktPhase == SKP_NONE && !sktEndSet) { simEnd = t + 4000; sktEndSet = true; }
  if (sktPhase != sktBotSeenPhase) {                               // a new screen: look at it for a moment
    sktBotSeenPhase = sktPhase;
    sktBotAt = t + (sktPhase == SKP_RESULT || sktPhase == SKP_LIST ? 1200 : 250);
    if (sktPhase == SKP_BID && !sktCapBid && !skt.toSay) { captureAt.insert(frames + 3); sktCapBid = true; }
    if (sktPhase == SKP_RESULT && !sktCapRes && skt.decl == 0) { captureAt.insert(frames + 20); sktCapRes = true; }
    if (sktPhase == SKP_LIST && !sktCapList && sktRows >= 4) { captureAt.insert(frames + 20); sktCapList = true; }
    return;
  }
  if (t < sktBotAt) return;
  uint8_t cs[12], n = sktHandList(cs);
  switch (sktPhase) {
  case SKP_BID: {
    uint16_t ask = skt.bidAsk();
    uint8_t want = ask && ask <= sktMaxBid(skt.hand[0], -4) ? 0 : 1;
    if (sktSel != want) botPress(t, RT, 50);
    else { botPress(t, OK, 60); sktBotBid++; }
    break; }
  case SKP_TAKE: {
    bool hand = sktCpuHand(skt.hand[0], skt.bid, sktBotPlan);
    botPress(t, sktSel == (hand ? 1 : 0) ? OK : RT, 60);
    sktBotPhase = SKP_TAKE;
    break; }
  case SKP_PUSH: {
    if (sktBotPhase != SKP_PUSH) { sktBotPlan = sktCpuPick(skt.hand[0], skt.bid); sktBotPhase = SKP_PUSH; }
    uint32_t diff = sktMark ^ sktBotPlan.push;
    uint8_t target = n;
    for (uint8_t i = 0; i < n && target == n; i++) if (diff >> cs[i] & 1) target = i;
    if (sktCur != target) botPress(t, target > sktCur ? RT : LT, 50);
    else { botPress(t, OK, 60); if (target == n) sktBotPushed++; }
    break; }
  case SKP_GAME: {
    uint8_t row = sktBotPlan.game == SG_NULL ? (sktBotPlan.flags & SKF_OUV ? 6 : 5) : sktBotPlan.game;
    if (sktSel != row) botPress(t, row > sktSel ? DN : UP, 50);
    else { botPress(t, OK, 60); sktBotDeclared++; sktBotPhase = 255; }
    break; }
  case SKP_PLAY: {
    if (!sktCapPlay && skt.done >= 2 && skt.nTrick && skt.legal(0) != skt.hand[0]) { captureAt.insert(frames + 2); sktCapPlay = true; }
    uint8_t c = sktCpuCard(skt, 0), idx = 0;
    while (idx < n && cs[idx] != c) idx++;
    if (sktCur != idx) botPress(t, idx > sktCur ? RT : LT, 50);
    else { botPress(t, OK, 60); sktBotCards++; }
    break; }
  case SKP_RESULT: case SKP_LIST:
    botPress(t, OK, 80); sktBotAt = t + 1500;
    break;
  }
}

// ---------------- TV ----------------
static uint32_t tvUiT = 0, tvListT = 0, tvPlayT = 0;
static uint8_t tvUiPhase = 0, tvPinStep = 0;
static std::vector<std::string> tvTypeQ = { "t", "v", ".de", "done" };    // the address: https://tv.de
// presses the arrows towards a key of the TV keyboard, then OK
static void tvTypeKey(uint32_t t) {
  const std::string &k = tvTypeQ.front();
  int ty = 4, tx = -1;
  for (int r = 0; r < 4 && tx < 0; r++) if (k.size() == 1 && strchr(TV_KB_ROWS[r], k[0])) { ty = r; tx = (int)(strchr(TV_KB_ROWS[r], k[0]) - TV_KB_ROWS[r]); }
  if (tx < 0) {
    const char *const *last = tvKbUrl ? TV_KB_URL : TV_KB_TXT;
    for (int i = 0; i < (tvKbUrl ? 6 : 3); i++) if (k == last[i]) tx = i;
  }
  if (tvKbY != ty) botPress(t, tvKbY < ty ? 2 : 1, 50);
  else if (tvKbX != tx) botPress(t, tvKbX < tx ? 8 : 4, 50);
  else { botPress(t, 3, 70); tvTypeQ.erase(tvTypeQ.begin()); }
}
static void tvUiBot(uint32_t t) {
  if (botBusy(t)) return;
  if (tvWhere == TVW_KEYS && !tvTypeQ.empty()) { tvTypeKey(t); return; }
  if (tvWhere == TVW_PIN || (tvWhere == TVW_SAY && tvPinStep < 2)) {
    // a wrong PIN first (000000), then 120000: UP, RIGHT, UP, UP, OK
    static const uint8_t PIN_KEYS[] = { 3, 3, 1, 8, 1, 1, 3 };
    if (tvPinStep == 5) captureMs.push_back(t + 250);             // the PIN typed, before OK
    if (tvPinStep < sizeof(PIN_KEYS)) botPress(t, PIN_KEYS[tvPinStep++], PIN_KEYS[tvPinStep - 1] == 3 ? 70 : 50);
    botFree = t + 300;
    return;
  }
  if (tvWhere == TVW_LIST && tvUiPhase == 0) {                  // the start page: the second video
    if (!tvListT) { tvListT = t; captureMs.push_back(t + 1500); }
    if (t == tvListT + 2000) botPress(t, 2, 50);
    if (t == tvListT + 2500) { botPress(t, 3, 70); tvUiPhase = 1; }
    return;
  }
  if (tvWhere == TVW_MENU && tvUiPhase == 1) { botPress(t, 3, 70); tvUiPhase = 2; return; }
  if (tvWhere == TVW_PLAYER && tvUiPhase == 2) {
    if (!tvPlayT) { tvPlayT = t; simTvBreakAt = t + 11200; captureMs.push_back(t + 4200); }
    uint32_t s = t - tvPlayT;
    if (s == 2700) botPress(t, 8, 50);                           // +10 s
    if (s == 5200 || s == 7200) botPress(t, 3, 70);              // pause, play
    if (s == 9200) botPress(t, 1, 50);                           // sharp
    if (s == 15700) { botPress(t, 3, 900); tvUiPhase = 3; }      // hold OK: back to the list
    return;
  }
  if (tvWhere == TVW_LIST && tvUiPhase == 3) {                  // to the search tab, a search typed
    static uint32_t t0 = 0;
    if (!t0) t0 = t;
    if (t == t0 + 1000 || t == t0 + 1200 || t == t0 + 1400) botPress(t, 8, 50);
    if (t == t0 + 1800) { botPress(t, 3, 70); tvTypeQ = { "c", "a", "t", "done" }; }
    if (t > t0 + 1800 && tvTypeQ.empty() && tvN) { tvUiPhase = 4; botFree = t + 2000; }
    return;
  }
  if (tvWhere == TVW_LIST && tvUiPhase == 4) { botPress(t, 3, 900); tvUiPhase = 5; }   // hold OK: to the menu
}
static bool tvMemRead(void *ctx, uint8_t *dst, uint32_t n) {
  std::pair<const uint8_t *, uint32_t> &m = *(std::pair<const uint8_t *, uint32_t> *)ctx;
  if (n > m.second) return false;
  memcpy(dst, m.first, n);
  m.first += n; m.second -= n;
  return true;
}
static uint32_t tvFnv(const uint8_t *p, size_t n) { uint32_t h = 2166136261u; while (n--) { h ^= *p++; h *= 16777619u; } return h; }

// the golden frames through the C decoder, one after the other; returns the screens' hashes
static bool tvDecodeAll(std::vector<std::string> &got) {
  std::vector<std::string> all = simTvFluent;
  all.insert(all.end(), simTvSharp.begin(), simTvSharp.end());
  uint8_t *fb = gfx.buffer();
  memset(fb, 0, SCR_W * SCR_H);
  for (size_t i = 0; i < all.size(); i++) {
    std::pair<const uint8_t *, uint32_t> m((const uint8_t *)all[i].data() + 1, (uint32_t)all[i].size() - 1);
    TvFrame fr;
    if (!tvDecode(fb, m.second, tvMemRead, &m, fr)) return false;
    char b[128];
    snprintf(b, sizeof(b), "%u %lu %u %u %08x", fr.seq, (unsigned long)fr.ts, fr.flags, fr.skipped, tvFnv(fb, SCR_W * SCR_H));
    got.push_back(b);
    tvGoldTop.push_back(tvFnv(fb, SCR_W * (SCR_H - TV_STRIP)));
  }
  return true;
}

// the golden stream through the C decoder: the same screens as the server's Python decoder
static void tvDecodeTest() {
  check("golden.bin of the server's tests found", simTvLoadGolden());
  FILE *f = fopen("../../MiniArcadeServer/tests/golden.txt", "r");
  std::vector<std::string> want, got;
  char line[128];
  while (f && fgets(line, sizeof(line), f)) if (line[0] != '#') { line[strcspn(line, "\n")] = 0; want.push_back(line); }
  if (f) fclose(f);
  bool ok = tvDecodeAll(got);
  for (size_t i = 0; i < want.size() && i < got.size(); i++)
    if (want[i] != got[i]) printf("      frame %zu: want '%s' got '%s'\n", i, want[i].c_str(), got[i].c_str());
  check("every golden frame decodes to the server's screen", ok && !want.empty() && got == want);
  // skipped tiles keep what was there
  uint8_t *fb = gfx.buffer();
  memset(fb, 0x5A, SCR_W * SCR_H);
  std::pair<const uint8_t *, uint32_t> m((const uint8_t *)simTvFluent[1].data() + 1, (uint32_t)simTvFluent[1].size() - 1);
  TvFrame fr;
  tvDecode(fb, m.second, tvMemRead, &m, fr);
  long kept = 0;
  for (int i = 0; i < SCR_W * SCR_H; i++) kept += fb[i] == 0x5A;
  printf("      frame 1: %u tiles skipped, %ld pixels kept\n", fr.skipped, kept);
  check("skipped tiles keep the old pixels (16 x 16 each)", fr.skipped > 200 && kept == (long)fr.skipped * 256);
  // broken frames are refused
  std::string cut = simTvFluent[0].substr(1, simTvFluent[0].size() - 2), bad = simTvFluent[0].substr(1, 7) + "\xFF";
  std::pair<const uint8_t *, uint32_t> m1((const uint8_t *)cut.data(), (uint32_t)cut.size()), m2((const uint8_t *)bad.data(), (uint32_t)bad.size());
  check("a cut frame and an unknown tile kind are refused", !tvDecode(fb, m1.second, tvMemRead, &m1, fr) && !tvDecode(fb, m2.second, tvMemRead, &m2, fr));
  // the clock: waits for the first frame, shows in time, drops late ones, starts anew far behind
  TvClock k = { false, 0, 0 };
  bool c1 = tvDue(k, 1000, 5000) == TV_WAIT && tvDue(k, 1000 + TV_PREBUF_MS, 5000) == TV_SHOW;
  bool c2 = tvDue(k, 1000 + TV_PREBUF_MS + 30, 5040) == TV_WAIT && tvDue(k, 1000 + TV_PREBUF_MS + 50, 5040) == TV_SHOW;
  bool c3 = tvDue(k, 1000 + TV_PREBUF_MS + 80 + TV_LATE_MS + 1, 5080) == TV_DROP;
  bool c4 = tvDue(k, 1000 + TV_PREBUF_MS + 5000, 5120) == TV_SHOW && tvDue(k, 1000 + TV_PREBUF_MS + 5040, 5160) == TV_SHOW;
  check("clock: first frame after the buffer, in time, late ones dropped, far behind starts anew", c1 && c2 && c3 && c4);
  // a console slower than the video (a frame takes 55 ms, they come every 40): frames are dropped, no slow motion
  TvClock c = { false, 0, 0 };
  uint32_t now = 0, shown = 0, dropped = 0, lastTs = 0;
  for (uint32_t ts = 0; ts < 10000; ts += 40) {
    uint8_t d;
    while ((d = tvDue(c, now, ts)) == TV_WAIT) now += 5;
    if (d == TV_SHOW) { shown++; now += 55; lastTs = ts; } else { dropped++; now += 2; }
  }
  long behind = (long)(now - c.t0) - (long)(lastTs - c.ts0);
  printf("      slow console: %u shown, %u dropped, %ld ms behind at the end\n", shown, dropped, behind);
  check("slow console: real time kept by dropping frames", dropped > 50 && shown > 120 && behind < 120);
}

// ---------------- Pumpkin Maze: a bot that rolls through a level ----------------
/* It plans over the tiles (BFS from the pumpkin's tile: keys first, then
   the goal; candies first when asked), looks ahead along the way as far
   as a straight line stays clear of walls, bumpers and pits, and steers
   towards that point with a speed it can still brake from. Before a
   moving pit's track it waits until the pit is away.                  */
static bool pmbCandy = false;                    // collect every candy first
static int  pmbTrap = -1;                        // a pit to roll into on purpose (the trap test)
static int  pmbWaits = 0;

static int8_t pmbArrow(uint8_t t) { return t >= PT_BR && t <= PT_BD ? (int8_t)(t - PT_BR) : -1; }
static bool pmbBlocked(uint8_t t) { return t == PT_WALL || t == PT_DOOR || t == PT_TRAP || t == PT_BUMP; }

static std::vector<int> pmbPlan() {
  int start = (int)(pm->y >> 12) * PM_W + (int)(pm->x >> 12);
  auto target = [&](int c) {
    uint8_t t = PTILE(pm->g[c / PM_W][c % PM_W]);
    if (pmbTrap >= 0) return c == pmbTrap;
    bool candy = false;
    if (pmbCandy) for (int i = 0; i < PM_N; i++) if (PTILE(pm->g[i / PM_W][i % PM_W]) == PT_CANDY) candy = true;
    if (candy) return t == PT_CANDY;
    if (pm->keys < pm->keysAll) return t == PT_KEY;
    return t == PT_GOAL;
  };
  std::vector<int> prev(PM_N, -2);
  std::queue<int> q;
  q.push(start); prev[start] = -1;
  while (!q.empty()) {
    int c = q.front(); q.pop();
    if (target(c)) {
      std::vector<int> p;
      for (int k = c; k >= 0; k = prev[k]) p.insert(p.begin(), k);
      return p;
    }
    int8_t from = pmbArrow(PTILE(pm->g[c / PM_W][c % PM_W]));
    for (int k = 0; k < 4; k++) {
      int x = c % PM_W + PM_DX[k], y = c / PM_W + PM_DY[k];
      if (x < 0 || y < 0 || x >= PM_W || y >= PM_H) continue;
      int n = y * PM_W + x;
      uint8_t t = PTILE(pm->g[y][x]);
      int8_t to = pmbArrow(t);
      if (from == (k ^ 1) || to == (k ^ 1)) continue;
      if (pmbBlocked(t) && n != pmbTrap) continue;
      if (prev[n] != -2) continue;
      prev[n] = c;
      q.push(n);
    }
  }
  return {};
}

// can the pumpkin roll straight from where it is to (tx, ty) (1/256 px)?
static bool pmbClear(int32_t tx, int32_t ty, int goalCell) {
  int32_t dx = tx - pm->x, dy = ty - pm->y;
  int n = (int)(sqrt((double)dx * dx + (double)dy * dy) / (3 * 256)) + 1;
  for (int i = 1; i <= n; i++) {
    int32_t x = pm->x + dx * i / n, y = pm->y + dy * i / n;
    for (int c = 0; c < 4; c++) {
      int cx = (int)((x + (c & 1 ? 1 : -1) * 7 * 256) >> 12), cy = (int)((y + (c & 2 ? 1 : -1) * 7 * 256) >> 12);
      uint8_t t = pmAt((int16_t)cx, (int16_t)cy);
      if (t == PT_WALL || t == PT_DOOR || t == PT_BUMP) return false;
      if (pmbArrow(t) >= 0 && cy * PM_W + cx != goalCell) return false;
    }
    int cx = (int)(x >> 12), cy = (int)(y >> 12);
    for (int yy = cy - 1; yy <= cy + 1; yy++)
      for (int xx = cx - 1; xx <= cx + 1; xx++) {
        uint8_t t = pmAt((int16_t)xx, (int16_t)yy);
        if ((t != PT_TRAP && t != PT_GOAL) || yy * PM_W + xx == goalCell) continue;
        double ex = (x - pmMid((int16_t)xx)) / 256.0, ey = (y - pmMid((int16_t)yy)) / 256.0;
        if (ex * ex + ey * ey < 13 * 13) return false;
      }
  }
  return true;
}

static void pmbAxis(int16_t &ax, int16_t &ay) {
  ax = ay = 0;
  if (!pm || pm->st != PS_ROLL) return;
  std::vector<int> p = pmbPlan();
  if (p.empty()) return;
  int last = (int)p.size() - 1, j = 0;
  for (int k = 1; k <= last && k <= 6; k++) {                    // as far ahead as the way is straight and clear
    if (!pmbClear(pmMid((int16_t)(p[k] % PM_W)), pmMid((int16_t)(p[k] / PM_W)), p[last])) break;
    j = k;
    if (pmbArrow(PTILE(pm->g[p[k] / PM_W][p[k] % PM_W])) >= 0) break;
  }
  if (j == 0 && last > 0) j = 1;
  // a moving pit that will be on one of the next tiles when the pumpkin gets there: wait on this side
  bool onTrack = pm->g[pm->y >> 12][pm->x >> 12] & PF_TRACK;
  uint32_t now = pm->steps;
  for (int k = 1; k <= 3 && k <= last && !onTrack && j; k++) {
    int c = p[k];
    if (!(pm->g[c / PM_W][c % PM_W] & PF_TRACK)) continue;
    double ex = (pmMid((int16_t)(c % PM_W)) - pm->x) / 256.0, ey = (pmMid((int16_t)(c / PM_W)) - pm->y) / 256.0;
    int arrive = (int)(sqrt(ex * ex + ey * ey) / 1.2);
    for (int dt = arrive - 8; dt <= arrive + 14 && j; dt += 2) {
      pm->steps = now + (uint32_t)(dt > 0 ? dt : 0);
      for (uint8_t i = 0; i < pm->nMov && j; i++) {
        int32_t mx, my;
        pmMoverAt(i, mx, my);
        double fx = (mx - pmMid((int16_t)(c % PM_W))) / 256.0, fy = (my - pmMid((int16_t)(c / PM_W))) / 256.0;
        if (fx * fx + fy * fy < 12 * 12) { j = 0; pmbWaits++; }
      }
    }
    pm->steps = now;
  }
  int32_t tx = j ? pmMid((int16_t)(p[j] % PM_W)) : pmMid((int16_t)(pm->x >> 12));
  int32_t ty = j ? pmMid((int16_t)(p[j] / PM_W)) : pmMid((int16_t)(pm->y >> 12));
  double dx = (tx - pm->x) / 256.0, dy = (ty - pm->y) / 256.0, d = sqrt(dx * dx + dy * dy);
  uint8_t here = pmAt((int16_t)(pm->x >> 12), (int16_t)(pm->y >> 12));
  double brake = here == PT_ICE ? 1.5 : 4.0, vmax = here == PT_ICE ? 200 : 360;
  if (pmbTrap >= 0) vmax = 120;
  double v = std::min(vmax, sqrt(2 * brake * d * 256) + (j < last && j ? 60 : 0));
  double vx = d > 0.5 ? dx / d * v : 0, vy = d > 0.5 ? dy / d * v : 0;
  ax = (int16_t)std::max(-256.0, std::min(256.0, (vx - pm->vx) * 5));
  ay = (int16_t)std::max(-256.0, std::min(256.0, (vy - pm->vy) * 5));
}

// a level played without the screen; returns the steps to the goal, -1 = not reached
static long pmbRun(uint8_t lv, int &lostPumpkins, int tries = 4) {
  lostPumpkins = 0;
  for (int t = 0; t < tries; t++) {
    if (!pmLoad(lv)) return -1;
    for (long s = 0; s < 20000; s++) {
      int16_t ax, ay;
      pmbAxis(ax, ay);
      pmStep(ax, ay);
      if (pm->st == PS_WON) { lostPumpkins += pm->lost; return (long)pm->steps; }
      if (pm->st == PS_LOST) break;
    }
    lostPumpkins += pm->lost;
  }
  return -1;
}

// the tests' own levels go through the phone's way in
static void pmPutLevel(uint8_t slot, const char *name, const char *const *rows) {
  std::string t = name;
  for (int y = 0; y < PM_H; y++) { t += "|"; t += rows[y]; }
  const char *why = appMazePut(slot, t.c_str());
  if (why) printf("      level %s refused: %s\n", name, why);
}

static void pmShot(const char *name) {                          // the board as it is now, into frames/
  fxReset();
  gfx.clear();
  pm->full = true;
  pmFrame(gameMillis());
  std::string s = scenario;
  scenario = name;
  frames = 1;
  writeFrame(gfx.buffer());
  scenario = s;
}

static void pumpkinLogic() {
  pm = (PmState *)calloc(1, sizeof(PmState));
  pmSvLoad();
  pmLuts();
  // the built-in levels: well formed, a way to the goal, and the bot rolls through
  int bad = 0, over = 0;
  long total = 0;
  for (uint8_t l = 0; l < PM_LEVELS; l++) {
    for (int y = 0; y < PM_H; y++) if (strlen(PM_MAPS[l][y]) != PM_W) { bad++; printf("      level %u row %d: %zu tiles\n", l + 1, y, strlen(PM_MAPS[l][y])); }
    char map[PM_N], name[PM_NAME + 1];
    pmMap(l, map, name);
    int16_t dist = -1;
    const char *why = pmCheck(map, &dist);
    if (why) { bad++; printf("      level %u: %s\n", l + 1, why); continue; }
    int lost = 0;
    clock_t c0 = clock();
    long st = pmbRun(l, lost);
    double us = (double)(clock() - c0) * 1e6 / CLOCKS_PER_SEC / (st > 0 ? st : 1);
    printf("      level %2u %-15s way %3d tiles, par %4.1f s, bot %5.1f s, %d pumpkins lost, %u candies, %u keys, %u moving pits (%.1f us/step)\n",
           l + 1, name, dist, pm->par / 10.0, st / 100.0, lost, pm->candyAll, pm->keysAll, pm->nMov, us);
    if (st < 0) { bad++; printf("      level %u: the bot did not get through\n", l + 1); }
    else { total += st; if (st / 10 > pm->par) over++; }
  }
  check("pumpkin: all 15 levels well formed, a way to the goal, rolled through by the bot", bad == 0);
  printf("      bot over par on %d levels, %.0f s for all\n", over, total / 100.0);
  check("pumpkin: the par times can be beaten (the bot is within par on most levels)", over <= 3);
  // a pit swallows only near its middle: fast past the rim, no; slow over the middle, yes
  static const char *const PIT[PM_H] = {
    "####################", "#S.................#", "#..................#", "#........O.......G.#", "#..................#",
    "#..................#", "#..................#", "#..................#", "#..................#", "#..................#",
    "#..................#", "#..................#", "####################" };
  pmPutLevel(7, "PIT TEST", PIT);
  pmLoad(PM_LEVELS + 7);
  pm->x = pmMid(4); pm->y = pmMid(3) - 8 * 256; pm->vx = PM_VMAX; pm->vy = 0;   // 8 px beside the middle, fast
  bool fell = false;
  for (int s = 0; s < 60; s++) { pmStep(0, 0); if (pm->st == PS_FALL) fell = true; }
  bool overRim = !fell && pm->x > pmMid(10);
  printf("      past the pit to x %d px\n", (int)(pm->x >> 8));
  pmLoad(PM_LEVELS + 7);
  pm->x = pmMid(7); pm->y = pmMid(3) - 3 * 256; pm->vx = 220;           // 3 px beside, slowly
  for (int s = 0; s < 200 && pm->st == PS_ROLL; s++) pmStep(0, 0);
  bool swallowed = pm->st == PS_FALL;
  for (int s = 0; s < 80; s++) pmStep(0, 0);
  printf("      fast past the rim: fell %d; slow: %d, then %u pumpkins left, back at the start %d\n", fell, swallowed, pm->lives,
         pm->x == pm->sx && pm->y == pm->sy);
  check("pumpkin: rolls over a pit's rim at speed, falls in near its middle", overRim && swallowed);
  check("pumpkin: a fall costs a pumpkin and starts again at the start", pm->lives == PM_LIVES - 1 && pm->x == pmMid(1) && pm->y == pmMid(1));
  for (int k = 0; k < 2; k++) {                                   // two more falls: the level is lost
    for (int s = 0; s < 60; s++) pmStep(0, 0);
    pm->x = pmMid(9); pm->y = pmMid(3) - 256; pm->vx = pm->vy = 0;
    for (int s = 0; s < 80; s++) pmStep(0, 0);
  }
  check("pumpkin: the third fall loses the level", pm->st == PS_LOST && pm->lives == 0);
  // floor, ice and mud: half a second of full tilt from rest, then let go
  static const char *const SURF[PM_H] = {
    "####################", "#S.................#", "#iiiiiiiiiiiiiiiiii#", "#mmmmmmmmmmmmmmmmmm#", "#..................#",
    "#..................#", "#..................#", "#..................#", "#..................#", "#..................#",
    "#..................#", "#................G.#", "####################" };
  pmPutLevel(7, "SURFACES", SURF);
  int32_t far[3];
  for (int k = 0; k < 3; k++) {
    pmLoad(PM_LEVELS + 7);
    pm->x = pmMid(1); pm->y = pmMid((int16_t)(k == 0 ? 1 : k + 1));
    for (int s = 0; s < 150; s++) pmStep(s < 50 ? 256 : 0, 0);
    far[k] = (pm->x - pmMid(1)) >> 8;
  }
  printf("      rolled: floor %d px, ice %d px, mud %d px\n", far[0], far[1], far[2]);
  check("pumpkin: ice slides on, mud holds", far[1] > far[0] * 3 / 2 && far[2] * 3 < far[0]);
  // a bumper kicks the pumpkin back; an arrow cannot be rolled against
  static const char *const BUMP[PM_H] = {
    "####################", "#S.................#", "#.....*............#", "#..................#", "#..................#",
    "#<<<<<<<<..........#", "#..................#", "#..................#", "#..................#", "#..................#",
    "#..................#", "#................G.#", "####################" };
  pmPutLevel(7, "BUMP", BUMP);
  pmLoad(PM_LEVELS + 7);
  pm->x = pmMid(3); pm->y = pmMid(2); pm->vx = 400;
  int32_t vmin = 0;
  for (int s = 0; s < 60; s++) { pmStep(0, 0); if (pm->vx < vmin) vmin = pm->vx; }
  printf("      bumper: %u kicks, back at %.1f px/step\n", pm->bumps, -vmin / 256.0);
  check("pumpkin: a bumper kicks the pumpkin away", pm->bumps >= 1 && -vmin >= PM_KICK * 9 / 10);
  pmLoad(PM_LEVELS + 7);
  pm->x = pmMid(1); pm->y = pmMid(5);
  for (int s = 0; s < 500; s++) pmStep(256, 0);
  int32_t xa = pm->x >> 12;
  pm->x = pmMid(12); pm->y = pmMid(5); pm->vx = pm->vy = 0;
  for (int s = 0; s < 300; s++) pmStep(0, 0);
  printf("      against the arrows: reached tile %d; let go behind them: rolled to tile %d\n", xa, (int)(pm->x >> 12));
  check("pumpkin: arrows push one way only", xa < 9 && pm->x >> 12 == 12);
  pmLoad(PM_LEVELS + 7);
  pm->x = pmMid(8); pm->y = pmMid(5);
  for (int s = 0; s < 300; s++) pmStep(0, 0);
  check("pumpkin: an arrow carries the pumpkin along", pm->x >> 12 == 1);
  // key and door (level 8): the door holds until the key is taken
  pmLoad(7);
  pm->x = pmMid(7); pm->y = pmMid(8);
  for (int s = 0; s < 200; s++) pmStep(0, 256);
  bool held = pm->y >> 12 == 8 && PTILE(pm->g[9][7]) == PT_DOOR;
  pmShot("pumpkin_door");
  pm->x = pmMid(15); pm->y = pmMid(1);
  pmStep(0, 0);
  bool open = pm->keys == 1 && PTILE(pm->g[9][7]) == PT_OPEN;
  pm->x = pmMid(7); pm->y = pmMid(8); pm->vx = pm->vy = 0;
  for (int s = 0; s < 200; s++) pmStep(0, 256);
  printf("      door: held %d, key taken and open %d, through to row %d\n", held, open, (int)(pm->y >> 12));
  check("pumpkin: a door holds until the key is taken, then lets the pumpkin through", held && open && pm->y >> 12 >= 10);
  // the rune: a fall starts there
  pmLoad(8);
  pm->x = pmMid(12); pm->y = pmMid(8);
  pmStep(0, 0);
  bool lit = pm->g[8][12] & PF_LIT;
  pm->x = pmMid(3); pm->y = pmMid(9);
  for (int s = 0; s < 90; s++) pmStep(0, 0);
  check("pumpkin: the rune lights up and a fall starts there", lit && pm->lives == PM_LIVES - 1 && pm->x == pmMid(12) && pm->y == pmMid(8));
  // the phone: own levels saved, read back, refused when broken, renamed, emptied
  static const char *const OWN[PM_H] = {
    "####################", "#S..c.....#........#", "#.........#..k.....#", "#..O......#........#", "#.........D........#",
    "#.........#....*...#", "#.........#........#", "######.####..>>>...#", "#..................#", "#..H...............#",
    "#.........C......G.#", "#..................#", "####################" };
  pmPutLevel(0, "MY CRYPT", OWN);
  char out[2048];
  appMazeGet(0, out, sizeof(out));
  std::string want = "MY CRYPT";
  for (int y = 0; y < PM_H; y++) { want += "|"; want += OWN[y]; }
  bool same = want == out;
  appMazeGet(-1, out, sizeof(out));
  bool listed = strstr(out, "\"slots\":8") && strstr(out, "{\"n\":\"MY CRYPT\",\"s\":0,\"t\":0}");
  printf("      list: %.90s...\n", out);
  check("pumpkin: an own level from the phone is kept and read back the same", same && listed);
  const char *e1 = appMazePut(1, "NO START|####|#..G#|####"), *e2 = appMazePut(1, "TWO GOALS|#SGG#"),
             *e3 = appMazePut(1, "WALLED|#S#G#|#####"), *e4 = appMazePut(1, "|#SG#"), *e5 = appMazePut(1, "BAD TILE|#SxG#"),
             *e6 = appMazePut(1, "DOOR|#SDG#"), *e7 = appMazePut(9, "OK|SG");
  printf("      refused: \"%s\" / \"%s\" / \"%s\" / \"%s\" / \"%s\" / \"%s\" / \"%s\"\n", e1, e2, e3, e4, e5, e6, e7);
  check("pumpkin: broken levels are refused with a reason", e1 && e2 && e3 && e4 && e5 && e6 && e7 && !strcmp(e3, "the goal cannot be reached from the start"));
  check("pumpkin: a tiny level with a short row is fine", !appMazePut(1, "TINY|S.G") && appMazeGet(1, out, sizeof(out)) && !strncmp(out, "TINY|S.G..", 10));
  pmSv.stars[PM_LEVELS] = 2; pmSv.best[PM_LEVELS] = 123;
  std::string ren = "RENAMED" + want.substr(want.find('|'));
  bool kept = !appMazePut(0, ren.c_str()) && pmSv.stars[PM_LEVELS] == 2;
  std::string changed = "NEW" + want.substr(want.find('|'));
  changed[changed.size() - 3 * (PM_W + 1) - 2] = '#';           // one tile more wall, low on the right
  bool reset = !appMazePut(0, changed.c_str()) && pmSv.stars[PM_LEVELS] == 0;
  bool gone = !appMazePut(1, "") && !appMazeGet(1, out, sizeof(out));
  check("pumpkin: renaming keeps the record, a changed level starts fresh, an empty text empties the slot", kept && reset && gone);
  pmPutLevel(0, "MY CRYPT", OWN);
  int lost = 0;
  long st = pmbRun(PM_LEVELS, lost);
  printf("      own level: %.1f s, %d lost\n", st / 100.0, lost);
  check("pumpkin: the own level plays (key, door, arrows, rune, a moving pit)", st > 0);
  appMazePut(7, "");
  // QR codes: the same modules as a reference encoder (segno, padded to the byte as the standard says) with the same mask
  struct QrGold { const char *text; uint8_t mask, n; const char *rows[37]; };
  static const QrGold QG[] = {
    { "WIFI:T:WPA;S:MiniArcade-AB12;P:12345678;;", 0, 29, { "1fc4367f","10487e41","1752405d","1749325d","17493e5d","10476f41","1fd5557f","0012fe00","1df1c1c4","1dbb496e","0757442f","1f3d8ceb","0fd6c1fb","0eb6498e","0a4cdda3","0dbac830","097dccec","09af4de2","1357c437","0da1ee29","14c24dfb","00160f1c","1fdc9d53","10520910","175d2dfc","174f8423","175b5c39","1059ef62","1fda431f" } },
    { "WIFI:T:WPA;S:MiniArcade-AB12;P:12345678;;", 5, 29, { "1fce9c7f","1043d041","174e315d","1756cd5d","17554f5d","104cc141","1fd5557f","00095000","18edb018","0204b691","004b35e8","05962251","057c6b51","141de734","0d50ac64","120537cf","0e61bd2b","1304e358","19fd6e9d","170a4093","13de3dfc","0019f113","1fd0ed54","1059a71a","174787f6","17442a99","17472dfe","1056109d","1fd632d8" } },
    { "http://192.168.4.1/maze", 6, 25, { "1fd397f","104cc41","1745c5d","1741f5d","174ac5d","1045d41","1fd557f","0016900","1b4e641","0729f9e","1bd62f9","0abf69f","0eeb1c1","1c30e32","1cc3b8f","1002115","17649f6","0013f12","1fc2559","1049d12","17557fb","175846b","1749017","10546f7","1fd1f49" } },
    { "WIFI:T:WPA;S:MiniArcade-AB12;P:a very long password with spaces\\;semicolon 1234567;;", 3, 37, { "1fd5efa07f","104f986541","175c17695d","175d6a0e5d","17535a135d","1046bb5541","1fd555557f","00053c1600","1e57bea69d","1f95efe0a3","046794f0b0","0fa0343d65","01c9594fc4","170f6291f5","1ad6875000","1f1a46f093","00d576fb0b","0d0478374c","094cf8c257","1d06fc022b","014ccd90be","06b00d2029","1c64443530","0027e0493e","1f523d1fd8","0f96aa1ef2","0ee9434d8e","11a9b590fa","066611f9fc","0011b53112","1fcf251f57","10451f631a","17438a85fe","1751c3f84b","17539038fc","10503279f4","1fd57c5cd7" } } };
  static uint8_t qb[QR_BUF];
  bool qok = true;
  for (const QrGold &q : QG) {
    uint8_t n = qrMake(q.text, qb, q.mask);
    if (n != q.n) { qok = false; printf("      QR \"%s\": %u modules, want %u\n", q.text, n, q.n); continue; }
    for (int y = 0; y < n; y++) {
      unsigned long long v = strtoull(q.rows[y], NULL, 16);
      for (int x = 0; x < n; x++)
        if ((bool)(qb[y * n + x] & 1) != (bool)(v >> (n - 1 - x) & 1)) { if (qok) printf("      QR \"%s\" mask %u: row %d differs\n", q.text, q.mask, y); qok = false; }
    }
  }
  uint8_t na = qrMake(QG[0].text, qb);
  std::string tooLong(110, 'x');
  check("pumpkin: QR codes match the reference encoder, too long a text is refused", qok && na == 29 && !qrMake(tooLong.c_str(), qb));
  // pictures of a few levels
  pmLoad(11); pm->x = pmMid(9); pm->y = pmMid(3); pm->steps = 1234; pmShot("pumpkin_l12");
  pmLoad(14); pm->x = pmMid(8) + 3 * 256; pm->y = pmMid(6); pm->steps = 2345; pm->keys = 1; pm->g[1][18] = PT_FLOOR; pmOpenDoors(); pmShot("pumpkin_l15");
  free(pm); pm = NULL;
}

// on the screen: the bot through the tilt (accelerometer) or through the arrow keys
static bool pmbKeys = false;
static int  pmbLevelsWon = 0, pmbOkAt = 0, pmbStop = 0, pmbLostMax = 0;
static uint32_t pmbAcc[2] = { 0, 0 };
static void pumpkinBot(uint32_t t) {
  if (!pm) { simAcc[0] = simAcc[1] = 0; return; }
  static uint8_t lastSt = PS_ROLL;
  if (pm->st == PS_WON && lastSt != PS_WON) {
    pmbLevelsWon++; pmbOkAt = (int)t + 2300;
    if (pmbLevelsWon == 1) captureMs.push_back(t + 2150);         // the box with its stars
    if (pmbStop && pmbLevelsWon >= pmbStop) simEnd = t + 2500;  // the box with the stars, then the end
  }
  if (pm->lost > pmbLostMax) pmbLostMax = pm->lost;
  if (pmbTrap >= 0 && pm->lost) pmbTrap = -1;                     // fell in on purpose: now to the goal
  lastSt = pm->st;
  if (pmbOkAt && (int)t >= pmbOkAt) { botPress(t, OK, 90); pmbOkAt = 0; return; }
  if (botBusy(t)) return;
  int16_t ax = 0, ay = 0;
  if (tiltGame && pm->steps) pmbAxis(ax, ay);                     // not while the level's name shows
  if (!pmbKeys) {                                                 // the accelerometer, as tilt.h reads it
    const int dead = 4100 * 4 / 10, full = 4100 * 2;
    simAcc[0] = (int16_t)(ax ? (ax > 0 ? 1 : -1) * (dead + abs(ax) * (full - dead) / 256) : 0);
    simAcc[1] = (int16_t)(ay ? (ay > 0 ? 1 : -1) * (dead + abs(ay) * (full - dead) / 256) : 0);
    return;
  }
  uint8_t m = 0;                                                  // the keys, pulsed as often as the axis asks
  int16_t a[2] = { ax, ay };
  for (int k = 0; k < 2; k++) {
    pmbAcc[k] += (uint32_t)abs(a[k]);
    if (pmbAcc[k] >= 256) { pmbAcc[k] -= 256; m |= (uint8_t)(k ? (a[k] > 0 ? DN : UP) : (a[k] > 0 ? RT : LT)); }
  }
  applyMask(m);
}

// Pumpkin Maze through the phone editor: the hotspot opens, a level comes in, the hotspot
// closes, the level is played; then the same in the home WLAN (no hotspot of its own)
static int      pmeStage = 0, pmeDn = 0;
static uint32_t pmeT = 0;
static NetState pmeNetIn = NET_OFF, pmeNetOut = NET_PLAY, pmeHomeOut = NET_OFF;
static const char *const PME_LEVEL[PM_H] = {
  "####################", "#S....#.....O......#", "#.....#..c.....c...#", "#..O..#.....C......#", "#.....D.....###....#",
  "#.....#...........k#", "#####.#####........#", "#...........iiii...#", "#..c........iiii...#", "#.......U...iiii.O.#",
  "#..*........mmmm...#", "#G..........mmmm...#", "####################" };
static void pmEditorBot(uint32_t t) {
  if (pmeStage != 3 && botBusy(t)) return;                        // a press still running
  switch (pmeStage) {
  case 0:                                                         // the phone saves a level while the editor shows
    if (t < pmeT) return;
    pmeNetIn = netState();
    pmPutLevel(0, "GRAVE RUN", PME_LEVEL);
    pmeStage = 1; pmeT = t + 1300;
    return;
  case 1: if (t >= pmeT) { botPress(t, OK, 80); pmeStage = 2; pmeT = t + 700; } return;      // done
  case 2: if (t >= pmeT) { pmeNetOut = netState(); botPress(t, OK, 80); pmeStage = 3; } return;   // play it
  case 3:
    pumpkinBot(t);
    if (pmbLevelsWon >= 1 && !pmbOkAt && !botBusy(t)) { pmeStage = 4; pmeT = t + 1500; }
    return;
  case 4: if (t >= pmeT) { netConnect(); pmeDn = 0; pmeStage = 5; } return;                   // now in the home WLAN
  case 5:
    if (botBusy(t)) return;
    if (pmeDn < PM_OWN) { botPress(t, DN, 60); pmeDn++; } else { botPress(t, OK, 80); pmeStage = 6; pmeT = t + 2000; }
    return;
  case 6: if (t >= pmeT && !botBusy(t)) { botPress(t, OK, 80); pmeStage = 7; pmeT = t + 800; } return;
  case 7: if (t >= pmeT) { pmeHomeOut = netState(); simEnd = t + 300; pmeStage = 8; } return;
  }
}

// ---------------- the translations ----------------
static uint32_t sprSwitch = 0, sprBack = 0;      // "sprache": when the language was switched there and back
static uint16_t sprLangSaved = 0;
/* Every TR("English", "Deutsch") in main/: the same placeholders in both,
   only letters the fonts have, and a German text longer than its English
   one must have been drawn in German somewhere - by the scenarios run with
   SPRACHE=de or by "sprache" itself - where it was checked against the
   screen's edges. Shorter ones fit wherever the English one fits.      */
struct TrPair { std::string en, de, at; };
static std::string trUnescape(const std::string &q) {
  std::string o;
  for (size_t i = 0; i < q.size(); i++) {
    if (q[i] == '\\' && i + 1 < q.size()) { i++; o += q[i] == 'n' ? '\n' : q[i]; }
    else o += q[i];
  }
  return o;
}
static std::vector<TrPair> trPairs() {
  std::vector<TrPair> v;
  static const std::regex re("TR\\(\\s*\"((?:[^\"\\\\]|\\\\.)*)\"\\s*,\\s*\"((?:[^\"\\\\]|\\\\.)*)\"\\s*\\)");
  DIR *d = opendir("../main");
  if (!d) return v;
  std::vector<std::string> files;
  while (dirent *e = readdir(d)) {
    std::string n = e->d_name;
    if (n.size() > 2 && (n.compare(n.size() - 2, 2, ".h") == 0 || (n.size() > 4 && n.compare(n.size() - 4, 4, ".cpp") == 0))) files.push_back(n);
  }
  closedir(d);
  for (const std::string &n : files) {
    std::ifstream in("../main/" + n);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string src = ss.str();
    for (std::sregex_iterator it(src.begin(), src.end(), re), end; it != end; ++it)
      v.push_back({ trUnescape((*it)[1]), trUnescape((*it)[2]), n });
  }
  return v;
}
static int trLetters(const std::string &s) { int n = 0; for (unsigned char c : s) if ((c & 0xC0) != 0x80) n++; return n; }
// the placeholders of a format, in order ("%%" is a letter); spec = true: one of them starts at i
static size_t trSpecEnd(const std::string &s, size_t i) {
  size_t k = i + 1;
  while (k < s.size() && strchr("-+ #0123456789.*hlLqjzt", s[k])) k++;
  return k < s.size() ? k + 1 : k;
}
static std::string trSpecs(const std::string &s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++)
    if (s[i] == '%') { if (i + 1 < s.size() && s[i + 1] == '%') { i++; continue; } size_t k = trSpecEnd(s, i); o += s.substr(i, k - i) + " "; i = k - 1; }
  return o;
}
static bool trGlyphsOk(const std::string &s) {                   // ASCII or ä ö ü Ä Ö Ü ß
  for (size_t i = 0; i < s.size(); i++) {
    unsigned char c = (unsigned char)s[i];
    if (c >= 32 && c <= 126) continue;
    if (c == 0xC3 && i + 1 < s.size() && strchr("\xA4\xB6\xBC\x84\x96\x9C\x9F", s[i + 1])) { i++; continue; }
    return false;
  }
  return true;
}
// the German text as drawn: every placeholder can be anything
static bool trDrawn(const std::string &de, const std::set<std::string> &drawn) {
  std::string pat, lit, longest;
  for (size_t i = 0; i < de.size(); i++) {
    if (de[i] == '%' && i + 1 < de.size() && de[i + 1] != '%') {
      size_t k = trSpecEnd(de, i);
      pat += ".*"; if (lit.size() > longest.size()) longest = lit; lit.clear(); i = k - 1; continue;
    }
    if (de[i] == '%') i++;
    if (strchr("\\^$.|?*+()[]{}", de[i])) pat += '\\';
    pat += de[i]; lit += de[i];
  }
  if (lit.size() > longest.size()) longest = lit;
  std::regex re(pat);
  for (const std::string &d : drawn)
    if (d.find(longest) != std::string::npos && std::regex_search(d, re)) return true;
  return false;
}
static void langTable() {
  std::vector<TrPair> v = trPairs();
  std::set<std::string> drawn = deTexts;
  std::ifstream in("frames/lang_de.txt");
  for (std::string l; std::getline(in, l); ) drawn.insert(l);
  int longer = 0, bad = 0, unseen = 0;
  for (const TrPair &p : v) {
    if (trSpecs(p.en) != trSpecs(p.de)) { printf("      %s: placeholders differ: '%s' / '%s'\n", p.at.c_str(), p.en.c_str(), p.de.c_str()); bad++; }
    if (!trGlyphsOk(p.de)) { printf("      %s: a letter the font lacks: '%s'\n", p.at.c_str(), p.de.c_str()); bad++; }
    if (trLetters(p.de) <= trLetters(p.en)) continue;
    longer++;
    if (!trDrawn(p.de, drawn)) { printf("      %s: longer in German, never drawn: '%s'\n", p.at.c_str(), p.de.c_str()); unseen++; }
  }
  printf("      %zu translations, %d longer in German, %zu German texts drawn\n", v.size(), longer, drawn.size());
  check("every translation: same placeholders, letters the font has", v.size() > 200 && !bad);
  check("every German text longer than its English one was drawn and stayed on the screen", !unseen);
}

int main(int argc, char **argv) {
  scenario = argc > 1 ? argv[1] : "menu";
  srand(1);
  if (getenv("PALREC") && scenario.compare(0, 6, "palace") == 0) {
    palRec = popen((std::string("gzip -1 > ") + getenv("PALREC")).c_str(), "w");
    atexit(palRecEnd);
  }
  if (scenario.find("fair") != std::string::npos || scenario == "frogroll") {
    printf("== %s\n", scenario.c_str());
    int r = logicTest();
    if (r >= 0) return r;
  }
  // a console that was set up before: four keys, a buzzer, a battery
  simNvsU16["pset"] = 3; simNvsU16["sset"] = 1; simNvsU16["snd"] = SND; simNvsU16["bat"] = BAT;
  for (int i = 0; i < 4; i++) { simNvsU16["p" + std::to_string(i)] = KEYS[i]; simNvsU16["a" + std::to_string(i)] = 1; }
  simNvsU16["p4"] = 255;
  simDe = getenv("SPRACHE") && !strcmp(getenv("SPRACHE"), "de");
  if (simDe) { simNvsU16["lang"] = 1; framePre = "de_"; }
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
    uint32_t t = openGameNamed("Mine 3D", M); simEnd = t + 187000;
    press(t + 300, DN); press(t + 600, OK, 80); t += 1000;      // ore hunt
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
  } else if (scenario == "mine3dfree") {
    // free play: dig and build without time, leave, come back to the same world
    uint32_t t = openGameNamed("Mine 3D", M); simEnd = t + 52000;
    press(t + 300, OK, 80);                                      // free play: new world
    press(t + 1000, DN, 40); press(t + 1400, DN, 40);            // look down, steep down
    press(t + 1800, OK, 60); press(t + 2500, OK, 60);            // dig two
    press(t + 4400, OK, 60); press(t + 4560, OK, 60);            // the bag
    for (int i = 0; i < 8; i++) press(t + 5000 + 250 * i, RT);   // gold
    press(t + 7200, OK, 60);
    press(t + 7900, OK, 60); press(t + 8700, OK, 60);            // fill the pit with gold
    press(t + 40000, OK, 900); press(t + 41500, OK, 900);        // pause, quit: after 40 s, no TIME UP
    press(t + 43500, OK, 80);                                    // Mine 3D again
    press(t + 44500, OK, 80);                                    // continue my world
    captureMs = { t + 9000, t + 46000 };
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
  } else if (scenario == "uploadask") {
    // WLAN stays on after the page; a firmware from the web page is asked about in the library
    uint32_t t = M;
    press(t, UP); press(t + 400, OK, 80);                        // settings
    t += 1000;
    for (int i = 0; i < 5; i++) { press(t, UP); t += 250; }      // up to the WLAN line
    press(t, OK, 80); t += 800;
    for (int i = 0; i < 5; i++) { press(t, DN); t += 250; }      // "stay online"
    press(t, OK, 80); t += 500;
    press(t, OK, 900); t += 1500;                                // out of the WLAN page ...
    press(t, OK, 900); t += 1500;                                // ... and out of the settings
    simAt.push_back({ t, [] { simJob = JOB_ASKING; } });         // the first upload: no
    press(t + 800, LT); t += 2000;
    simAt.push_back({ t, [] { simJob = JOB_ASKING; } });         // the second: yes
    press(t + 800, OK, 80);
    simEnd = t + 2500;
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
  } else if (scenario == "ladderpair") {
    // five keys on one pin, then UP+RIGHT together tell the supply; later pairs are read as two keys
    simNvsU16.erase("pset"); LADDER = 2; LADDER_R = true;
    press(2500, 1 << 4, 400); press(3300, 2 << 4, 400); press(4100, 3 << 4, 400); press(4900, 4 << 4, 400);
    press(5700, 5 << 4, 400);
    press(6700, 6 << 4, 700);                                    // UP + RIGHT: the supply
    script.push_back({ 13000, 7 << 4 });                         // DOWN + LEFT held to the end
    simEnd = 14000;
  } else if (scenario == "ladderbat") {
    // ladder keys on GPIO2, the battery divider on GPIO4 not known yet: it must not count as a key
    simNvsU16.erase("pset"); simNvsU16.erase("bat"); LADDER = 2; LADDER_R = true;
    press(2500, 1 << 4, 400); press(3300, 2 << 4, 400); press(4100, 3 << 4, 400); press(4900, 4 << 4, 400);
    press(5700, 5 << 4, 400);
    press(6700, 6 << 4, 700);                                    // UP + RIGHT: the supply
    simEnd = 12000;
  } else if (scenario == "plain2") {
    // two keys on GPIO6, which has no ADC: the wizard says so
    simNvsU16.erase("pset");
    press(2500, RT, 400); press(3300, RT, 600);
    simEnd = 4500;
  } else if (scenario == "tilt") {
    // settings -> tilt control on: sensor found, all four ways learned (LEFT, UP and DOWN
    // each first tilted a wrong way and asked again; LEFT not as far as RIGHT); level saved;
    // then Snake: the pause menu offers a new level, tilting steers
    simGyro = true; simNvsU16["snd"] = 255;                      // GPIO7 is the gyroscope's here
    press(M, UP); press(M + 400, OK, 80);
    for (int i = 0; i < 9; i++) press(M + 800 + 220 * i, DN);
    press(M + 2900, RT);
    accScript = { { M + 5000, 8000, 0 }, { M + 6000, 0, 0 },     // RIGHT
                  { M + 7500, 0, -8000 }, { M + 8500, 0, 0 },    // LEFT asked, away given: once more
                  { M + 12000, -6500, 0 }, { M + 13000, 0, 0 },  // LEFT, not as far
                  { M + 14500, 8000, 0 }, { M + 15500, 0, 0 },   // UP asked, right given: once more
                  { M + 19000, 0, -8000 }, { M + 20000, 0, 0 },  // UP (away)
                  { M + 21500, 0, -8000 }, { M + 22500, 0, 0 },  // DOWN asked, away again: once more
                  { M + 26000, 0, 7000 }, { M + 27000, 0, 0 },   // DOWN
                  { M + 40000, 0, -8000 } };                     // the last one: tilt up in the game
    press(M + 29800, DN); press(M + 30200, OK, 80);              // "tilt level": save it
    press(M + 35200, OK, 900);                                   // leave the settings
    press(M + 36500, DN); press(M + 36900, OK, 80);              // Snake
    press(M + 38000, OK, 900);                                   // pause ...
    press(M + 39500, OK, 80);                                    // ... continue
    simEnd = M + 41000;
    setenv("GRAB", "TILT SETUP", 1);
  } else if (scenario == "sdcard") {
    // a card on GPIO20 is found at the first start; the settings show it
    simSd = true;
    press(M, UP); press(M + 400, OK, 80);                        // settings
    for (int i = 0; i < 16; i++) press(M + 800 + 200 * i, DN);   // down to "sd card"
    simEnd = M + 5000;
    setenv("GRAB", "sd card      7.4 GB, 92% free", 1);
  } else if (scenario == "fps") {
    // settings -> show fps on, then Snake shows its frame rate
    press(M, UP); press(M + 400, OK, 80);
    for (int i = 0; i < 5; i++) press(M + 800 + 220 * i, DN);
    press(M + 2200, RT);
    press(M + 2800, OK, 900);                                    // leave the settings
    press(M + 4200, DN); press(M + 4600, OK, 80);                // Snake
    simEnd = M + 9000;
    setenv("GRAB", "SNAKE", 1);
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
  } else if (scenario == "blocks") {                            // classic, a bot until no piece fits
    uint32_t t = openGameNamed("Block Puzzle", M); simEnd = t + 150000;
    press(t + 300, OK, 80);
    pzStart = t + 1000; pzBot = blocksBot; captureMs = { t + 30000 };
    setenv("GRAB", "NO SPACE", 1);
  } else if (scenario == "blocksadv") {                         // adventure: level select, level 13 played to its goal
    simNvsU16["BLKlv"] = 12;
    uint32_t t = openGameNamed("Block Puzzle", M); simEnd = t + 200000;
    press(t + 300, DN); press(t + 600, OK, 80); press(t + 1100, OK, 80);
    pzStart = t + 1800; pzBot = blocksBot; bkBotRnd = 0; captureMs = { t + 9000 };
    setenv("GRAB", "LEVEL 13 DONE", 1);
  } else if (scenario == "blockspz") {                          // puzzle 1: every piece back where it belongs
    uint32_t t = openGameNamed("Block Puzzle", M); simEnd = t + 120000;
    press(t + 300, UP); press(t + 600, OK, 80); press(t + 1100, OK, 80);
    pzStart = t + 1800; pzBot = blocksBot; captureMs = { t + 2000 };
    setenv("GRAB", "CLEARED!", 1);
  } else if (scenario == "fruit") {                             // a bot drops fruits until the pot is full
    uint32_t t = openGameNamed("Fruit Pot", M); simEnd = t + 200000;
    pzStart = t + 600; pzBot = fruitBot; captureMs = { t + 40000, t + 90000 };
    setenv("GRAB", "POT FULL!", 1);
  } else if (scenario == "pumpkin") {                           // tilt: the first levels rolled through, candies and all
    simGyro = true; simNvsU16["snd"] = 255;                      // GPIO7 is the gyroscope's here
    simNvsU16["tilt"] = 1; simNvsU16["gsda"] = 21; simNvsU16["gscl"] = 8; simNvsU16["gax"] = 0x8100;
    uint32_t t = openGameNamed("Pumpkin Maze", M); simEnd = t + 200000;
    press(t + 700, OK, 80);                                      // level 1
    simBot = pumpkinBot; botFrom = t + 900;
    pmbCandy = true; pmbStop = 3;
    captureMs = { t + 400, t + 1500, t + 4500 };
  } else if (scenario == "pumpkinkeys") {                       // keys: level 3, first into a pit on purpose, then to the goal
    PmSave sv;
    memset(&sv, 0, sizeof(sv)); sv.ver = 1; sv.open = 2;
    simNvsBlob["PMZsv"] = std::string((const char *)&sv, sizeof(sv));
    uint32_t t = openGameNamed("Pumpkin Maze", M); simEnd = t + 120000;
    press(t + 1300, OK, 80);                                     // the list starts at the level reached
    simBot = pumpkinBot; botFrom = t + 1500;
    pmbKeys = true; pmbTrap = 1 * PM_W + 7;                      // the pit right of the start
    pmbStop = 1;
    captureMs = { t + 2000 };
  } else if (scenario == "pmeditor") {                          // the phone editor on the own hotspot, the level played
    uint32_t t = openGameNamed("Pumpkin Maze", M); simEnd = t + 200000;
    for (int i = 0; i < 3 + PM_OWN; i++) press(t + 700 + 250 * i, DN);   // to the own levels, down to the editor
    press(t + 700 + 250 * (3 + PM_OWN) + 300, OK, 80);
    uint32_t e = t + 700 + 250 * (3 + PM_OWN) + 300;
    simBot = pmEditorBot; botFrom = e + 400; pmeT = e + 1800;
    pmbKeys = true;
    captureMs = { e + 1500, e + 2600, e + 4200 };
  } else if (scenario == "pmlogic") {
    printf("== %s\n", scenario.c_str());
    pumpkinLogic();
    return failures ? 1 : 0;
  } else if (pzLogic()) {
    printf("== %s\n", scenario.c_str());
    printf("%s\n", failures ? "### FAILURES ###" : "   all checks passed");
    return failures ? 1 : 0;
  } else if (scenario == "tvdecode") {                          // the TV's decoder and clock, no screen
    printf("== %s\n", scenario.c_str());
    tvDecodeTest();
    return failures ? 1 : 0;
  } else if (scenario == "tvui") {
    // the TV from scratch: no server address (the phone page sets it), pairing,
    // the list with thumbnails, a video played: +10 s, pause, play, sharp, a lost
    // connection, back to the list, back to the menu
    uint32_t t = openGameNamed("TV", M); simEnd = t + 34000;
    simNvsU16["fps"] = 1;                                        // the frame rate shown over the video
    simBot = tvUiBot; botFrom = t;
    tvUiT = t;
    simTvLoadGolden();
    std::vector<std::string> g;
    tvDecodeAll(g);
    (void)t;
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
  } else if (scenario == "skat") {
    uint32_t t = openGameNamed("Skat", M); simEnd = t + 1000000;
    press(t + 400, OK, 80);                                      // "Liste: 12 games"
    simBot = skatBot; botFrom = t + 1000;
  } else if (scenario == "skatlogic") {
    printf("== %s\n", scenario.c_str());
    skatLogic();
    return failures ? 1 : 0;
  } else if (scenario == "minigolf") {
    uint32_t t = openGameNamed("Minigolf", M); simEnd = t + 300000;
    simBot = golfBot; botFrom = t + 800;
    captureMs = { t + 2500, t + 60000 };
    setenv("GRAB", "HOLE 6/9", 1);
  } else if (scenario == "chess") {
    uint32_t t = openGameNamed("Chess", M); simEnd = t + 20000;
    press(t + 300, UP); press(t + 600, OK, 80);                  // 2 players (the last)
    press(t + 1000, DN); press(t + 1300, OK, 80);                // board stays
    pzStart = t + 2000; pzBot = chessBot; captureMs = { t + 5600 };
    setenv("GRAB", "WINS", 1);
  } else if (scenario == "chesscpu") {
    // the bot plays black against the CPU on hard: the board turns, the CPU opens
    uint32_t t = openGameNamed("Chess", M); simEnd = t + 150000;
    press(t + 300, DN); press(t + 600, DN); press(t + 900, OK, 80);   // 1P hard
    press(t + 1300, DN); press(t + 1600, OK, 80);                // black
    pzStart = t + 2200; pzBot = chessCpuBot; captureMs = { t + 9000 };
    setenv("GRAB", "thinking...", 1);
  } else if (scenario == "chesssave") {
    // three moves against easy, leave, come back: the game goes on where it was
    uint32_t t = openGameNamed("Chess", M); simEnd = t + 26000;
    press(t + 300, OK, 80);                                      // 1P easy
    press(t + 700, OK, 80);                                      // white
    chBotMax = 3;
    pzStart = t + 1200; pzBot = chessCpuBot;
    press(t + 16000, OK, 900);                                   // hold: pause menu ...
    press(t + 17500, OK, 900);                                   // ... hold again: quit
    press(t + 20000, OK, 80);                                    // Chess again
    press(t + 21000, OK, 80);                                    // continue saved game
    captureMs = { t + 20600, t + 23000 };
  } else if (scenario == "mine3dlogic") {
    printf("== %s\n", scenario.c_str());
    mine3dLogic();
    return failures ? 1 : 0;
  } else if (scenario == "palace" || scenario == "palacecont") {
    if (scenario == "palacecont") simNvsU16["PALlv"] = 5;        // level 6 was reached before
    uint32_t t = openGameNamed("Palace", M); simEnd = t + 1500000;  // the bot plays all eight levels
    simBot = palaceBot; botFrom = t + 600; pbBusy = t + 1600;
    if (scenario == "palace") setenv("GRAB", "best ", 1);        // the game over panel
  } else if (scenario == "palacelogic") {
    printf("== %s\n", scenario.c_str());
    palaceLogic();
    return failures ? 1 : 0;
  } else if (scenario == "fxlogic") {
    printf("== %s\n", scenario.c_str());
    fxLogic();
    return failures ? 1 : 0;
  } else if (scenario == "palaceshots") {
    printf("== %s\n", scenario.c_str());
    palaceShots();
    return failures ? 1 : 0;
  } else if (scenario == "chesslogic") {
    printf("== %s\n", scenario.c_str());
    chessLogic();
    return failures ? 1 : 0;
  } else if (scenario == "mpchess") {                           // chess moves on the protocol, no screen
    printf("== %s\n", scenario.c_str());
    chessProtocol();
    return failures ? 1 : 0;
  } else if (scenario == "sprache") {
    // settings -> language Deutsch, at once and saved; every settings line and the setup
    // screens behind them in German, the library, the pause menu, the Pumpkin Maze list
    // and the TV's screens too; then back to English
    simNvsU16["tilt"] = 1; simNvsU16["gax"] = 0x8100;          // tilt control on, but no sensor
    const char URL[] = "https://tv.example.de";                 // a TV server, not paired yet
    simNvsBlob["url"] = std::string(URL, sizeof(URL));
    PmSave sv;                                                   // Pumpkin Maze: three levels played
    memset(&sv, 0, sizeof(sv)); sv.ver = 1; sv.open = 2;
    for (int i = 0; i < 3; i++) { sv.stars[i] = 2; sv.best[i] = (uint16_t)(150 + i * 40); }
    simNvsBlob["PMZsv"] = std::string((const char *)&sv, sizeof(sv));
    press(M, UP); press(M + 400, OK, 80);                        // settings
    uint32_t t = M + 900;
    press(t, UP); press(t + 250, UP); press(t + 500, RT);        // the last line but one: language
    sprSwitch = t + 500;
    simAt.push_back({ t + 800, [] { sprLangSaved = simNvsU16.count("lang") ? simNvsU16["lang"] : 0; } });
    captureMs = { t + 800 };
    t += 1100;
    for (int i = 0; i < 21; i++) {                               // every line once, back at the language
      press(t, DN);
      if (i == 8 || i == 9) { press(t + 100, RT); press(t + 200, RT); }   // red/blue, colours: changed and back
      if (i == 12) { press(t + 100, LT); press(t + 200, RT); }            // tilt: weak, normal again
      t += 300;
    }
    captureMs.push_back(t - 300 * 15);                           // the first lines
    t += 300;
    press(t, UP); press(t + 250, OK, 80); press(t + 1100, OK, 900);              // firmware version, back
    t += 2400;
    press(t, UP); press(t + 250, OK, 80); press(t + 1100, OK, 900);              // tv server: the keyboard, cancelled
    t += 2400;
    uint32_t w = t + 250;                                        // wlan and update
    press(t, UP); press(w, OK, 80);
    press(w + 500, DN); press(w + 750, DN); press(w + 1000, OK, 80);             // check for update: a newer one
    captureMs.push_back(w + 1500);
    simAt.push_back({ w + 1700, [] { simJob = JOB_UPDATING; } });
    simAt.push_back({ w + 2300, [] { simJob = JOB_IDLE; } });
    simAt.push_back({ w + 2600, [] { simJob = JOB_ASKING; } });  // a firmware from the web page: no
    press(w + 3200, LT);
    simAt.push_back({ w + 3600, [] { simRestoreAsk = true; simJob = JOB_IDLE; } });   // a backup from the phone: no
    press(w + 4200, LT);
    press(w + 4600, DN); press(w + 4900, OK, 80);                // the phone hotspot
    press(w + 5400, RT); press(w + 5800, RT); press(w + 6100, LT); press(w + 6500, OK, 80);
    simAt.push_back({ w + 6800, [] { simNoNet = true; } });      // joining a network that is not there
    press(w + 7000, UP); press(w + 7250, UP); press(w + 7500, UP); press(w + 7800, OK, 80);
    simAt.push_back({ w + 8400, [] { simNet = NET_FAILED; simNoNet = false; } });
    press(w + 9000, DN); press(w + 9300, OK, 80);                // set up with the phone
    press(w + 9900, OK, 900);                                    // hold OK: out of the page
    t = w + 11200;
    simAt.push_back({ t, [] { runClock = 40; } });               // too slow for the WLAN
    press(t + 200, OK, 80); press(t + 1000, OK, 900);
    simAt.push_back({ t + 2200, [] { runClock = 160; } });
    t += 2400;
    simAt.push_back({ t, [] { tftPins.miso = -1; } });           // sd card: the display's SDO not wired
    press(t, UP); press(t + 250, OK, 80);
    simAt.push_back({ t + 4500, [] { tftPins.miso = 9; simSd = true; } });
    press(t + 4700, OK, 80);                                     // ... then a card on GPIO20
    t += 8000;
    press(t, UP); press(t + 250, OK, 80); press(t + 900, LT); press(t + 1200, DN); press(t + 1500, OK, 900);   // battery
    t += 2800;
    press(t, UP); press(t + 250, OK, 80);                        // sound: no pin beeps
    for (int i = 0; i < 4; i++) press(t + 800 + 300 * i, RT);
    t += 800 + 300 * 4 + 2000;
    press(t, UP); press(t + 250, UP); press(t + 500, UP); press(t + 750, OK, 80);   // tilt level: no sensor
    t += 2950;
    press(t, UP); press(t + 250, OK, 80);                        // tilt control: set up again, no sensor
    t += 3600;
    for (int i = 0; i < 5; i++) press(t + 250 * i, UP);          // screen speed: slower, then faster and kept
    press(t + 1400, LT); press(t + 1800, RT); press(t + 3000, OK, 80);
    t += 3600;
    press(t, OK, 900);                                           // hold OK: the library
    captureMs.push_back(t + 1400);
    press(t + 1600, DN); press(t + 2000, OK, 80);                // Snake ...
    press(t + 3200, OK, 900);                                    // ... paused
    captureMs.push_back(t + 4600);
    press(t + 4900, DN); press(t + 5200, DN); press(t + 5500, OK, 80);   // quit to the menu
    t = openGameNamed("Pumpkin Maze", t + 6300);                // the level list, the own levels
    captureMs.push_back(t + 800);
    for (int i = 0; i < 3 + PM_OWN; i++) press(t + 1000 + 250 * i, DN);
    t += 1000 + 250 * (3 + PM_OWN) + 300;
    simAt.push_back({ t, [] { runClock = 40; } });               // the phone editor: too slow for the WLAN
    press(t + 200, OK, 80);
    simAt.push_back({ t + 4000, [] { runClock = 160; } });
    press(t + 4200, OK, 900);                                    // hold OK: the library
    t += 5600;
    for (int i = 0; i < 3; i++) press(t + 250 * i, DN);          // TV: no WLAN saved
    simAt.push_back({ t + 700, [] { simNoConfig = true; } });
    press(t + 900, OK, 80); press(t + 1900, OK, 80);
    simAt.push_back({ t + 2400, [] { simNoConfig = false; simNoNet = true; } });   // ... the network not there
    press(t + 2600, OK, 80);
    simAt.push_back({ t + 3200, [] { simNet = NET_FAILED; simNoNet = false; } });
    press(t + 3800, OK, 900);
    simAt.push_back({ t + 5000, [] { simTvDown = true; } });     // ... the server not answering
    press(t + 5200, OK, 80); press(t + 6400, OK, 900);
    simAt.push_back({ t + 7600, [] { simTvDown = false; strcpy(tvTok, "TOKEN-1234567890-abcdefghijklmnop"); } });
    press(t + 7800, OK, 80);                                     // ... paired: the list, then its setup
    press(t + 9800, LT);
    captureMs.push_back(t + 10400);
    press(t + 10800, OK, 900);
    t += 12200;
    press(t, DN); press(t + 400, OK, 80);                        // settings again: back to English
    press(t + 900, UP); press(t + 1150, UP); press(t + 1400, LT);
    sprBack = t + 1400;
    simEnd = t + 2400;
  } else if (scenario == "online") {
    // the lobby: rename, a friend, Chess with a console that has none, a "no",
    // 4 wins, ANNA's challenge to Tic Tac Toe accepted, another one declined, back
    uint32_t t = openGameNamed("Online", M); simEnd = t + 600000;
    mpSteps = "nfc:xc1yc1i2ai1dq";
    simBot = onlineDriver; botFrom = t;
    captureMs = { t + 1500, t + 2600 };
    setenv("GRAB", "wants to play", 1);
  } else if (scenario == "inetc4") {
    // internet: 4 wins with ANNA, her challenge to Tic Tac Toe accepted, a "no", no answer
    // at all, CARL busy, back
    uint32_t t = openGameNamed("Online", M); simEnd = t + 900000;
    inetScenario("Ic1i2axc1nc2ybq", t);
    captureMs = { t + 3500 };
  } else if (scenario == "inetpong") {
    // internet: Pong to its end over a relay 40-70 ms each way
    uint32_t t = openGameNamed("Online", M); simEnd = t + 900000;
    simNetMs = 40; simNetJit = 30;
    inetScenario("Ic3q", t);
    captureMs = { t + 9000 };
  } else if (scenario == "inetfriend") {
    // internet, not paired yet: the PIN, then the own code, BOB added by his code (offline),
    // a code nobody has, the own one, ANNA a friend with RIGHT, back
    uint32_t t = openGameNamed("Online", M); simEnd = t + 900000;
    inetScenario("Imfousrq", t);
    simNvsBlob.erase("tok");
    strcpy(tvPinRam, "120000");                                  // the PIN, as if typed on the phone page
  } else if (scenario == "inetall") {
    // internet: every online game with ANNA, one after the other, 30-50 ms each way;
    // Chess is left after 40 plies like in mpchessgame
    uint32_t t = openGameNamed("Online", M); simEnd = t + 3000000;
    simNetMs = 30; simNetJit = 20;
    inetScenario("Ic1c2c3c4c5c6c7c8c9c:q", t);
  } else if (scenario == "inetlost") {
    // internet: the server goes away in a game (connection lost), comes back; then ANNA is
    // switched off in a game (gone)
    uint32_t t = openGameNamed("Online", M); simEnd = t + 900000;
    inetScenario("IDc1Ugc2q", t);
  } else if (scenario.compare(0, 2, "mp") == 0 && scenario.size() > 2) {   // one game online, to its end
    static const char *const G[] = { "c4", "ttt", "pong", "snake", "pac", "ship", "dame", "mau", "bomb", "chessgame" };
    uint8_t g = 0;
    for (uint8_t i = 0; i < 10; i++) if (scenario == std::string("mp") + G[i]) g = i + 1;
    if (!g) { printf("unknown scenario %s\n", scenario.c_str()); return 2; }
    uint32_t t = openGameNamed("Online", M); simEnd = t + 600000;
    onlineGame(g, t);
    captureMs = { t + 9000, t + 16000 };
  } else { printf("unknown scenario %s\n", scenario.c_str()); return 2; }
  if (const char *cap = getenv("CAP"))                        // more frames to look at: CAP=300,450,...
    for (const char *c = cap; *c; ) { captureAt.insert(atol(c)); while (*c && *c != ',') c++; if (*c) c++; }
  printf("== %s\n", scenario.c_str());
  try {
    setup();
    for (;;) loop();
  } catch (SimEnd &) {}
  if (simDe) {                                                  // German: the screen's edges only, the texts for "sprache"
    check("frames were drawn", frames > 20);
    for (const std::string &o : deOver) printf("      off the screen: %s\n", o.c_str());
    check("German: the menus were drawn, no text runs off the screen", deOver.empty() && !deTexts.empty());
    FILE *f = fopen("frames/lang_de.txt", "a");
    if (f) { for (const std::string &x : deTexts) if (!strchr(x.c_str(), '\n')) fprintf(f, "%s\n", x.c_str()); fclose(f); }
    printf("   %zu German texts, %ld frames, %.1f s simulated\n", deTexts.size(), frames, clockUs / 1e6);
    return failures ? 1 : 0;
  }

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
  if (scenario == "mine3dfree") {
    printf("      kept: %u bytes, %u blocks changed, %d put, free %d\n", (unsigned)simNvsBlob["m3w"].size(), vox ? vox->edN : 0, mine3dPut, vox ? vox->free : 0);
    check("free play: no time limit", !seen("TIME UP") && seen("free play   0% built"));
    check("free play: blocks from the bag without end", seen("< gold >   UP+DOWN = take") && mine3dPut >= 1);
    check("the world kept in flash", simNvsBlob.count("m3w") && simNvsBlob["m3w"].size() > sizeof(VoxSaveHead));
    check("continued: the same world with its changes", seen("continue my world") && vox && vox->free && vox->edN >= 2);
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
  if (scenario == "blocks") {
    printf("   best stored %u, pieces placed %d\n", simNvsU16["hBLK"], bkBotPlaced);
    check("block puzzle: modes, board and tray shown", seen("BLOCK PUZZLE") && seen("classic") && seen("BLOCKS") && seen("lines 0"));
    check("block puzzle: lines cleared, combos and points", (seen("COMBO x2") || seen("STREAK x2")) && bkBotPlaced > 20);
    check("block puzzle: no space ends it, score kept", seen("NO SPACE") && over() && simNvsU16["hBLK"] > 0);
  }
  if (scenario == "blocksadv") {
    printf("   adventure reached %u\n", simNvsU16["BLKlv"]);
    check("block puzzle: level select with the level reached", seen("continue: level 13") && seen("level 1"));
    check("block puzzle: level 13 won, progress saved", seen("LEVEL 13 DONE") && simNvsU16["BLKlv"] == 13);
  }
  if (scenario == "blockspz") {
    check("block puzzle: puzzle picker", seen("PUZZLES") && seen("#1") && seen("the next one"));
    check("block puzzle: puzzle 1 cleared with its pieces, the next one offered", seen("CLEARED!") && simNvsU16["BLKdy"] >= 2);
  }
  if (scenario == "fruit") {
    printf("   best stored %u, merges %u\n", simNvsU16["hFRT"], frBotMerges);
    check("fruit pot: pot, next fruit and the chain shown", seen("FRUIT POT") && seen("NEXT") && seen("BIGGEST"));
    check("fruit pot: fruits merged", frBotMerges >= 5);
    check("fruit pot: a full pot ends it, score kept", seen("POT FULL!") && over() && simNvsU16["hFRT"] > 0);
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
  if (scenario == "skat") {
    printf("      skat: %u games, totals %d %d %d; the bot bid %d times, declared %d (pushed %d), played %d cards\n",
           sktRows, sktTotal[0], sktTotal[1], sktTotal[2], sktBotBid, sktBotDeclared, sktBotPushed, sktBotCards);
    check("skat ran: bidding, the Skat, trick play", seen("SKAT") && seen("REIZEN") && seen("Skat") && sktBotCards > 50);
    check("skat: you bid and declared through the keys", sktBotBid > 3 && sktBotDeclared > 0 && sktBotPushed > 0);
    check("skat: a Liste of 12 games to the end, game over shown", sktRows == 12 && seen("FINAL LIST") && overShown);
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
    check("levels offered, the finished game not kept", seen("1P hard") && simNvsBlob["chsave"].size() == 1);
  }
  if (scenario == "chesscpu") {
    printf("      CPU moves %d (most %u nodes, %.1f plies deep on average), bot moves %d, slowest answer %u ms (simulated)\n",
           chCpuMoves, (unsigned)chCpuMaxNodes, chCpuMoves ? (double)chCpuDepths / chCpuMoves : 0.0, chBotDone, (unsigned)chCpuMaxMs);
    check("chess against the CPU: hard, player black", seen("vs CPU hard") && seen("YOUR SIDE") && seen("thinking...") && seen("your go"));
    check("the CPU answered many times, within time and node budget", chCpuMoves >= 15 && chCpuOver == 0);
    check("the CPU searched deeper than one ply", chCpuMoves && chCpuDepths >= 3u * (uint32_t)chCpuMoves);
  }
  if (scenario == "chesssave") {
    const std::string &sv = simNvsBlob["chsave"];
    bool same = ch && sv.size() == CH_SAVED && !memcmp(sv.data() + offsetof(ChGame, pos), &ch->pos, sizeof(ChPos));
    printf("      saved %u bytes, plies %u\n", (unsigned)sv.size(), ch ? ch->plies : 0);
    check("left after three moves: offered to continue", chBotDone == 3 && seen("continue saved game") && seen("PAUSE"));
    check("the game goes on where it was left", same && ch->plies == 6 && ch->human == 1 && ch->level == 0 && ch->lastTo >= 0);
  }
  bool mpOver = seen("YOU WIN") || seen("YOU LOST") || seen("DRAW");
  if (scenario == "tvui") {
    bool acts = true;
    for (const char *a : { "act a=seek&d=10", "act a=pause", "act a=play", "act a=mode&m=1" }) {
      bool f = false;
      for (auto &l : simTvLog) f = f || l == a;
      if (!f) { printf("      missing: %s\n", a); acts = false; }
    }
    auto asked = [](const char *p) { for (auto &l : simTvLog) if (l.find(p) != std::string::npos) return true; return false; };
    printf("      %zu requests, %ld screens sent while playing, %ld golden, shown %u, late %u (reported %u times)\n",
           simTvLog.size(), tvSends, tvGoldSeen, simTvShown, simTvLate, simTvReports);
    char setLine[40];
    tvSettingsLine(setLine, sizeof(setLine));
    printf("      settings: '%s'\n", setLine);
    check("address typed on the console, connection tested", !strcmp(tvUrl, "https://tv.de") && asked("/api/ping"));
    check("pairing with the server PIN on the console, a wrong one first", seen("SERVER PIN") && seen("wrong PIN") &&
          simTvWrongPins == 1 && !strcmp(tvTok, "TOKEN-1234567890-abcdefghijklmnop") &&
          simNvsBlob.count("tok") && simNvsBlob["tok"].compare(0, 5, "TOKEN") == 0 && !strcmp(setLine, "tv server    tv.de paired"));
    check("a search typed on the console, remembered", asked("/api/search?q=cat") && !strcmp(tvRecent[0], "cat") &&
          simNvsBlob.count("srch") && simNvsBlob["srch"].compare(0, 3, "cat") == 0);
    check("start page: categories and a list with thumbnails", seen("< Beliebt >") && asked("/api/feed?c=0") && asked("/api/thumb/pop00000003"));
    check("the second video played", asked("/api/tv/play?id=pop00000001"));
    check("frames shown, and they are the golden pictures", tvGoldSeen > 60 && simTvShown > 60);
    std::string paused;
    for (const std::string &x : seenTexts) if (x.compare(0, 6, "PAUSED") == 0) paused = x;
    printf("      seen: '%s'\n", paused.c_str());
    check("keys sent: +10 s, pause, play, sharp", acts && paused.compare(0, 11, "PAUSED  0:1") == 0 && paused.find("/ 3:32") != std::string::npos);
    check("sharp pictures after UP", simTvMode == 1);
    check("after the lost connection it went on", simTvBroken == 1 && tvP.shownAll > 100);
    check("fps shown over the video", tvP.have);
    check("back: the video stopped, the list, then the menu", asked("/api/tv/ctl?a=stop") && tvUiPhase == 5 && pollHook == NULL);
  }
  if (scenario == "online") {
    printf("      name %s, games ANNA saw %d, challenges from ANNA %d\n", simName, simBotGames, mpInvited);
    check("lobby shown, ANNA found", seen("ONLINE") && seen("ANNA") && seen("free"));
    check("renamed to QLAYER", !strcmp(simName, "QLAYER") && seen("YOUR NAME"));
    check("ANNA is a friend now", linkIsFriend(simPeer.code()));
    check("no Chess with a console that has none", seen("ANNA has no Chess"));
    check("ANNA said no", seen("ANNA said no"));
    check("4 wins played to its end", seen("4 WINS") && seen("vs ANNA") && mpOver);
    check("ANNA's challenge accepted: Tic Tac Toe", seen("CHALLENGE!") && seen("TIC TAC TOE") && simBotGames == 2);
    check("the next one declined", mpInvited == 2 && mpStep == mpSteps.size());
    check("back in the menu, radio off", !simOpen && pollHook == NULL && barRight == NULL);
    check("no connection error", !seen("connection error"));
  }
  if (scenario.compare(0, 4, "inet") == 0) {
    printf("      steps %zu/%zu, games %d, ANNA saw %d (last: %s), relayed %u packets, polls %u / %u (ANNA), held %u ms in all\n",
           inStep, inSteps.size(), inGames, simBotGames, simBotResult == 1 ? "ANNA won" : simBotResult == 2 ? "ANNA lost" :
           simBotResult == 3 ? "draw" : "-", simNetMsgs, simNetPolls[0], simNetPolls[1], simNetHeld);
    for (const std::string &l : simNetLog) printf("        %s\n", l.c_str());
    check("internet chosen, the lobby shown", seen("INTERNET") && seen("Internet (server)") && inChose);
    check("all steps done, back in the menu, the page closed", inStep == inSteps.size() && !inS && pollHook == NULL && barRight == NULL);
    check("no connection error in a game", !seen("connection error"));
    bool left = true;
    for (auto &kv : fsSes) if (!kv.second.left[1]) left = false;                     // our side left every game
    check("every game left on the server", left && !fsOf.count("MY3CDE"));
  }
  if (scenario == "inetc4") {
    check("lobby: ANNA free, CARL busy with a game he would like", seen("ANNA") && seen("free") && seen("busy") && seen("Chess"));
    check("4 wins over the relay to its end, ANNA's challenge to Tic Tac Toe played too", seen("4 WINS") && seen("TIC TAC TOE") &&
          seen("vs ANNA") && mpOver && simBotGames == 2 && inGames == 2);
    check("ANNA's challenge shown", seen("CHALLENGE!") && seen("wants to play"));
    check("the last result the same on both", (seen("YOU WIN") && simBotResult == 2) || (seen("YOU LOST") && simBotResult == 1) ||
                                              (seen("DRAW") && simBotResult == 3));
    check("a no, no answer, a busy one", seen("ANNA said no") && seen("no answer from ANNA") && seen("CARL is busy"));
    check("the moves went through the relay, numbered", simNetMsgs > 20 && simNetSessions == 2);
  }
  if (scenario == "inetpong") {
    double tps = inGameMs ? inTicks * 1000.0 / inGameMs : 0;
    printf("      input delay %u ticks (%u ms), round trip %u ms, %.1f ticks/s, %u of %u frames waiting\n", simNetLead,
           simNetLead * RT_TICK_MS, inS ? 0 : annaNet.rtt, tps, inStalled, inFrames);
    check("pong over the relay to its end, the same result on both", seen("PONG") && mpOver && simBotGames == 1 &&
          ((seen("YOU WIN") && simBotResult == 2) || (seen("YOU LOST") && simBotResult == 1)));
    check("a longer input delay for the internet, from the round trips", simNetLead >= 10 && simNetLead <= 18);
    check("smooth: nearly full speed, hardly ever waiting", tps > 44 && inStalled * 20 < inFrames);
  }
  if (scenario == "inetfriend") {
    FsCon &me = fsCon["MY3CDE"], &bob = fsCon["BQB234"], &anna = fsCon["AN4K7Q"];
    auto has = [](const FsCon &c, const char *x) { return std::find(c.fr.begin(), c.fr.end(), x) != c.fr.end(); };
    check("not paired: the PIN, the token saved (TV and internet)", seen("not paired with the server") &&
          !strcmp(tvTok, "TOKEN-1234567890-abcdefghijklmnop"));
    check("the own code shown", seen("MY CODE") && seen("MY3CDE"));
    check("BOB added by his code, both ways", seen("FRIEND'S CODE") && seen("BOB is your friend now") && has(me, "BQB234") && has(bob, "MY3CDE"));
    check("BOB is offline: no challenge", seen("BOB is offline") && seen("offline"));
    check("a code nobody has, the own code", seen("no console has this code") && seen("that is your own code"));
    check("ANNA a friend with RIGHT", seen("ANNA is your friend now") && has(me, "AN4K7Q") && has(anna, "MY3CDE"));
  }
  if (scenario == "inetall") {
    std::string ours, hers;
    for (int r : inResults) ours += (char)('0' + r);
    for (int r : annaResults) hers += (char)('0' + r);
    printf("      results (1 ANNA won, 2 ANNA lost, 3 draw) - ours: %s  ANNA's: %s, chess %d plies, boards the same %d\n",
           ours.c_str(), hers.c_str(), cpPlies, mpChSame);
    check("all ten games over the relay; nine to their end, the same result on both", inGames == 10 && inResults.size() == 10 &&
          ours.substr(0, 9) == hers && hers.size() == 9 && hers.find('0') == std::string::npos);
    check("chess over the relay: 40 plies, both boards the same, then left", cpPlies >= 40 && mpChSame == 1 && seen("you left the game"));
  }
  if (scenario == "inetlost") {
    check("the server went away in a game: connection lost, back to the lobby", seen("connection lost") && seen("back to the lobby"));
    check("the lobby said so and recovered", inDownSeen && seen("server unreachable") && inBack);
    check("ANNA switched off in a game: gone", seen("ANNA is gone") && inGames == 2);
  }
  if (scenario.compare(0, 2, "mp") == 0 && scenario != "mpchess") {
    printf("      games ANNA saw %d (last: %s), steps %zu/%zu\n", simBotGames, simBotResult == 1 ? "ANNA won" :
           simBotResult == 2 ? "ANNA lost" : simBotResult == 3 ? "draw" : "-", mpStep, mpSteps.size());
    if (scenario == "mpchessgame") {
      printf("      chess: %d plies, boards the same %d, wrong moves %d\n", cpPlies, mpChSame, cpBad);
      check("chess online: 40 plies, both boards the same", seen("CHESS") && cpPlies >= 40 && mpChSame == 1 && !cpBad);
      check("left the game, ANNA was told", seen("you left the game") && simPeer.state == LK_LOBBY);
    } else {
      check("the game was played to its end", mpOver && simBotGames == 1);
      check("both saw the same result", (seen("YOU WIN") && simBotResult == 2) || (seen("YOU LOST") && simBotResult == 1) ||
                                        (seen("DRAW") && simBotResult == 3));
    }
    check("no connection error", !seen("connection error"));
    check("back in the menu, radio off", !simOpen && pollHook == NULL && mpStep == mpSteps.size());
  }
  if (scenario == "palace" || scenario == "palacecont") {
    printf("      level %u reached, score %u, best stored %u, level kept %u, %d deaths, %d pictures\n", palLv + 1, palScore,
           simNvsU16["hPAL"], simNvsU16["PALlv"], pbDeaths, pbShots);
    bool all = true;
    for (uint8_t lv = palaceStart(); lv < PAL_LEVELS; lv++) {
      char b[32];
      snprintf(b, sizeof(b), "LEVEL %u COMPLETE", lv + 1);
      all = all && seen(b) && seen(PAL_LEVEL[lv].name);
    }
    check("palace: the player escaped through all the levels, the Warden beaten", all && seen("FREE AT LAST!") && seen("THE WARDEN FALLS!"));
  }
  if (scenario == "palace") {
    check("palace: title, then level 1 played through", seen("PALACE") && seen("THE CELLARS") && seen("LEVEL 1 COMPLETE") && !seen("new game"));
    check("palace: game over with the score kept, the level reached kept", overShown && simNvsU16["hPAL"] > 800 && simNvsU16["PALlv"] == 7);
  }
  if (scenario == "palacecont") {
    check("palace: the level reached is offered, or an earlier one, or a new game",
          seen("continue: level 6") && seen("level 5") && seen("level 3") && seen("new game") && !seen("level 7"));
    check("palace: going on at level 6 with 7:30 for three levels", seen("LEVEL 6") && seen("7:30") && !seen("LEVEL 5 COMPLETE"));
    check("palace: the score counts the levels played", overShown && palScore > 300 && palScore < 300 + 450);
  }
  if (scenario == "wizard")    check("key wizard shown", frames > 20);
  if (scenario == "sprache") {
    auto after = [](const char *x, uint32_t from) { auto it = textAt.find(x); return it != textAt.end() && it->second > from; };
    for (const std::string &o : deOver) printf("      off the screen: %s\n", o.c_str());
    check("language: English first, Deutsch at once, saved", seen("language     English") && after("Sprache      Deutsch", sprSwitch) &&
          after("EINSTELLUNGEN", sprSwitch) && sprLangSaved == 1);
    check("German settings and setup screens", seen("Helligkeit   (LED an 3V3)") && seen("Kippstärke   normal") && seen("zurück zu den Spielen") &&
          seen("WLAN EINRICHTEN") && seen("HANDY-HOTSPOT") && seen("9.3 installieren") && seen("SICHERUNG") && seen("KEINE KARTE") &&
          seen("AKKU") && seen("KEIN SUMMER") && seen("KIPPEN IST AUS") && seen("KEIN SENSOR") && seen("BILD 40 MHz") &&
          seen("SD-KARTE BEREIT") && seen("Kippen       an (kein Sensor)"));
    check("German Pumpkin Maze list and TV screens", seen("Bestzeit 23.0 s") && seen("+ Level-Editor am Handy") && seen("kein WLAN") &&
          seen("keine Antwort") && seen("< Einrichten >") && seen("PUMPKIN MAZE"));
    check("German library and pause menu, the game names stay", seen("Einstellungen") && seen("REKORD") && seen("zum Menü") &&
          seen("Snake") && seen("SNAKE"));
    check("German: nothing ran off the screen", deOver.empty() && deTexts.size() > 100);
    check("back to English, saved", after("language     English", sprBack) && after("SETTINGS", sprBack) && simNvsU16["lang"] == 0 && cfgLang == 0);
    langTable();
  }
  if (scenario == "tilt") {
    printf("   tilt: on %d ready %d axes %02x %02x pins %d/%d\n", cfgTilt, gyroReady, tiltAx[0], tiltAx[1], gyroSda, gyroScl);
    check("sensor found on GPIO20 / GPIO7", gyroReady && gyroSda == 20 && gyroScl == 7);
    check("right = x, away = -y", tiltAx[0] == 0x00 && tiltAx[1] == 0x81);
    printf("   full tilt: right %d left %d up %d down %d\n", tiltFull[0], tiltFull[1], tiltFull[2], tiltFull[3]);
    check("four ways learned, wrong ways asked again", seen("LEFT did not match RIGHT") && seen("UP matched RIGHT/LEFT") &&
          seen("DOWN did not match UP") && seen("tilt DOWN (to you)") &&
          tiltFull[0] == 8000 && tiltFull[1] == 6500 && tiltFull[2] == 8000 && tiltFull[3] == 7000 && simNvsBlob.count("gful"));
    check("tilt control switched on", cfgTilt);
    check("level saved in the settings", seen("LEVEL SAVED") && cfgTiltFix);
    check("pause menu offers a new level", seen("tilt: level here"));
    check("tilting up presses UP in the game", seen("SNAKE") && tiltKey[B_UP] && !tiltKey[B_DOWN]);
    printf("   stepless: up %d, left-right %d\n", keyAxis(B_DOWN, B_UP), keyAxis(B_LEFT, B_RIGHT));
    check("stepless tilt: nearly full up, nothing sideways", keyAxis(B_DOWN, B_UP) > 200 && keyAxis(B_LEFT, B_RIGHT) == 0);
    // each side as far as it went when learned answers the same; LEFT (learned shorter) answers more for the same tilt
    tiltGame = true;
    auto an = [](int16_t x, uint8_t k) { simAcc[0] = x; simAcc[1] = 0; clockUs += 30000; tiltUpdate(); return tiltAn[k]; };
    int r8 = an(8000, B_RIGHT), l65 = an(-6500, B_LEFT), r4 = an(4000, B_RIGHT), l4 = an(-4000, B_LEFT);
    printf("   stepless: right 8000 -> %d, left 6500 -> %d, right 4000 -> %d, left 4000 -> %d\n", r8, l65, r4, l4);
    check("each side scaled by its own full tilt", abs(r8 - l65) <= 2 && l4 > r4 + 20);
    simNvsBlob.erase("gful");                                    // a setup from before the four ways: both sides the same
    tiltLoad();
    int r4o = an(4000, B_RIGHT), l4o = an(-4000, B_LEFT);
    check("an older setup (two axes, no full tilts) still works, the same on both sides", tiltAx[0] == 0x00 && tiltAx[1] == 0x81 && r4o == l4o && r4o > 50);
  }
  if (scenario == "sdcard") {
    printf("   sdcs %u, card %d\n", simNvsU16["sdcs"], simSdIn);
    check("card found on GPIO20 at the first start, kept", simNvsU16["sdcs"] == 21 && simSdIn);
    check("settings show the card", seen("sd card      7.4 GB, 92% free"));
  }
  if (scenario == "fps") {
    int fps = 0;
    for (const std::string &t : seenTexts) {
      int v;
      if (sscanf(t.c_str(), "%d fps", &v) == 1 && t.find(" fps") != std::string::npos && seen("SNAKE")) fps = v > fps ? v : fps;
    }
    printf("   shown: %d fps\n", fps);
    check("fps setting saved", simNvsU16.count("fps") && simNvsU16["fps"] == 1);
    check("frame rate shown in a game (30..62)", fps >= 30 && fps <= 62);
  }
  if (scenario == "ladder5") {
    printf("   OK key %u/%u, buzzer %u\n", BTN_PIN[4], BTN_MV[4], simNvsU16["snd"]);
    check("own OK key learned on the ladder", BTN_PIN[4] == 2 && BTN_MV[4] > 780);
    check("sound setup shows the own OK key", seen("OK = yes"));
    check("OK while beeping picked the buzzer", simNvsU16.count("snd") && simNvsU16["snd"] != 255 && simNvsU16["sset"] == 1);
  }
  if (scenario == "uploadask") {
    check("upload asked outside the WLAN page", seen("install the firmware"));
    check("the answer reached the web page (no, then yes)", simAnswer == 1);
    check("installing shown until the restart", seen("done - restarting"));
  }
  if (scenario == "ladderbat") {
    printf("   battery on %u, keys on %u %u %u %u %u\n", batPin, BTN_PIN[0], BTN_PIN[1], BTN_PIN[2], BTN_PIN[3], BTN_PIN[4]);
    bool onLadder = true;
    for (uint8_t b = 0; b < B_COUNT; b++) if (BTN_PIN[b] != 2) onLadder = false;
    check("battery divider found, not taken for a key", batPin == BAT && simNvsU16["bat"] == BAT);
    check("all five keys learned on the ladder pin", onLadder);
    check("no key held down afterwards", !kDown[0] && !kDown[1] && !kDown[2] && !kDown[3] && !kDown[4]);
    check("no pull left on the battery pin after the key setup", pinMode_[BAT] != 2 && pinMode_[BAT] != 3);
  }
  if (scenario == "ladderpair") {
    printf("   supply %u mV, keys %u %u %u %u %u mV\n", ladVs, BTN_MV[0], BTN_MV[1], BTN_MV[2], BTN_MV[3], BTN_MV[4]);
    check("five keys learned on one pin", BTN_MV[0] && BTN_MV[1] && BTN_MV[2] && BTN_MV[3] && BTN_MV[4] && BTN_PIN[4] == 2);
    check("supply measured from two keys together (3.24 V)", abs((int)ladVs - 3240) < 60 && simNvsU16["lvs"] == ladVs);
    bool all = true;
    for (uint8_t idx = 1; idx < 10; idx++) {                     // every set read back as its keys
      ladder = idx;
      uint8_t m = 0;
      for (int k = 0; k < 6 && m != LADDER_R_SET[idx]; k++) { clockUs += 2000; m = ladderMask(2); }
      if (m != LADDER_R_SET[idx]) { printf("   set %u read as %u\n", LADDER_R_SET[idx], m); all = false; }
    }
    check("one key and two keys together read right", all);
    // the supply sags by 2 % while UP is held, then RIGHT is pressed: both still read right
    bool held = true;
    ladder = 1;
    for (int k = 0; k < 400 && held; k++) {
      ladderDrift = -k / 6;                                      // down to -66 mV, slowly
      clockUs += 2000;
      if (!(ladderMask(2) & 1) && k > 4) held = false;
    }
    ladder = 4;
    uint8_t m = 0;
    for (int k = 0; k < 6; k++) { clockUs += 2000; m = ladderMask(2); }
    printf("   supply -66 mV: UP held %d, RIGHT read as %u\n", held, m);
    check("keys stay readable when the supply sags", held && m == 8);
    ladder = 0;
    for (int k = 0; k < 6; k++) { clockUs += 2000; m = ladderMask(2); }
    check("letting go still reads as nothing", m == 0);
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
  if (scenario == "pumpkin" || scenario == "pumpkinkeys" || scenario == "pmeditor") {
    printf("      levels won %d, waits for moving pits %d, panel: %.0f blocks of 32 px a frame while rolling (of 2400)\n",
           pmbLevelsWon, pmbWaits, pmbFrames ? (double)pmbBlocks / pmbFrames : 0.0);
    check("pumpkin: few parts of the picture change while rolling (under 10 %)", pmbFrames > 100 && pmbBlocks < pmbFrames * 240);
  }
  if (scenario == "pumpkin") {
    printf("      stars %u %u %u, open %u, best %u\n", pmSv.stars[0], pmSv.stars[1], pmSv.stars[2], pmSv.open, simNvsU16["hPMZ"]);
    check("pumpkin: tilt rolled through levels 1-3, the title cards and stars shown", pmbLevelsWon >= 3 && seen("FIRST ROLL") && seen("CANDY LANE") && seen("LEVEL CLEAR!") && seen("tilt the console to roll"));
    check("pumpkin: progress kept in flash (stars, times, the next level open, best = stars)",
          simNvsBlob.count("PMZsv") && pmSv.open >= 3 && pmSv.stars[0] >= 2 && pmSv.best[0] > 0 && simNvsU16["hPMZ"] == pmStarSum());
  }
  if (scenario == "pumpkinkeys") {
    printf("      pumpkins lost on the way: %d\n", pmbLostMax);
    check("pumpkin: keys roll it, a pit swallows it (a pumpkin lost), then the goal", pmbLevelsWon >= 1 && seen("PIT PARADE") && seen("hold the arrow keys to roll") && pmbLostMax >= 1);
  }
  if (scenario == "pmeditor") {
    printf("      hotspot when the level came: %d, after leaving: %d; home WLAN after leaving: %d\n", pmeNetIn, pmeNetOut, pmeHomeOut);
    check("pumpkin editor: the own hotspot opens with name, password and link, a QR code", pmeNetIn == NET_PLAY && seen("MiniArcade-AB12") && seen("12345678") &&
          seen("192.168.4.1/maze") && seen("QR: join the WLAN"));
    check("pumpkin editor: the level from the phone shows at once, the hotspot closes on leaving", seen("saved: GRAVE RUN") && pmeNetOut == NET_OFF);
    check("pumpkin editor: the own level is listed and played", seen("GRAVE RUN") && pmbLevelsWon >= 1 && pmSv.stars[PM_LEVELS] >= 1 && seen("OWN LEVEL 1"));
    check("pumpkin editor: in the home WLAN the link has its address and the WLAN stays on", seen("192.168.1.50/maze") && pmeHomeOut == NET_ONLINE);
  }
  check("sounds played", scenario == "wizard" || scenario == "pumpkin" || scenario == "chess" || scenario == "chesssave" || scenario == "ladder5" || scenario == "plain2" || scenario == "tilt" || scenario == "menu" || scenario == "settings" || toneCount > 0);
  printf("   %ld frames, %.1f s simulated\n", frames, clockUs / 1e6);
  return failures ? 1 : 0;
}
