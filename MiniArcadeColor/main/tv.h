// MiniArcade Color: TV - YouTube through the owner's MiniArcade server
// (MiniArcadeServer: yt-dlp + ffmpeg in Docker). The server does all the
// work: lists, search, thumbnails already in the console's colours, and the
// video as tiles that are painted straight into the frame buffer
// (tvcodec.h). The console needs the WLAN, the server's address (phone page
// "Settings" or typed here) and is paired once with a PIN.
// Part of console.cpp, which includes it - not compiled on its own.
#include "tvcodec.h"
#include "tvnet.h"
#include "jsonlite.h"                // reading the server's answers

#define TV_ACCENT rgb(230, 40, 40)
static const char *const IC_TV[12] = {
  "...w....w...", "....w..w....", ".....ww.....", "rrrrrrrrrrrr", "rbbbbbbbbbbr", "rbbbwbbbbbbr",
  "rbbbwwbbbbbr", "rbbbwwwbbbbr", "rbbbwwbbbbbr", "rbbbwbbbbbbr", "rrrrrrrrrrrr", ".ss......ss." };

enum : uint8_t { TVW_NONE, TVW_KEYS, TVW_PIN, TVW_SAY, TVW_LIST, TVW_MENU, TVW_PLAYER };
static uint8_t tvWhere = TVW_NONE;                    // which screen is up (for the host tests)

// ---------------- saved: server address, tokens, the last searches ----------------
#ifndef CONFIG_ARCADE_DEFAULT_SERVER
#define CONFIG_ARCADE_DEFAULT_SERVER ""
#endif
static const char *tvDefaultUrl = CONFIG_ARCADE_DEFAULT_SERVER;   // until another one is set (the host tests change it)
static char tvUrl[100], tvTok[64];
static char tvNtok[64];                               // internet play only: got without the PIN (inet.h)
static char tvRecent[4][41];                          // the last searches, newest first
static bool tvLoaded = false;

static void tvLoad() {
  if (tvLoaded) return;
  tvLoaded = true;
  prefs.begin("tv", true);
  size_t n = prefs.getBytes("url", tvUrl, sizeof(tvUrl) - 1);
  tvUrl[n] = 0;
  n = prefs.getBytes("tok", tvTok, sizeof(tvTok) - 1);
  tvTok[n] = 0;
  n = prefs.getBytes("ntok", tvNtok, sizeof(tvNtok) - 1);
  tvNtok[n] = 0;
  if (prefs.getBytes("srch", tvRecent, sizeof(tvRecent)) != sizeof(tvRecent)) memset(tvRecent, 0, sizeof(tvRecent));
  for (auto &r : tvRecent) r[sizeof(r) - 1] = 0;
  prefs.end();
  if (!tvUrl[0] && strlen(tvDefaultUrl) < sizeof(tvUrl)) strcpy(tvUrl, tvDefaultUrl);   // none set: the built-in one
}

static void tvSave() {
  prefs.begin("tv", false);
  prefs.putBytes("url", tvUrl, strlen(tvUrl) + 1);
  prefs.putBytes("tok", tvTok, strlen(tvTok) + 1);
  prefs.putBytes("ntok", tvNtok, strlen(tvNtok) + 1);
  prefs.putBytes("srch", tvRecent, sizeof(tvRecent));
  prefs.end();
}
static void tvForget() { tvTok[0] = 0; tvSave(); }
static void tvForgetNet() { tvNtok[0] = 0; tvSave(); }
// the token a connection starts with: the TV's (PIN), else internet play's - pairing
// with it keeps the console the server knows (its friend code and friends)
static const char *tvStartTok() { return tvTok[0] ? tvTok : tvNtok; }

// the address from the phone page or the keyboard; NULL = taken. A new server forgets the pairing.
// Empty = the built-in one (CONFIG_ARCADE_DEFAULT_SERVER).
static const char *tvSetUrl(const char *v) {
  char u[sizeof(tvUrl)];
  size_t n = strlen(v);
  while (n && *v == ' ') { v++; n--; }
  while (n && (v[n - 1] == '/' || v[n - 1] == ' ')) n--;
  if (n >= sizeof(u)) return "address too long";
  memcpy(u, v, n);
  u[n] = 0;
  for (size_t i = 0; i < n; i++)
    if (u[i] <= ' ' || u[i] > '~' || u[i] == '"' || u[i] == '\\') return "no spaces or quotes in the address";
  if (n && strncmp(u, "https://", 8) && strncmp(u, "http://", 7)) return "the address starts with https://";
  if (n && n < 10) return "address too short";
  tvLoad();
  if (!n && strlen(tvDefaultUrl) < sizeof(u)) strcpy(u, tvDefaultUrl);
  if (strcmp(u, tvUrl)) { strcpy(tvUrl, u); tvTok[0] = 0; tvNtok[0] = 0; tvSave(); }
  return NULL;
}

// ---------------- requests ----------------
/* The lists ask in the background (the screen goes on): tvQ says what the
   running request is for. tvAsk() waits for its answer - it first takes
   over from a background request (its answer is thrown away).          */
enum : uint8_t { TVQ_NONE, TVQ_NOW, TVQ_LIST, TVQ_THUMB, TVQ_DROP };
static uint8_t tvQ = TVQ_NONE;
static int8_t  tvQSlot = -1;                          // TVQ_THUMB: the slot it fills
static char    tvThumbId[4][12];
static bool    tvWantList = false;

static void tvQDrop() {
  while (tvReqState() == TVR_BUSY) delay(5);
  if (tvQ == TVQ_THUMB && tvQSlot >= 0) tvThumbId[tvQSlot][0] = 0;
  if (tvQ == TVQ_LIST) tvWantList = true;
  if (tvQ != TVQ_NONE) tvReqClear();
  tvQ = TVQ_NONE;
}

// runs one request and waits, the screen stays; NULL = failed (*st = HTTP status,
// 0 = no connection) or left with hold OK (*st = -1)
static const char *tvAsk(const char *path, bool post = false, int *st = NULL) {
  int dummy;
  if (!st) st = &dummy;
  *st = -1;
  tvQDrop();
  if (!tvReq(path, post)) { *st = 0; return NULL; }
  uint32_t t0 = millis();
  for (;;) {
    TvReqState s = tvReqState();
    if (s == TVR_DONE || s == TVR_ERROR) break;
    if (!poll(false)) { tvQ = TVQ_DROP; return NULL; }       // the answer is thrown away later
    if (millis() - t0 > 400) {                               // a hint that something happens
      char dots[16];
      snprintf(dots, sizeof(dots), "%s%.*s", TR("loading", "lade"), (int)((millis() / 300) % 4), "...");
      gfx.fill(SCR_W / 2 - 50, SCR_H - 22, 100, 18, C::BLACK);
      gfx.center(SCR_H - 19, dots, C::LTGREY, F_SMALL);
      gfx.send();
    }
  }
  *st = tvReqStatus();
  bool ok = tvReqState() == TVR_DONE;
  tvReqClear();
  return ok ? (const char *)tvReqBody(NULL) : NULL;
}

