// MiniArcade Color: chat with the other consoles through the own MiniArcade
// server (MiniArcadeServer/tvserver/chat.py), on the TV's connection and
// token (internet play's without the PIN will do).
/* The first time a chat name is chosen: 3 to 12 letters, digits, - and _,
   unique on the server (upper and lower case count as the same). One name
   per console: the server ties it to the token and to a hash of the MAC.
   Then the chats (newest first, unread ones marked), everybody with a name
   (alphabetical, from a search on) and a conversation: the history stays
   on the server, the console keeps the last CH_MSGS while it is open and
   asks for new ones every few seconds. RIGHT in a conversation blocks or
   unblocks the other one.
   Part of console.cpp, which includes it after inet.h - not compiled on
   its own.                                                            */
#define CH_ACCENT rgb(30, 160, 110)
#define CH_THEIRS rgb(60, 70, 120)                      // the other one's messages
#define CH_TITLE  "CHAT"
#define CH_NAME   13                                    // 12 characters and the 0
#define CH_TEXT   121                                   // a message: 120 bytes of UTF-8 and the 0
#define CH_MSGS   40                                    // messages kept while a conversation is open
#define CH_ROWS   20                                    // people or chats in one answer
#define CH_TALK_MS 3000                                 // a conversation asks for new messages this often
#define CH_LIST_MS 6000                                 // ... the chats list for news

static const char *const IC_CHAT[12] = {
  "............", "..gggggggg..", ".gggggggggg.", "gggggggggggg", "ggwwgwwgwwgg", "ggwwgwwgwwgg",
  "gggggggggggg", ".gggggggggg.", "..gggggggg..", "...gg.......", "..gg........", "............" };

enum : uint8_t { CW_NONE, CW_NAME, CW_KEYS, CW_LIST, CW_PEOPLE, CW_TALK, CW_ASK };
static uint8_t chWhere = CW_NONE;                       // which screen is up (for the host tests)

struct ChMsg { uint32_t id; uint16_t ago; bool mine; char x[CH_TEXT]; };
struct ChRow { char n[CH_NAME]; uint8_t unread, online, blocked; uint16_t ago; };
struct ChState {
  char    me[CH_NAME];                                  // the own chat name
  ChMsg   m[CH_MSGS];                                   // the open conversation, oldest first
  ChMsg   in[CH_ROWS];                                  // an answer's, while it is read
  uint8_t nm;
  bool    older;                                        // the server has older ones
  ChRow   rows[CH_ROWS];
  uint8_t nRows;
  bool    moreRows;
  uint16_t unread;                                      // all unread messages
  bool    gone, noname;                                 // the server forgot us (401) / our name (409)
};
static ChState *chS = NULL;                             // on the heap while the chat is open

// ---------------- requests ----------------
static bool chBgBusy = false;                           // a background request of ours is with tvnet

// a JSON string's inside: quotes and backslashes escaped, control characters gone
static size_t chEsc(char *o, size_t n, const char *s) {
  size_t k = 0;
  for (; *s && k + 3 < n; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') { o[k++] = '\\'; o[k++] = (char)c; }
    else o[k++] = c < 0x20 ? ' ' : (char)c;
  }
  o[k] = 0;
  return k;
}

// what every answer may say: forgotten, or no chat name (any more)
static void chStatus(int st, const char *r) {
  if (st == 401) chS->gone = true;
  if (st == 409 && r && strstr(r, "noname")) chS->noname = true;
}

static void chBgDrop() {                                // before a request the screen waits for
  while (chBgBusy && tvReqState() == TVR_BUSY) delay(5);
  if (chBgBusy) { tvReqClear(); chBgBusy = false; }
}

// one request, the screen waits (a hint after a moment); NULL = failed (*st as tvAsk)
static const char *chAsk(const char *path, const char *json, int *st) {
  chBgDrop();
  const char *r = tvAsk(path, true, st, json);
  if (!r && *st > 0) r = (const char *)tvReqBody(NULL);   // a refusal: its text says why
  chStatus(*st, r);
  return r;
}

static bool chBg(const char *path, const char *json) {
  if (chBgBusy || !tvReq(path, true, json)) return false;
  chBgBusy = true;
  return true;
}
// the background answer when it is there (NULL before); *st its status
static const char *chBgTake(int *st) {
  if (!chBgBusy) return NULL;
  TvReqState s = tvReqState();
  if (s != TVR_DONE && s != TVR_ERROR) return NULL;
  chBgBusy = false;
  *st = tvReqStatus();
  const char *r = (const char *)tvReqBody(NULL);
  tvReqClear();
  chStatus(*st, r);
  return r;
}

// ---------------- text ----------------
static uint8_t chLen(const char *s) {                   // bytes of the UTF-8 letter at s
  unsigned char c = (unsigned char)*s;
  uint8_t l = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
  for (uint8_t i = 1; i < l; i++) if (!s[i]) return i;
  return l;
}
static int16_t chW(const char *s, uint8_t l, Font f) {  // the width of one letter
  char one[5];
  memcpy(one, s, l);
  one[l] = 0;
  return Gfx::width(one, f);
}

