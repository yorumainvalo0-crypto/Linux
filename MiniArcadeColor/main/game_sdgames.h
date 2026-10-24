// MiniArcade Color: SD games - games in MAG script from the SD card.
// Part of console.cpp, which includes it after mag.h - not compiled on
// its own. The library entry "SD games" lists /sd/MINIARC/games/*.mag
// (only the header lines of each file are read), compiles the one picked
// (mag.h) and runs it: init() once, then update() and draw() every
// frame, 30 or 60 times a second. The pause menu, quitting and the game
// over screen are the console's own. High score and save() slots go to
// /sd/MINIARC/save/<file>.sav, written as .tmp and renamed (as the level
// packs of sdlevels.h do); nothing else is ever written to the card.
// A card pulled during a game only matters when the game needs a page of
// its file that is not cached: then it waits for the card, mounts it
// again, checks that the file is the same, and goes on.

#include <dirent.h>
#include <strings.h>

#define MAG_LIST   64                    // games listed at most
#define MAG_DIR    "MINIARC/games"

struct MagEntry { char file[13], title[25], author[25], info[101]; bool bad; };

static const char *const IC_SDGAMES[12] = {
  "..ssssssss..", ".skkkkkkkks.", "skwwkwwkwwks", "skkkkkkkkkks", "skkyyyyyykks", "skyykkkkyyks",
  "skykkcckkyks", "skyykkkkyyks", "skkyyyyyykks", "skkkkkkkkkks", "skkkkkkkkkks", "ssssssssssss" };

// the save file's path and its .tmp
static void magSavePath(char *b, size_t n, const char *file, const char *ext) {
  char base[13];
  snprintf(base, sizeof(base), "%s", file);
  char *dot = strrchr(base, '.');
  if (dot) *dot = 0;
  snprintf(b, n, "%s/MINIARC/save/%s.%s", SD_ROOT, base, ext);
}
// level packs keep their progress in these names: a game may not take them
static bool magNameTaken(const char *file) {
  static const char *const PACK[] = { "sokoban.", "pumpkin.", "blocks." };
  for (const char *p : PACK) if (!strncasecmp(file, p, strlen(p))) return true;
  return false;
}

// ---------------- the save file ----------------
static void magSaveRead(const char *file, uint16_t *best, int32_t *slots) {
  char p[84], b[48];
  magSavePath(p, sizeof(p), file, "sav");
  FILE *f = fopen(p, "r");
  if (!f) { magSavePath(p, sizeof(p), file, "tmp"); f = fopen(p, "r"); }      // what an interrupted save left
  if (!f) return;
  while (sdlGets(f, b, sizeof(b))) {
    long k, val;
    if (sscanf(b, "best %ld", &val) == 1 && val > 0 && val <= 65535) *best = (uint16_t)val;
    else if (slots && sscanf(b, "slot %ld %ld", &k, &val) == 2 && k >= 0 && k < MAG_SLOTS) slots[k] = (int32_t)val;
  }
  fclose(f);
}
// best and slots onto the card: written as .tmp, then renamed over .sav; false = no card, full, ...
static bool magSaveWrite() {
  MagVm &v = *mag;
  v.savedAt = millis();
  if (!sdReady()) return false;
  char p[84], q[84], b[48];
  const char *file = strrchr(v.path, '/') + 1;
  magSavePath(p, sizeof(p), file, "sav");
  magSavePath(q, sizeof(q), file, "tmp");
  snprintf(b, sizeof(b), "%s/MINIARC", SD_ROOT);
  mkdir(b, 0777);
  snprintf(b, sizeof(b), "%s/MINIARC/save", SD_ROOT);
  mkdir(b, 0777);
  FILE *out = fopen(q, "w");
  if (!out) return false;
  bool ok = fprintf(out, "# MiniArcade Color: SD game save (best score, save slots)\nbest %u\n", v.best) > 0;
  for (uint8_t i = 0; i < MAG_SLOTS && ok; i++) if (v.slots[i]) ok = fprintf(out, "slot %u %ld\n", i, (long)v.slots[i]) > 0;
  ok = !ferror(out) && ok;
  ok = !fclose(out) && ok;
  if (!ok) { remove(q); return false; }
  remove(p);
  if (rename(q, p)) return false;                   // the .tmp stays: read next time
  v.dirty = false;
  return true;
}
static void magSaveSlot(int32_t slot, int32_t val) {
  if (mag->slots[slot] == val) return;
  mag->slots[slot] = val;
  mag->dirty = true;
}
// a new record from the game over screen: onto the card, not into the flash
static void magHigh(uint16_t v) { mag->best = v; magSaveWrite(); }

