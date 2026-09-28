// MiniArcade: what the console tells the phone pages (phone.cpp serves them).
// Part of MiniArcade.ino, which includes it - not compiled on its own.

// =========================================================
//  PHONE   (ESP-IDF build only: stats, settings, screen, keys, levels)
// =========================================================
/* Everything but appKey is called from netTick() in the UI loop, while no
   game is in the middle of drawing - so the game state can be read and
   changed freely here.                                                 */
#ifdef HAVE_NET
#include <stdarg.h>

// appends to out, never past max; returns the new length
static int jsonAdd(char *out, int max, int n, const char *fmt, ...) {
  if (n >= max - 1) return n;
  va_list a;
  va_start(a, fmt);
  int k = vsnprintf(out + n, max - n, fmt, a);
  va_end(a);
  if (k < 0) return n;
  return n + k < max - 1 ? n + k : max - 1;
}

int appStats(char *out, int max) {
  statLoad();
  int n = jsonAdd(out, max, 0, "{\"games\":[");
  uint8_t games = realGames();
  for (uint8_t g = 0; g < games; g++)
    n = jsonAdd(out, max, n, "%s{\"n\":\"%s\",\"b\":%u,\"p\":%u,\"s\":%lu}", g ? "," : "",
                gameName(g), loadHigh(g), g < ST_GAMES ? st.plays[g] : 0,
                (unsigned long)(g < ST_GAMES ? st.secs[g] : 0));
  n = jsonAdd(out, max, n, "],\"awards\":[");
  for (uint8_t i = 0; i < AWARD_N; i++)
    n = jsonAdd(out, max, n, "%s{\"n\":\"%s\",\"h\":\"%s\",\"w\":%u}", i ? "," : "",
                AWARDS[i].name, AWARDS[i].how, (st.awards >> i) & 1 ? 1 : 0);
  n = jsonAdd(out, max, n, "],\"online\":[%u,%u],\"total\":%lu}", st.mpGames, st.mpWins,
              (unsigned long)statTotalSecs());
  return n;
}

int appSettings(char *out, int max) {
  const char *name = "PLAYER";
#ifdef HAVE_LINK
  name = linkName();
#endif
  return snprintf(out, max, "{\"name\":\"%s\",\"bright\":%u,\"sleep\":%u,\"sound\":%u,"
                  "\"clock\":%u,\"buzzer\":%u}",
                  name, (cfgBright * 100 + 127) / 255, cfgSleep, cfgMute ? 0 : 1,
                  cfgClock, sndPin != 255 ? 1 : 0);
}

const char *appSet(const char *key, const char *val) {
  int v = atoi(val);
  if (!strcmp(key, "bright")) {
    if (v < 1 || v > 100) return "brightness 1..100";
    cfgBright = (uint8_t)(v * 255 / 100);
    applyCfg();
  } else if (!strcmp(key, "sleep")) {
    if (v < 0 || v > 60) return "sleep 0..60 minutes";
    cfgSleep = (uint8_t)v;
    lastInput = millis();
  } else if (!strcmp(key, "sound")) {
    cfgMute = !v;
    sfx(1000, 60);                                // a beep to hear it (silent when off)
  } else if (!strcmp(key, "clock")) {
    if (v != 80 && v != 160) return "clock 80 or 160 MHz";
    cfgClock = (uint8_t)v;                        // used from the next start on
  } else if (!strcmp(key, "name")) {
#ifdef HAVE_LINK
    char n[LK_NAME + 1];
    uint8_t k = 0;
    for (const char *p = val; *p && k < LK_NAME; p++) {   // the letters the console can show
      char c = *p >= 'a' && *p <= 'z' ? *p - 32 : *p;
      if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || (c == ' ' && k)) n[k++] = c;
    }
    while (k && n[k - 1] == ' ') k--;
    n[k] = 0;
    if (!k) return "name: letters and digits, up to 6";
    linkSetName(n);
    return NULL;
#else
    return "no multiplayer in this build";
#endif
  } else return "unknown setting";
  saveCfg();
  return NULL;
}

void appScreen(uint8_t *out) { memcpy(out, oled.getBufferPtr(), 1024); }

void appKey(uint8_t key, bool down) {
  if (key >= B_COUNT) return;
  vKeyUntil[key] = down ? ((millis() + 700) | 1) : 0;   // the phone repeats it while held
}

// ---------------- Sokoban levels from the phone ----------------
/* Checks a level before it is kept: the known characters, one player, as
   many goals as boxes, and walls all round the part the player can reach
   (so nobody walks off the map). Solvable or not is up to its maker.  */
const char *skCheckLevel(const char *t) {
  static char g[SK_H][SK_W + 1];
  static uint8_t seen[SK_H][SK_W];
  memset(g, 0, sizeof(g));
  uint8_t w[SK_H] = { 0 }, h = 0, x = 0, px = 0, py = 0;
  int players = 0, boxes = 0, goals = 0;
  if (strlen(t) > SK_TEXTMAX) return "level too big";
  for (const char *p = t; ; p++) {
    if (*p == '|' || !*p) {
      w[h++] = x; x = 0;
      if (!*p) break;
      if (h >= SK_H) return "at most 8 rows";
      continue;
    }
    if (x >= SK_W) return "at most 21 columns";
    if (!strchr("#.$*@+ ", *p)) return "unknown character";
    if (*p == '@' || *p == '+') { players++; px = x; py = h; }
    if (*p == '$' || *p == '*') boxes++;
    if (*p == '.' || *p == '*' || *p == '+') goals++;
    g[h][x++] = *p;
  }
  if (players != 1) return "exactly one player";
  if (!boxes) return "at least one box";
  if (boxes != goals) return "as many goals as boxes";
  memset(seen, 0, sizeof(seen));                    // flood fill from the player
  uint8_t stack[SK_W * SK_H][2], n = 0;
  stack[n][0] = px; stack[n][1] = py; n++;
  seen[py][px] = 1;
  uint8_t inside = 0;
  while (n) {
    n--;
    uint8_t cx = stack[n][0], cy = stack[n][1];
    if (strchr("$*.", g[cy][cx])) inside++;
    static const int8_t DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
    for (uint8_t d = 0; d < 4; d++) {
      int nx = cx + DX[d], ny = cy + DY[d];
      if (ny < 0 || ny >= h || nx < 0 || nx >= w[ny]) return "the walls must close the level";
      if (g[ny][nx] == '#' || seen[ny][nx]) continue;
      seen[ny][nx] = 1;
      stack[n][0] = nx; stack[n][1] = ny; n++;
    }
  }
  int things = 0;
  for (uint8_t yy = 0; yy < h; yy++)
    for (uint8_t xx = 0; xx < w[yy]; xx++) if (strchr("$*.", g[yy][xx])) things++;
  if (inside != things) return "boxes and goals must be where the player can get";
  return NULL;
}

const char *appLevel(uint8_t slot, const char *text) {
  if (slot >= SK_OWN) return "no such slot";
  if (*text) {
    const char *why = skCheckLevel(text);
    if (why) return why;
  }
  char k[4] = { 's', 'k', (char)('0' + slot), 0 };
  prefs.begin("arcade", false);
  prefs.putBytes(k, text, strlen(text) + 1);        // "" = slot free again
  prefs.end();
  skOwnLoad();
  return NULL;
}

int appLevelGet(uint8_t slot, char *out, int max) {
  if (slot >= SK_OWN || max < 1) return 0;
  skOwnLoad();
  strlcpy(out, skOwn[slot], max);
  return strlen(out);
}
#endif