// ---------------- screens ----------------
static void tvTitle(const char *t) { wlanTitle(t, TV_ACCENT); batIcon(SCR_W - 30, 11); }

// a message until OK (true) or hold OK (false)
static bool tvSay(const char *title, const char *l1, const char *l2, const char *l3 = NULL) {
  btnClear();
  while (poll()) {
    tvWhere = TVW_SAY;
    if (btn(B_OK)) { sfx(1200, 40); return true; }
    tvTitle(title);
    gfx.center(70, l1, C::WHITE);
    if (l2) gfx.center(100, l2, C::YELLOW);
    if (l3) gfx.center(130, l3, C::LTGREY, F_SMALL);
    gfx.center(200, TR("OK = go on    hold OK = back", "OK = weiter    OK halten = zurück"), C::GREY, F_SMALL);
    gfx.send();
  }
  return false;
}

static void tvHook() { netTick(); }

// joins the saved WLAN; false = none saved, or left (also for internet play: its title)
static bool tvOnline(const char *title = "TV") {
  if (netState() == NET_ONLINE) return true;
  if (!netHasConfig()) { tvSay(title, TR("no WLAN saved", "kein WLAN"), TR("Settings > WLAN", "Einstellungen > WLAN"),
                                   TR("set it up with the phone first", "erst mit dem Handy einrichten")); return false; }
  if (netState() != NET_CONNECTING) netConnect();
  btnClear();
  while (poll()) {
    NetState st = netState();
    if (st == NET_ONLINE) return true;
    tvTitle(title);
    char b[48];
    snprintf(b, sizeof(b), st == NET_FAILED ? TR("can't join %.20s", "%.20s geht nicht") : TR("joining %.24s ...", "verbinde %.24s ..."), netSsid());
    gfx.center(100, b, st == NET_FAILED ? C::ORANGE : C::WHITE);
    if (st == NET_FAILED) gfx.center(140, TR("OK = try again", "OK = nochmal"), C::LIME);
    if (st == NET_FAILED && btn(B_OK)) netConnect();
    gfx.center(200, TR("hold OK = back", "OK halten = zurück"), C::GREY, F_SMALL);
    gfx.send();
  }
  return false;
}

// ---------------- the keyboard ----------------
/* A key grid steered by the arrows, OK types, hold OK = cancel. The last
   row: for an address "https://", ".de", ".com", ".net", delete, done; for
   a search space, delete, done.                                       */
static const char *const TV_KB_ROWS[4] = { "abcdefghij", "klmnopqrst", "uvwxyz0123", "456789.-:/" };
static const char *const TV_KB_URL[6] = { "https://", ".de", ".com", ".net", "del", "done" };
static const char *const TV_KB_TXT[3] = { "space", "delete", "done" };
static uint8_t tvKbX = 0, tvKbY = 0;                  // the cursor (host tests steer by it)
static bool    tvKbUrl = false;

// the word keys as shown: they follow the language
static const char *tvKbLabel(const char *k) {
  if (!strcmp(k, "del"))    return TR("del", "entf");
  if (!strcmp(k, "done"))   return TR("done", "fertig");
  if (!strcmp(k, "space"))  return TR("space", "Leerz.");
  if (!strcmp(k, "delete")) return TR("delete", "löschen");
  return k;
}

static bool tvKeyboard(const char *title, char *text, uint8_t max, bool url, const char *hint) {
  const char *const *last = url ? TV_KB_URL : TV_KB_TXT;
  const uint8_t nLast = url ? 6 : 3;
  tvKbX = tvKbY = 0;
  tvKbUrl = url;
  btnClear();
  while (poll()) {
    tvWhere = TVW_KEYS;
    if (btn(B_UP))    { tvKbY = tvKbY ? tvKbY - 1 : 4; sfx(700, 10); }
    if (btn(B_DOWN))  { tvKbY = (tvKbY + 1) % 5;      sfx(700, 10); }
    uint8_t w = tvKbY == 4 ? nLast : 10;
    if (tvKbX >= w) tvKbX = w - 1;
    if (btn(B_LEFT))  { tvKbX = tvKbX ? tvKbX - 1 : w - 1; sfx(700, 10); }
    if (btn(B_RIGHT)) { tvKbX = (tvKbX + 1) % w;           sfx(700, 10); }
    if (btn(B_OK)) {
      size_t n = strlen(text);
      sfx(1100, 25);
      const char *add = NULL;
      char one[2] = { 0, 0 };
      if (tvKbY < 4) { one[0] = TV_KB_ROWS[tvKbY][tvKbX]; add = one; }
      else if (tvKbX == nLast - 1) return true;                       // done
      else if (tvKbX == nLast - 2) { if (n) text[n - 1] = 0; }        // delete
      else if (!url) add = n && text[n - 1] != ' ' ? " " : NULL;      // space
      else if (tvKbX == 0) { strcpy(text, "https://"); }              // starts anew
      else add = last[tvKbX];
      if (add && n + strlen(add) <= max) strcat(text, add);
    }
    tvTitle(title);
    gfx.rbox(8, 38, SCR_W - 16, 26, 5, C::BLACK);
    gfx.rframe(8, 38, SCR_W - 16, 26, 5, C::GREY);
    size_t n = strlen(text);
    const char *shown = n > 28 ? text + n - 28 : text;     // the end stays in view
    int16_t x = gfx.text(14, 42, shown, C::WHITE);
    if ((millis() / 400) % 2) gfx.fill(x + 1, 42, 2, 18, C::YELLOW);
    for (uint8_t r = 0; r < 4; r++)
      for (uint8_t k = 0; k < 10; k++) {
        int16_t kx = 12 + k * 30, ky = 72 + r * 28;
        bool on = r == tvKbY && k == tvKbX;
        gfx.rbox(kx, ky, 26, 24, 4, on ? TV_ACCENT : C::DKGREY);
        char ch[2] = { TV_KB_ROWS[r][k], 0 };
        gfx.text(kx + 8, ky + 3, ch, on ? C::WHITE : C::LTGREY);
      }
    int16_t kw = 296 / nLast;
    for (uint8_t k = 0; k < nLast; k++) {
      bool on = tvKbY == 4 && k == tvKbX;
      const char *lb = tvKbLabel(last[k]);
      Font f = Gfx::width(lb) <= kw - 6 ? F_TEXT : F_SMALL;
      gfx.rbox(12 + k * kw, 186, kw - 4, 24, 4, on ? TV_ACCENT : C::DKGREY);
      gfx.text(12 + k * kw + (kw - 4 - Gfx::width(lb, f)) / 2, f == F_TEXT ? 189 : 193, lb, on ? C::WHITE : C::LTGREY, f);
    }
    gfx.center(SCR_H - 14, hint, C::GREY, F_SMALL);
    gfx.send();
  }
  return false;
}

