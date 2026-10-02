// MiniArcade: AI chat. Part of MiniArcade.ino, which includes it -
// not compiled on its own. The requests to Claude are in net.cpp.

// =========================================================
//  AI CHAT   (ESP-IDF build only: needs a WLAN with internet)
// =========================================================
/* The question is typed on the console with an on-screen keyboard and goes
   to Claude; the answer comes back as text to scroll through. The API key
   is typed in the same way once, or entered on the phone's settings page.
   Keyboard: arrows pick a key, OK types it. The bottom row switches
   between small and capital letters (ABC) and symbols (#+), and has space,
   delete and send. Holding OK leaves.                                    */
#ifdef HAVE_NET

#define AI_COLS 25                   // characters per line in the small font
#define AI_ROWS 6                    // answer lines below the yellow band

// ---------------- on-screen keyboard ----------------
#define KB_COLS 13
#define KB_CW   9                    // cell width
#define KB_X0   ((SCR_W - KB_COLS * KB_CW) / 2)
#define KB_RH   12                   // 4 rows of 12 px below the yellow band

static const char *const KB_KEYS[3][3] = {
  { "abcdefghijklm", "nopqrstuvwxyz", "1234567890.,?" },
  { "ABCDEFGHIJKLM", "NOPQRSTUVWXYZ", "1234567890.,?" },
  { "!\"#$%&'()*+-/", ":;<=>@[]_{}~^", "1234567890.,?" } };

// the bottom row: wide keys over columns [from, to)
enum { KB_CAPS, KB_SYM, KB_SPACE, KB_DEL, KB_SEND, KB_WIDE };
static const uint8_t KB_SPAN[KB_WIDE][2] = { { 0, 2 }, { 2, 4 }, { 4, 8 }, { 8, 10 }, { 10, 13 } };

struct Kb {
  char    *text;
  uint16_t max;
  uint8_t  page, row, col;           // page 0 small, 1 capitals, 2 symbols
};

static uint8_t kbWide(uint8_t col) {
  for (uint8_t k = 0; k < KB_WIDE; k++) if (col < KB_SPAN[k][1]) return k;
  return KB_SEND;
}

/* The keys for the keyboard; true when "send" was chosen with some text. */
static bool kbInput(Kb &k) {
  if (btn(B_UP))   { k.row = (k.row + 3) % 4; sfx(700, 8); }
  if (btn(B_DOWN)) { k.row = (k.row + 1) % 4; sfx(700, 8); }
  if (btn(B_LEFT)) {
    if (k.row < 3) k.col = (k.col + KB_COLS - 1) % KB_COLS;
    else k.col = KB_SPAN[(kbWide(k.col) + KB_WIDE - 1) % KB_WIDE][0];
    sfx(700, 8);
  }
  if (btn(B_RIGHT)) {
    if (k.row < 3) k.col = (k.col + 1) % KB_COLS;
    else k.col = KB_SPAN[(kbWide(k.col) + 1) % KB_WIDE][0];
    sfx(700, 8);
  }
  if (!btn(B_OK)) return false;
  size_t n = strlen(k.text);
  char c = 0;
  if (k.row < 3) c = KB_KEYS[k.page][k.row][k.col];
  else switch (kbWide(k.col)) {
    case KB_CAPS:  k.page = k.page == 1 ? 0 : 1; sfx(900, 15); return false;
    case KB_SYM:   k.page = k.page == 2 ? 0 : 2; sfx(900, 15); return false;
    case KB_SPACE: c = ' '; break;
    case KB_DEL:
      if (n) { k.text[n - 1] = 0; sfx(500, 15); } else sfx(250, 40);
      return false;
    default:
      if (!n) { sfx(250, 40); return false; }
      sfx(1200, 50);
      return true;
  }
  if (n >= k.max) { sfx(250, 40); return false; }   // full
  k.text[n] = c;
  k.text[n + 1] = 0;
  sfx(1000, 10);
  return false;
}

