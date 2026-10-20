// MiniArcade Color: online play with a console nearby (ESP-NOW). Part of
// console.cpp, which includes it - not compiled on its own.
/* The OLED console's multiplayer page (MiniArcade/main/multiplayer.h) in
   colour, on the same protocol (linkcore.h): an OLED and a colour console
   find each other and play the same games together. The rules are the
   shared ones (boardgames.h, rtgames.h, the colour games' own), the
   colour games draw the boards. Chess is only between colour consoles:
   game LKG_CHESS, beacon flag LK_CAP_CHESS, and a chess move is two moves
   on the radio (from, then to | promotion << 6).
   Everybody who has this page open shows up in the list of the others,
   with a name and a 4 letter code taken from the chip. A challenge waits
   30 s for an answer. The radio is only on while this page is open.   */
#include "link.h"

#define MP_ACCENT rgb(0, 160, 200)
#define MP_GAMES_N LKG_CHESS

static const char *const MP_GAMES[MP_GAMES_N] = { "4 wins", "Tic Tac Toe", "Pong", "Snake", "Pac-Man", "Battleship",
                                                  "Checkers", "Mau-Mau", "Bomberman", "Chess" };   // LKG_C4 ..
static const char *const *const MP_ICON[MP_GAMES_N] = { IC_CONNECT4, IC_TICTACTOE, IC_PONG, IC_SNAKE, IC_PACMAN,
                                                        IC_BATTLESHIP, IC_CHECKERS, IC_MAUMAU, IC_BOMBER, IC_CHESS };
static const char *mpGameName(uint8_t g) { return g >= LKG_C4 && g <= MP_GAMES_N ? MP_GAMES[g - 1] : "?"; }

static const char *const IC_ONLINE[12] = {
  "............", ".c........c.", "c..c....c..c", "c.c..ww..c.c", "c.c..ww..c.c", "c..c.ss.c..c",
  ".c...ss...c.", ".....ss.....", "....ssss....", "...ss..ss...", "..ss....ss..", ".ssssssssss." };

/* Moves go out one after the other: the next one only when the last one
   is confirmed (a Checkers move of several jumps, Mau-Mau: draw, then
   play, a chess move). The poll hook sends them, also while a move is
   shown moving; mpQFlush waits until all are out before a game ends.  */
static uint8_t mpQ[16], mpQn;
static void mpQPush(uint8_t m) { if (mpQn < sizeof(mpQ)) mpQ[mpQn++] = m; }
static void mpQSend() {
  if (mpQn && linkCore().sendMove(mpQ[0], linkNow())) memmove(mpQ, mpQ + 1, --mpQn);
}
static void mpQFlush(void (*draw)(const char *)) {
  uint32_t t0 = millis();
  while (mpQn && linkCore().state == LK_PLAYING && millis() - t0 < 5000 && poll()) draw(NULL);
}

static void mpHook() { linkTick(); mpQSend(); lastInput = millis(); }   // radio on: no sleep

/* The bar on top of a game shows who we play against instead of score
   and best (core.h's statusBar, also while a game's own animation runs). */
static char mpRight[32];
static void mpVs() { snprintf(mpRight, sizeof(mpRight), "vs %.6s", linkCore().opp.name); barRight = mpRight; }
static void mpBar(const char *title, col accent) { statusBar(title, 0, 0, accent); }
static void mpScoreBar(const char *title, uint16_t mine, uint16_t theirs, col accent) {
  snprintf(mpRight, sizeof(mpRight), "YOU %u : %u %.6s", mine, theirs, linkCore().opp.name);
  mpBar(title, accent);
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
    wlanTitle("ONLINE", MP_ACCENT);
    panel(24, 76, SCR_W - 48, 96, NULL, MP_ACCENT);
    gfx.center(l2 ? 100 : 114, l1, C::WHITE);
    if (l2) gfx.center(130, l2, C::LTGREY);
    gfx.send();
  }
}

// a banner over the middle of a game
static void mpBanner(const char *msg, col c) {
  if (!msg) return;
  int16_t w = Gfx::width(msg, F_BOLD) + 36, x = (SCR_W - w) / 2;
  gfx.rbox(x + 4, 112, w, 42, 10, C::BLACK);
  gfx.rbox(x, 108, w, 42, 10, colDark(c, 3));
  gfx.rframe(x, 108, w, 42, 10, c);
  gfx.center(117, msg, C::WHITE, F_BOLD);
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
    wlanTitle("YOUR NAME", MP_ACCENT);
    gfx.right(SCR_W - 12, 10, linkCore().code(), C::YELLOW, F_TEXT);
    for (uint8_t i = 0; i < LK_NAME; i++) {
      char c[2] = { n[i], 0 };
      int16_t x = (SCR_W - LK_NAME * 46) / 2 + i * 46 + 2;
      bool on = i == pos;
      gfx.rbox(x, 70, 42, 56, 6, on ? MP_ACCENT : colDark(MP_ACCENT, 3));
      gfx.rframe(x, 70, 42, 56, 6, on ? C::WHITE : MP_ACCENT);
      gfx.text(x + (42 - Gfx::width(c, F_BIG)) / 2, 80, c, C::WHITE, F_BIG);
      if (on) {
        gfx.triangle(x + 21, 60, x + 15, 66, x + 27, 66, C::WHITE);
        gfx.triangle(x + 21, 136, x + 15, 130, x + 27, 130, C::WHITE);
      }
    }
    gfx.center(156, "UP / DOWN letter, LEFT / RIGHT place", C::LTGREY, F_SMALL);
    char b[40];
    snprintf(b, sizeof(b), "%s = save", okKeys());
    gfx.center(176, b, C::LIME);
    gfx.center(SCR_H - 16, "the others see this name and your code", C::GREY, F_SMALL);
    gfx.send();
  }
}

// ---------------- the end of a game ----------------
/* After the deciding move: show the board, keep resending until the other
   side has the last move (at most a few seconds), then back to the list. */
static void mpFinish(void (*draw)(const char *), const char *msg) {
  LinkCore &L = linkCore();
  L.finish();
  sfx(strcmp(msg, "YOU LOST") ? 1400 : 200, 250);
  uint32_t t0 = millis();
  btnClear();
  while (millis() - t0 < 3000 || (!L.delivered() && millis() - t0 < 8000)) {
    if (!poll() || (btn(B_OK) && millis() - t0 > 800)) break;
    draw(msg);
  }
  L.toLobby();
}

static void mpBroken() { linkCore().cancel(); mpNote("connection error", NULL); }

// what the end of a game means for the line under it
static const char *mpResult(uint8_t w, uint8_t me) {     // w: 1 / 2 = player 0 / 1, 3 = draw
  return w == 3 ? "DRAW" : (w == me + 1 ? "YOU WIN" : "YOU LOST");
}

// ---------------- 4 wins ----------------
/* A move is the column. Each console shows itself red (1). */
static uint8_t mpWinWho;
static void mpC4Draw(const char *msg) {
  c4Draw(-1, -1, 0, 0, mpWinWho, msg, NULL);
  gfx.send();
}