// the file's size and the hash of its first MAG_HEAD bytes: the same file after a remount; false = unreadable
static bool __attribute__((noinline)) magFileId(const char *path, uint32_t &size, uint32_t &head) {
  int fd = open(path, O_RDONLY);
  if (fd < 0) return false;
  off_t n = lseek(fd, 0, SEEK_END);
  bool ok = n >= 0 && lseek(fd, 0, SEEK_SET) == 0;
  uint8_t b[64];
  uint32_t h = 2166136261u, left = MAG_HEAD;
  while (ok && left) {
    ssize_t k = read(fd, b, sizeof(b));
    if (k < 0) ok = false;
    if (k <= 0) break;
    for (ssize_t i = 0; i < k; i++) { h ^= b[i]; h *= 16777619u; }   // FNV-1a
    left = left > (uint32_t)k ? left - (uint32_t)k : 0;
  }
  close(fd);
  size = (uint32_t)n; head = h;
  return ok;
}

// ---------------- screens ----------------
static void magTitle(const char *t, col accent) {
  gfx.gradient(0, 0, SCR_W, SCR_H, colDark(accent, 3), C::BLACK);
  gfx.textShadow(10, 8, t, C::WHITE, C::BLACK, F_BOLD);
  gfx.hline(0, 38, SCR_W, accent);
}
// how much of s fits a line of w pixels: cut at a blank (a word too long for a line: where it no longer fits)
static size_t magWrapCut(const char *s, int16_t w, Font f) {
  char b[100];
  size_t k = 0, cut = 0;
  while (s[k] && k < sizeof(b) - 1) {
    memcpy(b, s, k + 1); b[k + 1] = 0;
    if (Gfx::width(b, f) > w) break;
    k++;
    if (!s[k] || s[k] == ' ') cut = k;
  }
  if (!s[k]) cut = k;
  return cut ? cut : k ? k : 1;
}
// text wrapped into lines of w pixels from y (most lines); returns the y after it
static int16_t magWrap(int16_t x, int16_t y, int16_t w, const char *s, col c, Font f, uint8_t most) {
  char b[100];
  for (uint8_t n = 0; *s && n < most; n++) {
    size_t cut = magWrapCut(s, w, f);
    memcpy(b, s, cut); b[cut] = 0;
    gfx.text(x, y, b, c, f);
    y = (int16_t)(y + Gfx::height(f) + 3);
    s += cut;
    while (*s == ' ') s++;
  }
  return y;
}

// line n of the game's file (for an error), "" when it cannot be read
static void magSourceLine(const char *path, uint16_t n, char *out, size_t max) {
  out[0] = 0;
  if (!n || !sdReady()) return;
  FILE *f = fopen(path, "rb");
  if (!f) return;
  char b[96];
  uint16_t at = 1;
  bool start = true;
  while (fgets(b, sizeof(b), f)) {
    if (at == n && start) {
      const char *s = b;
      while (*s == ' ' || *s == '\t') s++;
      snprintf(out, max, "%.*s", (int)(max - 1), s);
      size_t k = strlen(out);
      while (k && (out[k - 1] == '\n' || out[k - 1] == '\r')) out[--k] = 0;
      for (char *p = out; *p; p++) if (*p == '\t') *p = ' ';
      break;
    }
    start = strchr(b, '\n') != NULL;                 // a long line goes on in the next piece
    if (start) at++;
  }
  fclose(f);
}

