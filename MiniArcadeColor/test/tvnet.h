// Host stand-in for main/tvnet.h: a pretend MiniArcade server. Short requests
// get fixed answers (a little later, in simulated time); the stream plays the
// golden frames of the server's tests (MiniArcadeServer/tests/golden.bin) as
// a video in time with the simulated clock: the fluent frames over and over,
// or the sharp ones, with an info record every half second like the server.
#pragma once
#include "../main/tvnet.h"
#include "gfx.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <deque>

static std::vector<std::string> simTvLog;               // every path asked for
static std::vector<std::string> simTvFluent, simTvSharp; // frame payloads of golden.bin
static bool     simTvOn = false, simTvStream = false;
static char     simTvToken[64] = "";
static int      simTvWrongPins = 0;
static uint8_t  simTvState = 0, simTvMode = 0;          // the server's player
static uint16_t simTvGen = 1;
static uint32_t simTvPos = 0;                           // ms of video where this gen started
static TvReqState simTvReq = TVR_IDLE;
static int      simTvStatus = 0;
static uint32_t simTvReqAt = 0;
static std::string simTvResp, simTvAct;
static std::deque<uint8_t> simTvRing;
static uint32_t simTvT0 = 0, simTvK = 0, simTvNextSeg = 0, simTvBytes = 0;
static uint16_t simTvSeq = 0, simTvBroken = 0;
static uint32_t simTvBreakAt = 0;                       // the connection drops once at this time (ms)
static uint32_t simTvShown = 0, simTvLate = 0, simTvReports = 0;

static uint32_t simTvMs() { return (uint32_t)(clockUs / 1000); }

static bool simTvLoadGolden() {
  if (!simTvFluent.empty()) return true;
  FILE *f = fopen("../../MiniArcadeServer/tests/golden.bin", "rb");
  if (!f) return false;
  std::string d;
  char b[4096];
  for (size_t n; (n = fread(b, 1, sizeof(b), f)) > 0; ) d.append(b, n);
  fclose(f);
  for (size_t i = 0; i + 4 <= d.size(); ) {
    uint32_t n = (uint8_t)d[i] | (uint8_t)d[i + 1] << 8 | (uint8_t)d[i + 2] << 16 | (uint32_t)(uint8_t)d[i + 3] << 24;
    std::string p = d.substr(i + 4, n);
    if (p[0] == 'F') (p[1] & 2 ? simTvSharp : simTvFluent).push_back(p);
    i += 4 + n;
  }
  return !simTvFluent.empty();
}

static void simTvPut(const std::string &p) {
  uint32_t n = (uint32_t)p.size();
  for (int k = 0; k < 4; k++) simTvRing.push_back((uint8_t)(n >> (8 * k)));
  simTvRing.insert(simTvRing.end(), p.begin(), p.end());
  simTvBytes += n + 4;
}

static void simTvInfo() {
  std::string p(14, '\0');
  uint32_t dur = 212000;
  p[0] = 'I'; p[1] = (char)simTvState; p[2] = (char)simTvMode; p[3] = (char)simTvGen; p[4] = (char)(simTvGen >> 8);
  for (int k = 0; k < 4; k++) { p[5 + k] = (char)(simTvPos >> (8 * k)); p[9 + k] = (char)(dur >> (8 * k)); }
  p[13] = 25;
  p += "Golden test video";
  p += '\0';
  simTvPut(p);
}

static void simTvNewGen() { simTvGen++; simTvK = 0; simTvT0 = simTvMs(); }

// what the server does with a key from the console (at the start of a segment)
static void simTvApply(const std::string &a) {
  simTvLog.push_back("act " + a);
  uint32_t at = simTvPos + simTvK * 40;
  if (a == "a=pause") { simTvState = 2; simTvPos = at; }
  else if (a == "a=play") simTvState = 1;
  else if (a == "a=seek&d=10") simTvPos = at + 10000;
  else if (a == "a=seek&d=-10") simTvPos = at > 10000 ? at - 10000 : 0;
  else if (a == "a=mode&m=1") { simTvMode = 1; simTvPos = at; }
  else if (a == "a=mode&m=0") { simTvMode = 0; simTvPos = at; }
  simTvNewGen();
}

// the stream as time goes by: an info record every 500 ms, frames 500 ms ahead
static void simTvPump() {
  if (!simTvStream) return;
  uint32_t now = simTvMs();
  if (simTvBreakAt && now >= simTvBreakAt) {              // the connection drops: half a frame was in flight
    simTvBreakAt = 0;
    simTvBroken++;
    for (int k = 0; k < 300 && !simTvRing.empty(); k++) simTvRing.pop_back();
    simTvNextSeg = now + 300;
    return;
  }
  if (simTvRing.size() > TV_RING) return;                 // the decoder is behind: TCP waits
  if (now >= simTvNextSeg) {
    simTvNextSeg = now + 500;
    if (!simTvAct.empty()) { simTvApply(simTvAct); simTvAct.clear(); }
    simTvInfo();
  }
  if (simTvState != 1) return;
  const std::vector<std::string> &fr = simTvMode ? simTvSharp : simTvFluent;
  while (simTvT0 + simTvK * 40 <= now + 500 && simTvRing.size() < TV_RING) {
    std::string p = fr[simTvK % fr.size()];
    uint32_t ts = simTvPos + simTvK * 40;
    if (simTvK % fr.size() == 0) p[1] |= 1;                 // the first of the row is a key frame (it is)
    p[2] = (char)simTvSeq; p[3] = (char)(simTvSeq >> 8);
    for (int k = 0; k < 4; k++) p[4 + k] = (char)(ts >> (8 * k));
    simTvPut(p);
    simTvSeq++;
    simTvK++;
  }
}

