// MiniArcade: statistics and awards. Part of MiniArcade.ino, which includes
// it - not compiled on its own.

// =========================================================
//  STATS + AWARDS
// =========================================================
/* Kept as one small blob in flash and written only when a game is left,
   so the flash does not wear out. Times are counted with gameMillis(),
   the pause menu does not count.                                        */
#define ST_GAMES 32                     // room for this many games

struct __attribute__((packed)) StatBlob {
  uint8_t  ver;                         // 3 (version 2 had 32 awards, version 1 16 games)
  uint16_t plays[ST_GAMES];             // times started (a restart counts)
  uint32_t secs[ST_GAMES];              // seconds played
  uint64_t awards;                      // one bit per award
  uint16_t mpGames, mpWins;             // online games
};
static StatBlob st;
static bool     stLoaded = false;

struct __attribute__((packed)) StatBlob2 {   // firmware 9.5 .. 9.11
  uint8_t  ver;
  uint16_t plays[ST_GAMES];
  uint32_t secs[ST_GAMES];
  uint32_t awards;
  uint16_t mpGames, mpWins;
};

struct __attribute__((packed)) StatBlob1 {   // firmware 9.4
  uint8_t  ver;
  uint16_t plays[16];
  uint32_t secs[16];
  uint32_t awards;
  uint16_t mpGames, mpWins;
};

void statLoad() {
  if (stLoaded) return;
  stLoaded = true;
  memset(&st, 0, sizeof(st));
  prefs.begin("arcade", true);
  size_t n = prefs.getBytes("stats", &st, sizeof(st));
  if (n != sizeof(st) || st.ver != 3) {      // an older layout: keep it all
    StatBlob2 o2;
    StatBlob1 o1;
    memset(&st, 0, sizeof(st));
    if (prefs.getBytes("stats", &o2, sizeof(o2)) == sizeof(o2) && o2.ver == 2) {
      memcpy(st.plays, o2.plays, sizeof(o2.plays));
      memcpy(st.secs, o2.secs, sizeof(o2.secs));
      st.awards = o2.awards; st.mpGames = o2.mpGames; st.mpWins = o2.mpWins;
    } else if (prefs.getBytes("stats", &o1, sizeof(o1)) == sizeof(o1) && o1.ver == 1) {
      memcpy(st.plays, o1.plays, sizeof(o1.plays));
      memcpy(st.secs, o1.secs, sizeof(o1.secs));
      st.awards = o1.awards; st.mpGames = o1.mpGames; st.mpWins = o1.mpWins;
    }
    st.ver = 3;
  }
  prefs.end();
}

static inline bool awardHas(uint8_t i) { return (st.awards >> i) & 1; }

void statSave() {
  prefs.begin("arcade", false);
  prefs.putBytes("stats", &st, sizeof(st));
  prefs.end();
}

// ---------------- the awards ----------------
/* score: the best score of one game (index as in the library) must reach
   the value. Everything else is checked by its own rule in awardCheck(). */
enum { AW_SCORE, AW_FIRST, AW_ALL, AW_RECORD, AW_TIME, AW_ONLINE, AW_WINS };
struct Award { const char *name, *how; uint8_t kind, game; uint16_t need; };