/* The last two lines of the text in the yellow band (or the title while
   it is empty), the keys below. send = the label of the send key.      */
static void kbDraw(const Kb &k, const char *title, const char *send) {
  size_t n = strlen(k.text);
  if (!n) {
    oled.setFont(FONT_B);
    oled.drawStr(2, 12, title);
    oled.setFont(FONT);
  } else {
    size_t last = n / AI_COLS;                     // line with the cursor
    char line[AI_COLS + 1];
    for (uint8_t r = 0; r < 2; r++) {
      if (last + r < 1) continue;
      size_t from = (last + r - 1) * AI_COLS;
      if (from > n) continue;
      size_t len = n - from < AI_COLS ? n - from : AI_COLS;
      memcpy(line, k.text + from, len);
      line[len] = 0;
      oled.drawStr(1, 6 + r * 8, line);
    }
    if ((millis() / 400) & 1) oled.drawHLine(1 + (n % AI_COLS) * 5, 15, 4);   // cursor
  }
  for (uint8_t r = 0; r < 3; r++)
    for (uint8_t c = 0; c < KB_COLS; c++) {
      int16_t x = KB_X0 + c * KB_CW, y = TOP_H + r * KB_RH;
      bool sel = r == k.row && c == k.col;
      if (sel) { oled.drawBox(x, y + 1, KB_CW, KB_RH - 1); oled.setDrawColor(0); }
      char s[2] = { KB_KEYS[k.page][r][c], 0 };
      oled.drawStr(x + 2, y + 9, s);
      oled.setDrawColor(1);
    }
  const char *label[KB_WIDE] = { k.page == 1 ? "abc" : "ABC", k.page == 2 ? "abc" : "#+",
                                 "space", "del", send };
  uint8_t on = k.row == 3 ? kbWide(k.col) : 255;
  for (uint8_t w = 0; w < KB_WIDE; w++) {
    int16_t x = KB_X0 + KB_SPAN[w][0] * KB_CW, y = TOP_H + 3 * KB_RH;
    int16_t wd = (KB_SPAN[w][1] - KB_SPAN[w][0]) * KB_CW - 1;
    if (w == on) { oled.drawBox(x, y + 1, wd, KB_RH - 1); oled.setDrawColor(0); }
    else oled.drawFrame(x, y + 1, wd, KB_RH - 1);
    oled.drawStr(x + (wd - oled.getStrWidth(label[w])) / 2, y + 9, label[w]);
    oled.setDrawColor(1);
  }
}

// ---------------- answer text ----------------
/* The small font knows plain ASCII only: umlauts become ae / oe / ue,
   accents are dropped, typographic quotes and dashes become plain ones and
   emoji go. Markdown that slipped through (**bold**, `code`, # headings)
   is removed.                                                          */
static const char AI_LATIN[64 + 1] =                // U+00C0..U+00FF, '*' = more letters
  "AAAA*A*CEEEEIIII" "DNOOOO*xOUUU*YT*" "aaaa*a*ceeeeiiii" "dnoooo*/ouuu*yty";

static const char *aiWide(uint32_t cp) {             // the ones that need more letters
  switch (cp) {
    case 0xC4: return "Ae"; case 0xD6: return "Oe"; case 0xDC: return "Ue";
    case 0xE4: return "ae"; case 0xF6: return "oe"; case 0xFC: return "ue";
    case 0xDF: return "ss"; case 0xC6: return "AE"; case 0xE6: return "ae";
    case 0xC5: return "A";  case 0xE5: return "a";
    case 0x2026: return "..."; case 0x2192: return "->"; case 0x2190: return "<-";
    case 0x20AC: return "EUR"; case 0x2264: return "<="; case 0x2265: return ">=";
    case 0xB1: return "+-";
    case 0xA0: case 0x2002: case 0x2003: case 0x2009: case 0x202F: return " ";
    case 0xA1: return "!";  case 0xBF: return "?";  case 0xB0: return "o";
    case 0xB2: return "2";  case 0xB3: return "3";  case 0xB7: return ".";
    case 0xAB: case 0xBB: case 0x201C: case 0x201D: case 0x201E: case 0x2033: return "\"";
    case 0x2018: case 0x2019: case 0x201A: case 0x2032: return "'";
    case 0x2010: case 0x2011: case 0x2012: case 0x2013: case 0x2014: case 0x2212: return "-";
    case 0x2022: case 0x25CF: case 0x25AA: return "*";
    case 0x2248: return "~";  case 0xD7: return "x";
    case 0x200B: case 0x200D: case 0xFE0F: return "";   // invisible
    default: return NULL;
  }
}