/* 0 = decided or the other one left, 1 = we gave up, 2 = broken (already said) */
static uint8_t mpC4() {
  LinkCore &L = linkCore();
  memset(c4, 0, sizeof(c4));
  c4V.two = true; c4V.score = 0;
  c4V.who[1] = "YOU"; c4V.who[2] = L.opp.name;
  mpWinWho = 0;
  uint8_t sel = 3, ret = 0;
  bool mine = L.iStart;
  const char *end = NULL;
  btnClear();
  while (!end) {
    if (!poll()) { L.cancel(); ret = 1; break; }        // hold OK = give up
    if (L.state != LK_PLAYING) break;                   // the other one left
    c4V.turn = mine ? 1 : 2;
    int8_t drop = -1;
    if (mine) {
      if (btn(B_LEFT)  && sel > 0)        { sel--; sfx(700, 15); }
      if (btn(B_RIGHT) && sel < C4_W - 1) { sel++; sfx(700, 15); }
      if (btn(B_OK) || btn(B_DOWN)) {
        if (c4[0][sel]) sfx(200, 60);                   // column full
        else { mpQPush(sel); drop = (int8_t)sel; }
      }
    } else {
      uint8_t m;
      if (L.moveIn(&m)) {
        if (m >= C4_W || c4[0][m]) { mpBroken(); ret = 2; break; }
        drop = (int8_t)m;
      }
    }
    if (drop >= 0) {
      uint8_t who = mine ? 1 : 2;
      if (c4Fall((uint8_t)drop, who) == -2) { L.cancel(); ret = 1; break; }
      if (!mine) sfx(350, 40);
      mine = !mine;
      if (c4Wins(who)) { mpWinWho = who; end = who == 1 ? "YOU WIN" : "YOU LOST"; }
      else if (c4Full()) end = "DRAW";
      btnClear();
      continue;
    }
    c4Draw(mine ? sel : -1, -1, 0, 0, 0, NULL, mine ? "your go" : "their go");
    gfx.send();
  }
  if (end) { mpQFlush(mpC4Draw); mpFinish(mpC4Draw, end); }
  memset(c4V.who, 0, sizeof(c4V.who));
  c4V.two = false;
  return ret;
}

// ---------------- Tic Tac Toe ----------------
/* A move is the field 0..8; whoever begins plays X. */
static int8_t mpXoLine;
static void mpXoDraw(const char *msg) {
  xoDraw(9, mpXoLine, 0, NULL, msg);
  gfx.send();
}

static uint8_t mpTTT() {
  LinkCore &L = linkCore();
  uint8_t me = L.iStart ? 1 : 2, cur = 4, ret = 0;
  memset(xo, 0, sizeof(xo));
  memset(xoV.tally, 0, sizeof(xoV.tally));
  xoV.two = true; xoV.score = 0; xoV.last = 9;
  xoV.who[me] = "YOU"; xoV.who[3 - me] = L.opp.name;
  mpXoLine = -1;
  bool mine = L.iStart;
  const char *end = NULL;
  btnClear();
  while (!end) {
    if (!poll()) { L.cancel(); ret = 1; break; }
    if (L.state != LK_PLAYING) break;
    int8_t set = -1;
    if (mine) {
      if (btn(B_LEFT)  && cur % 3)     { cur--;    sfx(700, 15); }
      if (btn(B_RIGHT) && cur % 3 < 2) { cur++;    sfx(700, 15); }
      if (btn(B_UP)    && cur > 2)     { cur -= 3; sfx(700, 15); }
      if (btn(B_DOWN)  && cur < 6)     { cur += 3; sfx(700, 15); }
      if (btn(B_OK)) {
        if (xo[cur]) sfx(300, 60);                      // taken already
        else { mpQPush(cur); set = (int8_t)cur; sfx(500, 40); }
      }
    } else {
      uint8_t m;
      if (L.moveIn(&m)) {
        if (m > 8 || xo[m]) { mpBroken(); ret = 2; break; }
        set = (int8_t)m;
        sfx(350, 40);
      }
    }
    if (set >= 0) {
      uint8_t who = mine ? me : 3 - me;
      xoSet((uint8_t)set, who);
      mine = !mine;
      if ((mpXoLine = xoLine(who)) >= 0) end = who == me ? "YOU WIN" : "YOU LOST";
      else if (xoFull()) end = "DRAW";
    }
    if (end) break;
    xoDraw(mine ? cur : 9, -1, mine ? me : 3 - me, mine ? "your go" : "their go", NULL);
    gfx.send();
  }
  if (end) { mpQFlush(mpXoDraw); mpFinish(mpXoDraw, end); }
  memset(xoV.who, 0, sizeof(xoV.who));
  xoV.two = false;
  return ret;
}

// ---------------- Checkers ----------------
/* Each jump is one move (square | direction << 5 | "more" << 7), so a move
   of several jumps is several moves; both boards apply the same steps.
   Each console shows its own side at the bottom.                       */
static void mpCkDraw(const char *msg) {
  ckDraw(ck, -1, -1, NULL, NULL, msg);
  gfx.send();
}

static uint8_t mpCheckers() {
  LinkCore &L = linkCore();
  uint8_t me = L.iStart ? 0 : 1, ret = 0;
  char them[16];
  snprintf(them, sizeof(them), "%.6s moves", L.opp.name);
  ck.begin();
  ckV.hide = ckV.lastFrom = ckV.lastTo = -1;
  ckV.fp = 0; ckV.score = 0; ckV.two = true;
  ckV.flip = me == 1;
  ckV.who[me] = "you"; ckV.who[1 - me] = L.opp.name;
  ckCur = 52; ckSel = -1;
  mpQn = 0;
  const char *end = NULL;
  btnClear();
  while (!end) {
    if (!poll()) { L.cancel(); ret = 1; break; }        // hold OK = give up
    if (L.state != LK_PLAYING) break;                   // the other one left
    bool mine = ck.turn == me;
    if (mine) {
      int16_t m = ckInput(ck);
      if (m >= 0) {
        Checkers t = ck;
        bool more = t.apply((uint8_t)(m & 31), (uint8_t)(m >> 5));
        mpQPush((uint8_t)(m | (more ? 0x80 : 0)));
        if (!ckMove((uint8_t)(m & 31), (uint8_t)(m >> 5), "your go")) { L.cancel(); ret = 1; break; }
        if (!more) ckSel = -1;
        btnClear();
      }
    } else {
      uint8_t m;
      if (L.moveIn(&m)) {
        if (!ck.legal(m & 31, (m >> 5) & 3)) { mpBroken(); ret = 2; break; }
        if (!ckMove(m & 31, (m >> 5) & 3, them)) { L.cancel(); ret = 1; break; }
        if (ck.turn == me) btnClear();
      }
    }
    if (ck.winner) { end = mpResult(ck.winner, me); break; }
    mine = ck.turn == me;
    ckDraw(ck, mine ? (int8_t)ckCur : -1, mine ? ckSel : -1, mine ? "your go" : "their go",
           mine && ck.chain >= 0 ? "take on!" : NULL, NULL);
    gfx.send();
  }
  if (end) { mpQFlush(mpCkDraw); mpFinish(mpCkDraw, end); }
  ckV.flip = false;
  ckV.who[0] = ckV.who[1] = NULL;
  return ret;
}

// ---------------- Mau-Mau ----------------
/* The deck is shuffled from the session number, the same on both consoles;
   every draw, pass and card played goes over as one move.            */
static const char *mpMmWho[MM_MAXP];
static void mpMmDraw(const char *msg) {
  mmDraw(mm, -1, msg);
  gfx.send();
}