static const Award AWARDS[] = {
  { "FIRST STEPS",  "play any game",           AW_FIRST,  0, 0 },
  { "EXPLORER",     "try every game",          AW_ALL,    0, 0 },
  { "RECORD BREAKER", "beat a best score",       AW_RECORD, 0, 0 },
  { "BLOCK STACKER","Tetris: 1000 points",     AW_SCORE,  0, 1000 },
  { "LONG SNAKE",   "Snake: 300 points",       AW_SCORE,  1, 300 },
  { "PONG MASTER",  "Pong: 50 points",         AW_SCORE,  2, 50 },
  { "DEMON HUNTER", "Doom: 200 points",        AW_SCORE,  3, 200 },
  { "MINER",        "Mine: dig 500 blocks",    AW_SCORE,  4, 500 },
  { "PILOT",        "Tunnel 3D: 50 rings",     AW_SCORE,  5, 50 },
  { "FREE BIRD",    "Flappy: 20 pipes",        AW_SCORE,  6, 20 },
  { "DEFENDER",     "Invaders: 1000 points",   AW_SCORE,  7, 1000 },
  { "SPRINTER",     "Dino: 1000 points",       AW_SCORE,  8, 1000 },
  { "BRICKLAYER",   "Breakout: 1000 points",   AW_SCORE,  9, 1000 },
  { "ROCK CRUSHER", "Rocks: 1000 points",      AW_SCORE, 10, 1000 },
  { "SPEEDSTER",    "Racer: 1000 points",      AW_SCORE, 11, 1000 },
  { "FROG KING",    "Frogger: 500 points",     AW_SCORE, 12, 500 },
  { "FOUR IN A ROW","4 wins: 300 points",      AW_SCORE, 13, 300 },
  { "NOUGHTS PRO",  "Tic Tac Toe: 300 points", AW_SCORE, 14, 300 },
  { "TILE MASTER",  "2048: 5000 points",       AW_SCORE, 15, 5000 },
  { "DEMINER",      "Minesweeper: win once",   AW_SCORE, 16, 1 },
  { "GHOST HUNTER", "Pac-Man: 3000 points",    AW_SCORE, 17, 3000 },
  { "ACE PILOT",    "Shooter: 1000 points",    AW_SCORE, 18, 1000 },
  { "LONG JUMP",    "Jump & Run: 500 points",  AW_SCORE, 19, 500 },
  { "BOX PUSHER",   "Sokoban: solve 6 levels", AW_SCORE, 20, 6 },
  { "WAREHOUSE",    "Sokoban: solve all",      AW_SCORE, 20, 12 },
  { "ADMIRAL",      "Battleship: beat the CPU", AW_SCORE, 21, 1 },
  { "NUMBER NINJA", "Sudoku: 300 points",      AW_SCORE, 22, 300 },
  { "LIGHTS OFF",   "Lights Out: 100 points",  AW_SCORE, 23, 100 },
  { "SWEET TOOTH",  "Match 3: 2000 points",    AW_SCORE, 24, 2000 },
  { "GRANDMASTER",  "Checkers: 300 points",    AW_SCORE, 25, 300 },
  { "LAST CARD",    "Mau-Mau: 300 points",     AW_SCORE, 26, 300 },
  { "DEMOLITION",   "Bomberman: 500 points",   AW_SCORE, 27, 500 },
  { "CAVE DIVER",   "Cave: 500 points",        AW_SCORE, 28, 500 },
  { "SKYSCRAPER",   "Stack: 30 floors",        AW_SCORE, 29, 30 },
  { "HOLE IN ONE",  "Minigolf: 500 points",    AW_SCORE, 30, 500 },
  { "EAGLE",        "Lander: 1000 points",     AW_SCORE, 31, 1000 },
  { "ONE HOUR",     "play 1 hour in all",      AW_TIME,   0, 60 },
  { "MARATHON",     "play 10 hours in all",    AW_TIME,   0, 600 },
  { "HELLO THERE",  "play an online game",     AW_ONLINE, 0, 1 },
  { "CHAMPION",     "win 5 online games",      AW_WINS,   0, 5 },
};
static const uint8_t AWARD_N = sizeof(AWARDS) / sizeof(AWARDS[0]);
static_assert(sizeof(AWARDS) / sizeof(AWARDS[0]) <= 64, "one bit per award in StatBlob.awards");

const char *gameName(uint8_t i);        // from the library table
uint8_t     realGames();

static uint32_t statTotalSecs() {
  uint32_t s = 0;
  for (uint8_t i = 0; i < ST_GAMES; i++) s += st.secs[i];
  return s;
}

// shows "AWARD!" with the name, a short tune, OK or 2 s go on
static void awardShow(const Award &a) {
  NoPause np;
  static const uint16_t TUNE[4] = { 880, 1109, 1319, 1760 };
  uint32_t t0 = millis();
  uint8_t  note = 0;
  btnClear();
  while (poll()) {
    uint32_t t = millis() - t0;
    if (note < 4 && t >= note * 110u) sfx(TUNE[note++], 100);
    if (t > 2000 || (t > 400 && btn(B_OK))) break;
    oled.setFont(FONT_B);
    centerStr(12, "AWARD!");
    oled.drawHLine(0, TOP_H - 1, SCR_W);
    centerStr(36, a.name);
    oled.setFont(FONT);
    centerStr(50, a.how);
    oled.sendBuffer();
  }
  btnClear();
}

/* Checks every award that is not won yet; the new ones are shown and saved.
   record = a best score was just beaten.                                */