// the next line of s that fits w pixels, broken at a space where it can:
// its length in bytes; *next = where the line after it starts
static uint16_t chLine(const char *s, int16_t w, Font f, const char **next) {
  int16_t x = 0;
  const char *p = s, *sp = NULL;
  while (*p) {
    uint8_t l = chLen(p);
    int16_t cw = chW(p, l, f);
    if (x + cw > w && p > s) break;
    if (*p == ' ') sp = p;
    x += cw;
    p += l;
  }
  if (!*p) { *next = p; return (uint16_t)(p - s); }
  if (sp && sp > s) { *next = sp + 1; return (uint16_t)(sp - s); }
  *next = p;
  return (uint16_t)(p - s);
}
// lines of s at width w, and the widest one
static uint8_t chLines(const char *s, int16_t w, Font f, int16_t *widest) {
  uint8_t n = 0;
  *widest = 0;
  while (*s) {
    const char *nx;
    uint16_t l = chLine(s, w, f, &nx);
    int16_t lw = 0;
    for (uint16_t i = 0; i < l; ) { uint8_t k = chLen(s + i); lw += chW(s + i, k, f); i += k; }
    if (lw > *widest) *widest = lw;
    n++;
    s = nx;
  }
  return n;
}
// draws s in lines of w pixels from (x, y); returns the y below
static int16_t chDraw(int16_t x, int16_t y, const char *s, int16_t w, col c, Font f, int16_t lh) {
  char b[CH_TEXT];
  while (*s) {
    const char *nx;
    uint16_t l = chLine(s, w, f, &nx);
    if (l >= sizeof(b)) l = sizeof(b) - 1;
    memcpy(b, s, l);
    b[l] = 0;
    gfx.text(x, y, b, c, f);
    y += lh;
    s = nx;
  }
  return y;
}

static void chAgo(char *b, size_t n, uint16_t m) {      // minutes ago, short
  if (m < 1)          snprintf(b, n, "%s", TR("now", "jetzt"));
  else if (m < 60)    snprintf(b, n, "%u min", m);
  else if (m < 1440)  snprintf(b, n, "%u h", m / 60);
  else                snprintf(b, n, TR("%u d", "%u T"), m / 1440);
}

static void chTitle(const char *t) { wlanTitle(t, CH_ACCENT); batIcon(SCR_W - 30, 11); }

// a short message; OK or 3 s go on (as mpNote, in the chat's colours)
static void chNote(const char *l1, const char *l2) {
  uint32_t t0 = millis();
  btnClear();
  mpNoting = true;
  while (millis() - t0 < 3000) {
    if (!poll() || btn(B_OK)) break;
    chTitle(CH_TITLE);
    panel(24, 76, SCR_W - 48, 96, NULL, CH_ACCENT);
    gfx.center(l2 ? 100 : 114, l1, C::WHITE);
    if (l2) gfx.center(130, l2, C::LTGREY);
    gfx.send();
  }
  mpNoting = false;
}

// a question over the screen: OK = yes (true), hold OK = no
static bool chConfirm(const char *l1, const char *l2, const char *yes) {
  uint8_t was = chWhere;
  btnClear();
  bool r = false;
  while (poll()) {
    chWhere = CW_ASK;
    if (btn(B_OK)) { sfx(1200, 40); r = true; break; }
    chTitle(CH_TITLE);
    panel(20, 64, SCR_W - 40, 120, NULL, CH_ACCENT);
    gfx.center(84, l1, C::WHITE);
    if (l2) gfx.center(112, l2, C::LTGREY, F_SMALL);
    char b[48];
    snprintf(b, sizeof(b), "%s = %s", okKeys(), yes);
    gfx.center(140, b, C::LIME);
    gfx.center(160, TR("hold OK = no", "OK halten = nein"), C::GREY, F_SMALL);
    gfx.send();
  }
  chWhere = was;
  btnClear();
  return r;
}

// ---------------- the keyboard ----------------
/* Five rows of ten letters, the last row: Aa (capitals on/off), space,
   delete, done. Hold OK = cancel. A name takes only letters, digits,
   - and _ (the others beep).                                          */
static const char *const CH_KB[5]    = { "abcdefghij", "klmnopqrst", "uvwxyzäöüß", "1234567890", ".,!?:-()'/" };
static const char *const CH_KB_UP[5] = { "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZÄÖÜß", "1234567890", "\"+*=#&%@_;" };
static uint8_t chKbX = 0, chKbY = 0;                    // the cursor (host tests steer by it) ...
static bool    chKbUp = false;
static const char *chKbText = NULL;                     // ... and the text being typed

static const char *chKey(uint8_t r, uint8_t k, uint8_t *len) {   // the key's letter in UTF-8
  const char *s = (chKbUp ? CH_KB_UP : CH_KB)[r];
  for (uint8_t i = 0; i < k; i++) s += chLen(s);
  *len = chLen(s);
  return s;
}

