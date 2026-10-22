// MiniArcade Color: online play over the internet, through the own
// MiniArcade server - the one the TV uses, with its pairing (address, PIN,
// the console's token): a console paired for the TV can play at once.
// Part of console.cpp, which includes it after tv.h - not compiled on its own.
/* The lobby lists the consoles that are online on the server and the
   friends (added once by their 6 character code, online or not). OK on a
   free one challenges it, like nearby; the games are multiplayer.h's.
   What goes over the wire is inetcore.h's, on the TV's one HTTPS
   connection (tvnet.cpp). Real-time games get the input delay the server
   picks from both round trips (MiniArcadeServer/tvserver/net.py).     */
#include <new>
#include "inetcore.h"

#define IN_ACCENT rgb(40, 110, 230)
#define IN_BODY   1200                                  // the longest poll (16 packets)
#define IN_TITLE  "INTERNET"

enum : uint8_t { IW_NONE, IW_LOBBY, IW_INVITED, IW_INVITING, IW_CODE, IW_MYCODE, IW_MENU, IW_WAIT, IW_GAMES };
static uint8_t inWhere = IW_NONE;                       // which screen is up (for the host tests)

struct InState {
  NetLink n;
  char    body[IN_BODY];
  bool    req;                                          // a request of ours is with tvnet
};
static InState *inS = NULL;                             // on the heap while the page is open

// hands the requests to the TV's connection and the answers back
static void inPump() {
  NetLink &N = inS->n;
  uint32_t now = linkNow();
  if (inS->req) {
    TvReqState s = tvReqState();
    if (s == TVR_DONE || s == TVR_ERROR) {
      size_t n = 0;
      const char *b = (const char *)tvReqBody(&n);
      N.onAnswer(tvReqStatus(), b, n, now);
      tvReqClear();
      inS->req = false;
    }
  }
  if (!inS->req) {
    char path[96];
    if (N.next(path, sizeof(path), inS->body, sizeof(inS->body), now)) {
      if (tvReq(path, true, inS->body[0] ? inS->body : NULL)) inS->req = true;
      else N.onAnswer(0, "", 0, now);
    }
  }
  N.tick(now);
}
static void inHook() { netTick(); inPump(); mpQSend(); lastInput = millis(); }   // online: no sleep

static void inTitle(const char *t) { wlanTitle(t, IN_ACCENT); batIcon(SCR_W - 30, 11); }

// the server cannot be reached (for a few seconds now)
static bool inDown() {
  const NetLink &N = inS->n;
  return N.status != 200 && N.status != -1 && (!N.everOk || lkDue(linkNow(), N.lastOk + 3000));
}

// ---------------- the own code, a friend's code ----------------
static void inMyCode() {
  NetLink &N = inS->n;
  btnClear();
  while (poll()) {
    inWhere = IW_MYCODE;
    if (btn(B_OK)) { sfx(1200, 40); break; }
    inTitle(TR("MY CODE", "MEIN CODE"));
    gfx.center(48, TR("your friend code:", "dein Freundescode:"), C::WHITE);
    for (uint8_t i = 0; i < IN_CODE; i++) {
      int16_t x = (SCR_W - IN_CODE * 46) / 2 + i * 46 + 2;
      char c[2] = { N.me[0] ? N.me[i] : '-', 0 };
      gfx.rbox(x, 78, 42, 56, 6, colDark(IN_ACCENT, 2));
      gfx.rframe(x, 78, 42, 56, 6, IN_ACCENT);
      gfx.text(x + (42 - Gfx::width(c, F_BIG)) / 2, 88, c, C::YELLOW, F_BIG);
    }
    gfx.center(150, TR("friends add you with it,", "damit fügen dich Freunde hinzu,"), C::LTGREY);
    gfx.center(172, TR("then they see you online", "dann sehen sie dich online"), C::LTGREY);
    gfx.center(SCR_H - 16, TR("OK = back", "OK = zurück"), C::GREY, F_SMALL);
    gfx.send();
  }
  btnClear();
}

/* UP/DOWN letter, LEFT/RIGHT place, OK = done when all six are there.
   The letters are the server's: no 0/O, 1/I/L to mix up.              */