void awardCheck(bool record) {
  statLoad();
  uint8_t games = realGames();
  uint32_t mins = statTotalSecs() / 60;
  bool changed = false;
  for (uint8_t i = 0; i < AWARD_N; i++) {
    if (awardHas(i)) continue;
    const Award &a = AWARDS[i];
    bool won = false;
    switch (a.kind) {
    case AW_SCORE:  won = a.game < games && loadHigh(a.game) >= a.need; break;
    case AW_FIRST:  for (uint8_t g = 0; g < games; g++) if (st.plays[g]) won = true; break;
    case AW_ALL:    won = true; for (uint8_t g = 0; g < games; g++) if (!st.plays[g]) won = false; break;
    case AW_RECORD: won = record; break;
    case AW_TIME:   won = mins >= a.need; break;
    case AW_ONLINE: won = st.mpGames >= a.need; break;
    case AW_WINS:   won = st.mpWins >= a.need; break;
    }
    if (!won) continue;
    st.awards |= 1ULL << i;
    changed = true;
    awardShow(a);
  }
  if (changed) statSave();
}

// ---------------- bookkeeping, called by the library ----------------
static uint32_t stStart = 0;

void statGameStart(uint8_t g) {
  statLoad();
  if (g < ST_GAMES && st.plays[g] < 65535) st.plays[g]++;
  stStart = gameMillis();
}

void statGameEnd(uint8_t g) {
  if (g < ST_GAMES) st.secs[g] += (gameMillis() - stStart + 500) / 1000;
  statSave();
  awardCheck(false);
}

void statOnline(bool won) {             // one online game decided
  statLoad();
  if (st.mpGames < 65535) st.mpGames++;
  if (won && st.mpWins < 65535) st.mpWins++;
  statSave();
  awardCheck(false);
}

// ---------------- the page ----------------
static void statTime(char *b, size_t n, uint32_t s) {
  if (s >= 3600) snprintf(b, n, "%luh%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60));
  else           snprintf(b, n, "%lum", (unsigned long)(s / 60));
}

/* Two pages, LEFT / RIGHT switches: the numbers of every game, and the
   awards with what each one needs. Hold OK = back.                      */
void statsRun() {
  statLoad();
  uint8_t page = 0, top = 0, sel = 0;
  uint8_t games = realGames();
  btnClear();
  while (poll()) {
    uint8_t n = page ? AWARD_N : games;
    if (btn(B_LEFT)  && page)     { page = 0; top = sel = 0; sfx(600, 15); }
    if (btn(B_RIGHT) && !page)    { page = 1; top = sel = 0; sfx(600, 15); }
    if (btn(B_UP)    && sel)      { sel--; sfx(700, 15); }
    if (btn(B_DOWN)  && sel + 1 < n) { sel++; sfx(700, 15); }
    if (btn(B_OK)) break;

    char b[32], t[12];
    oled.setFont(FONT_B);
    if (!page) {
      oled.drawStr(2, 12, "STATS");
      oled.setFont(FONT);
      statTime(t, sizeof(t), statTotalSecs());
      snprintf(b, sizeof(b), "%s  AWARDS>", t);
      rightStr(12, b);
    } else {
      uint8_t won = 0;
      for (uint8_t i = 0; i < AWARD_N; i++) if (awardHas(i)) won++;
      oled.drawStr(2, 12, "AWARDS");
      oled.setFont(FONT);
      snprintf(b, sizeof(b), "%u/%u  <STATS", won, AWARD_N);
      rightStr(12, b);
    }
    oled.drawHLine(0, TOP_H - 1, SCR_W);

    const uint8_t ROWS_ST = page ? 5 : 6;             // awards: last line explains
    if (sel < top) top = sel;
    if (sel >= top + ROWS_ST) top = sel - ROWS_ST + 1;
    for (uint8_t r = 0; r < ROWS_ST && top + r < n; r++) {
      uint8_t i = top + r, y = TOP_H + r * 8;
      if (!page) {
        statTime(t, sizeof(t), st.secs[i]);
        snprintf(b, sizeof(b), "%-11.11s%4ux %6s", gameName(i), st.plays[i], t);
      } else {
        bool has = awardHas(i);
        snprintf(b, sizeof(b), "%c %s", has ? '*' : '-', AWARDS[i].name);
      }
      if (i == sel) { oled.drawBox(0, y, SCR_W, 8); oled.setDrawColor(0); }
      oled.drawStr(2, y + 7, b);
      oled.setDrawColor(1);
    }
    if (page) {
      oled.drawHLine(0, SCR_H - 9, SCR_W);
      oled.drawStr(2, SCR_H - 1, AWARDS[sel].how);
    }
    oled.sendBuffer();
  }
}