static bool chKeyboard(const char *title, char *text, size_t max, bool name, const char *done) {
  const char *last[4] = { "Aa", TR("space", "Leerz."), TR("delete", "löschen"), done };
  chKbX = chKbY = 0;
  chKbText = text;
  btnClear();
  while (poll()) {
    chWhere = CW_KEYS;
    if (btn(B_UP))    { chKbY = chKbY ? chKbY - 1 : 5; sfx(700, 10); }
    if (btn(B_DOWN))  { chKbY = (chKbY + 1) % 6;      sfx(700, 10); }
    uint8_t w = chKbY == 5 ? 4 : 10;
    if (chKbX >= w) chKbX = w - 1;
    if (btn(B_LEFT))  { chKbX = chKbX ? chKbX - 1 : w - 1; sfx(700, 10); }
    if (btn(B_RIGHT)) { chKbX = (chKbX + 1) % w;           sfx(700, 10); }
    if (btn(B_OK)) {
      size_t n = strlen(text);
      sfx(1100, 25);
      if (chKbY < 5) {
        uint8_t l;
        const char *k = chKey(chKbY, chKbX, &l);
        bool ok = !name || (l == 1 && (isalnum((unsigned char)*k) || *k == '-' || *k == '_'));
        if (ok && n + l <= max) { memcpy(text + n, k, l); text[n + l] = 0; }
        else sfx(200, 60);
      } else if (chKbX == 0) chKbUp = !chKbUp;
      else if (chKbX == 1) { if (!name && n && text[n - 1] != ' ' && n < max) strcat(text, " "); else if (name) sfx(200, 60); }
      else if (chKbX == 2) { while (n && ((unsigned char)text[n - 1] & 0xC0) == 0x80) n--; if (n) text[n - 1] = 0; }
      else return true;
    }
    chTitle(title);
    gfx.right(SCR_W - 40, 12, TR("hold = cancel", "halten = abbrechen"), C::GREY, F_SMALL);
    gfx.rbox(6, 34, SCR_W - 12, 60, 5, C::BLACK);
    gfx.rframe(6, 34, SCR_W - 12, 60, 5, C::GREY);
    // the end stays in view: the last three lines
    const char *s = text;
    int16_t wide;
    uint8_t lines = chLines(s, SCR_W - 24, F_TEXT, &wide);
    for (uint8_t i = 0; lines > 3 && i < lines - 3; i++) { const char *nx; chLine(s, SCR_W - 24, F_TEXT, &nx); s = nx; }
    int16_t y = chDraw(12, 37, s, SCR_W - 24, C::WHITE, F_TEXT, 18);
    if (!*text) y += 18;
    {                                                   // the cursor after the last letter
      const char *ls = s, *nx;
      while (*ls) { chLine(ls, SCR_W - 24, F_TEXT, &nx); if (!*nx) break; ls = nx; }
      int16_t lw = 0;
      for (const char *p = ls; *p; ) { uint8_t k = chLen(p); lw += chW(p, k, F_TEXT); p += k; }
      if ((millis() / 400) % 2) gfx.fill(12 + lw + 1, y - 18, 2, 17, C::YELLOW);
    }
    char cnt[24];
    snprintf(cnt, sizeof(cnt), "%u/%u", (unsigned)strlen(text), (unsigned)max);
    gfx.right(SCR_W - 10, 84, cnt, C::GREY, F_SMALL);
    for (uint8_t r = 0; r < 5; r++)
      for (uint8_t k = 0; k < 10; k++) {
        int16_t kx = 10 + k * 30, ky = 98 + r * 23;
        bool on = r == chKbY && k == chKbX;
        gfx.rbox(kx, ky, 28, 21, 4, on ? CH_ACCENT : C::DKGREY);
        uint8_t l;
        const char *ks = chKey(r, k, &l);
        char ch[5];
        memcpy(ch, ks, l);
        ch[l] = 0;
        gfx.text(kx + (28 - Gfx::width(ch)) / 2, ky + 2, ch, on ? C::WHITE : C::LTGREY);
      }
    for (uint8_t k = 0; k < 4; k++) {
      static const int16_t KX[5] = { 10, 60, 150, 230, 310 };
      bool on = chKbY == 5 && k == chKbX;
      const char *lb = last[k];
      int16_t kw = KX[k + 1] - KX[k] - 2;
      Font f = Gfx::width(lb) <= kw - 6 ? F_TEXT : F_SMALL;
      gfx.rbox(KX[k], 214, kw, 22, 4, on ? CH_ACCENT : (k == 0 && chKbUp ? colDark(CH_ACCENT, 2) : C::DKGREY));
      gfx.text(KX[k] + (kw - Gfx::width(lb, f)) / 2, f == F_TEXT ? 216 : 220, lb, on ? C::WHITE : C::LTGREY, f);
    }
    gfx.send();
  }
  return false;
}