static char    inCode[IN_CODE + 1];                     // as typed (the host tests look at it)
static uint8_t inCodePos = 0;
static bool inCodeEntry(char *out) {
  static const char CH[] = "-23456789ABCDEFGHJKMNPQRSTUVWXYZ";
  const uint8_t NCH = sizeof(CH) - 1;
  memset(inCode, '-', IN_CODE);
  inCode[IN_CODE] = 0;
  inCodePos = 0;
  btnClear();
  while (poll()) {
    inWhere = IW_CODE;
    const char *f = strchr(CH, inCode[inCodePos]);
    uint8_t k = f ? (uint8_t)(f - CH) : 0;
    if (btn(B_UP))                              { inCode[inCodePos] = CH[(k + 1) % NCH];       sfx(700, 15); }
    if (btn(B_DOWN))                            { inCode[inCodePos] = CH[(k + NCH - 1) % NCH]; sfx(700, 15); }
    if (btn(B_LEFT)  && inCodePos)              { inCodePos--; sfx(600, 15); }
    if (btn(B_RIGHT) && inCodePos < IN_CODE - 1) { inCodePos++; sfx(600, 15); }
    if (btn(B_OK)) {
      if (strchr(inCode, '-')) { sfx(200, 80); continue; }
      strcpy(out, inCode);
      sfx(1200, 50);
      btnClear();
      return true;
    }
    inTitle(TR("FRIEND'S CODE", "CODE DES FREUNDES"));
    gfx.center(44, TR("the code your friend sees", "der Code, den dein Freund"), C::WHITE);
    gfx.center(64, TR("under OK on the own row", "in seiner Zeile unter OK sieht"), C::LTGREY, F_SMALL);
    for (uint8_t i = 0; i < IN_CODE; i++) {
      char c[2] = { inCode[i], 0 };
      int16_t x = (SCR_W - IN_CODE * 46) / 2 + i * 46 + 2;
      bool on = i == inCodePos;
      gfx.rbox(x, 92, 42, 56, 6, on ? IN_ACCENT : colDark(IN_ACCENT, 3));
      gfx.rframe(x, 92, 42, 56, 6, on ? C::WHITE : IN_ACCENT);
      gfx.text(x + (42 - Gfx::width(c, F_BIG)) / 2, 102, c, C::WHITE, F_BIG);
      if (on) {
        gfx.triangle(x + 21, 82, x + 15, 88, x + 27, 88, C::WHITE);
        gfx.triangle(x + 21, 158, x + 15, 152, x + 27, 152, C::WHITE);
      }
    }
    gfx.center(170, TR("UP / DOWN letter, LEFT / RIGHT place", "UP / DOWN Zeichen, LEFT / RIGHT Stelle"), C::LTGREY, F_SMALL);
    char b[48];
    snprintf(b, sizeof(b), TR("%s = add   hold = back", "%s = hinzufügen   halten = zurück"), okKeys());
    gfx.center(194, b, C::LIME, F_SMALL);
    gfx.send();
  }
  btnClear();
  return false;
}

// a friend added or removed: waits for the server, says how it went
static void inFriend(const char *code, bool on) {
  NetLink &N = inS->n;
  N.friendSet(code, on);
  uint32_t t0 = millis();
  while (N.frRes == INF_SENDING && millis() - t0 < 15000 && poll(false)) {
    inWhere = IW_WAIT;
    inTitle(IN_TITLE);
    gfx.center(110, TR("asking the server ...", "frage den Server ..."), C::WHITE);
    gfx.send();
  }
  char b[40];
  const char *l1 = b, *l2 = NULL;
  switch (N.frRes) {
  case INF_ADDED:   snprintf(b, sizeof(b), TR("%.6s is your friend now", "%.6s ist jetzt dein Freund"), N.frName); break;
  case INF_REMOVED: snprintf(b, sizeof(b), TR("%.6s is no friend any more", "%.6s ist kein Freund mehr"), N.frName); break;
  case INF_UNKNOWN: l1 = TR("no console has this code", "keine Konsole hat diesen Code"); l2 = code; break;
  case INF_SELF:    l1 = TR("that is your own code", "das ist dein eigener Code"); break;
  case INF_FULL:    l1 = TR("the friends list is full", "Freundesliste voll"); l2 = TR("(16 friends)", "(16 Freunde)"); break;
  default:          l1 = TR("server unreachable", "Server nicht erreichbar"); break;
  }
  N.frRes = INF_NONE;
  mpNote(l1, l2, IN_TITLE);
}

