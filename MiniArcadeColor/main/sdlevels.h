// MiniArcade Color: extra levels from the SD card. Part of console.cpp,
// which includes it after sd.h - not compiled on its own.
// Sokoban, Pumpkin Maze and the Block Puzzle adventure read packs from
// /sd/MINIARC/levels/<game>.txt: one level per line, a line that starts
// with '#' and has no '|' is a comment, empty lines are left out (the
// formats: sdcard/MINIARC/levels/README.txt). The card is only read,
// never formatted; the one thing written to it is the progress of these
// levels, /sd/MINIARC/save/<game>.sav, keyed by a hash of the level's
// line, so a changed pack never shows wrong progress. Nothing of the SD
// levels goes into the console's flash.
// A pack is never loaded whole: when a level list opens, the file is read
// once line by line and an index of where each good line starts is kept
// on the heap (999 levels at most), with the save file; a level is read
// into RAM (sdl.line) before it starts, so pulling the card goes
// unnoticed until the list opens again, which then says the card is gone.
// sdlFree() drops the index when a level starts, sdlEnd() everything when
// the game is left.
#include <sys/stat.h>

#ifndef SD_ROOT
#define SD_ROOT "/sd"                    // the simulator maps it to a folder
#endif
#ifndef SDL_ROOM                         // n more bytes on the heap leave enough for the rest
#include "esp_heap_caps.h"
#define SDL_ROOM(n) (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= (size_t)(n) + 16384)
#endif

#define SDL_MAX   999                    // levels of a pack at most
#define SDL_LINE  320                    // a level line at most (Pumpkin: a name and 13 rows of 20)
#define SDL_SAVES 999                    // progress records a save file keeps at most (the oldest go)
#define SDL_NOTE  4000                   // ms a note about the card stays on the level list

enum { SDL_NONE, SDL_OK, SDL_GONE, SDL_MEM };       // no levels (no card, no pack) / levels / card pulled / no room
enum { SDL_SAVED, SDL_SAME, SDL_FAIL };             // what sdlSave did

typedef bool (*SdlCheck)(const char *line);         // a game's check of a level line
typedef uint16_t (*SdlMerge)(uint16_t had, uint16_t now);   // progress: what to keep of the old and the new
struct SdlProg { uint32_t h; uint16_t v; };

struct SdLevels {
  const char *game;                      // "sokoban", "pumpkin", "blocks": the file names
  SdlCheck  ok;
  uint32_t *off;                         // the index: where each good line starts (while the list is open)
  SdlProg  *prog;                        // the save file read in (while the list is open)
  char     *line;                        // the level looked at or played, as read from the card
  uint16_t  n, bad, nProg;               // good levels, broken lines left out, progress records
  uint16_t  at;                          // the level in line
  uint32_t  hash, next;                  // its hash; where the line after it starts
  uint32_t  openAt;                      // when the list opened (the note)
  uint8_t   st;
  bool      fail;                        // the last progress could not be saved
};
static SdLevels sdl;
static bool sdlSeen = false;             // levels came from the card since the start: a missing card is told

// the card as it is now; one put back in is mounted again on its saved pin (once a minute at most)
static bool sdNow() {
  static uint32_t tried = 0;
  if (sdReady()) return true;
  if (tried && millis() - tried < 60000) return false;
  tried = millis() | 1;
  prefs.begin("arcade", true);
  uint16_t saved = prefs.getUShort("sdcs", 0);
  prefs.end();
  if (saved == 0 || saved >= 255 || tftPins.miso < 0 || !sdPinFree((uint8_t)(saved - 1))) return false;
  if (!sdMount((int8_t)(saved - 1))) return false;
  sdCs = (int8_t)(saved - 1);
  sdLast[0] = 0;
  return true;
}

static uint32_t sdlHash(const char *s) {               // FNV-1a
  uint32_t h = 2166136261u;
  for (; *s; s++) { h ^= (uint8_t)*s; h *= 16777619u; }
  return h;
}