// an error: what, where, the line of the file; OK or holding OK goes back
static void __attribute__((noinline)) magShowError(const char *path, const char *kind, const char *msg, uint16_t line) {
  NoPause np;
  char src[64], b[140];
  magSourceLine(path, line, src, sizeof(src));
  if (line) snprintf(b, sizeof(b), "line %u: %s", line, msg);
  else snprintf(b, sizeof(b), "%s", msg);
  const char *file = strrchr(path, '/');
  file = file ? file + 1 : path;
  sfx(200, 300);
  btnClear();
  while (poll()) {
    if (btn(B_OK)) break;
    magTitle(kind, C::RED);
    gfx.text(10, 46, file, C::LTGREY, F_SMALL);
    Font f = Gfx::width(b, F_TEXT) <= 4 * 300 ? F_TEXT : F_SMALL;
    int16_t y = magWrap(10, 62, 300, b, C::YELLOW, f, f == F_TEXT ? 5 : 9);
    if (src[0]) {
      y = (int16_t)(y + 6);
      gfx.rbox(6, y, SCR_W - 12, 20, 4, C::NAVY);
      gfx.rframe(6, y, SCR_W - 12, 20, 4, C::BLUE);
      char t[60];
      snprintf(t, sizeof(t), "%.*s", (int)sizeof(t) - 1, src);
      while (Gfx::width(t, F_SMALL) > SCR_W - 24) t[strlen(t) - 1] = 0;
      gfx.text(12, (int16_t)(y + 6), t, C::WHITE, F_SMALL);
    }
    char h[48];
    snprintf(h, sizeof(h), TR("%s = back", "%s = zurück"), okKeys());
    gfx.center(SCR_H - 16, h, C::GREY, F_SMALL);
    gfx.send();
  }
  btnClear();
}

/* The card is out and the game needs a page it has not cached: it waits
   here. Every second the card is mounted again if it is back; the file
   must be the one the game was compiled from (size and the hash of its
   first bytes). Holding OK leaves the game. true = go on.            */
static bool magCardWait() {
  MagVm &v = *mag;
  if (v.fd >= 0) { close(v.fd); v.fd = -1; }
  NoPause np;
  v.waits++;
  uint32_t t0 = millis(), tryAt = 0;
  bool ok = false;
  sfx(300, 150);
  btnClear();
  while (poll()) {
    if (millis() - tryAt >= 1000) {
      tryAt = millis();
      uint32_t size, head;
      if (sdNow(1000) && magFileId(v.path, size, head)) {
        if (size == v.size && head == v.head && (v.fd = open(v.path, O_RDONLY)) >= 0) { ok = true; break; }
        magFail("the game's file on the card has changed - start it again");
        break;
      }
    }
    gfx.gradient(0, 0, SCR_W, SCR_H, C::DKBLUE, C::BLACK);
    gfx.rbox(130, 50, 60, 76, 6, C::GREY);           // a card
    gfx.fill(130, 50, 18, 14, C::DKBLUE);
    for (int16_t i = 0; i < 4; i++) gfx.fill((int16_t)(152 + i * 9), 56, 5, 12, C::YELLOW);
    gfx.center(140, TR("insert the SD card", "SD-Karte wieder einstecken"), C::YELLOW, F_TEXT);
    gfx.center(166, TR("the game goes on when it is back", "das Spiel geht dann weiter"), C::LTGREY, F_SMALL);
    char h[48];
    snprintf(h, sizeof(h), TR("hold %s = quit", "%s halten = beenden"), okKeys());
    gfx.center(SCR_H - 16, h, C::GREY, F_SMALL);
    gfx.send();
  }
  pausedMs += millis() - t0;                         // game time stood still
  btnClear();
  return ok;
}

// ---------------- running a game ----------------
static void magEnd() {
  MagVm *v = mag;
  if (!v) return;
  if (v->dirty) magSaveWrite();
  if (v->fd >= 0) close(v->fd);
  free(v->code); free(v->lines); free(v->fn); free(v->g); free(v->spr); free(v->map); free(v->rows); free(v->lvl);
  free(v->ts); free(v->pg); free(v->pool); free(v->sprAt); free(v->mbuf); free(v->stack);
  free(v);
  mag = NULL;
}

// the start, and every new round: the vars as declared, then init()
static bool magStart() {
  MagVm &v = *mag;
  memset(v.g, 0, (size_t)v.nG * 4);
  v.score = 0; v.bar = false; v.over = false; v.mid = -1;
  v.t0 = gameMillis();
  v.budget = v.budgetMax = MAG_BUDGET0;
  if (!magRun(v.fGinit, "init")) return false;
  return v.fInit == 255 || magRun(v.fInit, "init");
}

static uint16_t magClamp16(int32_t s) { return (uint16_t)(s < 0 ? 0 : s > 65535 ? 65535 : s); }

/* The game at path into mag, compiled and ready to run. 0 = ready; 1 =
   cannot start (memory, file), 2 = an error in the game: msg says what,
   line where (0 = not about a line); mag is gone then.               */