// the own row's menu: the code, a friend by code, the name, the game we would like
static void inMenu() {
  NetLink &N = inS->n;
  for (;;) {
    char o0[32], o2[32], o3[40];
    snprintf(o0, sizeof(o0), TR("my code  %s", "mein Code  %s"), N.me[0] ? N.me : "...");
    snprintf(o2, sizeof(o2), TR("my name  %s", "mein Name  %s"), linkName());
    snprintf(o3, sizeof(o3), TR("I'd like: %s", "Lieber: %s"), N.want ? mpGameName(N.want) : TR("any game", "egal"));
    const char *const OPT[4] = { o0, TR("add a friend by code", "Freund per Code"), o2, o3 };
    inWhere = IW_MENU;
    uint8_t k = chooseMode(IN_TITLE, OPT, 4, IN_ACCENT);
    if (k == 0) inMyCode();
    else if (k == 1) { char c[IN_CODE + 1]; if (inCodeEntry(c)) inFriend(c, true); }
    else if (k == 2) { mpName(); linkCore().setName(linkName()); }
    else if (k == 3) N.want = (uint8_t)((N.want + 1) % (MP_GAMES_N + 1));   // the next one, round
    if (k != 3) break;
  }
  btnClear();
}

// ---------------- the lobby ----------------
struct InRow { InPeer p; };
// online friends, the others online, then the friends that are not
static uint8_t inRows(InRow *rows, uint8_t max) {
  NetLink &N = inS->n;
  uint8_t n = 0;
  for (uint8_t pass = 0; pass < 2; pass++)
    for (uint8_t i = 0; i < N.nLob && n < max; i++)
      if (N.lob[i].friendly == (pass == 0)) { rows[n].p = N.lob[i]; rows[n].p.online = true; n++; }
  for (uint8_t i = 0; i < N.nFr && n < max; i++)
    if (N.find(N.lob, N.nLob, N.fr[i].code) < 0) { rows[n].p = N.fr[i]; rows[n].p.friendly = true; rows[n].p.online = false; n++; }
  return n;
}

static void inOutcome() {                               // how our challenge ended
  NetLink &N = inS->n;
  char b[40];
  const char *n = N.outTo.name, *l1 = b;
  switch (N.out) {
  case INO_NO:      snprintf(b, sizeof(b), TR("%.6s said no", "%.6s sagt nein"), n); break;
  case INO_BUSY:    snprintf(b, sizeof(b), TR("%.6s is busy", "%.6s spielt gerade"), n); break;
  case INO_GONE:    snprintf(b, sizeof(b), TR("%.6s is gone", "%.6s ist weg"), n); break;
  case INO_TIMEOUT: snprintf(b, sizeof(b), TR("no answer from %.6s", "keine Antwort von %.6s"), n); break;
  case INO_OFFLINE: snprintf(b, sizeof(b), TR("%.6s is offline", "%.6s ist offline"), n); break;
  case INO_UNKNOWN: l1 = TR("that console is gone", "Konsole unbekannt"); break;
  default:          l1 = TR("server unreachable", "Server nicht erreichbar"); break;
  }
  N.out = INO_NONE;
  mpNote(l1, NULL, IN_TITLE);
}