static void sdlPath(char *b, size_t n, const char *dir, const char *ext) {
  snprintf(b, n, "%s/MINIARC/%s/%s.%s", SD_ROOT, dir, sdl.game, ext);
}

/* One line into b (n bytes) without its end and the blanks after it;
   0 = the end of the file (or an error), 1 = a line, 2 = a line too long
   for b, read to its end and left out.                               */
static uint8_t sdlGets(FILE *f, char *b, size_t n) {
  if (!fgets(b, (int)n, f)) return 0;
  size_t k = strlen(b);
  if (k && b[k - 1] != '\n' && k == n - 1) {
    int c;
    while ((c = fgetc(f)) != EOF && c != '\n') {}
    return 2;
  }
  while (k && (b[k - 1] == '\n' || b[k - 1] == '\r' || b[k - 1] == ' ' || b[k - 1] == '\t')) b[--k] = 0;
  return 1;
}
static bool sdlComment(const char *b) {
  if (!strncmp(b, "\xEF\xBB\xBF", 3)) b += 3;          // a text editor's BOM
  return !b[0] || (b[0] == '#' && !strchr(b, '|'));
}

// the save file: <game>.sav, or what an interrupted save left as <game>.tmp
static FILE *sdlSaveOpen() {
  char p[80];
  sdlPath(p, sizeof(p), "save", "sav");
  FILE *f = fopen(p, "r");
  if (f) return f;
  sdlPath(p, sizeof(p), "save", "tmp");
  return fopen(p, "r");
}
// one record "hash value"; false = a comment or broken
static bool sdlRecord(const char *b, SdlProg &r) {
  char *e;
  unsigned long h = strtoul(b, &e, 16);
  if (e != b + 8 || *e != ' ') return false;
  unsigned long v = strtoul(e + 1, &e, 10);
  if (*e || !v || v > 65535) return false;
  r.h = (uint32_t)h; r.v = (uint16_t)v;
  return true;
}

static void sdlFree() {                                  // the index and the progress: a level starts
  free(sdl.off); sdl.off = NULL;
  free(sdl.prog); sdl.prog = NULL;
  sdl.nProg = 0;
}
static void sdlEnd() {                                   // everything: the game is left
  sdlFree();
  free(sdl.line);
  memset(&sdl, 0, sizeof(sdl));
}

// the card was pulled: its levels are not offered any more, the list says so
static void sdlGone() {
  sdlFree();
  sdl.n = 0;
  sdl.st = SDL_GONE;
  sdl.openAt = millis() | 1;
  if (sdl.line) sdl.line[0] = 0;
}

// the progress of a level as read when the list opened, 0 = none
static uint16_t sdlProg(uint32_t h) {
  for (uint16_t i = 0; i < sdl.nProg; i++) if (sdl.prog[i].h == h) return sdl.prog[i].v;
  return 0;
}

/* The level list opens: the game's pack indexed, its save file read.
   Returns sdl.st; SDL_NONE without a card or a pack, as if there were
   no such thing.                                                     */