static uint8_t mpMauMau() {
  LinkCore &L = linkCore();
  uint8_t me = L.iStart ? 0 : 1, ret = 0;
  mm.begin(L.session() * 2654435761u, 2);
  mmMe = me;
  mpMmWho[me] = "YOU"; mpMmWho[1 - me] = L.opp.name;
  mmWho = mpMmWho;
  mmCur = 1; mmScore = 0; mmFly.on = false;
  mpQn = 0;
  strcpy(mmMsg, "dealing...");
  memset(mmShown, 0, sizeof(mmShown));                  // deal: one card after the other
  for (uint8_t k = 0; k < MM_HAND && !ret; k++)
    for (uint8_t p = 0; p < 2 && !ret; p++) {
      bool own = p == me;
      if (!mmFlyCard(0xFF, MM_DX, MM_DY, own ? 4 + (k + 1) * (MM_HW + 4) : mmZoneX(1, 2) - MM_HW / 2, own ? MM_HY : 36, 140))
        { L.cancel(); ret = 1; }
      mmShown[p]++;
      sfx(500 + k * 60, 10);
    }
  memset(mmShown, 255, sizeof(mmShown));
  if (mm.turn == me) strcpy(mmMsg, "your go");
  else snprintf(mmMsg, sizeof(mmMsg), "%.6s begins", L.opp.name);
  const char *end = NULL;
  btnClear();
  while (!end && !ret) {
    if (!poll()) { L.cancel(); ret = 1; break; }
    if (L.state != LK_PLAYING) break;
    if (mm.turn == me) {
      int16_t m = mmInput();
      if (m == -2) { L.cancel(); ret = 1; break; }
      if (m >= 0) {
        MauMau test = mm;                               // allowed? (draw after drawing is not)
        if (test.move((uint8_t)m)) {
          mpQPush((uint8_t)m);
          if (!mmPlay(me, (uint8_t)m)) { L.cancel(); ret = 1; break; }
          if (mm.drew && mm.turn == me) {               // the cursor goes to the new card
            uint8_t cards[MM_CARDS], n = mmHand(mm, me, cards);
            for (uint8_t i = 0; i < n; i++) if (cards[i] == mm.drawn) mmCur = i + 1;
          }
          btnClear();
        }
      }
    } else {
      uint8_t m;
      if (L.moveIn(&m)) {
        MauMau test = mm;
        if (!test.move(m)) { mpBroken(); ret = 2; break; }
        if (!mmPlay(1 - me, m)) { L.cancel(); ret = 1; break; }
        if (mm.turn == me) btnClear();
      }
    }
    if (mm.winner) { end = mm.winner - 1 == me ? "YOU WIN" : "YOU LOST"; break; }
    mmDraw(mm, mm.turn == me ? (int8_t)mmCur : -1, NULL);
    gfx.send();
  }
  if (end) {
    if (mm.winner - 1 == me) strcpy(mmMsg, "you have no card left");
    else snprintf(mmMsg, sizeof(mmMsg), "%.6s has no card left", L.opp.name);
    mpQFlush(mpMmDraw);
    mpFinish(mpMmDraw, end);
  }
  mmMe = 0;
  mmWho = MM_NAMES;
  return ret;
}

// ---------------- Battleship ----------------
/* Taking turns: every move is one byte, (result of their last shot << 6)
   | our shot. When our fleet is gone the last move only carries the
   result. Their fleet stays unknown: bsTheirs only holds one stand-in
   cell per ship, so the panel can show which ones are sunk.          */
static void mpBsDraw(const char *msg) {
  bool won = !strcmp(msg ? msg : "", "YOU WIN");
  bsDraw(-1, msg, won ? "all sunk" : "fleet lost", won ? C::YELLOW : C::RED, false);
  gfx.send();
}

// a ship was sunk at cell: its length marks the first stand-in of that length
static void mpBsSunk(uint8_t cell) {
  uint8_t stack[BS_CELLS], n = 0, len = 0, seen[BS_CELLS];
  memset(seen, 0, sizeof(seen));
  stack[n++] = cell; seen[cell] = 1;
  while (n) {
    uint8_t c = stack[--n];
    len++;
    static const int8_t DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
    for (uint8_t d = 0; d < 4; d++) {
      int x = c % BS_N + DX[d], y = c / BS_N + DY[d];
      if (!bsInside(x, y)) continue;
      uint8_t o = (uint8_t)(y * BS_N + x);
      if (!seen[o] && bsShot[o] == BS_SUNK) { seen[o] = 1; stack[n++] = o; }
    }
  }
  for (uint8_t s = 0; s < BS_SHIPS; s++)
    if (BS_LEN[s] == len && !(bsTheirs[s] & BS_HITBIT)) { bsTheirs[s] |= BS_HITBIT; return; }
}

static uint8_t mpBattle() {
  LinkCore &L = linkCore();
  memset(bsShot, 0, sizeof(bsShot));
  memset(bsTheirs, 0, sizeof(bsTheirs));
  for (uint8_t s = 0; s < BS_SHIPS; s++) bsTheirs[s] = s + 1;
  bsV.shots = 0; bsV.aimCell = bsV.fxCell = -1;
  mpQn = 0;
  if (!bsSetup()) { L.cancel(); return 1; }             // hold OK while setting up = give up
  uint8_t total = 0;
  for (uint8_t s = 0; s < BS_SHIPS; s++) total += BS_LEN[s];
  uint8_t &cur = bsV.cur;
  cur = 27;
  bool &mine = bsV.myGo;
  mine = L.iStart;
  bool waiting = false;                                 // for the result of our shot
  uint8_t shotAt = 0, lastRes = 0, hits = 0, ret = 0;
  char them[16];
  snprintf(them, sizeof(them), "%.6s fires", L.opp.name);
  const char *l1 = mine ? "your go" : "their go", *l2 = mine ? "OK = fire" : NULL, *ours = NULL;
  col c1 = mine ? C::WHITE : C::LTGREY;
  const char *end = NULL;
  btnClear();
  while (!end) {
    if (!poll()) { L.cancel(); ret = 1; break; }
    if (L.state != LK_PLAYING) break;
    if (mine) {
      int16_t c = bsAim(cur, bsShot);
      if (c >= 0) {
        mpQPush((uint8_t)((lastRes << 6) | c));
        shotAt = (uint8_t)c; waiting = true; mine = false;
        if (!bsShoot(false, (uint8_t)c, "fire!", NULL)) { L.cancel(); ret = 1; break; }
        l1 = "fire!"; l2 = "waiting..."; c1 = C::WHITE;
      }
    } else {
      uint8_t m;
      if (L.moveIn(&m)) {
        uint8_t res = m >> 6, cell = m & 63;
        if (waiting) {                                  // how our shot went
          waiting = false;
          if (!res) { mpBroken(); ret = 2; break; }
          bsMark(bsShot, shotAt, res);
          bsV.shots++;
          bsV.fxCell = (int8_t)shotAt; bsV.fxMine = false; bsV.fxRes = res; bsV.fxAt = gameMillis();
          if (res == BS_SUNK) mpBsSunk(shotAt);
          if (res != BS_MISS) hits++;
          sfx(res == BS_MISS ? 300 : 1000, res == BS_SUNK ? 250 : 60);
          ours = res == BS_MISS ? "you missed" : (res == BS_HIT ? "you hit!" : "you sunk one!");
          if (hits >= total) { end = "YOU WIN"; break; }
        }
        if (!bsShoot(true, cell, them, NULL)) { L.cancel(); ret = 1; break; }   // their shot
        uint8_t r = bsFire(bsMine, cell);
        lastRes = r ? r : (uint8_t)BS_MISS;
        bsV.fxCell = (int8_t)cell; bsV.fxMine = true; bsV.fxRes = lastRes; bsV.fxAt = gameMillis();
        sfx(lastRes == BS_MISS ? 250 : 180, lastRes == BS_MISS ? 40 : 200);
        if (bsAllSunk(bsMine)) {                        // the last move: just the result
          mpQPush((uint8_t)(lastRes << 6));
          end = "YOU LOST";
          break;
        }
        mine = true;
        l1 = lastRes == BS_MISS ? "your go" : (lastRes == BS_HIT ? "ouch!" : "sunk us");   // their shot ...
        c1 = lastRes == BS_MISS ? C::WHITE : C::RED;
        l2 = ours ? ours : "OK = fire";                                                    // ... and ours
        btnClear();
      }
    }
    bsDraw(mine ? cur : -1, l1, l2, c1, false);
    gfx.send();
  }
  if (end) { mpQFlush(mpBsDraw); mpFinish(mpBsDraw, end); }
  return ret;
}