// ---------------- the own name ----------------
// asks for a chat name and takes it on the server; false = left
static bool chName(bool first) {
  char n[CH_NAME];
  snprintf(n, sizeof(n), "%s", first ? "" : chS->me);
  for (;;) {
    if (first) {
      btnClear();
      bool go = false;
      while (poll()) {
        chWhere = CW_NAME;
        if (btn(B_OK)) { sfx(1200, 40); go = true; break; }
        chTitle(CH_TITLE);
        gfx.center(52, TR("choose your chat name", "wähle deinen Chat-Namen"), C::WHITE);
        gfx.center(84, TR("3 to 12 letters or digits, - _", "3 bis 12 Buchstaben, Ziffern, - _"), C::LTGREY, F_SMALL);
        gfx.center(102, TR("everybody sees it, nobody else gets it", "alle sehen ihn, kein anderer bekommt ihn"), C::LTGREY, F_SMALL);
        gfx.center(120, TR("one name per console", "ein Name pro Konsole"), C::LTGREY, F_SMALL);
        char b[40];
        snprintf(b, sizeof(b), TR("%s = type it", "%s = eintippen"), okKeys());
        gfx.center(160, b, C::LIME);
        gfx.center(SCR_H - 16, TR("hold OK = back", "OK halten = zurück"), C::GREY, F_SMALL);
        gfx.send();
      }
      if (!go) return false;
    }
    if (!chKeyboard(TR("CHAT NAME", "CHAT-NAME"), n, CH_NAME - 1, true, TR("done", "fertig"))) return false;
    char esc[40], body[80];
    chEsc(esc, sizeof(esc), n);
    snprintf(body, sizeof(body), "{\"n\":\"%s\",\"dev\":\"%s\"}", esc, tvDeviceId());
    int st = 0;
    const char *r = chAsk("/api/chat/name", body, &st);
    if (st < 0 || chS->gone) return false;
    if (st == 200 && r) {
      jsStr(r, r + strlen(r), "n", chS->me, sizeof(chS->me));
      chS->noname = false;
      char b[40];
      snprintf(b, sizeof(b), TR("you are %s", "du bist %s"), chS->me);
      chNote(b, NULL);
      return true;
    }
    if (st == 409 && r && strstr(r, "taken")) chNote(TR("this name is taken", "Name schon vergeben"), n);
    else if (st == 409) chNote(TR("3 to 12 letters or digits", "3 bis 12 Buchstaben/Ziffern"), TR("and - _ only", "und nur - _"));
    else chNote(TR("server unreachable", "Server nicht erreichbar"), NULL);
    first = false;
  }
}

// ---------------- lists ----------------
// rows from an answer's list under key: chats ("c") or people ("p")
static void chParseRows(const char *r, const char *key) {
  const char *e = r + strlen(r);
  chS->nRows = 0;
  chS->moreRows = jsInt(r, e, "more", 0) != 0;
  const char *q = jsFind(r, e, key);
  if (!q) return;
  for (const char *oe; chS->nRows < CH_ROWS && (q = jsObj(q, e, &oe)); q = oe) {
    ChRow &w = chS->rows[chS->nRows];
    if (!jsStr(q, oe, "n", w.n, sizeof(w.n)) || !w.n[0]) continue;
    long u = jsInt(q, oe, "u", 0), m = jsInt(q, oe, "m", 0);
    w.unread  = (uint8_t)(u > 99 ? 99 : u);
    w.online  = jsInt(q, oe, "o", 0) != 0;
    w.blocked = jsInt(q, oe, "b", 0) != 0;
    w.ago     = (uint16_t)(m > 65535 ? 65535 : m);
    chS->nRows++;
  }
}

static void chRow(int16_t y, bool on, const char *name, col nc, const char *right, col rc) {
  gfx.rbox(6, y, SCR_W - 12, 25, 5, on ? CH_ACCENT : colDark(CH_ACCENT, 3));
  if (on) gfx.rframe(6, y, SCR_W - 12, 25, 5, C::WHITE);
  gfx.text(30, y + 4, name, nc);
  if (right) gfx.right(SCR_W - 14, y + 8, right, rc, F_SMALL);
}

static void chTalk(const char *who);
static int8_t chPeopleSel = 0, chListSel = 0;           // the rows picked (host tests steer by them)
static uint16_t chPeopleOff = 0;                        // the people's page

/* Everybody with a name, alphabetical. Row 0 = the search (OK types the
   start of a name), then back / on by a page when there are more.      */