static uint8_t magLoad(const char *path, char *msg, size_t msgN, uint16_t *line) {
  *line = 0;
  size_t heap = 0;
  mag = (MagVm *)magAlloc(sizeof(MagVm), &heap);
  if (!mag) { snprintf(msg, msgN, "not enough memory"); return 1; }
  MagVm &v = *mag;
  v.heap = heap;
  v.fd = -1; v.mid = -1;
  snprintf(v.path, sizeof(v.path), "%s", path);
  // the file as it is now: size and the hash of its start, to know it again after a remount
  if (!magFileId(path, v.size, v.head)) { magEnd(); snprintf(msg, msgN, "cannot read the file"); return 1; }
  if (!magCompile(mag, msg, msgN, line)) { magEnd(); return *line ? 2 : 1; }   // line 0: memory or the file
  v.stack = (int32_t *)magAlloc(MAG_STACK * 4, &v.heap);
  v.pg = (uint8_t *)magAlloc(MAG_PAGES * MAG_PAGE, &v.heap);
  v.pool = (uint8_t *)magAlloc(MAG_POOL, &v.heap);
  v.sprAt = (int16_t *)magAlloc((size_t)v.nSpr * 2, &v.heap);
  v.fd = open(path, O_RDONLY);
  if (!v.stack || !v.pg || !v.pool || !v.sprAt || v.fd < 0) {
    snprintf(msg, msgN, "%s", v.fd >= 0 ? "not enough memory" : "cannot read the file");
    magEnd();
    return 1;
  }
  for (uint16_t i = 0; i < v.nSpr; i++) v.sprAt[i] = -1;
  return 0;
}

static size_t magHeapPeak = 0;                       // the most a game took at once (the simulator reports it)
static void (*magFrameHook)() = NULL;                // the simulator looks at every frame

// file in MINIARC/games: compiled and played until the player leaves
static void __attribute__((noinline)) magPlay(const char *file) {
  char path[80];
  snprintf(path, sizeof(path), "%s/" MAG_DIR "/%s", SD_ROOT, file);
  if (magNameTaken(file)) {
    magShowError(path, TR("CANNOT START", "START GEHT NICHT"), "sokoban, pumpkin and blocks are the names of the level packs' save files - rename the .mag file", 0);
    return;
  }
  // compile (the screen says so: a big file takes a moment)
  gfx.gradient(0, 0, SCR_W, SCR_H, C::DKBLUE, C::BLACK);
  gfx.center(110, TR("loading...", "lädt..."), C::LTGREY, F_TEXT);
  gfx.send();
  char msg[120];
  uint16_t line = 0;
  uint8_t r = magLoad(path, msg, sizeof(msg), &line);
  if (r) {
    magShowError(path, r == 2 ? TR("ERROR IN THE GAME", "FEHLER IM SPIEL") : TR("CANNOT START", "START GEHT NICHT"), msg, line);
    return;
  }
  MagVm &v = *mag;
  magSaveRead(file, &v.best, v.slots);
  // the console's side of a game, as the library sets it for the built-in ones
  curHigh = v.best;
  highHook = magHigh;
  pauseOk = true; tiltGame = true;
  tiltGameStart();
  wantRestart = false;
  btnClear();
  bool run = magStart();
  uint32_t period = 1000 / v.fps, next = millis();
  while (run) {
    next += period;
    int32_t wait = (int32_t)(next - millis());
    if (wait > 1) delay((uint32_t)(wait - 1));
    else if (wait < -(int32_t)period) next = millis();   // fell behind: no catching up
    if (!poll()) {
      if (!wantRestart) break;
      wantRestart = false;                             // the pause menu's "restart"
      run = magStart();
      continue;
    }
    for (uint8_t k = 0; k < B_COUNT; k++) { v.press[k] = k == B_OK ? btnTap(B_OK) : btn(k); v.held[k] = btnHeld(k); }
    v.budget = v.budgetMax = MAG_BUDGET;
    bool ok = magRun(v.fUpdate, "update") && magRun(v.fDraw, "draw");
    v.opsFrame = (uint32_t)(MAG_BUDGET - v.budget);
    v.frames++; v.opsSum += v.opsFrame;
    if (v.opsFrame > v.opsMax) v.opsMax = v.opsFrame;
    if (v.heap > magHeapPeak) magHeapPeak = v.heap;
    if (!ok) break;
    if (v.bar) {                                       // the title as far as it fits left of the score
      char t[15];
      snprintf(t, sizeof(t), "%.14s", v.title);
      statusBar(t, magClamp16(v.score), curHigh, C::CYAN);
    }
    if (magFrameHook) magFrameHook();
    gfx.send();
    if (v.over) {
      v.over = false;
      if (v.dirty) magSaveWrite();
      if (!gameOver(magClamp16(v.overScore))) break;
      run = magStart();
      next = millis();
      continue;
    }
    if (v.dirty && millis() - v.savedAt > 2000) magSaveWrite();   // at most every 2 s
  }
  if (v.heap > magHeapPeak) magHeapPeak = v.heap;
  pauseOk = tiltGame = false;
  wantRestart = false;
  highHook = NULL;
  if (v.failed) {
    char err[100];
    snprintf(err, sizeof(err), "%s", v.err);
    uint16_t l = magLineAt(v.errPc);
    magEnd();
    magShowError(path, TR("THE GAME STOPPED", "DAS SPIEL BRACH AB"), err, l);
  } else magEnd();
  btnClear();
}