static uint8_t sdlOpen(const char *game, SdlCheck ok) {
  sdlFree();
  sdl.game = game; sdl.ok = ok;
  sdl.n = sdl.bad = 0;
  sdl.openAt = millis() | 1;
  sdl.st = SDL_NONE;
  if (!sdNow()) { if (sdlSeen) sdl.st = SDL_GONE; return sdl.st; }
  if (!sdl.line && (!SDL_ROOM(SDL_LINE) || !(sdl.line = (char *)malloc(SDL_LINE)))) { sdl.st = SDL_MEM; return sdl.st; }
  char p[80];
  sdlPath(p, sizeof(p), "levels", "txt");
  FILE *f = fopen(p, "r");
  if (!f) { if (sdlSeen) sdl.st = SDL_GONE; return sdl.st; }
  uint16_t cand = 0;                                     // first the lines that could be levels
  for (uint8_t r; cand < SDL_MAX && (r = sdlGets(f, sdl.line, SDL_LINE)); ) if (r == 2 || !sdlComment(sdl.line)) cand++;
  if (cand && (!SDL_ROOM(cand * 4u) || !(sdl.off = (uint32_t *)malloc(cand * 4u)))) { fclose(f); sdl.st = SDL_MEM; return sdl.st; }
  rewind(f);
  for (uint16_t k = 0; k < cand; ) {                     // then each one checked by the game
    long at = ftell(f);
    uint8_t r = sdlGets(f, sdl.line, SDL_LINE);
    if (!r) break;
    if (r == 1 && sdlComment(sdl.line)) continue;
    k++;
    if (r == 1 && at >= 0 && ok(sdl.line)) sdl.off[sdl.n++] = (uint32_t)at;
    else sdl.bad++;
  }
  fclose(f);
  if (!sdl.n && !sdl.bad) { sdlFree(); return sdl.st; }
  sdl.st = SDL_OK;                                       // (no good level but broken ones: they are told)
  if (sdl.n) sdlSeen = true;
  if ((f = sdlSaveOpen())) {                             // the progress: records counted, then read
    uint16_t k = 0;
    SdlProg r;
    while (k < SDL_SAVES && sdlGets(f, sdl.line, SDL_LINE)) if (sdlRecord(sdl.line, r)) k++;
    if (k && SDL_ROOM(k * sizeof(SdlProg)) && (sdl.prog = (SdlProg *)malloc(k * sizeof(SdlProg)))) {
      rewind(f);
      while (sdl.nProg < k && sdlGets(f, sdl.line, SDL_LINE)) if (sdlRecord(sdl.line, r)) sdl.prog[sdl.nProg++] = r;
    }
    fclose(f);
  }
  sdl.line[0] = 0;
  return sdl.st;
}

// a good line from the file position at into sdl.line (as level num); scan: broken lines on the way are
// passed over (going on to the next level); false = none, or the card is gone
static bool sdlReadAt(uint32_t at, uint16_t num, bool check, bool scan) {
  if (!sdl.line) return false;
  if (!sdReady()) { sdlGone(); return false; }
  char p[80];
  sdlPath(p, sizeof(p), "levels", "txt");
  FILE *f = fopen(p, "r");
  bool got = false;
  if (f && !fseek(f, (long)at, SEEK_SET)) {
    for (uint8_t r; !got && (r = sdlGets(f, sdl.line, SDL_LINE)); )
      if (r == 1 && !sdlComment(sdl.line)) {
        if (!check || sdl.ok(sdl.line)) got = true;
        else if (!scan) break;                           // the file changed since the list opened
      }
    long e = ftell(f);
    sdl.next = e > 0 ? (uint32_t)e : 0;
  }
  if (f) fclose(f);
  if (!got) { if (f) sdl.line[0] = 0; else sdlGone(); return false; }
  sdl.at = num;
  sdl.hash = sdlHash(sdl.line);
  return true;
}
// level i of the index into sdl.line (check: as the game needs it, before it is played)
static bool sdlRead(uint16_t i, bool check = true) { return sdl.off && i < sdl.n && sdlReadAt(sdl.off[i], i, check, false); }
// the level after the one in sdl.line (the index may be gone by then)
static bool sdlNext() { return sdl.next && sdl.at + 1 < sdl.n && sdlReadAt(sdl.next, (uint16_t)(sdl.at + 1), true, true); }

/* The progress of the level in sdl.line onto the card: merge decides
   what to keep of what was saved and the new result; only when that
   changes anything is the file written - as <game>.tmp, then renamed
   over <game>.sav. A full, write-protected or pulled card: SDL_FAIL,
   nothing else happens. had = what was saved before (0 = nothing).  */
