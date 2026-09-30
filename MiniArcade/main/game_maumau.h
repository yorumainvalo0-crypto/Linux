// MiniArcade: Mau-Mau. Part of MiniArcade.ino, which includes it -
// not compiled on its own. The rules are in boardgames.h.

// =========================================================
//  MAU-MAU   (against 1 to 3 CPU players)
// =========================================================
/* Your cards are along the bottom; LEFT/RIGHT picks one, OK plays it.
   The first place, left of the cards, is the draw pile: OK there draws
   (after drawing it says PASS). A jack asks for the suit you wish.
   Points for a win: 50 plus 10 for every card the others still hold. */
#define MM_CW  10                       // small card: 10 x 15
#define MM_CH  15

static MauMau mm;
static uint8_t mmCur = 0;               // 0 = draw pile, 1.. = the cards in hand
static char    mmMsg[24];
static bool    mmWishing;               // the suit question is open ...
static uint8_t mmWishSel;               // ... and this suit is marked

// 5 x 5 suits: clubs, spades, hearts, diamonds
static const uint8_t MM_SUIT[4][5] = {
  { 0x04, 0x0E, 0x15, 0x1F, 0x04 }, { 0x04, 0x0E, 0x1F, 0x1F, 0x04 },
  { 0x0A, 0x1F, 0x1F, 0x0E, 0x04 }, { 0x04, 0x0E, 0x1F, 0x0E, 0x04 } };
// 3 x 5 letters J Q K A (the digits come from Sudoku's small font)
static const uint8_t MM_LET[4][5] = { {1,1,1,5,7}, {7,5,5,7,1}, {5,5,6,5,5}, {2,5,7,5,5} };

static void mmSuit(int16_t x, int16_t y, uint8_t s, uint8_t scale) {
  for (uint8_t r = 0; r < 5; r++)
    for (uint8_t c = 0; c < 5; c++)
      if (MM_SUIT[s][r] & (0x10 >> c)) oled.drawBox(x + c * scale, y + r * scale, scale, scale);
}
static void mmRank(int16_t x, int16_t y, uint8_t r) {          // small rank, 3 or 7 px wide
  if (r <= MR_9) { suDigit(x, y, 7 + r); return; }
  if (r == MR_10) { suDigit(x, y, 1); suDigit(x + 4, y, 0); return; }
  const uint8_t *g = MM_LET[r - MR_J];
  for (uint8_t i = 0; i < 5; i++) for (uint8_t c = 0; c < 3; c++) if (g[i] & (4 >> c)) oled.drawPixel(x + c, y + i);
}

static void mmCard(int16_t x, int16_t y, uint8_t c) {            // small card, face up
  oled.setDrawColor(0); oled.drawBox(x, y, MM_CW, MM_CH); oled.setDrawColor(1);
  oled.drawFrame(x, y, MM_CW, MM_CH);
  mmRank(x + 2, y + 2, MauMau::rank(c));
  mmSuit(x + 2, y + 8, MauMau::suit(c), 1);
}
static void mmBack(int16_t x, int16_t y) {                       // face down
  oled.setDrawColor(0); oled.drawBox(x, y, MM_CW, MM_CH); oled.setDrawColor(1);
  oled.drawFrame(x, y, MM_CW, MM_CH);
  for (uint8_t k = 2; k < MM_CH - 2; k += 2) oled.drawHLine(x + 2 + (k & 2 ? 1 : 0), y + k, MM_CW - 5);
}

// the cards of player p in order; returns how many
static uint8_t mmHand(const MauMau &g, uint8_t p, uint8_t *out) {
  uint8_t n = 0;
  for (uint8_t c = 0; c < MM_CARDS; c++) if (g.hand[p] & (1UL << c)) out[n++] = c;
  return n;
}