static void chPeople() {
  char q[CH_NAME] = "";
  uint16_t &off = chPeopleOff;
  off = 0;
  bool load = true;
  int8_t &sel = chPeopleSel;
  sel = 0;
  uint8_t top = 0;
  for (;;) {
    if (load) {
      char esc[32], body[64];
      chEsc(esc, sizeof(esc), q);
      snprintf(body, sizeof(body), "{\"q\":\"%s\",\"off\":%u}", esc, off);
      int st = 0;
      chWhere = CW_PEOPLE;
      const char *r = chAsk("/api/chat/people", body, &st);
      if (st < 0 || chS->gone || chS->noname) return;
      if (st == 200 && r) chParseRows(r, "p");
      else { chNote(TR("server unreachable", "Server nicht erreichbar"), NULL); return; }
      load = false;
      sel = 0;
      top = 0;
    }
    // rows: search, [back], people, [more]
    uint8_t back = off ? 1 : 0, n = 1 + back + chS->nRows + (chS->moreRows ? 1 : 0);
    if (!poll()) return;
    chWhere = CW_PEOPLE;
    if (btn(B_UP))   { sel = sel ? sel - 1 : n - 1; sfx(700, 15); }
    if (btn(B_DOWN)) { sel = (sel + 1) % n;         sfx(700, 15); }
    if (btn(B_OK)) {
      sfx(1200, 40);
      if (sel == 0) {
        char t[CH_NAME];
        snprintf(t, sizeof(t), "%s", q);
        if (chKeyboard(TR("SEARCH", "SUCHE"), t, CH_NAME - 1, true, TR("search", "suchen"))) { strcpy(q, t); off = 0; load = true; }
      } else if (back && sel == 1) { off = off >= CH_ROWS ? off - CH_ROWS : 0; load = true; }
      else if (chS->moreRows && sel == n - 1) { off += CH_ROWS; load = true; }
      else {
        char who[CH_NAME];
        strcpy(who, chS->rows[sel - 1 - back].n);
        chTalk(who);
        return;                                         // back to the chats: this one is there now
      }
      btnClear();
      continue;
    }
    chTitle(TR("PEOPLE", "LEUTE"));
    const uint8_t ROWS = 6;
    if (sel < top) top = sel;
    if (sel >= top + ROWS) top = sel - ROWS + 1;
    for (uint8_t i = 0; i < ROWS && top + i < n; i++) {
      uint8_t row = top + i;
      int16_t y = 40 + i * 28;
      bool on = row == sel;
      char b[40];
      if (row == 0) {
        snprintf(b, sizeof(b), TR("search: %s", "Suche: %s"), q[0] ? q : TR("(from A)", "(ab A)"));
        chRow(y, on, b, C::YELLOW, TR("OK = type", "OK = tippen"), on ? C::WHITE : C::GREY);
      } else if (back && row == 1) chRow(y, on, TR("<< back", "<< zurück"), C::LTGREY, NULL, 0);
      else if (chS->moreRows && row == n - 1) chRow(y, on, TR("more >>", "weitere >>"), C::LTGREY, NULL, 0);
      else {
        const ChRow &w = chS->rows[row - 1 - back];
        chRow(y, on, w.n, w.blocked ? C::GREY : C::WHITE, w.blocked ? TR("blocked", "blockiert") : (w.online ? "online" : NULL),
              w.blocked ? C::ORANGE : C::LIME);
        if (w.online && !w.blocked) gfx.fill(14, y + 9, 8, 8, C::LIME);
      }
    }
    if (n == 1) gfx.center(110, q[0] ? TR("no name from there on", "kein Name ab da") : TR("nobody else has a name yet", "noch niemand sonst da"), C::LTGREY);
    gfx.center(SCR_H - 16, TR("OK = write to   hold OK = back", "OK = schreiben   OK halten = zurück"), C::GREY, F_SMALL);
    gfx.send();
  }
}

// ---------------- a conversation ----------------
// messages from an answer: appended (after = true) or the newest / older ones put in front
static uint8_t chParseMsgs(const char *r, bool append) {
  const char *e = r + strlen(r);
  const char *q = jsFind(r, e, "m");
  if (!q || *q != '[') return 0;
  ChMsg *in = chS->in;                                  // the answer's, oldest first
  uint8_t n = 0;
  for (const char *oe; n < CH_ROWS && (q = jsObj(q, e, &oe)); q = oe) {
    ChMsg &m = in[n];
    m.id = (uint32_t)jsInt(q, oe, "i", 0);
    m.mine = jsInt(q, oe, "m", 0) != 0;
    long a = jsInt(q, oe, "a", 0);
    m.ago = (uint16_t)(a > 65535 ? 65535 : a);
    if (!m.id || !jsStr(q, oe, "x", m.x, sizeof(m.x))) continue;
    n++;
  }
  uint8_t added = 0;
  if (append) {
    for (uint8_t i = 0; i < n; i++) {
      if (chS->nm && in[i].id <= chS->m[chS->nm - 1].id) continue;
      if (chS->nm == CH_MSGS) { memmove(chS->m, chS->m + 1, sizeof(ChMsg) * (CH_MSGS - 1)); chS->nm--; chS->older = true; }
      chS->m[chS->nm++] = in[i];
      added++;
    }
  } else {                                              // older ones in front, as many as fit
    uint8_t room = CH_MSGS - chS->nm, k = n < room ? n : room;
    memmove(chS->m + k, chS->m, sizeof(ChMsg) * chS->nm);
    memcpy(chS->m, in + (n - k), sizeof(ChMsg) * k);
    chS->nm += k;
    added = k;
  }
  return added;
}

