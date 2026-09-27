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
#define HAVE_LINK 1

static const char *mpGameName(uint8_t g) { return g == LKG_C4 ? "4 wins" : "Tic Tac Toe"; }

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
  if (end) { sfx(strcmp(end, "LOST") ? 1400 : 200, 250); mpFinish(mpC4Draw, end); }
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
  if (end) { sfx(strcmp(end, "LOST") ? 1400 : 200, 250); mpFinish(mpTtDraw, end); }
  ttVs = NULL;
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
      gameEnd = (L.game == LKG_C4) ? mpC4() : mpTTT();
      btnClear();
      continue;
    }

    if (st == LK_INVITED) {                             // somebody challenges us
      if (L.game != LKG_C4 && L.game != LKG_TTT) { L.answer(false, linkNow()); continue; }
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
        static const char *const games[2] = { "4 wins", "Tic Tac Toe" };
        uint8_t g = chooseMode("CHALLENGE", games, 2);
        if (g != 255) {
          for (uint8_t i = 0; i < L.peerCount(); i++)   // the list may have changed meanwhile
            if (!strcmp(L.peer(i).code, selCode)) { L.invite(i, g == 0 ? LKG_C4 : LKG_TTT, linkNow()); break; }
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