/* The table as player "me" sees it. names[p] for the others; cursor < 0 = none */
void mmDraw(const MauMau &g, uint8_t me, int8_t cursor, const char *const *names) {
  oled.setFont(FONT_B);
  oled.drawStr(2, 12, "MAU-MAU");
  oled.setFont(FONT);
  rightStr(12, mmMsg);
  oled.drawHLine(0, TOP_H - 1, SCR_W);
  // draw pile and the open card
  mmBack(2, 20);
  char b[24];
  snprintf(b, sizeof(b), "%u", g.nDeck);
  oled.drawStr(14, 30, b);
  int16_t tx = 30, ty = 18;                                      // open card, big
  oled.drawFrame(tx, ty, 18, 26);
  uint8_t t = g.top();
  static const char *const RN[8] = { "7", "8", "9", "10", "J", "Q", "K", "A" };
  oled.drawStr(tx + 2, ty + 8, RN[MauMau::rank(t)]);
  mmSuit(tx + 4, ty + 12, MauMau::suit(t), 2);
  if (g.wish != MM_NONE) { oled.drawStr(51, 25, "wish"); mmSuit(56, 28, g.wish, 2); }
  if (g.penalty) { snprintf(b, sizeof(b), "+%u", g.penalty); oled.setFont(FONT_B); oled.drawStr(51, 44, b); oled.setFont(FONT); }
  // the others: name, cards, an arrow at the one whose turn it is
  uint8_t row = 0;
  for (uint8_t k = 1; k < g.players; k++) {
    uint8_t p = (me + k) % g.players;
    snprintf(b, sizeof(b), "%c%-6.6s%2u", g.turn == p ? '>' : ' ', names[p], MauMau::count(g.hand[p]));
    oled.drawStr(72, 24 + row * 9, b);
    row++;
  }
  // own hand along the bottom, the draw place first
  uint8_t cards[MM_CARDS], n = mmHand(g, me, cards);
  uint8_t slots = n + 1, vis = 11;
  uint8_t first = 0;
  if (cursor >= vis) first = cursor - vis + 1;
  for (uint8_t s = first; s < slots && s < first + vis; s++) {
    int16_t x = (s - first) * (MM_CW + 1) + 2, y = SCR_H - MM_CH - ((s == cursor) ? 2 : 0);
    if (s == 0) {
      mmBack(x, y);
      if (g.turn == me && g.drew) { oled.setDrawColor(0); oled.drawBox(x + 1, y + 5, 8, 7); oled.setDrawColor(1); oled.drawStr(x + 1, y + 11, "P"); }
    } else mmCard(x, y, cards[s - 1]);
    if (s == cursor) oled.drawHLine(x, SCR_H - 1, MM_CW);
  }
}

// asks for the suit after a jack
static uint8_t mmWish(const MauMau &g, uint8_t me, const char *const *names) {
  uint8_t &w = mmWishSel;
  w = 0;
  mmWishing = true;
  btnClear();
  while (true) {
    if (!poll()) { mmWishing = false; return w; }
    if (btn(B_LEFT))  { w = (w + 3) % 4; sfx(700, 8); }
    if (btn(B_RIGHT)) { w = (w + 1) % 4; sfx(700, 8); }
    if (btnTap(B_OK)) { sfx(1000, 30); btnClear(); mmWishing = false; return w; }
    mmDraw(g, me, -1, names);
    oled.setDrawColor(0); oled.drawBox(20, 18, 88, 30); oled.setDrawColor(1);
    oled.drawFrame(20, 18, 88, 30);
    centerStr(27, "wish a suit:");
    for (uint8_t s = 0; s < 4; s++) {
      mmSuit(32 + s * 18, 32, s, 2);
      if (s == w) oled.drawFrame(30 + s * 18, 30, 14, 14);
    }
    oled.sendBuffer();
  }
}

/* The keys of player "me" on its turn: returns a move (see boardgames.h)
   or -1. A jack asks for the wished suit right away.               */