/* The page with the server: 0 = left, 1 = the server forgot us (pair again) */
static char    inSelCode[IN_CODE + 1];                  // the row picked ("" = own row)
static uint8_t inLobby() {
  NetLink &N = inS->n;
  LinkCore &L = linkCore();
  uint8_t top = 0, gameEnd = 0, ret = 0;
  uint32_t nextBeep = 0;
  inSelCode[0] = 0;
  btnClear();
  for (;;) {
    N.lists = inWhere == IW_LOBBY;                      // the polls bring the lists while they are shown
    bool alive = poll();
    uint32_t now = linkNow();
    if (N.unpaired) { mpNote(TR("not paired any more", "nicht gekoppelt"), TR("pair again with the PIN", "neu mit der PIN koppeln"), IN_TITLE); ret = 1; break; }
    LkState st = L.state;
    if (st == LK_OVER) {                                // the game is over: why
      if (gameEnd == 1) mpNote(TR("you left the game", "Spiel verlassen"), NULL, IN_TITLE);
      else if (gameEnd == 0) mpEnded(LK_PLAYING, IN_TITLE);
      gameEnd = 0;
      mpQn = 0;
      L.toLobby();
      btnClear();
      continue;
    }
    if (st == LK_PLAYING) { inWhere = IW_NONE; N.lists = false; gameEnd = mpGame(); continue; }

    if (N.inv && !N.sid) {                              // somebody challenges us
      inWhere = IW_INVITED;
      if (N.invGame < LKG_C4 || N.invGame > MP_GAMES_N) { N.answer(false); continue; }   // a game we lack
      if (millis() >= nextBeep) { nextBeep = millis() + 2000; sfx(1500, 90); }
      if (!N.busy()) {
        if (btn(B_OK))                             { N.answer(true); sfx(1200, 60); btnClear(); }
        else if (btn(B_LEFT) || btn(B_RIGHT) || !alive) { N.answer(false); sfx(300, 80); btnClear(); }
      }
      char who[24], k[48];
      snprintf(who, sizeof(who), "%.6s %.6s", N.invFrom.name, N.invFrom.code);
      snprintf(k, sizeof(k), TR("%s = play   LEFT = no", "%s = spielen   LEFT = nein"), okKeys());
      mpChallengeOf(TR("CHALLENGE!", "HERAUSFORDERUNG!"), who, N.invGame, (int32_t)(N.invUntil - now),
                    N.busy() ? TR("one moment ...", "einen Moment ...") : TR("wants to play", "will spielen"), k, C::ORANGE);
      continue;
    }
    if (N.invGone) { N.invGone = false; mpNote(TR("too late, it was taken back", "zu spät, zurückgezogen"), NULL, IN_TITLE); continue; }
    if (N.out == INO_SENDING || N.out == INO_WAIT) {    // waiting for an answer
      inWhere = IW_INVITING;
      if (btn(B_LEFT) || !alive) { N.takeBack(); btnClear(); continue; }
      char who[24];
      snprintf(who, sizeof(who), "%.6s %.6s", N.outTo.name, N.outTo.code);
      mpChallengeOf(TR("CHALLENGE", "HERAUSFORDERUNG"), who, N.outGame, (int32_t)(N.outUntil - now),
                    TR("waiting for an answer", "wartet auf Antwort"), TR("LEFT = take it back", "LEFT = zurücknehmen"), IN_ACCENT);
      continue;
    }
    if (N.out != INO_NONE) { inOutcome(); continue; }

    // ---- the list ----
    if (!alive) break;                                  // hold OK = back to the games
    inWhere = IW_LOBBY;
    InRow rows[IN_LIST * 2];
    bool down = inDown();
    uint8_t n = down ? 0 : inRows(rows, IN_LIST * 2);   // unreachable: only the own row
    int8_t sel = 0;                                     // 0 = own row, 1.. = the others
    for (uint8_t i = 0; i < n; i++) if (inSelCode[0] && !strcmp(rows[i].p.code, inSelCode)) sel = i + 1;
    if (btn(B_UP)   && sel > 0) { sel--; sfx(700, 15); }
    if (btn(B_DOWN) && sel < n) { sel++; sfx(700, 15); }
    if (sel) strcpy(inSelCode, rows[sel - 1].p.code); else if (!down) inSelCode[0] = 0;

    if (sel && btn(B_RIGHT)) { InPeer p = rows[sel - 1].p; sfx(900, 30); inFriend(p.code, !p.friendly); btnClear(); continue; }
    if (btn(B_OK)) {
      sfx(1200, 50);
      if (!sel) inMenu();
      else {
        InPeer p = rows[sel - 1].p;
        char b[32];
        if (!p.online) { snprintf(b, sizeof(b), TR("%.6s is offline", "%.6s ist offline"), p.name); mpNote(b, NULL, IN_TITLE); }
        else if (p.busy) { snprintf(b, sizeof(b), TR("%.6s is busy", "%.6s spielt gerade"), p.name); mpNote(b, NULL, IN_TITLE); }
        else {
          inWhere = IW_GAMES;
          uint8_t g = chooseMode(TR("CHALLENGE", "HERAUSFORDERUNG"), MP_GAMES, MP_GAMES_N, IN_ACCENT);
          if (g != 255 && !N.invite(p, g + 1, linkNow())) mpNote(TR("one moment please", "Moment bitte"), NULL, IN_TITLE);
        }
      }
      btnClear();
      continue;
    }

    inTitle(IN_TITLE);
    char b[48];
    if (N.everOk && !down) {
      snprintf(b, sizeof(b), "%u online  %u ms", N.nLob, N.rtt);
      gfx.right(SCR_W - 40, 12, b, C::LTGREY, F_SMALL);
    }
    const uint8_t ROWS = 6;
    if (sel < top) top = sel;
    if (sel >= top + ROWS) top = sel - ROWS + 1;
    for (uint8_t r = 0; r < ROWS; r++) {
      uint8_t row = top + r;
      int16_t y = 40 + r * 28;
      if (row > n) {
        if (row == 1) {
          const char *l1, *l2;
          col c = C::LTGREY;
          if (down) { l1 = TR("server unreachable", "Server nicht erreichbar"); l2 = TR("trying again ...", "versuche es weiter ..."); c = C::ORANGE; }
          else if (!N.everOk) { l1 = TR("connecting ...", "verbinde ..."); l2 = NULL; }
          else { l1 = TR("nobody online right now", "gerade niemand online"); l2 = TR("friends: OK on your own row", "Freunde: OK in deiner Zeile"); }
          gfx.text(30, y + 4, l1, c);
          if (l2) gfx.text(30, y + 32, l2, C::GREY, F_SMALL);
          if (down) {
            const char *h = strstr(tvUrl, "://");
            gfx.text(30, y + 48, h ? h + 3 : tvUrl, C::GREY, F_SMALL);
          }
        }
        break;
      }
      bool on = row == sel;
      gfx.rbox(6, y, SCR_W - 12, 25, 5, on ? IN_ACCENT : colDark(IN_ACCENT, 3));
      if (on) gfx.rframe(6, y, SCR_W - 12, 25, 5, C::WHITE);
      if (row == 0) {
        gfx.text(30, y + 4, linkName(), C::WHITE);
        gfx.text(100, y + 4, N.me[0] ? N.me : "------", C::LTGREY);
        gfx.text(186, y + 4, TR("you", "du"), C::YELLOW);
        gfx.right(SCR_W - 14, y + 8, TR("OK = menu", "OK = Menü"), on ? C::WHITE : C::GREY, F_SMALL);
        continue;
      }
      const InPeer &p = rows[row - 1].p;
      if (p.friendly) gfx.text(12, y + 4, "*", C::YELLOW);
      gfx.text(30, y + 4, p.name, p.online ? C::WHITE : C::GREY);
      gfx.text(100, y + 4, p.code, C::LTGREY);
      const char *s = !p.online ? TR("offline", "offline") : (p.busy ? TR("busy", "spielt") : TR("free", "frei"));
      gfx.text(186, y + 4, s, !p.online ? C::GREY : (p.busy ? C::ORANGE : C::LIME));
      if (p.online && p.game >= LKG_C4 && p.game <= MP_GAMES_N) gfx.right(SCR_W - 12, y + 8, mpGameName(p.game), C::GREY, F_SMALL);
    }
    const char *hint = !sel ? TR("OK = code, friends, name   hold OK = back", "OK = Code, Freunde, Name   halten = zurück")
                     : rows[sel - 1].p.friendly ? TR("OK = challenge   RIGHT = no friend   hold = back", "OK = fordern   RIGHT = kein Freund   halten = zurück")
                     : TR("OK = challenge   RIGHT = friend   hold = back", "OK = fordern   RIGHT = Freund   halten = zurück");
    gfx.center(SCR_H - 16, hint, C::GREY, F_SMALL);
    gfx.send();
  }
  N.lists = false;
  return ret;
}