// ---------------- real-time: Pong, Snake, Pac-Man, Bomberman ----------------
/* Both consoles compute the same game (rtgames.h); only the keys go over
   the radio, in lockstep (linkcore.h). A tick is taken as soon as both
   inputs for it are there - a lost packet just makes it wait a moment.
   The games keep the OLED console's field; here it is drawn bigger.   */
union MpRt { RtPong pg; RtSnake sn; RtPac pm; RtBomb rb; };
static MpRt   *mpRt = NULL;                             // on the heap while a game runs
static uint8_t mpMe;                                    // our player number
static bool    mpStalled;
static uint8_t mpFace[2] = { 3, 3 };                    // Pac-Man: where each one looks

static void mpWaiting(const char *msg) {
  if (mpStalled && !msg) msg = "waiting...";
  if (msg) mpBanner(msg, C::YELLOW);
}

// Pong: the 128 x 48 court of rtgames.h, 2.5 times as wide and 4.2 times as high
#define MP_PGX(x) ((int16_t)((x) * 5 / 2))
#define MP_PGY(y) ((int16_t)(TOP_H + 2 + ((y) - RP_TOP) * 21 / 5))
static void mpPongDraw(const char *msg) {
  const RtPong &g = mpRt->pg;
  int16_t y0 = MP_PGY(RP_TOP), y1 = MP_PGY(RP_BOT);
  gfx.fill(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(0, 15, 40));
  gfx.gradient(0, y0, SCR_W, y1 - y0, rgb(0, 40, 90), rgb(0, 15, 40));
  gfx.hline(0, y0 - 1, SCR_W, C::LTGREY);
  gfx.hline(0, y1, SCR_W, C::LTGREY);
  for (int16_t y = y0 + 4; y < y1; y += 14) gfx.fill(SCR_W / 2 - 1, y, 3, 8, rgb(100, 140, 200));
  char b[4];
  snprintf(b, sizeof(b), "%u", g.pts[mpMe]);
  gfx.text(SCR_W / 2 - 50, y0 + 10, b, rgb(100, 180, 255), F_BIG);
  snprintf(b, sizeof(b), "%u", g.pts[1 - mpMe]);
  gfx.text(SCR_W / 2 + 30, y0 + 10, b, rgb(255, 130, 130), F_BIG);
  // we always play on the left: the other console sees it mirrored
  for (uint8_t p = 0; p < 2; p++)
    gfx.rbox(p == mpMe ? MP_PGX(2) : MP_PGX(123), MP_PGY(g.pad[p]), 8, RP_PH * 21 / 5, 3, p == mpMe ? C::CYAN : C::PINK);
  int16_t bx = g.bx >> 4;
  if (mpMe) bx = 126 - bx;
  if (!g.wait || (g.wait / 5) % 2) gfx.disc(MP_PGX(bx) + 2, MP_PGY(g.by >> 4) + 4, 4, C::YELLOW);   // blinks before a serve
  mpWaiting(msg);
  mpScoreBar("PONG", g.pts[mpMe], g.pts[1 - mpMe], C::BLUE);
  gfx.send();
}

// Snake: the 32 x 12 field with 10 px fields; ours green, theirs orange
#define MP_SNY (TOP_H + 48)
static void mpSnakeDraw(const char *msg) {
  const RtSnake &g = mpRt->sn;
  gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(10, 40, 10), C::BLACK);
  for (uint8_t y = 0; y < RS_H; y++)
    for (uint8_t x = 0; x < RS_W; x++)
      gfx.fill(x * 10, MP_SNY + y * 10, 10, 10, (x + y) & 1 ? rgb(34, 102, 34) : rgb(51, 128, 51));
  gfx.frame(0, MP_SNY - 1, SCR_W, RS_H * 10 + 2, C::LTGREY);
  for (uint8_t f = 0; f < RS_FOOD; f++) if (g.fx[f] < RS_W) snApple(g.fx[f] * 10, MP_SNY + g.fy[f] * 10);
  static const int8_t DX[5] = { 0, 0, 0, -1, 1 }, DY[5] = { 0, -1, 1, 0, 0 };
  for (uint8_t p = 0; p < 2; p++) {
    bool own = p == mpMe;
    col a = own ? rgb(153, 255, 51) : rgb(255, 170, 60), b = own ? rgb(102, 204, 0) : rgb(220, 110, 20);
    for (int16_t i = g.len[p] - 1; i >= 0; i--) {
      int16_t px = g.x[p][i] * 10, py = MP_SNY + g.y[p][i] * 10;
      if (i == 0) {
        gfx.rbox(px, py, 10, 10, 3, own ? C::LIME : C::ORANGE);
        int16_t ex = DX[g.dir[p]], ey = DY[g.dir[p]];     // eyes look ahead
        gfx.fill(px + 4 + ey * 2 + ex * 2, py + 4 + ex * 2 + ey * 2, 2, 2, C::BLACK);
        gfx.fill(px + 4 - ey * 2 + ex * 2, py + 4 - ex * 2 + ey * 2, 2, 2, C::BLACK);
      } else gfx.rbox(px + 1, py + 1, 8, 8, 2, i % 4 < 2 ? a : b);
    }
  }
  gfx.rbox(10, TOP_H + 12, 12, 12, 3, C::LIME);
  gfx.text(28, TOP_H + 10, "YOU", C::WHITE);
  gfx.rbox(SCR_W - 22, TOP_H + 12, 12, 12, 3, C::ORANGE);
  gfx.right(SCR_W - 28, TOP_H + 10, linkCore().opp.name, C::WHITE);
  gfx.center(MP_SNY + RS_H * 10 + 14, "arrows steer - the longer one", C::GREY, F_SMALL);
  gfx.center(MP_SNY + RS_H * 10 + 28, "wins when the other one crashes", C::GREY, F_SMALL);
  mpWaiting(msg);
  mpScoreBar("SNAKE", g.len[mpMe], g.len[1 - mpMe], C::GREEN);
  gfx.send();
}