int16_t mmInput(const MauMau &g, uint8_t me, const char *const *names) {
  uint8_t cards[MM_CARDS], n = mmHand(g, me, cards);
  if (mmCur > n) mmCur = n;
  if (btn(B_LEFT)  && mmCur > 0) { mmCur--; sfx(700, 8); }
  if (btn(B_RIGHT) && mmCur < n) { mmCur++; sfx(700, 8); }
  if (!btnTap(B_OK)) return -1;
  if (mmCur == 0) { sfx(600, 30); return g.drew ? MM_PASS : MM_DRAW; }
  uint8_t c = cards[mmCur - 1];
  if (!g.canPlay(c)) { sfx(200, 120); return -1; }
  uint8_t w = MauMau::rank(c) == MR_J ? mmWish(g, me, names) : 0;
  sfx(1000, 30);
  return c | (w << 5);
}

// what a move was, for the message line
void mmSay(const char *who, uint8_t m, const MauMau &g) {
  static const char *const RN[8] = { "7", "8", "9", "10", "J", "Q", "K", "A" };
  static const char *const SN[4] = { "clubs", "spades", "hearts", "diam." };
  if (m == MM_DRAW) snprintf(mmMsg, sizeof(mmMsg), "%.6s draws", who);
  else if (m == MM_PASS) snprintf(mmMsg, sizeof(mmMsg), "%.6s passes", who);
  else if (MauMau::rank(m & 31) == MR_J) snprintf(mmMsg, sizeof(mmMsg), "%.6s J: %s", who, SN[(m >> 5) & 3]);
  else snprintf(mmMsg, sizeof(mmMsg), "%.6s %s %s", who, RN[MauMau::rank(m & 31)], SN[MauMau::suit(m & 31)]);
  (void)g;
}

void maumauRun() {
  static const char *const MODES[3] = { "vs 1 CPU", "vs 2 CPUs", "vs 3 CPUs" };
  static const char *const NAMES[4] = { "YOU", "CPU1", "CPU2", "CPU3" };
  bool again = true;
  while (again) {
    again = false;
    uint8_t mode = chooseMode("MAU-MAU", MODES, 3);
    if (mode == 255) return;
    mm.begin(millis() * 2654435761u, mode + 2);
    mmCur = 1;
    strcpy(mmMsg, "your go");
    uint32_t cpuAt = 0;
    btnClear();
    while (!mm.winner) {
      if (!poll()) return;
      if (mm.turn == 0) {
        int16_t m = mmInput(mm, 0, NAMES);
        if (m >= 0 && mm.move((uint8_t)m)) {
          mmSay("you", (uint8_t)m, mm);
          if (mm.drew) {                                        // the cursor goes to the new card
            uint8_t cards[MM_CARDS], n = mmHand(mm, 0, cards);
            for (uint8_t i = 0; i < n; i++) if (cards[i] == mm.drawn) mmCur = i + 1;
          }
          cpuAt = millis() + 700;
        }
      } else if (millis() >= cpuAt) {
        uint8_t who = mm.turn, m = mm.cpuMove();
        mm.move(m);
        mmSay(NAMES[who], m, mm);
        cpuAt = millis() + 700;
        sfx(m == MM_DRAW ? 400 : 800, 25);
      }
      mmDraw(mm, 0, mm.turn == 0 ? mmCur : -1, NAMES);
      oled.sendBuffer();
    }
    bool won = mm.winner == 1;
    uint16_t score = 0;
    if (won) { score = 50; for (uint8_t p = 1; p < mm.players; p++) score += 10 * MauMau::count(mm.hand[p]); }
    snprintf(mmMsg, sizeof(mmMsg), "%s", won ? "MAU-MAU!" : "you lost");
    sfx(won ? 1400 : 250, 250);
    for (uint8_t i = 0; i < 70 && poll(); i++) { mmDraw(mm, 0, -1, NAMES); oled.sendBuffer(); }
    again = gameOver(score);
  }
}