// the server's address, typed on the console; false = left with hold OK
static bool tvTypeUrl() {
  char t[sizeof(tvUrl)];
  strcpy(t, tvUrl[0] ? tvUrl : "https://");
  for (;;) {
    if (!tvKeyboard(TR("TV SERVER", "TV-SERVER"), t, sizeof(t) - 1, true,
                    TR("your MiniArcade server - hold OK = cancel", "dein MiniArcade-Server - OK halten = abbrechen"))) { btnClear(); return false; }
    const char *err = tvSetUrl(t);
    if (!err) { btnClear(); return true; }
    if (!tvSay(TR("TV SERVER", "TV-SERVER"), err, NULL)) return false;
  }
}

// the settings line and its OK (settings.h)
void tvSettingsLine(char *out, size_t n) {
  tvLoad();
  const char *h = strstr(tvUrl, "://");
  h = h ? h + 3 : tvUrl;
  if (!tvUrl[0]) snprintf(out, n, TR("tv server    not set", "TV-Server    fehlt"));
  else if (strlen(h) <= 8) snprintf(out, n, TR("tv server    %s %s", "TV-Server    %s %s"), h, tvTok[0] ? TR("paired", "gepaart") : "-");
  else snprintf(out, n, TR("tv server    %.6s.. %s", "TV-Server    %.6s.. %s"), h, tvTok[0] ? TR("paired", "gepaart") : "-");
}
void tvSettingsOk() { tvLoad(); tvTypeUrl(); }

// pairing: the server answers (a test), then its PIN on the console
static char tvPinRam[12];                             // a PIN typed on the phone page: used once, never saved

static bool tvPinEntry(char *pin) {
  static int8_t dig[8] = { 0, 0, 0, 0, 0, 0, -1, -1 };  // -1 = no digit (the PIN ends before)
  static uint8_t cur = 0;
  btnClear();
  while (poll()) {
    tvWhere = TVW_PIN;
    if (btn(B_UP))    { dig[cur] = dig[cur] >= 9 ? -1 : dig[cur] + 1; sfx(800, 10); }
    if (btn(B_DOWN))  { dig[cur] = dig[cur] <= -1 ? 9 : dig[cur] - 1; sfx(800, 10); }
    if (btn(B_LEFT))  { cur = cur ? cur - 1 : 7; sfx(700, 10); }
    if (btn(B_RIGHT)) { cur = (cur + 1) % 8;     sfx(700, 10); }
    uint8_t n = 0;
    while (n < 8 && dig[n] >= 0) n++;
    if (btn(B_OK)) {
      if (n < 4) { sfx(300, 120); continue; }
      for (uint8_t i = 0; i < n; i++) pin[i] = (char)('0' + dig[i]);
      pin[n] = 0;
      sfx(1200, 40);
      return true;
    }
    tvTitle(TR("SERVER PIN", "SERVER-PIN"));
    gfx.center(44, TR("PIN of your MiniArcade server", "PIN deines MiniArcade-Servers"), C::WHITE);
    for (uint8_t i = 0; i < 8; i++) {
      int16_t x = 24 + i * 35, y = 76;
      bool on = i == cur;
      gfx.rbox(x, y, 31, 46, 5, on ? TV_ACCENT : C::DKGREY);
      char d[2] = { dig[i] >= 0 ? (char)('0' + dig[i]) : '-', 0 };
      gfx.text(x + 6, y + 6, d, i < n ? C::WHITE : C::GREY, F_BIG);
    }
    gfx.triangle(24 + cur * 35 + 15, 128, 24 + cur * 35 + 9, 136, 24 + cur * 35 + 21, 136, C::YELLOW);
    gfx.center(150, TR("UP/DOWN digit, LEFT/RIGHT move", "UP/DOWN Ziffer, LEFT/RIGHT Stelle"), C::LTGREY, F_SMALL);
    gfx.center(166, TR("'-' ends a shorter PIN", "'-' beendet eine kürzere PIN"), C::LTGREY, F_SMALL);
    gfx.center(186, TR("OK = pair    hold OK = back", "OK = koppeln  halten = zurück"), C::LIME);
    gfx.center(212, TR("(CONSOLE_PIN, also on the server's phone page)", "(CONSOLE_PIN, auch auf der Handy-Seite des Servers)"), C::GREY, F_SMALL);
    gfx.send();
  }
  return false;
}

// does the server answer? Else: try again or another address; false = left
static bool tvReach() {
  char why[48];
  for (;;) {
    tvTitle(TR("TV SERVER", "TV-SERVER"));
    char b[60];
    snprintf(b, sizeof(b), "%.48s", tvUrl);
    gfx.center(80, TR("testing the connection ...", "teste die Verbindung ..."), C::WHITE);
    gfx.center(110, b, C::CYAN, F_SMALL);
    gfx.send();
    int st = 0;
    const char *r = tvAsk("/api/ping", false, &st);
    if (st < 0) return false;
    if (r && strstr(r, "\"ok\"")) return true;
    if (st) snprintf(why, sizeof(why), TR("the server said %d", "der Server sagt %d"), st);
    else snprintf(why, sizeof(why), TR("no answer", "keine Antwort"));
    btnClear();
    for (;;) {
      if (!poll()) return false;
      tvWhere = TVW_SAY;
      if (btn(B_OK)) break;
      if (btn(B_LEFT)) {
        if (!tvTypeUrl()) return false;
        tvNetStop();
        if (!tvNetStart(tvUrl, tvStartTok())) return false;
        break;
      }
      tvTitle(TR("TV SERVER", "TV-SERVER"));
      gfx.center(60, why, C::ORANGE, F_BOLD);
      gfx.center(100, b, C::CYAN, F_SMALL);
      gfx.center(130, TR("OK = try again", "OK = nochmal"), C::LIME);
      gfx.center(156, TR("LEFT = change the address", "LEFT = Adresse ändern"), C::LIME);
      gfx.center(204, TR("hold OK = back", "OK halten = zurück"), C::GREY, F_SMALL);
      gfx.send();
    }
  }
}