// Pac-Man: the colour game's maze; ours yellow, theirs green
static void mpPacDraw(const char *msg) {
  const RtPac &g = mpRt->pm;
  gfx.fill(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(0, 0, 20));
  gfx.fill(0, PAC_Y0, SCR_W, RM_H * PAC_T, C::BLACK);
  pacMaze();
  for (uint8_t y = 0; y < RM_H; y++)
    for (uint8_t x = 0; x < RM_W; x++) {
      uint32_t bit = 1UL << x;
      int16_t sx = PAC_X0 + x * PAC_T, sy = PAC_Y0 + y * PAC_T;
      if (g.dots[y] & bit) gfx.fill(sx + 4, sy + 4, 2, 2, rgb(255, 200, 160));
      if ((g.power[y] & bit) && (g.t / 10) % 2 == 0) gfx.disc(sx + 5, sy + 5, 3, rgb(255, 200, 160));
    }
  for (uint8_t i = 0; i < RM_GH; i++) pacGhost(g, i, g.t);
  for (uint8_t p = 0; p < 2; p++) {
    const RmEnt &e = g.pac[p];
    if (e.dir) mpFace[p] = e.dir;
    if (!g.alive(p) || !(!g.freeze || (g.freeze / 6) % 2 == 0 || g.t < RM_FREEZE)) continue;
    int16_t x, y;
    pacPos(e, g.pacPeriod(), &x, &y);
    uint8_t open = e.dir ? (uint8_t)((g.t / 3) % 3) : 1;
    col c = p == mpMe ? C::YELLOW : C::LIME;
    pacMan(x, y, mpFace[p], open == 2 ? 1 : open, c);
    if (x > SCR_W - PAC_T) pacMan(x - RM_W * PAC_T, y, mpFace[p], open, c);
  }
  // above the maze: both with their lives; below: the dots left
  for (uint8_t k = 0; k < 2; k++) {
    uint8_t p = k ? 1 - mpMe : mpMe;
    int16_t x = k ? SCR_W / 2 + 8 : 8;
    gfx.text(x, TOP_H + 14, k ? linkCore().opp.name : "YOU", k ? C::LIME : C::YELLOW);
    for (uint8_t i = 0; i < g.lives[p]; i++) pacMan(x + 74 + i * 16, TOP_H + 16, 4, 1, k ? C::LIME : C::YELLOW);
  }
  char b[20];
  snprintf(b, sizeof(b), "DOTS %u", g.dotsLeft);
  gfx.text(8, PAC_Y0 + RM_H * PAC_T + 14, b, C::LTGREY, F_SMALL);
  if (g.fright) {
    gfx.text(SCR_W - 130, PAC_Y0 + RM_H * PAC_T + 14, "POWER", C::CYAN, F_SMALL);
    gfx.fill(SCR_W - 100, PAC_Y0 + RM_H * PAC_T + 15, (int16_t)(g.fright * 90 / 350), 6, C::BLUE);
  }
  if (!msg && !g.alive(mpMe)) msg = "no lives left";
  if (!msg && g.freeze && g.t < RM_FREEZE) msg = "READY!";
  mpWaiting(msg);
  mpScoreBar("PAC-MAN", g.score[mpMe], g.score[1 - mpMe], C::YELLOW);
  gfx.send();
}

// Bomberman: the colour game's field and HUD
static const char *mpRbWho[RB_MAXP];
static void mpBombDraw(const char *msg) {
  const RtBomb &g = mpRt->rb;
  bombDraw(g, NULL, millis());
  mpWaiting(msg);
  mpScoreBar("BOMBER", g.score[mpMe], g.score[1 - mpMe], C::ORANGE);
  gfx.send();
}

/* 0 = decided or the other one left, 1 = we gave up, 2 = broken */
static uint8_t mpRealtime() {
  LinkCore &L = linkCore();
  bool pong = L.game == LKG_PONG, pac = L.game == LKG_PAC, bomb = L.game == LKG_BOMB;
  mpRt = (MpRt *)malloc(sizeof(MpRt));
  if (!mpRt) { L.cancel(); mpNote("not enough memory", NULL); return 2; }
  mpMe = L.iStart ? 0 : 1;
  uint32_t seed = L.session() * 2654435761u;             // the same on both consoles
  if (pong) mpRt->pg.begin(seed);
  else if (pac) mpRt->pm.begin(seed, 2);
  else if (bomb) mpRt->rb.begin(seed, 2);
  else mpRt->sn.begin(seed);
  mpRbWho[mpMe] = "YOU"; mpRbWho[1 - mpMe] = L.opp.name;
  bombWho = mpRbWho; bombMe = mpMe;
  mpFace[0] = mpFace[1] = 3;
  void (*draw)(const char *) = pong ? mpPongDraw : (pac ? mpPacDraw : (bomb ? mpBombDraw : mpSnakeDraw));
  uint8_t  latch = 0;                                     // snake, pac-man: last direction pressed
  bool     bombKey = false;                               // bomberman: OK waits for the next input
  uint32_t next = millis(), lastStep = millis();
  const char *end = NULL;
  uint8_t ret = 0;
  mpStalled = false;
  btnClear();
  while (!end) {
    if (!poll()) { L.cancel(); ret = 1; break; }          // hold OK = give up
    if (L.state != LK_PLAYING) break;                     // the other one left
    if (btn(B_UP))    latch = 1;
    if (btn(B_DOWN))  latch = 2;
    if (btn(B_LEFT))  latch = 3;                          // the field is the same on both
    if (btn(B_RIGHT)) latch = 4;                          // consoles, not mirrored
    if (bomb && btnTap(B_OK)) bombKey = true;
    uint32_t now = millis();
    for (uint8_t k = 0; k < 3 && lkDue(now, next); k++) {
      uint8_t in[2];
      if (!L.syncStep(&in[mpMe], &in[1 - mpMe])) break;
      uint8_t ev;
      if (bomb) {
        uint8_t bev = mpRt->rb.step(in);
        if (bev & RB_EV_BOOM)  sfx((uint16_t)(90 + random(40)), 120);
        if (bev & RB_EV_BOMB)  sfx(500, 20);
        if (bev & RB_EV_POWER) sfx(1300, 60);
        if (bev & RB_EV_DEAD)  sfx(200, 250);
        ev = 0;
      } else ev = pong ? mpRt->pg.step(in) : (pac ? mpRt->pm.step(in) : mpRt->sn.step(in));
      uint8_t mine = pong ? (uint8_t)((btnHeld(B_UP) ? 1 : 0) | (btnHeld(B_DOWN) ? 2 : 0)) :
                     bomb ? (uint8_t)(bombHeld() | (bombKey ? 8 : 0)) : latch;
      bombKey = false;
      L.syncPut(mine, linkNow());
      next += RT_TICK_MS;
      lastStep = now;
      if (ev & RT_EV_HIT)   sfx(pac ? 1300 : 750, pac ? 60 : 30);
      if (ev & RT_EV_EAT)   sfx(pac ? (mpRt->pm.t % 2 ? 880 : 660) : 1100, pac ? 12 : 45);
      if (ev & RT_EV_POINT) sfx(300, 120);
    }
    if ((int32_t)(now - next) > 100) next = now;          // do not race to catch up
    mpStalled = now - lastStep > 400;
    uint8_t w = pong ? mpRt->pg.winner : (pac ? mpRt->pm.winner :
                (bomb ? (mpRt->rb.winner == RB_DRAW ? 3 : mpRt->rb.winner) : mpRt->sn.winner));
    if (w) { end = mpResult(w, mpMe); break; }
    draw(NULL);
  }
  if (end) { mpStalled = false; mpFinish(draw, end); }
  bombWho = BOMB_NAMES; bombMe = 0;
  free(mpRt);
  mpRt = NULL;
  return ret;
}