bool tvNetStart(const char *, const char *tok) { simTvOn = simTvLoadGolden(); snprintf(simTvToken, sizeof(simTvToken), "%s", tok); simTvReq = TVR_IDLE; return simTvOn; }
void tvNetStop() { simTvOn = simTvStream = false; simTvRing.clear(); }
void tvNetToken(const char *tok) { snprintf(simTvToken, sizeof(simTvToken), "%s", tok); }

static std::string simTvItems(const char *tag) {
  std::string s = "{\"items\":[";
  for (int i = 0; i < 7; i++) {
    char b[200];
    snprintf(b, sizeof(b), "%s{\"id\":\"%s%08d\",\"t\":\"%s video number %d with a long title\",\"c\":\"Channel %d\",\"d\":%d,\"f\":%d}",
             i ? "," : "", tag, i, tag, i + 1, i, 60 + i * 97, i == 1);
    s += b;
  }
  return s + "]}";
}

bool tvReq(const char *path, bool post) {
  if (!simTvOn || simTvReq == TVR_BUSY) return false;
  std::string p = path;
  simTvLog.push_back(std::string(post ? "POST " : "GET ") + p);
  simTvReq = TVR_BUSY;
  simTvReqAt = simTvMs() + 40;
  simTvStatus = 200;
  bool paired = !strcmp(simTvToken, "TOKEN-1234567890-abcdefghijklmnop");
  if (p == "/api/ping") simTvResp = "{\"ok\":1,\"name\":\"MiniArcade\"}";
  else if (p.compare(0, 9, "/api/pair") == 0) {                 // the server's PIN is 120000
    if (p.find("&pin=120000&") != std::string::npos) simTvResp = "{\"token\":\"TOKEN-1234567890-abcdefghijklmnop\",\"id\":\"c1\"}";
    else { simTvWrongPins++; simTvStatus = 403; simTvResp = "wrong PIN"; }
  } else if (!paired) { simTvStatus = 401; simTvResp = "not paired"; }
  else if (p == "/api/cats") simTvResp = "{\"cats\":[\"Beliebt\",\"Musik\",\"Gaming\"]}";
  else if (p.compare(0, 9, "/api/feed") == 0) simTvResp = simTvItems(p == "/api/feed?c=0" ? "pop" : "cat");
  else if (p.compare(0, 11, "/api/search") == 0) simTvResp = simTvItems("sea");
  else if (p.compare(0, 9, "/api/list") == 0) simTvResp = simTvItems("lst");
  else if (p.compare(0, 8, "/api/fav") == 0) simTvResp = "{\"ok\":1}";
  else if (p.compare(0, 11, "/api/thumb/") == 0) {
    simTvResp.assign(80 * 45, 0);
    for (int y = 0; y < 45; y++) for (int x = 0; x < 80; x++) simTvResp[y * 80 + x] = (char)rgb(x * 3, y * 5, 160 + (p[16] & 3) * 30);
  } else if (p.compare(0, 12, "/api/tv/play") == 0) { simTvState = 1; simTvMode = 0; simTvPos = 0; simTvNewGen(); simTvResp = "{\"ok\":1}"; }
  else if (p == "/api/tv/now") {
    char b[160];
    snprintf(b, sizeof(b), "{\"g\":%u,\"s\":%u,\"id\":\"pop0000001\",\"t\":\"Golden test video\",\"pos\":0}", simTvGen, simTvState);
    simTvResp = b;
  } else if (p.compare(0, 11, "/api/tv/ctl") == 0) { simTvState = 0; simTvNewGen(); simTvResp = "{\"s\":0}"; }
  else { simTvStatus = 404; simTvResp = "?"; }
  return true;
}
TvReqState tvReqState() {
  if (simTvReq == TVR_BUSY && simTvMs() >= simTvReqAt) simTvReq = simTvStatus == 200 ? TVR_DONE : TVR_ERROR;
  return simTvReq;
}
int tvReqStatus() { return simTvStatus; }
const uint8_t *tvReqBody(size_t *len) { if (len) *len = simTvResp.size(); return (const uint8_t *)simTvResp.c_str(); }
void tvReqClear() { if (simTvReq != TVR_BUSY) simTvReq = TVR_IDLE; }

bool tvStreamStart() { simTvStream = true; simTvRing.clear(); simTvNextSeg = 0; return true; }
void tvStreamStop() { simTvStream = false; simTvRing.clear(); }
void tvStreamAct(const char *a) { simTvAct = a; }
void tvStreamCount(uint16_t shown, uint16_t late, uint16_t) { simTvShown += shown; simTvLate += late; simTvReports++; }
size_t tvStreamAvail() { simTvPump(); return simTvRing.size(); }
size_t tvStreamPeek(uint8_t *dst, size_t n) {
  simTvPump();
  if (n > simTvRing.size()) n = simTvRing.size();
  for (size_t i = 0; i < n; i++) dst[i] = simTvRing[i];
  return n;
}
size_t tvStreamRead(uint8_t *dst, size_t n) {
  n = tvStreamPeek(dst, n);
  simTvRing.erase(simTvRing.begin(), simTvRing.begin() + n);
  return n;
}
int tvStreamStatus() { return 200; }
uint16_t tvStreamBroken() { return simTvBroken; }
void tvStreamReset() { simTvRing.clear(); }
uint32_t tvStreamBytes() { return simTvBytes; }
const char *tvConsoleCode() { return "QX7K"; }
uint32_t tvFreeHeap(uint32_t *lowest) { if (lowest) *lowest = 61 * 1024; return 74 * 1024; }