void aiAscii(const char *in, char *out, size_t max) {
  size_t n = 0;
  bool lineStart = true;
  for (const uint8_t *p = (const uint8_t *)in; *p && n < max - 1; ) {
    uint32_t cp = *p;
    uint8_t len = 1;
    if (cp >= 0xF0)      { cp &= 0x07; len = 4; }
    else if (cp >= 0xE0) { cp &= 0x0F; len = 3; }
    else if (cp >= 0xC0) { cp &= 0x1F; len = 2; }
    else if (cp >= 0x80) { p++; continue; }        // stray continuation byte
    uint8_t i = 1;
    for (; i < len && (p[i] & 0xC0) == 0x80; i++) cp = (cp << 6) | (p[i] & 0x3F);
    p += i;
    if (i < len) continue;                         // cut off sequence
    const char *s = NULL;
    char one[2] = { 0, 0 };
    if (cp == '\r' || cp == '`') continue;
    if (cp == '*' && *p == '*') { p++; continue; }  // **bold**
    if (cp == '#' && lineStart) { while (*p == '#') p++; while (*p == ' ') p++; continue; }
    if (cp == '\t') cp = ' ';
    if (cp == '\n' || (cp >= 32 && cp < 127)) { one[0] = (char)cp; s = one; }
    else if ((s = aiWide(cp)) != NULL) {}
    else if (cp >= 0xC0 && cp <= 0xFF) { one[0] = AI_LATIN[cp - 0xC0]; s = one; }
    else if (cp >= 0x2000) s = "";                 // symbols and emoji: left out
    else s = "?";
    for (; *s && n < max - 1; s++) out[n++] = *s;
    lineStart = cp == '\n';
  }
  out[n] = 0;
}

/* Word wrap into lines of AI_COLS: start and length of each line. */
struct AiLine { uint16_t at; uint8_t len; };
#define AI_LINES 200

static uint16_t aiWrap(const char *t, AiLine *ln, uint16_t max) {
  uint16_t k = 0;
  size_t pos = 0;
  while (t[pos] && k < max) {
    size_t end = pos, space = 0;
    bool spaceSeen = false;
    while (end - pos < AI_COLS && t[end] && t[end] != '\n') {
      if (t[end] == ' ') { space = end; spaceSeen = true; }
      end++;
    }
    if (!t[end] || t[end] == '\n') { ln[k++] = { (uint16_t)pos, (uint8_t)(end - pos) }; pos = end + (t[end] ? 1 : 0); }
    else if (t[end] == ' ')        { ln[k++] = { (uint16_t)pos, (uint8_t)(end - pos) }; pos = end + 1; }
    else if (spaceSeen && space > pos) { ln[k++] = { (uint16_t)pos, (uint8_t)(space - pos) }; pos = space + 1; }
    else                           { ln[k++] = { (uint16_t)pos, (uint8_t)AI_COLS }; pos += AI_COLS; }   // one long word
  }
  return k;
}

// ---------------- the page ----------------
static char    aiQ[AI_QUESTION_MAX + 1];          // the question, kept between visits
static char    aiShown[AI_ANSWER_MAX];            // the answer in plain ASCII
static AiLine  aiLn[AI_LINES];
static uint16_t aiLines = 0, aiTop = 0;
static Kb      aiKb = { aiQ, AI_QUESTION_MAX, 0, 0, 0 };