static void chMsgsBody(char *body, size_t n, const char *who, const char *extra) {
  char esc[32];
  chEsc(esc, sizeof(esc), who);
  snprintf(body, n, "{\"n\":\"%s\"%s}", esc, extra);
}

static void chTalk(const char *who) {
  char body[200];
  chS->nm = 0;
  chS->older = false;
  bool online = false, blocked = false;
  // the newest ones
  chMsgsBody(body, sizeof(body), who, "");
  int st = 0;
  chWhere = CW_TALK;
  const char *r = chAsk("/api/chat/msgs", body, &st);
  if (st < 0 || chS->gone || chS->noname) return;
  if (st != 200 || !r) { chNote(st == 404 ? TR("this name is gone", "Name gibt es nicht mehr") : TR("server unreachable", "Server nicht erreichbar"), NULL); return; }
  {
    const char *e = r + strlen(r);
    online = jsInt(r, e, "o", 0) != 0;
    blocked = jsInt(r, e, "b", 0) != 0;
    chS->older = jsInt(r, e, "more", 0) != 0;
    chParseMsgs(r, false);
  }
  uint8_t skip = 0;                                     // the newest ones not shown (scrolled up)
  uint32_t next = millis() + CH_TALK_MS;
  btnClear();
  for (;;) {
    bool alive = poll();
    chWhere = CW_TALK;
    if (chS->gone || chS->noname) return;
    // news in the background
    if (const char *a = chBgTake(&st)) {
      if (st == 200) {
        const char *e = a + strlen(a);
        online = jsInt(a, e, "o", 0) != 0;
        blocked = jsInt(a, e, "b", 0) != 0;
        uint8_t k = chParseMsgs(a, true);               // the new ones are the last k
        bool theirs = false;
        for (uint8_t i = chS->nm - k; i < chS->nm; i++) if (!chS->m[i].mine) theirs = true;
        if (theirs) { sfx(1500, 60); sfx(1900, 60); }
      }
      next = millis() + CH_TALK_MS;
    }
    if (!chBgBusy && (int32_t)(millis() - next) >= 0) {
      char ex[32];
      snprintf(ex, sizeof(ex), ",\"after\":%lu", chS->nm ? (unsigned long)chS->m[chS->nm - 1].id : 0UL);
      chMsgsBody(body, sizeof(body), who, ex);
      if (!chBg("/api/chat/msgs", body)) next = millis() + 500;
    }
    if (!alive) { chBgDrop(); return; }
    if (btn(B_UP)) {
      if (skip + 1 < chS->nm) { skip++; sfx(700, 10); }
      else if (chS->older && chS->nm < CH_MSGS) {       // the next older page from the server
        char ex[32];
        snprintf(ex, sizeof(ex), ",\"before\":%lu", (unsigned long)chS->m[0].id);
        chMsgsBody(body, sizeof(body), who, ex);
        const char *o = chAsk("/api/chat/msgs", body, &st);
        if (st == 200 && o) { chS->older = jsInt(o, o + strlen(o), "more", 0) != 0; if (chParseMsgs(o, false)) skip++; }
        sfx(700, 10);
      } else sfx(200, 40);
    }
    if (btn(B_DOWN) && skip) { skip--; sfx(700, 10); }
    if (btn(B_RIGHT)) {                                 // block / unblock
      char l1[40];
      snprintf(l1, sizeof(l1), blocked ? TR("unblock %s?", "%s freigeben?") : TR("block %s?", "%s blockieren?"), who);
      if (chConfirm(l1, blocked ? TR("then you can write again", "dann könnt ihr wieder schreiben")
                                : TR("then nothing of it reaches you", "dann kommt nichts mehr an"),
                    blocked ? TR("unblock", "freigeben") : TR("block", "blockieren"))) {
        chMsgsBody(body, sizeof(body), who, blocked ? ",\"on\":0" : ",\"on\":1");
        const char *b = chAsk("/api/chat/block", body, &st);
        if (st == 200 && b) blocked = jsInt(b, b + strlen(b), "b", 0) != 0;
      }
      btnClear();
      continue;
    }
    if (btn(B_OK)) {                                    // write
      sfx(1200, 40);
      if (blocked) { chNote(TR("you blocked this one", "du hast blockiert"), TR("RIGHT = unblock", "RIGHT = freigeben")); btnClear(); continue; }
      char t[CH_TEXT] = "", title[32];
      snprintf(title, sizeof(title), TR("TO %s", "AN %s"), who);
      if (chKeyboard(title, t, CH_TEXT - 1, false, TR("send", "senden")) && t[0]) {
        char esc[2 * CH_TEXT], ne[32];
        chEsc(esc, sizeof(esc), t);
        chEsc(ne, sizeof(ne), who);
        char sb[2 * CH_TEXT + 48];
        snprintf(sb, sizeof(sb), "{\"n\":\"%s\",\"x\":\"%s\"}", ne, esc);
        const char *s = chAsk("/api/chat/send", sb, &st);
        if (chS->gone || chS->noname) return;
        if (st == 200) { sfx(1400, 50); skip = 0; next = millis(); }
        else if (st == 409 && s && strstr(s, "blocked")) chNote(TR("not sent: blocked", "nicht gesendet: blockiert"), NULL);
        else if (st == 429) chNote(TR("not so fast", "nicht so schnell"), NULL);
        else if (st >= 0) chNote(TR("not sent", "nicht gesendet"), TR("server unreachable", "Server nicht erreichbar"));
      }
      btnClear();
      continue;
    }

    // ---- the screen: the newest at the bottom ----
    chTitle(who);
    int16_t tx = 14 + Gfx::width(who, F_BOLD) + 12;
    if (blocked) gfx.text(tx, 12, TR("blocked", "blockiert"), C::ORANGE, F_SMALL);
    else if (online) { gfx.fill(tx, 14, 7, 7, C::LIME); gfx.text(tx + 11, 12, "online", C::LIME, F_SMALL); }
    const int16_t TOP = 36, BOT = 220, BW = 240;
    int16_t y = BOT;
    for (int16_t i = (int16_t)chS->nm - 1 - skip; i >= 0 && y > TOP; i--) {
      const ChMsg &m = chS->m[i];
      int16_t wide;
      uint8_t lines = chLines(m.x, BW - 12, F_TEXT, &wide);
      int16_t h = lines * 18 + 6, bw = wide + 12;
      y -= h;
      int16_t x = m.mine ? SCR_W - 8 - bw : 8;
      if (y < TOP) {                                    // the top one only in part: just its frame
        gfx.rbox(x, TOP, bw, y + h - TOP, 6, m.mine ? colDark(CH_ACCENT, 1) : CH_THEIRS);
        break;
      }
      gfx.rbox(x, y, bw, h, 6, m.mine ? colDark(CH_ACCENT, 1) : CH_THEIRS);
      chDraw(x + 6, y + 3, m.x, BW - 12, C::WHITE, F_TEXT, 18);
      char a[12];
      chAgo(a, sizeof(a), m.ago);
      if (m.mine) gfx.right(x - 4, y + h - 10, a, C::GREY, F_SMALL);
      else gfx.text(x + bw + 4, y + h - 10, a, C::GREY, F_SMALL);
      y -= 4;
    }
    if (!chS->nm) gfx.center(110, TR("no messages yet - say hello!", "noch nichts - sag hallo!"), C::LTGREY);
    if (skip) gfx.right(SCR_W - 8, TOP, TR("DOWN = newer", "DOWN = neuere"), C::YELLOW, F_SMALL);
    const char *hint = blocked ? TR("RIGHT = unblock   hold OK = back", "RIGHT = freigeben   OK halten = zurück")
                               : TR("OK = write  UP = older  RIGHT = block", "OK = schreiben  UP = älter  RIGHT = blocken");
    gfx.center(SCR_H - 14, hint, C::GREY, F_SMALL);
    gfx.send();
  }
}

