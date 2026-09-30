// Host stand-in for main/link.h: the real protocol (linkcore.h) with a
// second console "ANNA" next to us that is played by a little bot. The
// scenarios set what the bot does.
#pragma once
#include "../main/link.h"
#include "../main/rtgames.h"
#include "../main/boardgames.h"
#include <vector>
#include <stdio.h>

extern uint64_t simMicros();

// ---- what the bot does (set by the scenario) ----
static int      simBotAnswer   = 1;      // 1 = accept, 0 = decline, -1 = never answer
static uint32_t simBotInviteAt = 0;      // ms: bot challenges us then (0 = never)
static uint8_t  simBotGame     = LKG_TTT;
static int      simBotLeaveAfter = -1;   // bot gives up after this many of its moves
static int      simBotGames    = 0;      // games the bot saw to the end
static int      simBotResult   = 0;      // last game: 1 bot won, 2 bot lost, 3 draw

struct SimPkt { bool toBot; std::vector<uint8_t> d; };
static std::vector<SimPkt> simAir;
static LinkCore simMe, simBot;
static bool     simOpen = false;
static char     simName[LK_NAME + 1] = "PLAYER";
static char     simFriends[10][5];
static uint8_t  simNFriends = 0;
static const uint8_t SIM_MY_MAC[6]  = { 0x24, 0x6F, 0x28, 0x01, 0x02, 0x03 };
static const uint8_t SIM_BOT_MAC[6] = { 0x24, 0x6F, 0x28, 0x99, 0x88, 0x77 };

static void simSendMe(void *, const uint8_t *, const uint8_t *d, uint8_t n)  { simAir.push_back({ true,  std::vector<uint8_t>(d, d + n) }); }
static bool     simBotOld      = false;  // bot plays an older firmware: no real-time games
static void simSendBot(void *, const uint8_t *, const uint8_t *d, uint8_t n) {
  std::vector<uint8_t> v(d, d + n);
  if (simBotOld && n >= sizeof(LkPacket) && v[3] == LK_BEACON) v[offsetof(LkPacket, flags)] &= ~LK_CAPS;
  simAir.push_back({ false, v });
}

uint32_t  linkNow()   { return (uint32_t)(simMicros() / 1000); }
LinkCore &linkCore()  { return simMe; }

bool linkOpen() {
  simMe.begin(SIM_MY_MAC, simName, simSendMe, NULL, linkNow(), 99);
  simBot.begin(SIM_BOT_MAC, "ANNA", simSendBot, NULL, linkNow(), 1234);
  simAir.clear();
  simOpen = true;
  return true;
}
void linkClose() { simMe.cancel(); simOpen = false; }

const char *linkName() { return simName; }
void linkSetName(const char *n) { memset(simName, 0, sizeof(simName)); strncpy(simName, n, LK_NAME); simMe.setName(simName); }
bool linkIsFriend(const char *c) { for (uint8_t i = 0; i < simNFriends; i++) if (!strcmp(simFriends[i], c)) return true; return false; }
void linkToggleFriend(const char *c) {
  for (uint8_t i = 0; i < simNFriends; i++)
    if (!strcmp(simFriends[i], c)) { memcpy(simFriends[i], simFriends[--simNFriends], 5); return; }
  if (simNFriends < 10) strcpy(simFriends[simNFriends++], c);
}
uint8_t linkList(uint8_t *idx, uint8_t max) {
  uint8_t n = 0;
  for (uint8_t i = 0; i < simMe.peerCount() && n < max; i++) idx[n++] = i;
  return n;
}

// ---- the bot's own copy of the board ----
static uint8_t  botC4[6][7], botTT[9];
static bool     botTurn = false, botInGame = false;
static uint32_t botNext = 0;
static int      botMoves = 0;