// the server's PIN typed, the console's own token back; false = left
static bool tvPinPair() {
  char path[120], pin[12];
  for (;;) {
    if (tvPinRam[0]) { strcpy(pin, tvPinRam); tvPinRam[0] = 0; }
    else if (!tvPinEntry(pin)) return false;
    int k = snprintf(path, sizeof(path), "/api/pair?code=%s&pin=%s&name=", tvConsoleCode(), pin);
    for (const char *c = linkName(); *c && k < (int)sizeof(path) - 2; c++) path[k++] = *c == ' ' ? '+' : *c;
    path[k] = 0;
    int st = 0;
    const char *r = tvAsk(path, true, &st);
    memset(pin, 0, sizeof(pin));
    if (st < 0) return false;
    char tok[sizeof(tvTok)] = "";
    if (r && jsStr(r, r + strlen(r), "token", tok, sizeof(tok)) && tok[0]) {
      strcpy(tvTok, tok);
      tvNtok[0] = 0;                                  // internet play's token: became this one
      tvSave();
      tvNetToken(tvTok);
      sfx(1400, 120);
      return true;
    }
    tvWhere = TVW_SAY;
    const char *title = TR("PAIRING", "KOPPELN");
    bool go = st == 403 ? tvSay(title, TR("wrong PIN", "falsche PIN"), NULL, TR("OK = try again", "OK = nochmal"))
            : st == 429 ? tvSay(title, TR("too many wrong PINs", "PIN zu oft falsch"), TR("wait a few minutes", "bitte kurz warten"))
            : tvSay(title, st ? TR("the server refused", "Server lehnt ab") : TR("no answer", "keine Antwort"), NULL);
    if (!go) return false;
  }
}

static bool tvPair() { return tvReach() && tvPinPair(); }

// ---------------- the player ----------------
// the server's reason (plain text, e.g. YouTube's pause) for the screen
static char tvErr[52];
static void tvErrText(const char *b, size_t n) {
  size_t k = 0;
  for (size_t i = 0; i < n && b[i] && k < sizeof(tvErr) - 1 && b[0] != '{' && b[0] != '<'; i++)
    if (b[i] >= ' ' && b[i] <= '~') tvErr[k++] = b[i];
  tvErr[k] = 0;
}