// ---------------- the chats ----------------
/* Row 0 = the people (a new chat), then the chats, newest first, the last
   row = the own name (OK changes it). Hold OK = back to the games.     */
static void chList() {
  int8_t &sel = chListSel;
  sel = 0;
  uint8_t top = 0;
  uint16_t lastUnread = 0;
  bool loaded = false;
  uint32_t next = 0;
  char selName[CH_NAME] = "";
  btnClear();
  for (;;) {
    bool alive = poll();
    chWhere = CW_LIST;
    if (chS->gone || chS->noname) return;
    int st = 0;
    if (const char *a = chBgTake(&st)) {
      if (st == 200) {
        chParseRows(a, "c");
        chS->unread = (uint16_t)jsInt(a, a + strlen(a), "u", 0);
        if (loaded && chS->unread > lastUnread) { sfx(1500, 60); sfx(1900, 60); }
        lastUnread = chS->unread;
        loaded = true;
      }
      next = millis() + CH_LIST_MS;
    }
    if (!chBgBusy && (int32_t)(millis() - next) >= 0 && !chBg("/api/chat/list", "{}")) next = millis() + 500;
    if (!alive) { chBgDrop(); return; }
    uint8_t n = chS->nRows + 2;
    for (uint8_t i = 0; selName[0] && i < chS->nRows; i++) if (!strcmp(chS->rows[i].n, selName)) sel = i + 1;   // stays on its name
    if (sel >= n) sel = n - 1;
    if (btn(B_UP))   { sel = sel ? sel - 1 : n - 1; sfx(700, 15); }
    if (btn(B_DOWN)) { sel = (sel + 1) % n;         sfx(700, 15); }
    if (sel >= 1 && sel <= chS->nRows) strcpy(selName, chS->rows[sel - 1].n); else selName[0] = 0;
    if (btn(B_OK)) {
      sfx(1200, 40);
      if (sel == 0) chPeople();
      else if (sel == n - 1) chName(false);
      else { char who[CH_NAME]; strcpy(who, chS->rows[sel - 1].n); chTalk(who); }
      if (chS->gone || chS->noname) return;
      next = 0;                                         // fresh news at once
      chBgDrop();
      btnClear();
      continue;
    }
    chTitle(CH_TITLE);
    char b[48];
    if (chS->unread) {
      snprintf(b, sizeof(b), TR("%u new", "%u neu"), chS->unread);
      gfx.right(SCR_W - 40, 12, b, C::YELLOW, F_SMALL);
    }
    const uint8_t ROWS = 6;
    if (sel < top) top = sel;
    if (sel >= top + ROWS) top = sel - ROWS + 1;
    for (uint8_t i = 0; i < ROWS && top + i < n; i++) {
      uint8_t row = top + i;
      int16_t y = 40 + i * 28;
      bool on = row == sel;
      if (row == 0) chRow(y, on, TR("+ new chat", "+ neuer Chat"), C::YELLOW, TR("all people", "alle Leute"), on ? C::WHITE : C::GREY);
      else if (row == n - 1) {
        snprintf(b, sizeof(b), TR("my name: %s", "mein Name: %s"), chS->me);
        chRow(y, on, b, C::LTGREY, TR("OK = change", "OK = ändern"), on ? C::WHITE : C::GREY);
      } else {
        const ChRow &w = chS->rows[row - 1];
        char a[12];
        chAgo(a, sizeof(a), w.ago);
        chRow(y, on, w.n, w.blocked ? C::GREY : C::WHITE, a, on ? C::WHITE : C::GREY);
        if (w.online && !w.blocked) gfx.fill(14, y + 9, 8, 8, C::LIME);
        if (w.unread) {
          snprintf(b, sizeof(b), "%u", w.unread);
          int16_t bw = Gfx::width(b, F_SMALL) + 10;
          gfx.rbox(200, y + 5, bw, 15, 7, C::ORANGE);
          gfx.text(205, y + 8, b, C::WHITE, F_SMALL);
        }
      }
    }
    if (!loaded) gfx.center(SCR_H - 34, TR("loading ...", "lade ..."), C::GREY, F_SMALL);
    else if (!chS->nRows) gfx.center(SCR_H - 34, TR("no chats yet: first row = find people", "noch keine Chats: erste Zeile = Leute finden"), C::GREY, F_SMALL);
    gfx.center(SCR_H - 16, TR("OK = open   hold OK = back", "OK = öffnen   OK halten = zurück"), C::GREY, F_SMALL);
    gfx.send();
  }
}