static bool botC4Win(uint8_t w) {
  for (int r = 0; r < 6; r++) for (int c = 0; c < 7; c++) {
    if (botC4[r][c] != w) continue;
    static const int D[4][2] = { { 0, 1 }, { 1, 0 }, { 1, 1 }, { 1, -1 } };
    for (auto &d : D) {
      int k = 1;
      while (k < 4) { int rr = r + d[0] * k, cc = c + d[1] * k;
        if (rr < 0 || rr > 5 || cc < 0 || cc > 6 || botC4[rr][cc] != w) break; k++; }
      if (k == 4) return true;
    }
  }
  return false;
}
static bool botTTWin(uint8_t w) {
  static const uint8_t L[8][3] = { {0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},{2,5,8},{0,4,8},{2,4,6} };
  for (auto &l : L) if (botTT[l[0]] == w && botTT[l[1]] == w && botTT[l[2]] == w) return true;
  return false;
}
static bool botFull() {
  if (simBot.game == LKG_C4) { for (int c = 0; c < 7; c++) if (!botC4[0][c]) return false; return true; }
  for (int i = 0; i < 9; i++) if (!botTT[i]) return false;
  return true;
}
static bool botApply(uint8_t m, uint8_t who) {        // who: 1 = bot, 2 = us
  if (simBot.game == LKG_C4) {
    if (m > 6) return false;
    for (int r = 5; r >= 0; r--) if (!botC4[r][m]) { botC4[r][m] = who; return true; }
    return false;
  }
  if (m > 8 || botTT[m]) return false;
  botTT[m] = who;
  return true;
}
static bool botDone() {
  bool w1 = simBot.game == LKG_C4 ? botC4Win(1) : botTTWin(1);
  bool w2 = simBot.game == LKG_C4 ? botC4Win(2) : botTTWin(2);
  if (w1 || w2 || botFull()) { simBotGames++; simBotResult = w1 ? 1 : (w2 ? 2 : 3); simBot.finish(); botInGame = false; return true; }
  return false;
}

// real-time games: the bot runs its own copy, like a second console would
static RtPong   botPg;
static RtSnake  botSn;
static RtPac    botPm;
static RtBomb   botRb;
static uint32_t botRtNext = 0, botRnd = 5;
static bool     botRt = false;

static void simBotRealtime(uint32_t now) {
  LinkCore &B = simBot;
  bool pong = B.game == LKG_PONG, pac = B.game == LKG_PAC, bomb = B.game == LKG_BOMB;
  uint8_t me = B.iStart ? 0 : 1;
  if (!botRt) {                                        // a new game begins
    botRt = true;
    uint32_t seed = B.session() * 2654435761u;
    if (pong) botPg.begin(seed); else if (pac) botPm.begin(seed, 2); else if (bomb) botRb.begin(seed, 2); else botSn.begin(seed);
    botRtNext = now;
  }
  for (int k = 0; k < 3 && lkDue(now, botRtNext); k++) {
    uint8_t in[2];
    if (!B.syncStep(&in[me], &in[1 - me])) break;
    if (pong) botPg.step(in); else if (pac) botPm.step(in); else if (bomb) botRb.step(in); else botSn.step(in);
    uint8_t mine;
    if (bomb) {                                        // the CPU player of the game itself
      mine = botRb.ai(me);
    } else if (pac) {                                         // wander: a new open way now and then
      const RmEnt &e = botPm.pac[me];
      mine = botPm.want[me];
      if (!mine || rtRand(botRnd) % 40 == 0 || RtPac::wallAt(e.x + RtPac::dx(mine), e.y + RtPac::dy(mine)))
        for (int t = 0; t < 8; t++) {
          uint8_t d = 1 + rtRand(botRnd) % 4;
          if (!RtPac::wallAt(e.x + RtPac::dx(d), e.y + RtPac::dy(d))) { mine = d; break; }
        }
    } else if (pong) {                                        // follow the ball, not too well
      int c = botPg.pad[me] + RP_PH / 2, b = botPg.by >> 4;
      mine = (rtRand(botRnd) % 5 < 2) ? 0 : (b < c - 2 ? 1 : (b > c + 2 ? 2 : 0));
    } else {                                           // keep going, turn before a wall
      static const int8_t DX[5] = { 0, 0, 0, -1, 1 }, DY[5] = { 0, -1, 1, 0, 0 };
      mine = botSn.dir[me];
      for (int t = 0; t < 8; t++) {
        uint8_t d = t ? (uint8_t)(1 + rtRand(botRnd) % 4) : mine;
        int nx = botSn.x[me][0] + DX[d], ny = botSn.y[me][0] + DY[d];
        if (nx >= 0 && nx < RS_W && ny >= 0 && ny < RS_H && !botSn.body(nx, ny)) { mine = d; break; }
      }
    }
    B.syncPut(mine, now);
    botRtNext += RT_TICK_MS;
  }
  uint8_t w = pong ? botPg.winner : (pac ? botPm.winner : (bomb ? (botRb.winner == RB_DRAW ? 3 : botRb.winner) : botSn.winner));
  if (w) {
    simBotGames++;
    simBotResult = w == 3 ? 3 : (w == me + 1 ? 1 : 2);
    B.finish();
    botRt = false;
  }
}