// ---------------- the page ----------------
void inetRun() {
  tvLoad();
  bool wasLinked = phoneLink;
  phoneLink = false;
  pollHook = tvHook;
  if (tvOnline(IN_TITLE)) {
    while (tvUrl[0] || tvTypeUrl()) {
      if (!tvNetStart(tvUrl, tvTok)) { mpNote(TR("not enough memory", "zu wenig Speicher"), NULL, IN_TITLE); break; }
      if (!tvTok[0]) {                                  // the TV's pairing: once for both
        if (!tvSay(IN_TITLE, TR("not paired with the server", "nicht mit dem Server gekoppelt"),
                   TR("pair it with the server PIN", "mit der Server-PIN koppeln"), TR("(the TV uses the same)", "(gilt auch für TV)"))) break;
        if (!tvPair()) break;
      }
      void *mem = malloc(sizeof(InState));
      if (!mem) { mpNote(TR("not enough memory", "zu wenig Speicher"), NULL, IN_TITLE); break; }
      inS = new (mem) InState();
      inS->req = false;
      static const uint8_t ME[6] = { 0x02, 'O', 'W', 'N', 0, 1 };   // linkcore's own address (made up)
      inS->n.begin(&linkCore(), ME, linkName(), linkNow(), (uint32_t)esp_random());
      pollHook = inHook;
      uint8_t r = inLobby();
      // goodbye: a game ends, our last packets out, the session left - a moment at most
      inS->n.leave(linkNow());
      for (uint32_t t0 = millis(); millis() - t0 < 2500 && !(inS->n.idle() && !inS->req); ) { inHook(); delay(10); }
      pollHook = tvHook;
      inS->~InState();
      free(inS);
      inS = NULL;
      linkCore().toLobby();
      tvNetStop();
      if (r != 1) break;
      tvForget();                                       // the server forgot us: pair again
    }
  }
  tvNetStop();
  inWhere = IW_NONE;
  pollHook = NULL;
  if (wasLinked || wlanStay) { phoneLink = true; pollHook = phoneHook; }
  else netStop();
}