// ---------------- Chess ----------------
/* Whoever begins plays white; each console has its own side at the
   bottom. A chess move goes over as two moves: the square it leaves,
   then the square it goes to | the promotion << 6 (0 queen, 1 rook,
   2 bishop, 3 knight). The rules are game_chess.h's.                  */
#define MPC_SQ 26
#define MPC_X  8
#define MPC_Y  (TOP_H + 4)
#define MPC_PX (MPC_X + MPC_SQ * 8 + 8)                 // the panel on the right

struct MpChess {
  ChPos   pos;
  ChMove  moves[220];                                   // legal moves right now
  int     n;
  int8_t  sel, lastFrom, lastTo;                        // picked square, the last move
  uint8_t cr, cc;                                       // cursor on the board (as drawn)
  bool    black;                                        // we play black (the board turned)
  char    last[12];
};
static MpChess *mpCh = NULL;                            // on the heap while a game runs

static void mpChessEnc(const ChMove &m, uint8_t out[2]) {
  out[0] = m.from;
  out[1] = (uint8_t)(m.to | (m.promo ? (CH_Q - m.promo) << 6 : 0));
}
// the two bytes back to one of the legal moves; false = none fits
static bool mpChessDec(const ChMove *legal, int n, uint8_t a, uint8_t b, ChMove *out) {
  uint8_t to = b & 63, promo = (uint8_t)(CH_Q - (b >> 6));
  if (a > 63) return false;
  for (int i = 0; i < n; i++)
    if (legal[i].from == a && legal[i].to == to && (legal[i].promo ? legal[i].promo == promo : !(b >> 6))) {
      *out = legal[i];
      return true;
    }
  return false;
}

static int  mpChSq(int r, int c) { return mpCh->black ? (7 - r) * 8 + (7 - c) : r * 8 + c; }
static void mpChXY(int sq, int16_t &x, int16_t &y) {
  int r = sq / 8, c = sq % 8;
  if (mpCh->black) { r = 7 - r; c = 7 - c; }
  x = (int16_t)(MPC_X + c * MPC_SQ); y = (int16_t)(MPC_Y + r * MPC_SQ);
}

// the pieces one side has lost, small, from what is left on the board
static void mpChLost(const ChPos &p, bool white, int16_t y) {
  static const uint8_t START[7] = { 0, 8, 2, 2, 2, 1, 1 };
  uint8_t cnt[7] = { 0 };
  for (int i = 0; i < 64; i++) if (p.b[i] && (p.b[i] > 0) == white) cnt[abs(p.b[i])]++;
  int k = 0;
  for (int kind = CH_Q; kind >= CH_P; kind--)
    for (int j = cnt[kind]; j < START[kind]; j++, k++)
      chPiece((int16_t)(MPC_PX + (k % 7) * 13), (int16_t)(y + (k / 7) * 13), (int8_t)(white ? kind : -kind), 1);
}

static void mpChDraw(const char *status, col statusCol, const char *msg) {
  MpChess &g = *mpCh;
  bool mine = (bool)g.pos.white != g.black;
  gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(60, 40, 20), rgb(20, 10, 0));
  gfx.fill(MPC_X - 3, MPC_Y - 3, MPC_SQ * 8 + 6, MPC_SQ * 8 + 6, rgb(90, 60, 30));
  int king = chInCheck(g.pos, g.pos.white) ? chKing(g.pos, g.pos.white) : -1;
  for (int sq = 0; sq < 64; sq++) {
    int16_t x, y;
    mpChXY(sq, x, y);
    bool light = ((sq / 8) + (sq % 8)) % 2 == 0;
    col c = light ? rgb(255, 204, 153) : rgb(153, 102, 51);
    if (sq == g.lastFrom || sq == g.lastTo) c = light ? rgb(230, 230, 120) : rgb(180, 180, 70);
    if (sq == g.sel) c = rgb(110, 200, 110);
    if (sq == king) c = rgb(230, 70, 70);
    gfx.fill(x, y, MPC_SQ, MPC_SQ, c);
    if (g.pos.b[sq]) chPiece(x, y, g.pos.b[sq]);
  }
  if (g.sel >= 0)                                       // where the picked piece can go
    for (int i = 0; i < g.n; i++)
      if (g.moves[i].from == g.sel) {
        int16_t x, y;
        mpChXY(g.moves[i].to, x, y);
        if (g.pos.b[g.moves[i].to]) gfx.frame(x + 1, y + 1, MPC_SQ - 2, MPC_SQ - 2, rgb(40, 140, 40), 3);
        else gfx.disc(x + MPC_SQ / 2, y + MPC_SQ / 2, 4, rgb(40, 140, 40));
      }
  if (mine && !msg) gfx.frame(MPC_X + g.cc * MPC_SQ, MPC_Y + g.cr * MPC_SQ, MPC_SQ, MPC_SQ, C::YELLOW, 3);
  for (int i = 0; i < 8; i++) {                         // files and ranks along the board
    char f[2] = { (char)('a' + (g.black ? 7 - i : i)), 0 }, r[2] = { (char)('8' - (g.black ? 7 - i : i)), 0 };
    gfx.text(MPC_X + i * MPC_SQ + 1, MPC_Y + MPC_SQ * 8 - 9, f, C::BLACK, F_SMALL);
    gfx.text(MPC_X + 1, MPC_Y + i * MPC_SQ + 1, r, C::BLACK, F_SMALL);
  }
  // the panel: the other one on top, we at the bottom, each with the pieces taken from the other
  for (uint8_t k = 0; k < 2; k++) {
    bool white = k ? !g.black : g.black;                 // k 0 = the other one, 1 = we
    bool act = (bool)g.pos.white == white && !msg;
    int16_t y = k ? SCR_H - 44 : MPC_Y;
    gfx.rbox(MPC_PX - 2, y, SCR_W - MPC_PX - 2, 40, 6, act ? colDark(C::BROWN) : rgb(45, 28, 12));
    if (act) gfx.rframe(MPC_PX - 2, y, SCR_W - MPC_PX - 2, 40, 6, C::YELLOW);
    gfx.disc(MPC_PX + 10, y + 20, 7, white ? C::WHITE : rgb(40, 40, 40));
    gfx.circle(MPC_PX + 10, y + 20, 7, C::GREY);
    gfx.text(MPC_PX + 22, y + 4, k ? "YOU" : linkCore().opp.name, C::WHITE, k ? F_TEXT : F_SMALL);
    gfx.text(MPC_PX + 22, y + 26, white ? "white" : "black", C::LTGREY, F_SMALL);
    mpChLost(g.pos, !white, k ? SCR_H - 86 : MPC_Y + 44);   // what this side has taken
  }
  if (status) gfx.text(MPC_PX, 118, status, statusCol, F_TEXT);
  if (g.last[0]) gfx.text(MPC_PX, 140, g.last, C::LTGREY, F_SMALL);
  if (msg) {
    int16_t w = Gfx::width(msg, F_BOLD) + 28, x = MPC_X + (MPC_SQ * 8 - w) / 2;
    gfx.rbox(x + 4, 120, w, 42, 10, C::BLACK);
    gfx.rbox(x, 116, w, 42, 10, rgb(80, 40, 10));
    gfx.rframe(x, 116, w, 42, 10, C::ORANGE);
    gfx.text(x + 14, 125, msg, C::WHITE, F_BOLD);
  }
  mpBar("CHESS", C::BROWN);
}