// ---------------- the page ----------------
/* With the server: who we are, a name the first time, then the chats.
   0 = left, 1 = the server forgot our token (a new one).               */
static uint8_t chMain() {
  for (;;) {
    int st = 0;
    chWhere = CW_LIST;
    const char *r = chAsk("/api/chat/me", "{}", &st);
    if (chS->gone) return 1;
    if (st < 0) return 0;
    if (st != 200 || !r) {
      if (!chConfirm(TR("server unreachable", "Server nicht erreichbar"), inHost(), TR("try again", "nochmal"))) return 0;
      continue;
    }
    chS->me[0] = 0;
    jsStr(r, r + strlen(r), "n", chS->me, sizeof(chS->me));
    chS->noname = !chS->me[0];
    if (chS->noname && !chName(true)) return chS->gone ? 1 : 0;
    chList();
    if (chS->gone) return 1;
    if (!chS->noname) return 0;                         // left the list
    chS->noname = false;                                // our name went (taken over on this board): ask anew
  }
}

void chatRun() {
  tvLoad();
  inDefaultName();
  bool wasLinked = phoneLink;
  phoneLink = false;
  pollHook = tvHook;
  if (tvOnline(CH_TITLE)) {
    while (tvUrl[0] || tvTypeUrl()) {
      if (!tvNetStart(tvUrl, tvStartTok())) { chNote(TR("not enough memory", "zu wenig Speicher"), NULL); break; }
      if (!tvStartTok()[0] && !inJoin()) break;         // first time: a token without the PIN (or the PIN)
      void *mem = malloc(sizeof(ChState));
      if (!mem) { chNote(TR("not enough memory", "zu wenig Speicher"), NULL); break; }
      chS = new (mem) ChState();
      chBgBusy = false;
      uint8_t r = chMain();
      chBgDrop();
      chS->~ChState();
      free(chS);
      chS = NULL;
      tvNetStop();
      if (r != 1) break;
      if (tvTok[0]) tvForget(); else tvForgetNet();     // the server forgot us: a new token
    }
  }
  tvNetStop();
  chWhere = CW_NONE;
  pollHook = NULL;
  if (wasLinked || wlanStay) { phoneLink = true; pollHook = phoneHook; }
  else netStop();
}