// ---------------- the list ----------------
// the header lines of a file: game, author, info (nothing more is read)
static void magPeek(const char *file, MagEntry &e) {
  char p[84], b[120];
  snprintf(p, sizeof(p), "%s/" MAG_DIR "/%.12s", SD_ROOT, file);
  memset(&e, 0, sizeof(e));
  snprintf(e.file, sizeof(e.file), "%.12s", file);
  snprintf(e.title, sizeof(e.title), "%.12s", file);
  e.bad = true;
  FILE *f = fopen(p, "r");
  if (!f) return;
  for (uint8_t n = 0; n < 40; n++) {
    uint8_t r = sdlGets(f, b, sizeof(b));
    if (!r) break;
    if (r == 2) break;
    char *s = b;
    if (!n && !strncmp(s, "\xEF\xBB\xBF", 3)) s += 3;
    while (*s == ' ' || *s == '\t') s++;
    if (!*s || !strncmp(s, "--", 2)) continue;
    char *q1 = strchr(s, '"'), *q2 = q1 ? strchr(q1 + 1, '"') : NULL;
    char *dst = NULL; size_t max = 0;
    if (!strncmp(s, "game", 4) && (s[4] == ' ' || s[4] == '"')) { dst = e.title; max = sizeof(e.title); }
    else if (!strncmp(s, "author", 6) && (s[6] == ' ' || s[6] == '"')) { dst = e.author; max = sizeof(e.author); }
    else if (!strncmp(s, "info", 4) && (s[4] == ' ' || s[4] == '"')) { dst = e.info; max = sizeof(e.info); }
    else if (!strncmp(s, "fps", 3)) continue;
    else break;
    if (!q1 || !q2) break;
    *q2 = 0;
    snprintf(dst, max, "%s", q1 + 1);
    if (dst == e.title) e.bad = !e.title[0];
    if (e.bad) break;
  }
  fclose(f);
  if (e.bad) snprintf(e.info, sizeof(e.info), "%s", TR("no 'game' line at the start: not a MAG game?", "keine 'game'-Zeile am Anfang: kein MAG-Spiel?"));
}

static bool magIsMag(const char *n) {
  size_t k = strlen(n);
  return k > 4 && k <= 12 && !strcasecmp(n + k - 4, ".mag") && n[0] != '.';
}

// every .mag file of the folder, sorted by title; NULL with n = 0: none (or no memory)
static MagEntry * __attribute__((noinline)) magList(uint16_t &n) {
  n = 0;
  char p[64];
  snprintf(p, sizeof(p), "%s/" MAG_DIR, SD_ROOT);
  DIR *d = opendir(p);
  if (!d) return NULL;
  uint16_t k = 0;
  while (dirent *e = readdir(d)) if (magIsMag(e->d_name)) k++;
  if (k > MAG_LIST) k = MAG_LIST;
  MagEntry *l = k ? (MagEntry *)magAlloc(k * sizeof(MagEntry), NULL) : NULL;
  if (!l) { closedir(d); return NULL; }
  rewinddir(d);
  while (dirent *e = readdir(d)) if (n < k && magIsMag(e->d_name)) magPeek(e->d_name, l[n++]);
  closedir(d);
  for (uint16_t i = 1; i < n; i++)                   // by title
    for (uint16_t j = i; j > 0 && strcasecmp(l[j - 1].title, l[j].title) > 0; j--) { MagEntry t = l[j]; l[j] = l[j - 1]; l[j - 1] = t; }
  return l;
}

