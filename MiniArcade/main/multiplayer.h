// MiniArcade: multiplayer (ESP-IDF build only). Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  MULTIPLAYER   (ESP-IDF build only: two consoles nearby over ESP-NOW)
// =========================================================
/* Everybody who has this page open shows up in the list of the others,
   with a name and a 4 letter code taken from the chip. A challenge waits
   30 s for an answer. The radio is only on while this page is open, so
   nobody gets challenged in the middle of a game of Tetris.             */
#if defined(HAVE_NET) && __has_include("link.h")
#include "link.h"
#include "rtgames.h"
#define HAVE_LINK 1

static const char *const MP_GAMES[6] = { "4 wins", "Tic Tac Toe", "Pong", "Snake", "Pac-Man", "Battleship" };   // LKG_C4 ..
static const char *mpGameName(uint8_t g) { return g >= LKG_C4 && g <= LKG_SHIP ? MP_GAMES[g - 1] : "?"; }

static void mpHook() { linkTick(); lastInput = millis(); }  // radio on: no deep sleep

static void mpTitle(const char *t, const char *right) {
  oled.setFont(FONT_B);
  oled.drawStr(2, 12, t);
  oled.drawHLine(0, TOP_H - 1, SCR_W);
  oled.setFont(FONT);
  if (right) rightStr(12, right);
}

static int mpSecondsLeft() {
  int32_t ms = (int32_t)(linkCore().deadline - linkNow());
  return ms > 0 ? (int)(ms + 999) / 1000 : 0;
}

// a short message; OK or 3 s go on
static void mpNote(const char *l1, const char *l2) {
  uint32_t t0 = millis();
  btnClear();
  while (millis() - t0 < 3000) {
    if (!poll() || btn(B_OK)) break;
    mpTitle("MULTIPLAYER", NULL);
    centerStr(34, l1);
    if (l2) centerStr(46, l2);
    oled.sendBuffer();
  }
}