static uint8_t sdlSave(uint16_t now, SdlMerge merge, uint16_t *had = NULL) {
  uint32_t h = sdl.hash;
  uint16_t old = 0, recs = 0;
  char b[40], p[80], q[80];
  sdl.fail = true;
  if (had) *had = 0;
  if (!sdReady()) return SDL_FAIL;
  sdlPath(p, sizeof(p), "save", "sav");
  sdlPath(q, sizeof(q), "save", "tmp");
  FILE *in = fopen(p, "r");
  if (!in && (in = fopen(q, "r"))) { fclose(in); rename(q, p); in = fopen(p, "r"); }   // an interrupted save: take its file
  SdlProg r;
  if (in) {
    while (sdlGets(in, b, sizeof(b))) if (sdlRecord(b, r)) { recs++; if (r.h == h) old = r.v; }
    if (ferror(in)) { fclose(in); return SDL_FAIL; }
  }
  if (had) *had = old;
  uint16_t keep = merge(old, now);
  if (keep == old) { if (in) fclose(in); sdl.fail = false; return SDL_SAME; }
  snprintf(b, sizeof(b), "%s/MINIARC", SD_ROOT);
  mkdir(b, 0777);
  snprintf(b, sizeof(b), "%s/MINIARC/save", SD_ROOT);
  mkdir(b, 0777);
  FILE *out = fopen(q, "w");
  if (!out) { if (in) fclose(in); return SDL_FAIL; }
  bool ok = fprintf(out, "# MiniArcade Color: progress of the SD levels (level hash, result)\n") > 0;
  uint16_t drop = !old && recs >= SDL_SAVES ? (uint16_t)(recs - SDL_SAVES + 1) : 0;   // full: the oldest go
  if (in) {
    rewind(in);
    while (ok && sdlGets(in, b, sizeof(b))) {
      if (!sdlRecord(b, r)) continue;
      if (drop) { drop--; continue; }
      if (r.h == h) r.v = keep;
      ok = fprintf(out, "%08lx %u\n", (unsigned long)r.h, r.v) > 0;
    }
    fclose(in);
  }
  if (ok && !old) ok = fprintf(out, "%08lx %u\n", (unsigned long)h, keep) > 0;
  ok = !ferror(out) && ok;
  ok = !fclose(out) && ok;
  if (!ok) { remove(q); return SDL_FAIL; }
  remove(p);
  if (rename(q, p)) return SDL_FAIL;                     // the new file stays as .tmp: read next time
  for (uint16_t i = 0; i < sdl.nProg; i++) if (sdl.prog[i].h == h) sdl.prog[i].v = keep;
  sdl.fail = false;
  return SDL_SAVED;
}

// fewer is better (moves); 0 = not solved
static uint16_t sdlFewer(uint16_t had, uint16_t now) { return had && had <= now ? had : now; }

// a short note over the list for a few seconds after it opened: card gone, no room, lines left out
static void sdlNote(int16_t y) {
  if (!sdl.openAt || millis() - sdl.openAt > SDL_NOTE) return;
  char b[64];
  if (sdl.st == SDL_GONE) snprintf(b, sizeof(b), "%s", TR("SD card removed: built-in levels only", "SD-Karte fehlt: nur eingebaute Level"));
  else if (sdl.st == SDL_MEM) snprintf(b, sizeof(b), "%s", TR("SD levels: not enough memory", "SD-Level: zu wenig Speicher"));
  else if (sdl.st == SDL_OK && sdl.bad) snprintf(b, sizeof(b), TR("SD card: %u levels, %u broken skipped", "SD-Karte: %u Level, %u kaputte übersprungen"), sdl.n, sdl.bad);
  else return;
  int16_t w = (int16_t)(Gfx::width(b, F_SMALL) + 16);
  gfx.rbox((int16_t)((SCR_W - w) / 2), y, w, 17, 5, sdl.st == SDL_OK ? C::DKBLUE : C::DKRED);
  gfx.rframe((int16_t)((SCR_W - w) / 2), y, w, 17, 5, C::WHITE);
  gfx.center((int16_t)(y + 4), b, C::WHITE, F_SMALL);
}

// the small "SD" mark of a level from the card
static void sdlTag(int16_t x, int16_t y) {
  gfx.rbox(x, y, 22, 13, 3, C::CYAN);
  gfx.text((int16_t)(x + 5), (int16_t)(y + 2), "SD", C::NAVY, F_SMALL);
}