static uint16_t magBest(const char *file) { uint16_t b = 0; magSaveRead(file, &b, NULL); return b; }   // from its save file

// the library entry "SD games"
void sdGamesRun() {
  uint16_t n = 0, sel = 0, top = 0, best = 0, bestOf = 0xFFFF;
  MagEntry *list = NULL;
  bool card = sdNow(1000), scanned = false;
  uint32_t looked = millis();
  const uint8_t ROWS = 5;
  btnClear();
  while (poll()) {
    if (!scanned && card) { list = magList(n); scanned = true; sel = top = 0; bestOf = 0xFFFF; }
    if (!card && millis() - looked > 1500) { looked = millis(); card = sdNow(1000); }   // put in while the screen shows
    if (n) {
      if (btn(B_UP))   { sel = sel ? sel - 1 : n - 1; sfx(700, 15); }
      if (btn(B_DOWN)) { sel = (sel + 1) % n;         sfx(700, 15); }
      if (btn(B_OK)) {
        sfx(1200, 50);
        char file[13];
        snprintf(file, sizeof(file), "%s", list[sel].file);
        free(list); list = NULL;                       // the memory goes to the game
        magPlay(file);
        list = magList(n);                             // the card may have changed meanwhile
        if (sel >= n) sel = n ? n - 1 : 0;
        card = n || sdReady();
        bestOf = 0xFFFF;
        btnClear();
        continue;
      }
    } else if (btn(B_OK)) break;
    if (sel < top) top = sel;
    if (sel >= top + ROWS) top = (uint16_t)(sel - ROWS + 1);
    magTitle(TR("SD GAMES", "SD-SPIELE"), C::CYAN);
    char b[64];
    if (!n) {
      gfx.center(90, card ? TR("no games on the card", "keine Spiele auf der Karte") : TR("no SD card", "keine SD-Karte"), C::YELLOW, F_TEXT);
      gfx.center(126, TR("put .mag files into MINIARC/games", ".mag-Dateien nach MINIARC/games"), C::LTGREY, F_SMALL);
      gfx.center(142, TR("(sdcard/MINIARC/games/README.txt)", "(sdcard/MINIARC/games/README.txt)"), C::GREY, F_SMALL);
      snprintf(b, sizeof(b), TR("%s = back", "%s = zurück"), okKeys());
      gfx.center(SCR_H - 16, b, C::GREY, F_SMALL);
      gfx.send();
      continue;
    }
    snprintf(b, sizeof(b), TR("%u games   %s = play", "%u Spiele   %s = spielen"), n, okKeys());
    gfx.right(SCR_W - 10, 16, b, C::LTGREY, F_SMALL);
    for (uint8_t i = 0; i < ROWS && top + i < n; i++) {
      uint16_t k = (uint16_t)(top + i);
      int16_t y = (int16_t)(44 + i * 30);
      bool on = k == sel;
      if (on) { gfx.rbox(4, y, SCR_W - 8, 27, 5, colDark(C::CYAN, 1)); gfx.rframe(4, y, SCR_W - 8, 27, 5, C::WHITE); }
      gfx.text(14, (int16_t)(y + 5), list[k].title, list[k].bad ? C::RED : on ? C::WHITE : C::LTGREY, F_TEXT);
      gfx.right(SCR_W - 12, (int16_t)(y + 10), list[k].file, on ? C::LTGREY : C::GREY, F_SMALL);
    }
    if (top > 0) gfx.triangle(160, 40, 154, 43, 166, 43, C::WHITE);
    if (top + ROWS < n) gfx.triangle(160, 197, 154, 194, 166, 194, C::WHITE);
    if (bestOf != sel) { best = magBest(list[sel].file); bestOf = sel; }
    const MagEntry &e = list[sel];
    gfx.fill(0, 198, SCR_W, SCR_H - 198, C::NAVY);
    gfx.hline(0, 198, SCR_W, C::CYAN);
    if (e.author[0]) snprintf(b, sizeof(b), TR("by %s", "von %s"), e.author);
    else b[0] = 0;
    gfx.text(8, 202, b, C::CYAN, F_SMALL);
    if (best) { char s[24]; snprintf(s, sizeof(s), TR("best %u", "Rekord %u"), best); gfx.right(SCR_W - 8, 202, s, C::YELLOW, F_SMALL); }
    magWrap(8, 214, SCR_W - 16, e.info, C::LTGREY, F_SMALL, 2);
    gfx.send();
  }
  free(list);
  btnClear();
}