// battleship: a fixed fleet (4 3 3 2 2, not touching), random shots
static const uint8_t BOT_FLEET[64] = {
  1,1,1,1,0,0,2,0,
  0,0,0,0,0,0,2,0,
  3,0,0,0,0,0,2,0,
  3,0,0,4,4,0,0,0,
  3,0,0,0,0,0,0,0,
  0,0,0,0,0,0,5,0,
  0,0,0,0,0,0,5,0,
  0,0,0,0,0,0,0,0 };
static uint8_t botFleet[64], botSea[64];
static bool    botShipOn = false, botShipWait = false;
static uint8_t botShipRes = 0, botShipHits = 0;

static uint8_t botShipFire(uint8_t cell) {          // 1 miss, 2 hit, 3 sunk
  botFleet[cell] |= 0x80;
  uint8_t s = botFleet[cell] & 0x7F;
  if (!s) return 1;
  for (int i = 0; i < 64; i++) if ((botFleet[i] & 0x7F) == s && !(botFleet[i] & 0x80)) return 2;
  return 3;
}
static bool botShipDead() { for (int i = 0; i < 64; i++) if ((botFleet[i] & 0x7F) && !(botFleet[i] & 0x80)) return false; return true; }
static void botShipShoot(uint32_t now) {
  uint8_t c; do c = rand() % 64; while (botSea[c]);
  botSea[c] = 1;
  simBot.sendMove((botShipRes << 6) | c, now);
  botShipWait = true;
}

static void simBotShip(uint32_t now) {
  LinkCore &B = simBot;
  if (!botShipOn) {
    botShipOn = true; botShipWait = false; botShipRes = 0; botShipHits = 0;
    memcpy(botFleet, BOT_FLEET, 64); memset(botSea, 0, 64);
    botNext = now + 2500;                          // "sets up its fleet" first
    botTurn = B.iStart;
  }
  uint8_t m;
  if (B.moveIn(&m)) {
    uint8_t res = m >> 6, cell = m & 63;
    if (botShipWait) {
      botShipWait = false;
      if (res >= 2) botShipHits++;
      if (botShipHits >= 14) { simBotGames++; simBotResult = 1; B.finish(); botShipOn = false; return; }
    }
    uint8_t r = (botFleet[cell] & 0x80) ? 1 : botShipFire(cell);
    botShipRes = r;
    if (botShipDead()) { B.sendMove(r << 6, now); simBotGames++; simBotResult = 2; B.finish(); botShipOn = false; return; }
    botTurn = true; botNext = now + 400;
  }
  if (botTurn && now >= botNext && !botShipWait) { botTurn = false; botShipShoot(now); }
}

// checkers and mau-mau: the bot keeps its own copy, its moves queue up like ours
static Checkers botCk;
static MauMau   botMm;
static bool     botBgOn = false;
static uint8_t  botQ[16], botQn = 0;
static int      simBotCkDepth = 1;                  // how well the bot plays checkers
static void botQSend(uint32_t now) { if (botQn && simBot.sendMove(botQ[0], now)) memmove(botQ, botQ + 1, --botQn); }