static const char *mpChWhy;                             // the reason under the result
static void mpChessDraw(const char *msg) { mpChDraw(mpChWhy, C::YELLOW, msg); gfx.send(); }

// the piece for a pawn on the last row: LEFT/RIGHT, OK; 0 = left the game
static uint8_t mpChPromo() {
  static const uint8_t P[4] = { CH_Q, CH_R, CH_B, CH_N };
  uint8_t k = 0;
  btnClear();
  while (poll(false)) {
    if (btn(B_LEFT))  { k = (uint8_t)((k + 3) % 4); sfx(700, 15); }
    if (btn(B_RIGHT)) { k = (uint8_t)((k + 1) % 4); sfx(700, 15); }
    if (btn(B_OK)) return P[k];
    panel(60, 80, 200, 90, "PROMOTE", C::BROWN);
    for (int i = 0; i < 4; i++) {
      int16_t x = 74 + i * 46, y = 120;
      gfx.fill(x, y, MPC_SQ, MPC_SQ, i == k ? rgb(110, 200, 110) : rgb(255, 204, 153));
      chPiece(x, y, (int8_t)(mpCh->black ? -P[i] : P[i]));
    }
    gfx.send();
  }
  return 0;
}

static void mpChPlay(const ChMove &m) {
  MpChess &g = *mpCh;
  snprintf(g.last, sizeof(g.last), "%c%c-%c%c", 'a' + m.from % 8, '8' - m.from / 8, 'a' + m.to % 8, '8' - m.to / 8);
  bool take = g.pos.b[m.to] || (abs(g.pos.b[m.from]) == CH_P && m.to == g.pos.ep);
  chPlay(g.pos, m);
  g.lastFrom = (int8_t)m.from; g.lastTo = (int8_t)m.to;
  g.sel = -1;
  g.n = chMoves(g.pos, g.moves);
  sfx(take ? 500 : 900, take ? 80 : 40);
}

static uint8_t mpChess() {
  LinkCore &L = linkCore();
  mpCh = (MpChess *)malloc(sizeof(MpChess));
  if (!mpCh) { L.cancel(); mpNote("not enough memory", NULL); return 2; }
  MpChess &g = *mpCh;
  memset(&g, 0, sizeof(g));
  chStart(g.pos);
  g.black = !L.iStart;
  g.sel = g.lastFrom = g.lastTo = -1;
  g.cr = 6; g.cc = 4;                                   // on our king's pawn (as drawn)
  g.n = chMoves(g.pos, g.moves);
  mpQn = 0;
  mpChWhy = NULL;
  int16_t first = -1;                                   // the first half of their move
  uint8_t ret = 0;
  const char *end = NULL;
  btnClear();
  while (!end) {
    if (!poll()) { L.cancel(); ret = 1; break; }        // hold OK = give up
    if (L.state != LK_PLAYING) break;                   // the other one left
    bool mine = (bool)g.pos.white != g.black;
    if (mine) {
      if (btn(B_UP)    && g.cr > 0) { g.cr--; sfx(500, 8); }
      if (btn(B_DOWN)  && g.cr < 7) { g.cr++; sfx(500, 8); }
      if (btn(B_LEFT)  && g.cc > 0) { g.cc--; sfx(500, 8); }
      if (btn(B_RIGHT) && g.cc < 7) { g.cc++; sfx(500, 8); }
      if (btn(B_OK)) {
        int sq = mpChSq(g.cr, g.cc), move = -1;
        if (g.sel >= 0)
          for (int i = 0; i < g.n; i++)
            if (g.moves[i].from == g.sel && g.moves[i].to == sq) { move = i; break; }
        if (move >= 0) {
          ChMove m = g.moves[move];
          if (m.promo && !(m.promo = mpChPromo())) { L.cancel(); ret = 1; break; }
          uint8_t b[2];
          mpChessEnc(m, b);
          mpQPush(b[0]); mpQPush(b[1]);
          mpChPlay(m);
          btnClear();
        } else if (chSign(g.pos.b[sq]) == (g.pos.white ? 1 : -1)) {
          g.sel = g.sel == sq ? (int8_t)-1 : (int8_t)sq;   // pick, or put back
          sfx(1000, 20);
        } else if (g.sel >= 0) { g.sel = -1; sfx(200, 40); }
      }
    } else {
      uint8_t m;
      if (L.moveIn(&m)) {
        if (first < 0) first = m;
        else {
          ChMove mv;
          if (!mpChessDec(g.moves, g.n, (uint8_t)first, m, &mv)) { mpBroken(); ret = 2; break; }
          first = -1;
          mpChPlay(mv);
          btnClear();
        }
      }
    }
    bool check = chInCheck(g.pos, g.pos.white);
    mine = (bool)g.pos.white != g.black;
    if (!g.n) { end = check ? (mine ? "YOU LOST" : "YOU WIN") : "DRAW"; mpChWhy = check ? "checkmate" : "stalemate"; }
    else if (g.pos.half >= 100) { end = "DRAW"; mpChWhy = "50 moves"; }
    else if (chTooLittle(g.pos)) { end = "DRAW"; mpChWhy = "no mate"; }
    if (end) break;
    if (check) sfx(1300, 20);
    mpChDraw(check ? "CHECK!" : (mine ? "your go" : "their go"), check ? C::RED : C::WHITE, NULL);
    gfx.send();
  }
  if (end) { mpQFlush(mpChessDraw); mpFinish(mpChessDraw, end); }
  free(mpCh);
  mpCh = NULL;
  return ret;
}

// ---------------- the list ----------------
static void mpBars(int16_t x, int16_t y, int8_t rssi) {   // signal strength, bottom at y
  uint8_t n = rssi > -55 ? 4 : (rssi > -65 ? 3 : (rssi > -75 ? 2 : 1));
  for (uint8_t i = 0; i < 4; i++) {
    int16_t h = 4 + i * 3;
    gfx.fill(x + i * 5, y - h, 3, h, i < n ? C::LIME : C::DKGREY);
  }
}