static void tvDur(char *b, size_t n, uint32_t s) {
  if (s >= 3600) snprintf(b, n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
  else snprintf(b, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

static bool tvReadBlock(void *, uint8_t *dst, uint32_t n) {
  uint32_t t0 = millis();
  uint16_t br = tvStreamBroken();
  while (n) {
    size_t k = tvStreamRead(dst, n);
    dst += k; n -= k;
    if (!n) break;
    if (tvStreamBroken() != br || millis() - t0 > 3000) return false;
    delay(1);
  }
  return true;
}

static bool tvSkip(uint32_t n) {
  uint8_t b[64];
  while (n) { uint32_t k = n < sizeof(b) ? n : sizeof(b); if (!tvReadBlock(NULL, b, k)) return false; n -= k; }
  return true;
}

#define TV_STRIP 14                                   // the bottom rows for text over the video
static uint8_t *tvStripSave = NULL;

// text over the video: drawn, sent, then the frame buffer is as before (skipped tiles need it)
static void tvSendWith(const char *left, const char *right) {
  bool any = left[0] || right[0];
  uint8_t *strip = gfx.buffer() + (SCR_H - TV_STRIP) * SCR_W;
  if (any) {
    memcpy(tvStripSave, strip, SCR_W * TV_STRIP);
    gfx.fill(0, SCR_H - TV_STRIP, SCR_W, TV_STRIP, C::BLACK);
    gfx.text(4, SCR_H - 11, left, C::WHITE, F_SMALL);
    gfx.right(SCR_W - 4, SCR_H - 11, right, C::LIME, F_SMALL);
  }
  gfx.send();
  if (any) memcpy(strip, tvStripSave, SCR_W * TV_STRIP);
}

struct TvPlay {                                       // what the player knows (host tests look at it)
  TvInfo   info;
  TvClock  clk;
  bool     gotInfo, have, pend, waitKey;
  int32_t  waitGen;                                   // after a key: frames of this gen are old
  uint32_t pendTs, lastTs, lastShow;
  uint32_t shownAll, lateAll, keys;
};
static TvPlay tvP;

// takes records from the stream until a frame is painted (tvP.pend) or nothing is there
static void tvTakeRecords() {
  TvPlay &P = tvP;
  while (!P.pend) {
    uint8_t h[6];
    if (tvStreamPeek(h, 6) < 6) return;
    uint32_t len = tvU32(h);
    size_t av = tvStreamAvail();
    if (len < 2 || len > 200000) { tvStreamRead(h, 4); tvSkip(av - 4); P.waitKey = true; return; }   // nonsense: drop all
    if (h[4] == 'I') {
      if (av < 4 + len) return;
      uint8_t b[80];
      uint32_t k = len < sizeof(b) ? len : sizeof(b);
      tvStreamRead(h, 4);
      if (!tvReadBlock(NULL, b, k) || !tvSkip(len - k)) return;
      TvInfo in;
      if (!tvParseInfo(b, k, in)) continue;
      if (!P.gotInfo || in.gen != P.info.gen) { P.clk.run = false; P.waitKey = true; }
      if (P.waitGen >= 0 && in.gen != (uint16_t)P.waitGen) P.waitGen = -1;
      P.info = in;
      P.gotInfo = true;
      if (in.state != TVS_PLAYING && in.state != TVS_PAUSED) return;
      continue;
    }
    if (h[4] != 'F') { tvStreamRead(h, 4); tvSkip(len); continue; }
    if (P.waitKey && !(h[5] & 1)) {                   // changes to a picture we do not have
      if (av < 4 + len && av < TV_RING / 2) return;
      tvStreamRead(h, 4);
      tvSkip(len);
      continue;
    }
    if (av < 4 + len && av < TV_RING - 1024) return;  // the whole frame first (a huge one: as it comes)
    tvStreamRead(h, 5);
    TvFrame f;
    if (!tvDecode(gfx.buffer(), len - 1, tvReadBlock, NULL, f)) { P.waitKey = true; continue; }
    P.waitKey = false;
    P.have = true;
    P.pendTs = f.ts;
    P.pend = P.waitGen < 0 || P.info.gen != (uint16_t)P.waitGen;    // old: painted, not shown
  }
}

// plays what the server says until it stops or hold OK; false = the pairing is gone
static bool tvPlayer(const char *title) {
  tvQDrop();
  tvStripSave = (uint8_t *)malloc(SCR_W * TV_STRIP);
  void (*keepOverlay)() = gfx.overlay;
  gfx.overlay = nullptr;                              // the fps corner would spoil skipped tiles
  gfx.clear();
  gfx.center(100, title, C::WHITE, Gfx::width(title) <= SCR_W ? F_TEXT : F_SMALL);   // a long title in the small font
  gfx.center(130, TR("loading ...", "lade ..."), C::LTGREY, F_SMALL);
  gfx.send();
  if (!tvStripSave || !tvStreamStart()) {
    free(tvStripSave); tvStripSave = NULL;
    gfx.overlay = keepOverlay;
    tvSay("TV", TR("not enough memory", "zu wenig Speicher"), NULL);
    return true;
  }
  TvPlay &P = tvP;
  memset(&P, 0, sizeof(P));
  P.info.state = TVS_PLAYING;
  P.waitKey = true;
  P.waitGen = -1;
  P.lastShow = millis();
  bool paired = true;
  uint32_t noteUntil = 0, secT0 = millis(), repT0 = millis(), lastText = 0;
  uint16_t shown = 0, late = 0, secShown = 0, fps = 0, broken = tvStreamBroken();
  uint32_t secBytes = tvStreamBytes(), kbs = 0;
  char note[16] = "";
  btnClear();
  for (;;) {
    bool alive = poll(false);
    tvWhere = TVW_PLAYER;
    lastInput = millis();                             // watching is not idling
    if (!alive) break;
    uint32_t now = millis();
    // keys
    const char *act = NULL;
    if (btn(B_OK))    { bool p = P.info.state == TVS_PAUSED; act = p ? "a=play" : "a=pause"; strcpy(note, p ? TR("play", "weiter") : TR("pause", "Pause")); }
    if (btn(B_LEFT))  { act = "a=seek&d=-10"; strcpy(note, "-10 s"); }
    if (btn(B_RIGHT)) { act = "a=seek&d=10";  strcpy(note, "+10 s"); }
    if (btn(B_UP))    { act = "a=mode&m=1";   strcpy(note, TR("sharp", "scharf")); }
    if (btn(B_DOWN))  { act = "a=mode&m=0";   strcpy(note, TR("fluent", "weich")); }
    if (act) { sfx(1000, 20); tvStreamAct(act); P.waitGen = P.info.gen; noteUntil = now + 2000; P.pend = false; P.keys++; }
    // a lost connection: what is in the ring is half a story
    if (tvStreamBroken() != broken) {
      broken = tvStreamBroken();
      tvStreamReset();
      P.waitKey = true; P.pend = false; P.clk.run = false;
    }
    if (tvStreamStatus() == 401) { paired = false; break; }
    tvTakeRecords();
    uint8_t st = P.info.state;
    if (P.gotInfo && st != TVS_PLAYING && st != TVS_PAUSED) break;
    // the text over the video
    char left[48] = "", right[32] = "", a[12], b[12];
    tvDur(a, sizeof(a), (st == TVS_PLAYING && P.have ? P.lastTs : P.info.pos) / 1000);
    tvDur(b, sizeof(b), P.info.dur / 1000);
    bool buffering = st == TVS_PLAYING && (P.waitGen >= 0 || !P.have || now - P.lastShow > 800);
    if (st == TVS_PAUSED) snprintf(left, sizeof(left), TR("PAUSED  %s / %s", "PAUSE  %s / %s"), a, b);
    else if (now < noteUntil) snprintf(left, sizeof(left), "%s  %s / %s", note, a, b);
    else if (buffering) snprintf(left, sizeof(left), TR("buffering ...", "puffere ..."));
    if (cfgFps) snprintf(right, sizeof(right), "%u fps %lu kB/s", fps, (unsigned long)kbs);
    // a frame when it is due
    bool sent = false;
    if (P.pend && st == TVS_PLAYING) {
      uint8_t d = tvDue(P.clk, now, P.pendTs);
      if (d == TV_DROP && now - P.lastShow > 300) d = TV_SHOW;    // never freeze completely
      if (d == TV_SHOW) {
        tvSendWith(left, right);
        sent = true; shown++; secShown++; P.shownAll++; P.lastShow = now; P.lastTs = P.pendTs; P.pend = false;
      } else if (d == TV_DROP) { late++; P.lateAll++; P.lastTs = P.pendTs; P.pend = false; }
    } else if (P.pend) P.pend = false;
    if (!sent && (left[0] || right[0]) && now - lastText > 250) { tvSendWith(left, right); lastText = now; }
    if (now - secT0 >= 1000) {
      fps = (uint16_t)(secShown * 1000 / (now - secT0));
      kbs = (tvStreamBytes() - secBytes) / (now - secT0);
      secShown = 0; secBytes = tvStreamBytes(); secT0 = now;
    }
    if (now - repT0 >= 500) {                         // for the server: how it goes
      tvStreamCount(shown, late, (uint16_t)(tvStreamAvail() / 1024));
      shown = late = 0;
      repT0 = now;
    }
  }
  tvStreamStop();
  free(tvStripSave);
  tvStripSave = NULL;
  gfx.overlay = keepOverlay;
  if (paired && P.info.state == TVS_ERROR) tvSay("TV", TR("can't play this", "nicht spielbar"), P.info.title);
  if (paired) tvAsk("/api/tv/ctl?a=stop");
  btnClear();
  return paired;
}

// ---------------- the lists ----------------
#define TV_ITEMS 20
#define TV_ROWS   4
#define TV_THUMBS 4
#define TV_TW 80
#define TV_TH 45
struct TvItem { char id[12]; char t[41]; char c[25]; uint16_t d; uint8_t f; };
static TvItem   tvItems[TV_ITEMS];
static uint8_t  tvN = 0;
static uint8_t tvRecentN() { uint8_t n = 0; while (n < 4 && tvRecent[n][0]) n++; return n; }
static void tvRemember(const char *q) {
  uint8_t k = 3;
  for (uint8_t i = 0; i < 4; i++) if (!strcmp(tvRecent[i], q)) k = i;
  for (; k > 0; k--) strcpy(tvRecent[k], tvRecent[k - 1]);
  snprintf(tvRecent[0], sizeof(tvRecent[0]), "%s", q);
  tvSave();
}
static uint8_t *tvThumb = NULL;                       // TV_THUMBS pictures of 80 x 45
static uint8_t  tvThumbAge[TV_THUMBS];
static char     tvCats[6][14];
static uint8_t  tvCatN = 0;
static char     tvQuery[41] = "";
enum { TVT_SEARCH = 0, TVT_FAV, TVT_HIST, TVT_SETUP };   // after the categories

static uint8_t tvTabs() { return tvCatN + 4; }
static const char *tvTabName(uint8_t t) {
  const char *const X[4] = { TR("Search", "Suche"), TR("Favourites", "Favoriten"), TR("History", "Verlauf"), TR("Setup", "Einrichten") };
  return t < tvCatN ? tvCats[t] : X[t - tvCatN];
}

static void tvParseItems(const char *r) {
  tvN = 0;
  const char *e = r + strlen(r), *p = strstr(r, "\"items\"");
  if (!p) return;
  const char *end;
  for (const char *o; tvN < TV_ITEMS && (o = jsObj(p, e, &end)); p = end) {
    TvItem &it = tvItems[tvN];
    if (!jsStr(o, end, "id", it.id, sizeof(it.id)) || strlen(it.id) != 11) continue;
    if (!jsStr(o, end, "t", it.t, sizeof(it.t))) it.t[0] = 0;
    if (!jsStr(o, end, "c", it.c, sizeof(it.c))) it.c[0] = 0;
    long d = jsInt(o, end, "d", 0);
    it.d = (uint16_t)(d < 0 ? 0 : (d > 65535 ? 65535 : d));
    it.f = jsInt(o, end, "f", 0) ? 1 : 0;
    tvN++;
  }
}

static void tvParseCats(const char *r) {
  tvCatN = 0;
  const char *p = strstr(r, "\"cats\"");
  if (!p || !(p = strchr(p, '['))) return;
  while (tvCatN < 6 && (p = strchr(p, '"'))) {
    const char *q = strchr(p + 1, '"');
    if (!q) break;
    size_t n = (size_t)(q - p - 1);
    if (n > sizeof(tvCats[0]) - 1) n = sizeof(tvCats[0]) - 1;
    memcpy(tvCats[tvCatN], p + 1, n);
    tvCats[tvCatN++][n] = 0;
    for (p = q + 1; *p == ' '; p++) {}
    if (*p != ',') break;
  }
}

static int8_t tvThumbSlot(const char *id) {
  if (!tvThumb) return -1;
  for (uint8_t i = 0; i < TV_THUMBS; i++) if (!strcmp(tvThumbId[i], id)) return (int8_t)i;
  return -1;
}

static void tvThumbFree() {
  tvQDrop();
  free(tvThumb);
  tvThumb = NULL;
  memset(tvThumbId, 0, sizeof(tvThumbId));
}

// asks for one missing thumbnail of the rows in view; a slot not in view is reused
static bool tvThumbAsk(uint8_t first, uint8_t count) {
  if (!tvThumb) {
    tvThumb = (uint8_t *)malloc(TV_THUMBS * TV_TW * TV_TH);
    memset(tvThumbId, 0, sizeof(tvThumbId));
    if (!tvThumb) return false;
  }
  for (uint8_t r = 0; r < count && first + r < tvN; r++) {
    const char *id = tvItems[first + r].id;
    if (tvThumbSlot(id) >= 0) continue;
    int8_t slot = -1;
    for (uint8_t i = 0; i < TV_THUMBS; i++) {
      bool inView = false;
      for (uint8_t k = 0; k < count && first + k < tvN; k++) if (!strcmp(tvThumbId[i], tvItems[first + k].id)) inView = true;
      if (inView) continue;
      if (!tvThumbId[i][0]) { slot = (int8_t)i; break; }
      if (slot < 0 || tvThumbAge[i] > tvThumbAge[slot]) slot = (int8_t)i;
    }
    if (slot < 0) return false;
    char path[40];
    snprintf(path, sizeof(path), "/api/thumb/%s", id);
    if (!tvReq(path)) return false;
    strcpy(tvThumbId[slot], id);
    memset(tvThumb + slot * TV_TW * TV_TH, C::DKGREY, TV_TW * TV_TH);
    for (uint8_t i = 0; i < TV_THUMBS; i++) if (tvThumbAge[i] < 255) tvThumbAge[i]++;
    tvThumbAge[slot] = 0;
    tvQ = TVQ_THUMB;
    tvQSlot = slot;
    return true;
  }
  return false;
}

static void tvDrawThumb(int16_t x, int16_t y, const char *id) {
  int8_t s = tvThumbSlot(id);
  if (s < 0) { gfx.fill(x, y, TV_TW, TV_TH, C::DKGREY); return; }
  const uint8_t *p = tvThumb + s * TV_TW * TV_TH;
  uint8_t *fb = gfx.buffer();
  for (int16_t r = 0; r < TV_TH; r++) memcpy(fb + (y + r) * SCR_W + x, p + r * TV_TW, TV_TW);
}

// OK on a list entry: play, favourite, back
static uint8_t tvItemMenu(const TvItem &it) {
  const char *opts[3] = { TR("play", "abspielen"), it.f ? TR("favourite: remove", "Favorit: entfernen") : TR("favourite: add", "Favorit: dazu"),
                          TR("back", "zurück") };
  uint8_t sel = 0;
  gfx.dim(0, 0, SCR_W, SCR_H);
  btnClear();
  while (poll(false)) {
    tvWhere = TVW_MENU;
    if (btn(B_UP))   { sel = sel ? sel - 1 : 2; sfx(700, 15); }
    if (btn(B_DOWN)) { sel = (sel + 1) % 3;      sfx(700, 15); }
    if (btn(B_OK))   { sfx(1200, 40); return sel; }
    panel(40, 50, SCR_W - 80, 150, NULL, TV_ACCENT);
    char t[30];
    snprintf(t, sizeof(t), "%.28s", it.t);
    gfx.center(60, t, C::YELLOW, F_SMALL);
    for (uint8_t i = 0; i < 3; i++) {
      int16_t y = 84 + i * 34;
      gfx.rbox(52, y - 4, SCR_W - 104, 28, 5, i == sel ? TV_ACCENT : colDark(TV_ACCENT, 3));
      gfx.center(y + 1, opts[i], C::WHITE);
    }
    gfx.send();
  }
  return 2;
}

static void tvListPath(uint8_t tab, char *path, size_t max) {
  path[0] = 0;
  if (tab < tvCatN) snprintf(path, max, "/api/feed?c=%u", tab);
  else if (tab == tvCatN + TVT_FAV) snprintf(path, max, "/api/list?k=fav");
  else if (tab == tvCatN + TVT_HIST) snprintf(path, max, "/api/list?k=hist");
  else if (tab == tvCatN + TVT_SEARCH && tvQuery[0]) {
    size_t k = (size_t)snprintf(path, max, "/api/search?q=");
    for (const char *q = tvQuery; *q && k + 4 < max; q++) {
      char c = *q;
      bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_';
      if (c == ' ') path[k++] = '+';
      else if (plain) path[k++] = c;
      else k += (size_t)snprintf(path + k, max - k, "%%%02X", (uint8_t)c);
    }
    path[k] = 0;
  }
}

// the start page: categories, search, favourites, history, setup. false = pair again
static bool tvBrowse() {
  uint8_t tab = 0, sel = 0, top = 0;
  uint16_t curGen = 0;
  bool loadErr = false, first = true;
  uint32_t nextNow = 0;
  int st = 0;
  const char *r = tvAsk("/api/cats", false, &st);
  if (st < 0) return true;
  if (st == 401) return false;
  if (r) tvParseCats(r);
  if (!tvCatN) { strcpy(tvCats[0], TR("Popular", "Beliebt")); tvCatN = 1; }
  tvWantList = true;
  tvN = 0;
  btnClear();
  while (poll()) {
    tvWhere = TVW_LIST;
    uint32_t now = millis();
    bool search = tab == tvCatN + TVT_SEARCH, setup = tab == tvCatN + TVT_SETUP;
    const uint8_t rec = search ? 1 + tvRecentN() : 0;    // search: "new search" and the last ones first
    // an answer in the background
    TvReqState rs = tvReqState();
    if (tvQ != TVQ_NONE && (rs == TVR_DONE || rs == TVR_ERROR)) {
      size_t n = 0;
      const char *body = (const char *)tvReqBody(&n);
      uint8_t q = tvQ;
      tvQ = TVQ_NONE;
      if (tvReqStatus() == 401) { tvReqClear(); return false; }
      if (q == TVQ_LIST) {
        if (rs == TVR_DONE) tvParseItems(body);
        else { loadErr = true; tvErrText(body, n); }
      } else if (q == TVQ_THUMB) {
        if (rs == TVR_DONE && n == TV_TW * TV_TH && tvThumb) memcpy(tvThumb + tvQSlot * TV_TW * TV_TH, body, n);
      } else if (q == TVQ_NOW && rs == TVR_DONE) {
        const char *e = body + n;
        uint16_t g = (uint16_t)jsInt(body, e, "g", 0);
        uint8_t s = (uint8_t)jsInt(body, e, "s", 0);
        if (s == TVS_PLAYING && (first || g != curGen)) {      // started on the phone
          char t[41] = "";
          jsStr(body, e, "t", t, sizeof(t));
          tvReqClear();
          tvThumbFree();
          if (!tvPlayer(t)) return false;
          nextNow = millis() + 1500;
          if (tab == tvCatN + TVT_HIST) tvWantList = true;
          btnClear();
          continue;
        }
        curGen = g;
        first = false;
      }
      tvReqClear();
    }
    // the next background request: the list, what plays, a thumbnail
    if (tvQ == TVQ_NONE && tvReqState() == TVR_IDLE) {
      char path[140];
      if (tvWantList) {
        tvWantList = loadErr = false;
        tvN = 0;
        tvListPath(tab, path, sizeof(path));
        if (path[0] && tvReq(path)) tvQ = TVQ_LIST;
      } else if (now >= nextNow) {
        if (tvReq("/api/tv/now")) tvQ = TVQ_NOW;
        nextNow = now + 3000;
      } else if (!setup && tvN) tvThumbAsk(top > rec ? top - rec : 0, TV_ROWS);
    }
    bool loading = tvQ == TVQ_LIST || tvWantList;
    uint8_t rows = search ? tvN + rec : (setup ? 3 : (loading ? 0 : tvN));
    if (btn(B_LEFT))  { tab = tab ? tab - 1 : tvTabs() - 1; tvWantList = true; sel = top = 0; tvN = 0; sfx(800, 15); }
    if (btn(B_RIGHT)) { tab = (tab + 1) % tvTabs();         tvWantList = true; sel = top = 0; tvN = 0; sfx(800, 15); }
    search = tab == tvCatN + TVT_SEARCH; setup = tab == tvCatN + TVT_SETUP;
    if (rows) {
      if (btn(B_UP))   { sel = sel ? sel - 1 : rows - 1; sfx(700, 15); }
      if (btn(B_DOWN)) { sel = (sel + 1) % rows;         sfx(700, 15); }
    }
    if (sel >= (rows ? rows : 1)) sel = 0;
    if (sel < top) top = sel;
    if (sel >= top + TV_ROWS) top = sel - TV_ROWS + 1;
    if (btn(B_OK)) {
      sfx(1200, 40);
      if (loadErr) { tvWantList = true; continue; }
      if (setup) {
        if (sel == 0) { tvQDrop(); tvTypeUrl(); if (!tvTok[0]) return false; }   // a new server: pair there
        else if (sel == 1) { tvForget(); return false; }
        continue;
      }
      if (search && sel < rec) {
        char t[sizeof(tvQuery)];
        if (sel) strcpy(t, tvRecent[sel - 1]);
        else {
          t[0] = 0;
          if (!tvKeyboard(TR("SEARCH", "SUCHE"), t, sizeof(t) - 1, false, TR("hold OK = cancel", "OK halten = abbrechen"))) t[0] = 0;
        }
        if (t[0]) {
          strcpy(tvQuery, t);
          tvRemember(t);
          tvWantList = true;
          sel = top = 0;
        }
        btnClear();
        continue;
      }
      uint8_t i = sel - rec;
      if (i >= tvN) continue;
      TvItem it = tvItems[i];
      uint8_t m = tvItemMenu(it);
      char path[48];
      if (m == 0) {
        snprintf(path, sizeof(path), "/api/tv/play?id=%s", it.id);
        if (tvAsk(path, true, &st)) {
          tvThumbFree();
          if (!tvPlayer(it.t)) return false;
          nextNow = millis() + 1500;
          first = false;
          if (tab == tvCatN + TVT_HIST) tvWantList = true;
        } else if (st == 401) return false;
        else if (st >= 0) {
          size_t n;
          const char *b = (const char *)tvReqBody(&n);
          tvErrText(b, n);
          tvSay("TV", TR("the server did not start it", "der Server startet es nicht"), tvErr);
        }
      } else if (m == 1) {
        snprintf(path, sizeof(path), "/api/fav?id=%s&on=%u", it.id, it.f ? 0 : 1);
        if (tvAsk(path, true, &st)) {
          tvItems[i].f = !it.f;
          if (tab == tvCatN + TVT_FAV) tvWantList = true;
        } else if (st == 401) return false;
      }
      btnClear();
      continue;
    }
    // draw
    gfx.gradient(0, 0, SCR_W, SCR_H, colDark(TV_ACCENT, 4), C::BLACK);
    gfx.gradient(0, 0, SCR_W, 26, TV_ACCENT, colDark(TV_ACCENT, 2));
    gfx.textShadow(6, 4, "TV", C::WHITE, C::BLACK, F_TEXT);
    char tb[24];
    snprintf(tb, sizeof(tb), "< %s >", tvTabName(tab));
    gfx.center(4, tb, C::YELLOW);
    batIcon(SCR_W - 30, 8);
    if (setup) {
      char l[3][48];
      uint32_t low, fr = tvFreeHeap(&low);
      snprintf(l[0], 48, TR("server  %.38s", "Server  %.38s"), tvUrl);
      snprintf(l[1], 48, TR("unpair (console %s)", "entkoppeln (Konsole %s)"), tvConsoleCode());
      snprintf(l[2], 48, TR("memory free %lu kB, lowest %lu kB", "Speicher frei %lu kB, minimal %lu kB"), (unsigned long)(fr / 1024), (unsigned long)(low / 1024));
      for (uint8_t i = 0; i < 3; i++) {
        int16_t y = 40 + i * 30;
        if (i == sel) gfx.rbox(4, y - 4, SCR_W - 8, 26, 5, TV_ACCENT);
        gfx.text(12, y + 3, l[i], i == sel ? C::WHITE : C::LTGREY, F_SMALL);
      }
      gfx.text(12, 150, TR("OK on the first line: a new address", "OK auf der ersten Zeile: neue Adresse"), C::GREY, F_SMALL);
      gfx.text(12, 166, TR("unpair: the token is forgotten, the PIN asked again", "entkoppeln: Token vergessen, PIN neu abgefragt"), C::GREY, F_SMALL);
      gfx.text(12, 190, TR("frame rate on the video: Settings, show fps", "Bildrate im Video: Einstellungen, fps zeigen"), C::GREY, F_SMALL);
    } else if (loadErr) {
      gfx.center(80, TR("the server did not answer", "Server antwortet nicht"), C::ORANGE);
      gfx.center(104, tvErr, C::YELLOW, F_SMALL);           // e.g. "YouTube blockiert - Pause bis 18:45"
      gfx.center(130, TR("OK = try again", "OK = nochmal"), C::LIME);
    } else if (!rows) {
      const char *t = loading ? TR("loading ...", "lade ...") : tab == tvCatN + TVT_FAV ? TR("no favourites yet", "keine Favoriten")
                    : tab == tvCatN + TVT_HIST ? TR("nothing watched yet", "noch nichts gesehen") : TR("nothing here", "nichts da");
      gfx.center(100, t, C::LTGREY);
      gfx.center(130, TR("LEFT / RIGHT = other lists", "LEFT / RIGHT = andere Listen"), C::GREY, F_SMALL);
    } else {
      for (uint8_t k = top; k < top + TV_ROWS && k < rows; k++) {
        int16_t y = 29 + (k - top) * 52;
        bool on = k == sel;
        if (on) gfx.rbox(2, y - 1, SCR_W - 4, 50, 6, colDark(TV_ACCENT, 1));
        if (k < rec) {
          gfx.rbox(6, y + 2, TV_TW, TV_TH, 6, C::DKGREY);
          gfx.text(38, y + 14, k ? ">" : "?", C::WHITE, F_BOLD);
          char q[48];
          if (k) snprintf(q, sizeof(q), "%.22s", tvRecent[k - 1]);
          gfx.text(92, y + 4, k ? q : TR("new search ...", "neue Suche ..."), on ? C::WHITE : C::LTGREY);
          if (!k && loading) snprintf(q, sizeof(q), TR("searching \"%.20s\"", "suche \"%.20s\""), tvQuery);
          else if (!k) snprintf(q, sizeof(q), TR("OK = keyboard", "OK = Tastatur"));
          else snprintf(q, sizeof(q), TR("search again", "nochmal suchen"));
          gfx.text(92, y + 28, q, C::YELLOW, F_SMALL);
          continue;
        }
        const TvItem &it = tvItems[k - rec];
        tvDrawThumb(6, y + 2, it.id);
        char t[41];
        size_t n = strlen(it.t);
        const size_t W = 22;
        if (n <= W) strcpy(t, it.t);
        else if (on) {                                         // the chosen one runs through
          size_t sh = (millis() / 300) % (n - W + 8);
          sh = sh < 4 ? 0 : (sh - 4 > n - W ? n - W : sh - 4);
          snprintf(t, W + 1, "%s", it.t + sh);
        } else snprintf(t, sizeof(t), "%.20s..", it.t);
        gfx.text(92, y + 4, t, on ? C::WHITE : C::LTGREY);
        char cd[32], d[12];
        tvDur(d, sizeof(d), it.d);
        snprintf(cd, sizeof(cd), "%s%.24s", it.f ? "* " : "", it.c);
        gfx.text(92, y + 28, cd, it.f ? C::YELLOW : C::GREY, F_SMALL);
        if (it.d) gfx.right(SCR_W - 10, y + 28, d, C::YELLOW, F_SMALL);
      }
      if (top > 0) gfx.triangle(SCR_W - 12, 29, SCR_W - 17, 34, SCR_W - 7, 34, C::WHITE);
      if (top + TV_ROWS < rows) gfx.triangle(SCR_W - 12, 238, SCR_W - 17, 233, SCR_W - 7, 233, C::WHITE);
    }
    gfx.send();
  }
  return true;
}

void tvRun() {
  if (runClock < 80) { wlanNeedsClock(); return; }
  tvLoad();
  bool wasLinked = phoneLink;
  phoneLink = false;
  pollHook = tvHook;
  if (tvOnline()) {
    while (tvUrl[0] || tvTypeUrl()) {
      if (!tvNetStart(tvUrl, tvStartTok())) { tvSay("TV", TR("not enough memory", "zu wenig Speicher"), NULL); break; }
      if (!tvTok[0] && !tvPair()) break;
      bool left = tvBrowse();                          // false: the pairing is gone (or a new server)
      tvThumbFree();
      tvNetStop();
      if (left) break;
      tvForget();
    }
  }
  tvNetStop();
  tvThumbFree();
  tvQ = TVQ_NONE;
  pollHook = NULL;
  if (wasLinked || wlanStay) { phoneLink = true; pollHook = phoneHook; }
  else netStop();
}