static void simBotBoard(uint32_t now) {
  LinkCore &B = simBot;
  bool dame = B.game == LKG_DAME;
  uint8_t me = B.iStart ? 0 : 1;
  if (!botBgOn) {                                     // a new game begins
    botBgOn = true; botQn = 0; botMoves = 0;
    if (dame) botCk.begin(); else botMm.begin(B.session() * 2654435761u, 2);
    botNext = now + 600;
  }
  uint8_t m;
  if (B.moveIn(&m)) {                                 // ours arrive in order
    bool ok;
    if (dame) { ok = botCk.legal(m & 31, (m >> 5) & 3); if (ok) botCk.apply(m & 31, (m >> 5) & 3); }
    else ok = botMm.move(m);
    if (!ok) printf("      bot: move %u not allowed\n", m);
    botNext = now + 500;
  }
  uint8_t w = dame ? botCk.winner : botMm.winner;
  bool myTurn = dame ? botCk.turn == me : botMm.turn == me;
  if (!w && myTurn && now >= botNext && botQn < 8) {
    if (simBotLeaveAfter >= 0 && botMoves >= simBotLeaveAfter) { B.cancel(); botBgOn = false; return; }
    if (dame) {
      uint32_t r = botRnd | 1;
      CkStep st = botCk.best(simBotCkDepth, r); botRnd = r;
      bool more = botCk.apply(st.from, st.dir);
      botQ[botQn++] = st.from | (st.dir << 5) | (more ? 0x80 : 0);
    } else {
      uint8_t mv = botMm.cpuMove();
      botMm.move(mv);
      botQ[botQn++] = mv;
    }
    botMoves++;
    botNext = now + 300;
  }
  botQSend(now);
  w = dame ? botCk.winner : botMm.winner;
  if (w && !botQn) {
    simBotGames++;
    simBotResult = dame ? (w == 3 ? 3 : (w - 1 == me ? 1 : 2)) : (w - 1 == me ? 1 : 2);
    B.finish();
    botBgOn = false;
  }
}

static void simBotBrain(uint32_t now) {
  LinkCore &B = simBot;
  if (B.state == LK_INVITED && now >= botNext) {
    if (simBotAnswer >= 0) B.answer(simBotAnswer == 1, now);
  }
  if (B.state == LK_LOBBY && simBotInviteAt && now >= simBotInviteAt && B.peerCount()) {
    simBotInviteAt = 0;
    B.invite(0, simBotGame, now);
  }
  if (B.state == LK_OVER && B.delivered()) B.toLobby();
  if (B.state == LK_PLAYING && LinkCore::realtime(B.game)) { simBotRealtime(now); return; }
  if (B.state == LK_PLAYING && B.game == LKG_SHIP) { simBotShip(now); return; }
  if (B.state == LK_PLAYING && (B.game == LKG_DAME || B.game == LKG_MAU)) { simBotBoard(now); return; }
  if (B.state != LK_PLAYING) { botRt = false; botShipOn = false; botBgOn = false; }
  if (B.state != LK_PLAYING) { if (B.state != LK_INVITED) botNext = now + 1500; return; }
  if (!botInGame) {                                    // a new game begins
    botInGame = true; botMoves = 0;
    memset(botC4, 0, sizeof(botC4)); memset(botTT, 0, sizeof(botTT));
    botTurn = B.iStart; botNext = now + 700;
  }
  uint8_t m;
  if (B.moveIn(&m)) { botApply(m, 2); botTurn = true; botNext = now + 700; if (botDone()) return; }
  if (botTurn && now >= botNext) {
    if (simBotLeaveAfter >= 0 && botMoves >= simBotLeaveAfter) { B.cancel(); botInGame = false; return; }
    uint8_t legal[9], n = 0;
    for (uint8_t c = 0; c < (B.game == LKG_C4 ? 7 : 9); c++)
      if (B.game == LKG_C4 ? !botC4[0][c] : !botTT[c]) legal[n++] = c;
    if (!n) return;
    m = legal[rand() % n];
    botApply(m, 1);
    B.sendMove(m, now);
    botMoves++;
    botTurn = false;
    botDone();
  }
}

void linkTick() {
  if (!simOpen) return;
  uint32_t now = linkNow();
  std::vector<SimPkt> air;
  air.swap(simAir);
  for (auto &p : air) {
    if (p.toBot) simBot.onPacket(SIM_MY_MAC, p.d.data(), (int)p.d.size(), -58, now);
    else         simMe.onPacket(SIM_BOT_MAC, p.d.data(), (int)p.d.size(), -58, now);
  }
  simMe.tick(now);
  simBot.tick(now);
  simBotBrain(now);
}