// why a challenge or a game ended, from the state it was in before
static void mpEnded(LkState before) {
  LinkCore &L = linkCore();
  char b[32];
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

// the challenge screens: who, which game, the time left
static void mpChallenge(const char *title, const char *line, const char *keys, col accent) {
  LinkCore &L = linkCore();
  wlanTitle(title, accent);
  char b[32];
  snprintf(b, sizeof(b), "%ds", mpSecondsLeft());
  gfx.right(SCR_W - 12, 6, b, C::YELLOW, F_BOLD);
  snprintf(b, sizeof(b), "%.6s %.4s", L.opp.name, L.opp.code);
  gfx.center(44, b, C::WHITE, F_BOLD);
  gfx.center(76, line, C::LTGREY);
  uint8_t g = L.game >= LKG_C4 && L.game <= MP_GAMES_N ? L.game - 1 : 0;
  gfx.rbox(SCR_W / 2 - 34, 100, 68, 68, 10, colDark(accent, 2));
  gfx.sprite(SCR_W / 2 - 24, 110, MP_ICON[g], 12, ICON_KEYS, ICON_PAL, 4);
  gfx.center(174, mpGameName(L.game), C::YELLOW, F_BOLD);
  int32_t ms = (int32_t)(L.deadline - linkNow());
  bar(30, 202, SCR_W - 60, (uint8_t)(ms > 0 ? ms * 100 / LK_INVITE_MS : 0), accent);
  gfx.center(SCR_H - 16, keys, C::LIME, F_SMALL);
  gfx.send();
}

void onlineRun() {
  if (runClock < 80) { wlanNeedsClock(); return; }     // the radio needs 80 MHz+
  phoneLinkOff();                                       // the radio is ours now, not the WLAN's
  if (!linkOpen()) { mpNote("radio did not start", NULL); return; }
  LinkCore &L = linkCore();
  L.caps = LK_CAPS | LK_CAP_CHESS;                      // what this console plays
  pollHook = mpHook;
  mpQn = 0;
  char selCode[5] = "";                                 // "" = own row
  uint8_t  top = 0;
  uint32_t nextBeep = 0;
  LkState  before = LK_LOBBY;
  uint8_t  gameEnd = 0;                                 // what the game reported
  btnClear();
  for (;;) {
    bool alive = poll();
    LkState st = L.state;

    if (st == LK_OVER) {                                // challenge or game is over
      if (gameEnd == 1) mpNote("you left the game", NULL);
      else if (gameEnd == 0) mpEnded(before);
      gameEnd = 0;
      mpQn = 0;
      L.toLobby();
      before = LK_LOBBY;
      btnClear();
      continue;
    }
    before = st;

    if (st == LK_PLAYING) {
      mpQn = 0;
      mpVs();
      uint8_t g = L.game;
      gameEnd = LinkCore::realtime(g) ? mpRealtime() : g == LKG_C4 ? mpC4() : g == LKG_TTT ? mpTTT() :
                g == LKG_DAME ? mpCheckers() : g == LKG_MAU ? mpMauMau() : g == LKG_CHESS ? mpChess() : mpBattle();
      barRight = NULL;
      btnClear();
      continue;
    }

    if (st == LK_INVITED) {                             // somebody challenges us
      if (L.game < LKG_C4 || L.game > MP_GAMES_N) { L.answer(false, linkNow()); continue; }
      if (millis() >= nextBeep) { nextBeep = millis() + 2000; sfx(1500, 90); }
      if (btn(B_OK))                             { L.answer(true, linkNow()); sfx(1200, 60); btnClear(); continue; }
      if (btn(B_LEFT) || btn(B_RIGHT) || !alive) { L.answer(false, linkNow()); sfx(300, 80); btnClear(); continue; }
      char k[40];
      snprintf(k, sizeof(k), "%s = play   LEFT = no", okKeys());
      mpChallenge("CHALLENGE!", "wants to play", k, C::ORANGE);
      continue;
    }

    if (st == LK_INVITING) {                            // waiting for an answer
      if (btn(B_LEFT) || !alive) { L.cancel(); btnClear(); continue; }
      mpChallenge("CHALLENGE", "waiting for an answer", "LEFT = take it back", MP_ACCENT);
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
      else if (L.peer(idx[sel - 1]).busy) { char b[32]; snprintf(b, sizeof(b), "%.6s is busy", L.peer(idx[sel - 1]).name); mpNote(b, NULL); }
      else {
        uint8_t g = chooseMode("CHALLENGE", MP_GAMES, MP_GAMES_N, MP_ACCENT);
        if (g != 255) {
          bool found = false;
          for (uint8_t i = 0; i < L.peerCount(); i++)   // the list may have changed meanwhile
            if (!strcmp(L.peer(i).code, selCode)) {
              found = true;
              uint8_t need = LinkCore::capOf(g + 1);
              char b[32], c[32];
              if (need && !(L.peer(i).caps & need)) {
                if (g + 1 == LKG_CHESS) { snprintf(b, sizeof(b), "%.6s has no Chess", L.peer(i).name); snprintf(c, sizeof(c), "(only colour consoles)"); }
                else { snprintf(b, sizeof(b), "%.6s needs an update", L.peer(i).name); snprintf(c, sizeof(c), "for %s", MP_GAMES[g]); }
                mpNote(b, c);
              } else if (!L.invite(i, g + 1, linkNow())) {
                snprintf(b, sizeof(b), "%.6s is busy", L.peer(i).name);
                mpNote(b, NULL);
              }
              break;
            }
          if (!found) mpNote("that console is gone", NULL);
        }
      }
      btnClear();
      continue;
    }

    wlanTitle("ONLINE", MP_ACCENT);
    batIcon(SCR_W - 30, 11);
    char b[40];
    snprintf(b, sizeof(b), "%u nearby", n);
    gfx.right(SCR_W - 40, 12, b, C::LTGREY, F_SMALL);
    const uint8_t ROWS = 6;
    if (sel < top) top = sel;
    if (sel >= top + ROWS) top = sel - ROWS + 1;
    for (uint8_t r = 0; r < ROWS; r++) {
      uint8_t row = top + r;
      int16_t y = 40 + r * 28;
      if (row > n) {
        if (!n && row == 1) {
          static const char *const DOTS[4] = { "searching", "searching.", "searching..", "searching..." };
          gfx.text(30, y + 4, DOTS[(millis() / 400) % 4], C::LTGREY);
          gfx.text(30, y + 32, "the other console opens", C::GREY, F_SMALL);
          gfx.text(30, y + 44, "Online too (or Multiplayer on", C::GREY, F_SMALL);
          gfx.text(30, y + 56, "an OLED console)", C::GREY, F_SMALL);
        }
        break;
      }
      bool on = row == sel;
      gfx.rbox(6, y, SCR_W - 12, 25, 5, on ? MP_ACCENT : colDark(MP_ACCENT, 3));
      if (on) gfx.rframe(6, y, SCR_W - 12, 25, 5, C::WHITE);
      if (row == 0) {
        gfx.text(30, y + 4, linkName(), C::WHITE);
        gfx.text(100, y + 4, L.code(), C::LTGREY);
        gfx.text(160, y + 4, "you", C::YELLOW);
        gfx.right(SCR_W - 14, y + 8, "OK = rename", on ? C::WHITE : C::GREY, F_SMALL);
        continue;
      }
      const LkPeer &p = L.peer(idx[row - 1]);
      if (linkIsFriend(p.code)) gfx.text(12, y + 4, "*", C::YELLOW);
      gfx.text(30, y + 4, p.name, C::WHITE);
      gfx.text(100, y + 4, p.code, C::LTGREY);
      gfx.text(160, y + 4, p.busy ? "busy" : "free", p.busy ? C::ORANGE : C::LIME);
      if (!(p.caps & LK_CAP_CHESS)) gfx.text(214, y + 8, "OLED", C::GREY, F_SMALL);
      mpBars(SCR_W - 34, y + 20, p.rssi);
    }
    const char *hint = sel ? "OK = challenge   RIGHT = friend   hold OK = back" : "OK = your name   hold OK = back";
    gfx.center(SCR_H - 16, hint, C::GREY, F_SMALL);
    gfx.send();
  }
  pollHook = NULL;
  linkClose();
}