// ---------------- own name ----------------
static void mpName() {
  static const char CH[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  const uint8_t NCH = sizeof(CH) - 1;
  char n[LK_NAME + 1];
  memset(n, ' ', LK_NAME);
  n[LK_NAME] = 0;
  const char *cur = linkName();
  for (uint8_t i = 0; i < LK_NAME && cur[i]; i++) n[i] = cur[i];
  uint8_t pos = 0;
  btnClear();
  while (poll()) {
    const char *f = strchr(CH, n[pos]);
    uint8_t k = f ? (uint8_t)(f - CH) : 0;
    if (btn(B_UP))                         { n[pos] = CH[(k + 1) % NCH];       sfx(700, 15); }
    if (btn(B_DOWN))                       { n[pos] = CH[(k + NCH - 1) % NCH]; sfx(700, 15); }
    if (btn(B_LEFT)  && pos)               { pos--; sfx(600, 15); }
    if (btn(B_RIGHT) && pos < LK_NAME - 1) { pos++; sfx(600, 15); }
    if (btn(B_OK)) {
      uint8_t e = LK_NAME;
      while (e && n[e - 1] == ' ') e--;                 // trailing blanks go
      n[e] = 0;
      const char *start = n;
      while (*start == ' ') start++;
      if (*start) linkSetName(start);                   // an empty name keeps the old one
      sfx(1200, 50);
      return;
    }
    mpTitle("YOUR NAME", linkCore().code());
    oled.setFont(FONT_B);
    for (uint8_t i = 0; i < LK_NAME; i++) {
      char c[2] = { n[i], 0 };
      int16_t x = 22 + i * 14;
      oled.drawStr(x, 38, c);
      if (i == pos) oled.drawHLine(x - 1, 41, 9);        // cursor
      else          oled.drawPixel(x + 3, 41);
    }
    oled.setFont(FONT);
    centerStr(54, "UP/DOWN letter  OK=save");
    oled.sendBuffer();
  }
}

// ---------------- the games ----------------
/* After the deciding move: show the board, keep resending until the other
   side has the last move (at most a few seconds), then back to the list. */
static void mpFinish(void (*draw)(const char *), const char *msg) {
  LinkCore &L = linkCore();
  L.finish();
  uint32_t t0 = millis();
  btnClear();
  while (millis() - t0 < 3000 || (!L.delivered() && millis() - t0 < 8000)) {
    if (!poll() || (btn(B_OK) && millis() - t0 > 800)) break;
    draw(msg);
  }
  L.toLobby();
}

static uint8_t mpSel;                                   // for the draw callbacks
static void mpC4Draw(const char *msg) { c4Draw(mpSel, msg, 0); }
static void mpTtDraw(const char *msg) { ttDraw(9, ttLine(1) >= 0 ? ttLine(1) : ttLine(2), msg, NULL, 0); }

/* 0 = decided or the other one left, 1 = we gave up, 2 = broken (already said) */
static uint8_t mpC4() {
  LinkCore &L = linkCore();
  char vs[16];
  snprintf(vs, sizeof(vs), "vs %.6s", L.opp.name);
  c4Vs = vs;
  memset(c4, 0, sizeof(c4));
  mpSel = 3;
  bool mine = L.iStart;
  const char *end = NULL;
  uint8_t ret = 0;
  btnClear();
  while (!end) {
    if (!poll()) { L.cancel(); ret = 1; break; }        // hold OK = give up
    if (L.state != LK_PLAYING) break;                   // the other one left
    if (mine) {
      if (btn(B_LEFT)  && mpSel > 0)        { mpSel--; sfx(700, 15); }
      if (btn(B_RIGHT) && mpSel < C4_W - 1) { mpSel++; sfx(700, 15); }
      if ((btn(B_OK) || btn(B_DOWN)) && c4Drop(mpSel, 1) >= 0) {
        L.sendMove(mpSel, linkNow());
        sfx(500, 40);
        mine = false;
        if (c4Wins(1)) end = "YOU WIN";
        else if (c4Full()) end = "DRAW";
      }
      c4Draw(mpSel, "your go", 0);
    } else {
      uint8_t m;
      if (L.moveIn(&m)) {
        if (m >= C4_W || c4Drop(m, 2) < 0) { L.cancel(); mpNote("connection error", NULL); ret = 2; break; }
        sfx(350, 40);
        mine = true;
        if (c4Wins(2)) end = "LOST";
        else if (c4Full()) end = "DRAW";
      }
      c4Draw(mpSel, L.opp.name, 0);                     // whose turn it is
    }
  }
  if (end) { sfx(strcmp(end, "LOST") ? 1400 : 200, 250); mpFinish(mpC4Draw, end); statOnline(!strcmp(end, "YOU WIN")); }
  c4Vs = NULL;
  return ret;
}

static uint8_t mpTTT() {
  LinkCore &L = linkCore();
  char vs[16], you[12], them[12];
  snprintf(vs, sizeof(vs), "vs %.6s", L.opp.name);
  ttVs = vs;
  uint8_t me = L.iStart ? 1 : 2, cur = 4;               // whoever begins plays X
  snprintf(you, sizeof(you), "you: %c", me == 1 ? 'X' : 'O');
  snprintf(them, sizeof(them), "%.6s...", L.opp.name);
  memset(tt, 0, sizeof(tt));
  bool mine = L.iStart;
  const char *end = NULL;
  uint8_t ret = 0;
  btnClear();
  while (!end) {
    if (!poll()) { L.cancel(); ret = 1; break; }
    if (L.state != LK_PLAYING) break;
    if (mine) {
      if (btn(B_LEFT)  && cur % 3)     { cur--;    sfx(700, 15); }
      if (btn(B_RIGHT) && cur % 3 < 2) { cur++;    sfx(700, 15); }
      if (btn(B_UP)    && cur > 2)     { cur -= 3; sfx(700, 15); }
      if (btn(B_DOWN)  && cur < 6)     { cur += 3; sfx(700, 15); }
      if (btn(B_OK)) {
        if (tt[cur]) sfx(300, 60);                      // taken already
        else {
          tt[cur] = me;
          L.sendMove(cur, linkNow());
          sfx(500, 40);
          mine = false;
          if (ttLine(me) >= 0) end = "YOU WIN";
          else if (ttFull()) end = "DRAW";
        }
      }
      ttDraw(cur, -1, NULL, you, 0);
    } else {
      uint8_t m;
      if (L.moveIn(&m)) {
        if (m > 8 || tt[m]) { L.cancel(); mpNote("connection error", NULL); ret = 2; break; }
        tt[m] = 3 - me;
        sfx(350, 40);
        mine = true;
        if (ttLine(3 - me) >= 0) end = "LOST";
        else if (ttFull()) end = "DRAW";
      }
      ttDraw(9, -1, NULL, them, 0);
    }
  }
  if (end) { sfx(strcmp(end, "LOST") ? 1400 : 200, 250); mpFinish(mpTtDraw, end); statOnline(!strcmp(end, "YOU WIN")); }
  ttVs = NULL;
  return ret;
}

// ---------------- Battleship ----------------
/* Taking turns like the other two: every move is one byte,
   (result of their last shot << 6) | our shot. When our fleet is gone
   the last move only carries the result.                              */
static const char *mpBsMsg;
static void mpBsDraw(const char *msg) {
  char b[16];
  snprintf(b, sizeof(b), "vs %.6s", linkCore().opp.name);
  bsBar(b);
  bsDraw(bsShot, bsMine, -1, msg, NULL);
}

static uint8_t mpBattle() {
  LinkCore &L = linkCore();
  char vs[16];
  snprintf(vs, sizeof(vs), "vs %.6s", L.opp.name);
  if (!bsSetup(vs)) { L.cancel(); return 1; }       // hold OK while setting up = give up
  memset(bsShot, 0, sizeof(bsShot));
  bool mine = L.iStart, waiting = false;            // waiting: for the result of our shot
  uint8_t cur = 27, shotAt = 0, lastRes = 0, hits = 0;
  const char *end = NULL;
  mpBsMsg = NULL;
  uint8_t ret = 0;
  btnClear();
  while (!end) {
    if (!poll()) { L.cancel(); ret = 1; break; }
    if (L.state != LK_PLAYING) break;
    if (mine) {
      int16_t c = bsAim(cur, bsShot);
      if (c >= 0 && L.sendMove((lastRes << 6) | c, linkNow())) {
        shotAt = c; waiting = true; mine = false; sfx(500, 30);
      }
    } else {
      uint8_t m;
      if (L.moveIn(&m)) {
        uint8_t res = m >> 6, cell = m & 63;
        if (waiting) {                              // how our shot went
          waiting = false;
          if (!res) { L.cancel(); mpNote("connection error", NULL); ret = 2; break; }
          bsMark(bsShot, shotAt, res);
          if (res != BS_MISS) hits++;
          sfx(res == BS_MISS ? 300 : 1000, res == BS_SUNK ? 250 : 60);
          mpBsMsg = res == BS_MISS ? "miss" : (res == BS_HIT ? "hit!" : "sunk!");
          uint8_t total = 0;
          for (uint8_t s = 0; s < BS_SHIPS; s++) total += BS_LEN[s];
          if (hits >= total) { end = "YOU WIN"; break; }
        }
        uint8_t r = bsFire(bsMine, cell);           // their shot
        lastRes = r ? r : (uint8_t)BS_MISS;
        if (r != BS_MISS) sfx(180, 200);
        if (bsAllSunk(bsMine)) {                    // the last move: just the result
          L.sendMove(lastRes << 6, linkNow());
          end = "LOST";
          break;
        }
        mine = true;
        mpBsMsg = r == BS_HIT ? "ouch!" : (r == BS_SUNK ? "sunk us" : mpBsMsg);
      }
    }
    char b[16], t[12];
    snprintf(b, sizeof(b), "vs %.6s", L.opp.name);
    snprintf(t, sizeof(t), "%.6s...", L.opp.name);
    bsBar(b);
    bsDraw(bsShot, bsMine, mine ? cur : -1, mine ? "your go" : t, mpBsMsg);   // 2nd line: what just happened
  }
  if (end) { sfx(strcmp(end, "LOST") ? 1400 : 200, 250); mpFinish(mpBsDraw, end); statOnline(!strcmp(end, "YOU WIN")); }
  return ret;
}

// ---------------- real-time: Pong and Snake ----------------
/* Both consoles compute the same game (rtgames.h); only the keys go over
   the radio, in lockstep (linkcore.h). A tick is taken as soon as both
   inputs for it are there - a lost packet just makes it wait a moment. */
static RtPong  mpPg;
static RtSnake mpSn;
static uint8_t mpMe;                                    // our player number
static bool    mpStalled;

static void mpRtBar(uint8_t mine, uint8_t theirs) {
  char b[20];
  oled.setFont(FONT_B);
  snprintf(b, sizeof(b), "YOU %u", mine);
  oled.drawStr(2, 12, b);
  oled.setFont(FONT);
  snprintf(b, sizeof(b), "%u %.6s", theirs, linkCore().opp.name);
  rightStr(12, b);
  oled.drawHLine(0, TOP_H - 1, SCR_W);
}

static void mpRtMsg(const char *msg) {                  // centred, on a cleared band
  if (!msg) return;
  oled.setFont(FONT_B);
  uint8_t w = oled.getStrWidth(msg) + 8;
  oled.setDrawColor(0);
  oled.drawBox((SCR_W - w) / 2, 31, w, 15);
  oled.setDrawColor(1);
  oled.drawFrame((SCR_W - w) / 2, 31, w, 15);
  centerStr(43, msg);
  oled.setFont(FONT);
}

static void mpPongDraw(const char *msg) {
  RtPong &g = mpPg;
  mpRtBar(g.pts[mpMe], g.pts[1 - mpMe]);
  for (uint8_t y = TOP_H + 1; y < SCR_H; y += 4) oled.drawPixel(63, y);   // net
  // we always play on the left: the other console sees it mirrored
  for (uint8_t p = 0; p < 2; p++) oled.drawBox(p == mpMe ? 2 : 123, g.pad[p], 3, RP_PH);
  int16_t bx = g.bx >> 4;
  if (mpMe) bx = SCR_W - 2 - bx;
  if (!g.wait || (g.wait / 5) % 2) oled.drawBox(bx, g.by >> 4, 2, 2);  // blinks before a serve
  if (mpStalled && !msg) msg = "waiting...";
  mpRtMsg(msg);
  oled.sendBuffer();
}

static void mpSnakeDraw(const char *msg) {
  RtSnake &g = mpSn;
  mpRtBar(g.len[mpMe], g.len[1 - mpMe]);
  for (uint8_t p = 0; p < 2; p++)
    for (uint8_t i = 0; i < g.len[p]; i++) {
      uint8_t x = g.x[p][i] * 4, y = TOP_H + g.y[p][i] * 4;
      if (p == mpMe) oled.drawBox(x, y, 3, 3);                // ours filled
      else           oled.drawFrame(x, y, 3, 3);              // theirs hollow
      if (!i && p != mpMe) oled.drawPixel(x + 1, y + 1);      // their head
    }
  for (uint8_t f = 0; f < RS_FOOD; f++) {
    if (g.fx[f] >= RS_W) continue;
    uint8_t x = g.fx[f] * 4, y = TOP_H + g.fy[f] * 4;
    oled.drawPixel(x + 1, y); oled.drawHLine(x, y + 1, 3); oled.drawPixel(x + 1, y + 2);
  }
  if (mpStalled && !msg) msg = "waiting...";
  mpRtMsg(msg);
  oled.sendBuffer();
}

static void mpPacDraw(const char *msg) {                // pm: the game of game_pacman.h
  mpRtBar(pm.score[mpMe], pm.score[1 - mpMe]);
  if (mpStalled && !msg) msg = "waiting...";
  if (!msg && !pm.alive(mpMe)) msg = "no lives left";
  if (!msg && pm.freeze && pm.t < RM_FREEZE) msg = "READY!";
  pmDraw(mpMe, msg);
}

/* 0 = decided or the other one left, 1 = we gave up */
static uint8_t mpRealtime() {
  LinkCore &L = linkCore();
  bool pong = L.game == LKG_PONG, pac = L.game == LKG_PAC;
  mpMe = L.iStart ? 0 : 1;
  uint32_t seed = L.session() * 2654435761u;             // the same on both consoles
  if (pong) mpPg.begin(seed); else if (pac) pm.begin(seed, 2); else mpSn.begin(seed);
  uint8_t  latch = 0;                                     // snake: last direction pressed
  uint32_t next = millis(), lastStep = millis();
  const char *end = NULL;
  uint8_t ret = 0;
  btnClear();
  while (!end) {
    if (!poll()) { L.cancel(); ret = 1; break; }          // hold OK = give up
    if (L.state != LK_PLAYING) break;                     // the other one left
    if (btn(B_UP))    latch = 1;
    if (btn(B_DOWN))  latch = 2;
    if (btn(B_LEFT))  latch = 3;                          // the snake field is the same
    if (btn(B_RIGHT)) latch = 4;                          // on both consoles, not mirrored
    uint32_t now = millis();
    for (uint8_t k = 0; k < 3 && lkDue(now, next); k++) {
      uint8_t in[2];
      if (!L.syncStep(&in[mpMe], &in[1 - mpMe])) break;
      uint8_t ev = pong ? mpPg.step(in) : (pac ? pm.step(in) : mpSn.step(in));
      uint8_t mine = pong ? (uint8_t)((btnHeld(B_UP) ? 1 : 0) | (btnHeld(B_DOWN) ? 2 : 0)) : latch;
      L.syncPut(mine, linkNow());
      next += RT_TICK_MS;
      lastStep = now;
      if (ev & RT_EV_HIT)   sfx(750, 30);
      if (ev & RT_EV_EAT)   sfx(pac ? (pm.t % 2 ? 880 : 660) : 1100, pac ? 12 : 45);
      if (ev & RT_EV_POINT) sfx(300, 120);
    }
    if ((int32_t)(now - next) > 100) next = now;          // do not race to catch up
    mpStalled = now - lastStep > 400;
    uint8_t w = pong ? mpPg.winner : (pac ? pm.winner : mpSn.winner);
    if (w) end = w == 3 ? "DRAW" : (w == mpMe + 1 ? "YOU WIN" : "LOST");
    if (pong) mpPongDraw(NULL); else if (pac) mpPacDraw(NULL); else mpSnakeDraw(NULL);
  }
  if (end) {
    sfx(strcmp(end, "LOST") ? 1400 : 200, 250);
    mpStalled = false;
    mpFinish(pong ? mpPongDraw : (pac ? mpPacDraw : mpSnakeDraw), end);
    statOnline(!strcmp(end, "YOU WIN"));
  }
  return ret;
}

// ---------------- the list ----------------
static void mpBars(int16_t x, int16_t y, int8_t rssi) {   // signal strength
  uint8_t n = rssi > -55 ? 4 : (rssi > -65 ? 3 : (rssi > -75 ? 2 : 1));
  for (uint8_t i = 0; i < 4; i++) {
    uint8_t h = 2 + i * 2;
    if (i < n) oled.drawBox(x + i * 3, y - h, 2, h);
    else       oled.drawPixel(x + i * 3, y - 1);
  }
}

// why a challenge or a game ended, from the state it was in before
static void mpEnded(LkState before) {
  LinkCore &L = linkCore();
  char b[26];
  const char *n = L.opp.name;
  switch (L.why) {
  case LKE_DECLINED: snprintf(b, sizeof(b), "%.6s said no", n); break;
  case LKE_BUSY:     snprintf(b, sizeof(b), "%.6s is busy", n); break;
  case LKE_TIMEOUT:
    if (before == LK_INVITING) snprintf(b, sizeof(b), "no answer from %.6s", n);
    else snprintf(b, sizeof(b), "time is up");
    break;
  case LKE_GONE:     snprintf(b, sizeof(b), "%.6s is gone", n); break;
  case LKE_LEFT:
    if (before == LK_INVITED) snprintf(b, sizeof(b), "%.6s took it back", n);
    else snprintf(b, sizeof(b), "%.6s left the game", n);
    break;
  default: b[0] = 0; break;
  }
  if (b[0]) mpNote(b, NULL);
}

void multiplayerRun() {
  if (runClock < 80) { wlanNeedsClock(); return; }     // the radio needs 80 MHz+
  phoneLinkOff();                                       // the radio is ours now, not the WLAN's
  if (!linkOpen()) { mpNote("radio did not start", NULL); return; }
  pollHook = mpHook;
  LinkCore &L = linkCore();
  char selCode[5] = "";                                 // "" = own row
  uint8_t  top = 0;
  uint32_t nextBeep = 0;
  LkState  before = LK_LOBBY;
  uint8_t  gameEnd = 0;                                 // what mpC4 / mpTTT reported
  btnClear();
  for (;;) {
    bool alive = poll();
    LkState st = L.state;

    if (st == LK_OVER) {                                // challenge or game is over
      if (gameEnd == 1) mpNote("you left the game", NULL);
      else if (gameEnd == 0) mpEnded(before);
      gameEnd = 0;
      L.toLobby();
      before = LK_LOBBY;
      btnClear();
      continue;
    }
    before = st;

    if (st == LK_PLAYING) {
      gameEnd = LinkCore::realtime(L.game) ? mpRealtime() : L.game == LKG_C4 ? mpC4() :
                L.game == LKG_TTT ? mpTTT() : mpBattle();
      btnClear();
      continue;
    }

    if (st == LK_INVITED) {                             // somebody challenges us
      if (L.game < LKG_C4 || L.game > LKG_SHIP) { L.answer(false, linkNow()); continue; }
      if (millis() >= nextBeep) { nextBeep = millis() + 2000; sfx(1500, 90); }
      if (btn(B_OK))                      { L.answer(true, linkNow()); sfx(1200, 60); btnClear(); continue; }
      if (btn(B_LEFT) || btn(B_RIGHT) || !alive) { L.answer(false, linkNow()); sfx(300, 80); btnClear(); continue; }
      char t[8], b[32];
      snprintf(t, sizeof(t), "%ds", mpSecondsLeft());
      mpTitle("CHALLENGE!", t);
      snprintf(b, sizeof(b), "%.6s %.4s", L.opp.name, L.opp.code);
      oled.setFont(FONT_B);
      centerStr(33, b);
      oled.setFont(FONT);
      snprintf(b, sizeof(b), "wants to play %.11s", mpGameName(L.game));
      centerStr(45, b);
      centerStr(60, "OK = yes   LEFT = no");
      oled.sendBuffer();
      continue;
    }

    if (st == LK_INVITING) {                            // waiting for an answer
      if (btn(B_LEFT) || !alive) { L.cancel(); btnClear(); continue; }
      char t[8], b[32];
      snprintf(t, sizeof(t), "%ds", mpSecondsLeft());
      mpTitle("CHALLENGE", t);
      snprintf(b, sizeof(b), "waiting for %.6s %.4s", L.opp.name, L.opp.code);
      centerStr(32, b);
      centerStr(44, mpGameName(L.game));
      centerStr(60, "LEFT = take back");
      oled.sendBuffer();
      continue;
    }

    // ---- the list of consoles around ----
    if (!alive) break;                                  // hold OK = back to the games
    uint8_t idx[LK_PEERS];
    uint8_t n = linkList(idx, LK_PEERS);
    int8_t sel = 0;                                     // 0 = own row, 1.. = peers
    for (uint8_t i = 0; i < n; i++) if (selCode[0] && !strcmp(L.peer(idx[i]).code, selCode)) sel = i + 1;
    if (btn(B_UP)   && sel > 0) { sel--; sfx(700, 15); }
    if (btn(B_DOWN) && sel < n) { sel++; sfx(700, 15); }
    if (sel) strcpy(selCode, L.peer(idx[sel - 1]).code); else selCode[0] = 0;

    if (sel && btn(B_RIGHT)) { linkToggleFriend(selCode); sfx(900, 30); }
    if (btn(B_OK)) {
      sfx(1200, 50);
      if (!sel) mpName();
      else if (L.peer(idx[sel - 1]).busy) sfx(300, 120);
      else {
        uint8_t g = chooseMode("CHALLENGE", MP_GAMES, 6);
        if (g != 255) {
          for (uint8_t i = 0; i < L.peerCount(); i++)   // the list may have changed meanwhile
            if (!strcmp(L.peer(i).code, selCode)) {
              uint8_t need = LinkCore::capOf(g + 1);
              if (need && !(L.peer(i).caps & need)) {
                char b[26], c[26];
                snprintf(b, sizeof(b), "%.6s needs an update", L.peer(i).name);
                snprintf(c, sizeof(c), "for %s", MP_GAMES[g]);
                mpNote(b, c);
              } else L.invite(i, g + 1, linkNow());
              break;
            }
        }
      }
      btnClear();
      continue;
    }

    mpTitle("LOBBY", sel ? "OK=play >=friend" : "OK=rename");
    const uint8_t ROWS_MP = 6;
    if (sel < top) top = sel;
    if (sel >= top + ROWS_MP) top = sel - ROWS_MP + 1;
    for (uint8_t r = 0; r < ROWS_MP; r++) {
      uint8_t row = top + r, y = TOP_H + r * 8;
      if (row > n) {
        if (!n && row == 1) oled.drawStr(3, y + 7, "searching...");
        break;
      }
      char b[32];
      if (row == 0) snprintf(b, sizeof(b), " you: %.6s %.4s", linkName(), L.code());
      else {
        const LkPeer &p = L.peer(idx[row - 1]);
        snprintf(b, sizeof(b), "%c%-6.6s %.4s %s", linkIsFriend(p.code) ? '*' : ' ',
                 p.name, p.code, p.busy ? "busy" : "free");
      }
      if (row == sel) { oled.drawBox(0, y, SCR_W, 8); oled.setDrawColor(0); }
      oled.drawStr(1, y + 7, b);
      if (row) mpBars(SCR_W - 12, y + 7, L.peer(idx[row - 1]).rssi);
      oled.setDrawColor(1);
    }
    oled.sendBuffer();
  }
  pollHook = NULL;
  linkClose();
}
#endif