static void aiTick() {
  if (!phoneLink) netTick();             // with "stay online" poll() does it already
  lastInput = millis();                  // no deep sleep while the radio is on
}

static void aiTitle(const char *t) {
  oled.setFont(FONT_B);
  oled.drawStr(2, 12, t);
  oled.drawHLine(0, TOP_H - 1, SCR_W);
  oled.setFont(FONT);
}

// a few lines and OK to go on; false when left with a long OK
static bool aiMessage(const char *title, const char *const *lines, uint8_t n) {
  btnClear();
  while (poll()) {
    aiTick();
    if (btn(B_OK)) { sfx(1200, 40); btnClear(); return true; }
    aiTitle(title);
    for (uint8_t i = 0; i < n; i++) oled.drawStr(2, 24 + i * 8, lines[i]);
    oled.sendBuffer();
  }
  return false;
}

/* The key typed on the console. true = saved. */
static bool aiKeyEntry() {
  static const char *const HELP[5] = { "needs a Claude API key", "(console.anthropic.com)",
                                       "OK: type it in here", "or on the phone page:", "Settings > AI chat" };
  if (!aiMessage("AI CHAT", HELP, 5)) return false;
  static char key[201];
  Kb kb = { key, 200, 0, 0, 0 };
  key[0] = 0;
  uint32_t badUntil = 0;
  btnClear();
  bool saved = false;
  while (poll()) {
    if (kbInput(kb)) {
      if (netAiSetKey(key)) { saved = true; sfx(1400, 80); break; }
      badUntil = millis() + 1500;
      sfx(200, 200);
    }
    if (millis() < badUntil) {
      aiTitle("NOT A KEY");
      oled.drawStr(2, 30, "keys look like");
      oled.drawStr(2, 40, "sk-ant-api03-...");
      oled.drawStr(2, 50, "check the letters");
    } else kbDraw(kb, "API KEY", "save");
    oled.sendBuffer();
  }
  memset(key, 0, sizeof(key));                   // not left in memory
  btnClear();
  return saved;
}

/* Joins the saved WLAN if needed. started: this page switched it on. */
static bool aiOnline(bool &started) {
  if (netState() == NET_ONLINE) return true;
  if (netState() == NET_PLAY) {
    static const char *const L[4] = { "the phone hotspot has", "no internet: stop it on", "the WLAN page first.", "OK = back" };
    aiMessage("NO INTERNET", L, 4);
    return false;
  }
  if (!netHasConfig()) {
    static const char *const L[4] = { "no WLAN saved yet. Set", "it up: Settings > wlan", "and update > set up", "with phone.  OK = back" };
    aiMessage("NO WLAN", L, 4);
    return false;
  }
  netConnect();
  started = true;
  uint32_t t0 = millis();
  btnClear();
  while (poll()) {
    aiTick();
    NetState st = netState();
    if (st == NET_ONLINE) return true;
    if (st == NET_FAILED || millis() - t0 > 20000) {
      char b[32];
      snprintf(b, sizeof(b), "can't join %.16s", netSsid());
      const char *L[2] = { b, "OK = back" };
      aiMessage("NO WLAN", L, 2);
      return false;
    }
    aiTitle("AI CHAT");
    char b[32];
    snprintf(b, sizeof(b), "joining %.17s", netSsid());
    centerStr(36, b);
    for (uint8_t i = 0; i < 3; i++) if ((millis() / 300) % 4 > i) oled.drawBox(56 + i * 7, 44, 3, 3);
    oled.sendBuffer();
  }
  return false;
}

static void aiShow(const char *utf8) {
  aiAscii(utf8, aiShown, sizeof(aiShown));
  aiLines = aiWrap(aiShown, aiLn, AI_LINES);
  aiTop = 0;
}

void aiRun() {
  if (runClock < 80) { wlanNeedsClock(); return; }
  if (!netAiHasKey()) {
    if (!aiKeyEntry()) return;
  } else {
    static const char *const OPT[3] = { "ask Claude", "type a new API key", "forget the API key" };
    uint8_t o = chooseMode("AI CHAT", OPT, 3);
    if (o == 255) return;
    if (o == 1 && !aiKeyEntry()) return;
    if (o == 2) {
      netAiSetKey("");
      static const char *const L[3] = { "the key is gone.", "", "OK = back" };
      aiMessage("AI CHAT", L, 3);
      return;
    }
  }
  bool started = false;
  if (!aiOnline(started)) { if (started && !phoneLink) netStop(); return; }

  enum { AM_TYPE, AM_WAIT, AM_READ } mode = AM_TYPE;
  AiState s = netAiState();
  if (s == AI_ASKING) mode = AM_WAIT;                  // still busy with the last question
  else if (aiLines && (s == AI_DONE || s == AI_ERROR)) mode = AM_READ;
  btnClear();
  while (poll()) {
    aiTick();
    if (mode == AM_TYPE) {
      if (kbInput(aiKb)) {
        if (netAiAsk(aiQ)) mode = AM_WAIT;
        else { aiShow(netAiAnswer()); mode = AM_READ; }   // offline: says why
        btnClear();
        continue;
      }
      kbDraw(aiKb, "ASK CLAUDE", "send");
    } else if (mode == AM_WAIT) {
      s = netAiState();
      if (s != AI_ASKING) {
        aiShow(netAiAnswer());
        sfx(s == AI_DONE ? 1400 : 250, 80);
        mode = AM_READ;
        btnClear();
        continue;
      }
      aiTitle("CLAUDE");
      char dots[4] = { 0, 0, 0, 0 };
      for (uint8_t i = 0; i < (millis() / 400) % 4; i++) dots[i] = '.';
      char b[24];
      snprintf(b, sizeof(b), "thinking%s", dots);
      oled.drawStr(2, 26, b);
      AiLine q[3];
      uint16_t nq = aiWrap(aiQ, q, 3);              // the question below
      for (uint16_t i = 0; i < nq; i++) {
        char l[AI_COLS + 1];
        memcpy(l, aiQ + q[i].at, q[i].len);
        l[q[i].len] = 0;
        oled.drawStr(2, 42 + i * 8, l);
      }
    } else {
      uint16_t last = aiLines > AI_ROWS ? aiLines - AI_ROWS : 0;
      if (btn(B_UP)    && aiTop)        { aiTop--; sfx(700, 8); }
      if (btn(B_DOWN)  && aiTop < last) { aiTop++; sfx(700, 8); }
      if (btn(B_LEFT))  { aiTop = aiTop > AI_ROWS ? aiTop - AI_ROWS : 0; sfx(700, 8); }
      if (btn(B_RIGHT)) { aiTop = aiTop + AI_ROWS < last ? aiTop + AI_ROWS : last; sfx(700, 8); }
      if (btn(B_OK)) {                              // next question; after an error the old one stays
        if (netAiState() == AI_DONE) aiQ[0] = 0;
        mode = AM_TYPE;
        sfx(1200, 40);
        btnClear();
        continue;
      }
      aiTitle(netAiState() == AI_ERROR ? "NO ANSWER" : "CLAUDE");
      char b[16];
      if (aiLines > AI_ROWS) {
        snprintf(b, sizeof(b), "%u/%u", (unsigned)(aiTop + 1), (unsigned)(last + 1));
        rightStr(12, b);
      }
      for (uint8_t r = 0; r < AI_ROWS && aiTop + r < aiLines; r++) {
        const AiLine &l = aiLn[aiTop + r];
        char t[AI_COLS + 1];
        memcpy(t, aiShown + l.at, l.len);
        t[l.len] = 0;
        oled.drawStr(1, TOP_H + 7 + r * 8, t);
      }
    }
    oled.sendBuffer();
  }
  if (started && !phoneLink) netStop();
}

#endif
