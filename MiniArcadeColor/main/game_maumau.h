// MiniArcade Color: Mau-Mau against 1 to 3 CPU players. Part of
// console.cpp, which includes it; the rules and the CPU are in
// boardgames.h (shared with the OLED console). 32 cards on green felt.
// Your cards are along the bottom: LEFT/RIGHT picks one, OK plays it. The
// first place, left of the cards, is the draw pile: OK there draws (after
// drawing it says PASS). A 7 makes the next one draw 2, an 8 skips, a jack
// goes on anything and asks for the suit you wish.
// Points for a win: 50 plus 10 for every card the others still hold.
#include "boardgames.h"

#define MM_HW   34                     // a card in the hand
#define MM_HH   52
#define MM_HY   184                    // its top (picked: higher)
#define MM_BW   44                     // the open card
#define MM_BH   64
#define MM_DX   78                     // draw pile, top left
#define MM_DY   86
#define MM_PX   148                    // discard pile, top left
#define MM_PY   86

static MauMau mm;
static uint8_t mmCur = 0;               // 0 = draw pile, 1.. = the cards in hand
static char    mmMsg[28];
static bool    mmWishing;               // the suit question is open ...
static uint8_t mmWishSel;               // ... and this suit is marked
static uint16_t mmScore;
// a card on its way (dealt or played): face 0xFF = its back
static struct { bool on; uint8_t face; int16_t x, y; } mmFly;
static uint8_t mmShown[MM_MAXP];        // while dealing: cards each one has got so far (255 = all)

static const char *const MM_NAMES[4] = { "YOU", "CPU1", "CPU2", "CPU3" };
// the player sitting at the bottom and the names; another seat in an online game (multiplayer.h)
static uint8_t mmMe = 0;
static const char *const *mmWho = MM_NAMES;
static uint8_t mmSeat(uint8_t k, uint8_t players) { return (uint8_t)((mmMe + k) % players); }   // k-th seat from the bottom
static const char *const MM_RN[8] = { "7", "8", "9", "10", "J", "Q", "K", "A" };
static const char *const MM_SN[4] = { "clubs", "spades", "hearts", "diamonds" };

static col mmSuitCol(uint8_t s) { return s >= 2 ? rgb(220, 0, 0) : C::BLACK; }

// a suit sign centred on (cx, cy), h = half its size: clubs, spades, hearts, diamonds
static void mmSuit(int16_t cx, int16_t cy, uint8_t s, int16_t h, col c) {
  int16_t r = h / 2 + 1;
  switch (s) {
  case 0:                                                       // clubs: three leaves and a stem
    gfx.disc(cx, cy - h / 2, r, c);
    gfx.disc(cx - h / 2 - 1, cy + h / 5, r, c);
    gfx.disc(cx + h / 2 + 1, cy + h / 5, r, c);
    gfx.triangle(cx, cy, cx - h / 2, cy + h + 1, cx + h / 2, cy + h + 1, c);
    break;
  case 1:                                                       // spades: a heart upside down, a stem
    gfx.disc(cx - h / 2, cy + h / 4, r, c);
    gfx.disc(cx + h / 2, cy + h / 4, r, c);
    gfx.triangle(cx - h, cy, cx + h, cy, cx, cy - h - 1, c);
    gfx.triangle(cx, cy + h / 4, cx - h / 2, cy + h + 1, cx + h / 2, cy + h + 1, c);
    break;
  case 2:                                                       // hearts
    gfx.disc(cx - h / 2, cy - h / 4, r, c);
    gfx.disc(cx + h / 2, cy - h / 4, r, c);
    gfx.triangle(cx - h - 1, cy, cx + h + 1, cy, cx, cy + h + 1, c);
    break;
  default:                                                      // diamonds
    gfx.triangle(cx, cy - h - 1, cx - h + 1, cy, cx + h - 1, cy, c);
    gfx.triangle(cx - h + 1, cy, cx + h - 1, cy, cx, cy + h + 1, c);
    break;
  }
}

// a card face up, w x h; small = only the corner counts (hand, overlapped)
static void mmCard(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c, bool hi = false) {
  uint8_t s = MauMau::suit(c), r = MauMau::rank(c);
  col sc = mmSuitCol(s);
  gfx.rbox(x + 2, y + 2, w, h, 4, rgb(0, 40, 0));               // shadow on the felt
  gfx.rbox(x, y, w, h, 4, C::WHITE);
  gfx.rframe(x, y, w, h, 4, hi ? C::YELLOW : C::GREY);
  if (hi) gfx.rframe(x + 1, y + 1, w - 2, h - 2, 4, C::YELLOW);
  gfx.text(x + 3, y + 3, MM_RN[r], sc, F_BOLD);
  mmSuit(x + 9, y + 33, s, 4, sc);
  int16_t cx = x + w / 2 + 5, cy = y + h / 2 + 10;
  if (r >= MR_J && r <= MR_K) {                                 // a figure: its letter in a frame
    bool big = w >= 40;
    int16_t bw = big ? 22 : 16, bh = big ? 30 : 22, bx = x + w - bw - 3, by = y + h - bh - 3;
    gfx.rbox(bx, by, bw, bh, 3, rgb(255, 230, 153));
    gfx.rframe(bx, by, bw, bh, 3, sc);
    Font f = big ? F_BOLD : F_TEXT;
    gfx.text(bx + (bw - Gfx::width(MM_RN[r], f)) / 2 + 1, by + (bh - Gfx::height(f)) / 2 + 1, MM_RN[r], sc, f);
  } else mmSuit(cx, cy, s, r == MR_A ? 9 : 7, sc);
}

static void mmBack(int16_t x, int16_t y, int16_t w, int16_t h) {
  gfx.rbox(x + 2, y + 2, w, h, 4, rgb(0, 40, 0));
  gfx.rbox(x, y, w, h, 4, C::WHITE);
  gfx.rbox(x + 3, y + 3, w - 6, h - 6, 3, rgb(0, 51, 170));
  for (int16_t j = y + 6; j < y + h - 5; j += 4)                // a small pattern
    for (int16_t i = x + 5 + ((j - y) / 4 & 1) * 2; i < x + w - 5; i += 4) gfx.pixel(i, j, rgb(102, 153, 255));
  gfx.rframe(x + 3, y + 3, w - 6, h - 6, 3, rgb(102, 153, 255));
}

// the cards of player p in order; returns how many
static uint8_t mmHand(const MauMau &g, uint8_t p, uint8_t *out) {
  uint8_t n = 0;
  for (uint8_t c = 0; c < MM_CARDS; c++) if (g.hand[p] & (1UL << c)) out[n++] = c;
  return n;
}

// where the hand's places are: x of place s (0 = draw), first shown, step
static void mmLayout(uint8_t slots, int8_t cursor, uint8_t &first, int16_t &step) {
  first = 0;
  step = slots > 1 ? (int16_t)((SCR_W - 8 - MM_HW) / (slots - 1)) : 0;
  if (step > MM_HW + 4) step = MM_HW + 4;
  if (step < 12) {                                              // too many: a window that follows
    step = 12;
    uint8_t vis = (SCR_W - 8 - MM_HW) / 12 + 1;
    if (cursor >= vis) first = cursor - vis + 1;
  }
}

// the zone of an opponent (k = 1.. seen from player 0): its centre
static int16_t mmZoneX(uint8_t k, uint8_t players) { return (int16_t)(SCR_W * (2 * k - 1) / (2 * (players - 1))); }

/* The table as player 0 sees it; cursor < 0 = none, msg = banner */
static void mmDraw(const MauMau &g, int8_t cursor, const char *msg) {
  statusBar("MAU-MAU", mmScore, curHigh, C::RED);
  gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(0, 110, 40), rgb(0, 50, 15));
  // the others: name, cards (fanned backs), a frame round the one to move
  for (uint8_t k = 1; k < g.players; k++) {
    int16_t cx = mmZoneX(k, g.players);
    uint8_t p = mmSeat(k, g.players);
    uint8_t n = MauMau::count(g.hand[p]);
    if (n > mmShown[p]) n = mmShown[p];
    uint8_t shown = n > 10 ? 10 : n;
    bool act = g.turn == p && !g.winner;
    if (act) { gfx.rbox(cx - 50, 29, 100, 52, 8, rgb(0, 80, 30)); gfx.rframe(cx - 50, 29, 100, 52, 8, C::YELLOW); }
    char b[16];
    snprintf(b, sizeof(b), "%s %u", mmWho[p], n);
    gfx.text(cx - Gfx::width(b, F_SMALL) / 2, 32, b, act ? C::YELLOW : C::WHITE, F_SMALL);
    int16_t x0 = cx - (shown ? (shown - 1) * 6 + 14 : 0) / 2;
    for (uint8_t i = 0; i < shown; i++) mmBack(x0 + i * 6, 44, 14, 22);
  }
  // draw pile (a few backs on top of each other) and the open card
  uint8_t layers = g.nDeck > 6 ? 3 : (g.nDeck > 1 ? 2 : g.nDeck);
  for (uint8_t i = 0; i < layers; i++) mmBack(MM_DX - i * 2, MM_DY - i * 2, MM_BW, MM_BH);
  char b[16];
  uint8_t still = 0;                                            // dealing: not yet given out
  for (uint8_t p = 0; p < g.players; p++) if (mmShown[p] < MM_HAND) still += MM_HAND - mmShown[p];
  snprintf(b, sizeof(b), "%u", g.nDeck + still);
  gfx.text(MM_DX + MM_BW / 2 - Gfx::width(b, F_SMALL) / 2, MM_DY + MM_BH + 4, b, C::LIME, F_SMALL);
  if (g.nPile > 1) {                                            // the one below shows a little
    gfx.rbox(MM_PX + 6, MM_PY - 3, MM_BW, MM_BH, 4, rgb(220, 220, 220));
    gfx.rframe(MM_PX + 6, MM_PY - 3, MM_BW, MM_BH, 4, C::GREY);
  }
  mmCard(MM_PX, MM_PY, MM_BW, MM_BH, g.top());
  if (g.wish != MM_NONE) {                                      // the wished suit
    gfx.rbox(214, 92, 50, 52, 8, C::WHITE);
    gfx.text(222, 96, "wish", C::DKGREY, F_SMALL);
    mmSuit(239, 122, g.wish, 10, mmSuitCol(g.wish));
  }
  if (g.penalty) {
    snprintf(b, sizeof(b), "+%u", g.penalty);
    gfx.textShadow(272, 104, b, C::YELLOW, C::DKRED, F_BOLD);
  }
  gfx.center(164, mmMsg, C::WHITE, F_SMALL);
  // own hand along the bottom, the draw place first
  uint8_t cards[MM_CARDS], n = mmHand(g, mmMe, cards);
  if (n > mmShown[mmMe]) n = mmShown[mmMe];
  uint8_t slots = n + 1, first;
  int16_t step;
  mmLayout(slots, cursor, first, step);
  for (uint8_t s = first; s < slots; s++) {
    int16_t x = 4 + (s - first) * step;
    if (x > SCR_W - MM_HW) break;
    bool on = s == cursor;
    int16_t y = MM_HY - (on ? 10 : 0);
    if (s == 0) {
      mmBack(x, y, MM_HW, MM_HH);
      const char *lab = g.turn == mmMe && g.drew ? "PASS" : "DRAW";
      gfx.rbox(x + 3, y + 18, MM_HW - 6, 14, 3, C::BLACK);
      gfx.text(x + (MM_HW - Gfx::width(lab, F_SMALL)) / 2, y + 21, lab, on ? C::YELLOW : C::WHITE, F_SMALL);
    } else {
      bool fits = g.turn == mmMe && g.canPlay(cards[s - 1]);
      mmCard(x, y, MM_HW, MM_HH, cards[s - 1], on);
      if (cursor >= 0 && !fits) gfx.dim(x + 1, y + 1, step < MM_HW ? step - 1 : MM_HW - 2, MM_HH - 2);
    }
  }
  if (mmFly.on) {
    if (mmFly.face == 0xFF) mmBack(mmFly.x, mmFly.y, MM_HW, MM_HH);
    else mmCard(mmFly.x, mmFly.y, MM_HW, MM_HH, mmFly.face);
  }
  if (msg) {
    int16_t w = Gfx::width(msg, F_BOLD) + 36, x = (SCR_W - w) / 2;
    gfx.rbox(x + 4, 108, w, 42, 10, C::BLACK);
    gfx.rbox(x, 104, w, 42, 10, rgb(120, 0, 0));
    gfx.rframe(x, 104, w, 42, 10, C::YELLOW);
    gfx.center(113, msg, C::WHITE, F_BOLD);
  }
}

// a card flies from (x0, y0) to (x1, y1) over ms; false = left the game
static bool mmFlyCard(uint8_t face, int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t ms) {
  uint32_t t0 = gameMillis();
  mmFly.on = true; mmFly.face = face;
  while (true) {
    if (!poll()) { mmFly.on = false; return false; }
    uint32_t t = gameMillis() - t0;
    if (t > ms) t = ms;
    int32_t e = (int32_t)t * (2 * ms - t) * 256 / ((int32_t)ms * ms);   // eases out, 0..256
    mmFly.x = (int16_t)(x0 + (x1 - x0) * e / 256);
    mmFly.y = (int16_t)(y0 + (y1 - y0) * e / 256);
    mmDraw(mm, -1, NULL);
    gfx.send();
    if (t >= ms) break;
  }
  mmFly.on = false;
  return true;
}

// asks for the suit after a jack
static uint8_t mmWish(bool &left) {
  uint8_t &w = mmWishSel;
  w = 0;
  mmWishing = true;
  btnClear();
  while (true) {
    if (!poll()) { mmWishing = false; left = true; return w; }
    if (btn(B_LEFT))  { w = (w + 3) % 4; sfx(700, 8); }
    if (btn(B_RIGHT)) { w = (w + 1) % 4; sfx(700, 8); }
    if (btnTap(B_OK)) { sfx(1000, 30); btnClear(); mmWishing = false; return w; }
    mmDraw(mm, -1, NULL);
    panel(50, 60, 220, 110, "WISH A SUIT", C::RED);
    for (uint8_t s = 0; s < 4; s++) {
      int16_t x = 62 + s * 50;
      gfx.rbox(x, 104, 44, 44, 6, s == w ? C::YELLOW : C::WHITE);
      mmSuit(x + 22, 124, s, 11, mmSuitCol(s));
    }
    gfx.center(152, MM_SN[w], C::WHITE, F_SMALL);
    gfx.send();
  }
}

/* The keys of the player on its turn: returns a move (see boardgames.h)
   or -1 (-2 = left the game). A jack asks for the wished suit.       */
static int16_t mmInput() {
  uint8_t cards[MM_CARDS], n = mmHand(mm, mmMe, cards);
  if (mmCur > n) mmCur = n;
  if (btn(B_LEFT)  && mmCur > 0) { mmCur--; sfx(700, 8); }
  if (btn(B_RIGHT) && mmCur < n) { mmCur++; sfx(700, 8); }
  if (!btnTap(B_OK)) return -1;
  if (mmCur == 0) { sfx(600, 30); return mm.drew ? MM_PASS : MM_DRAW; }
  uint8_t c = cards[mmCur - 1];
  if (!mm.canPlay(c)) { sfx(200, 120); return -1; }
  bool left = false;
  uint8_t w = MauMau::rank(c) == MR_J ? mmWish(left) : 0;
  if (left) return -2;
  sfx(1000, 30);
  return c | (w << 5);
}

// what a move was, for the message line
static void mmSay(const char *who, uint8_t m) {
  if (m == MM_DRAW) snprintf(mmMsg, sizeof(mmMsg), "%s: draw", who);
  else if (m == MM_PASS) snprintf(mmMsg, sizeof(mmMsg), "%s: pass", who);
  else if (MauMau::rank(m & 31) == MR_J) snprintf(mmMsg, sizeof(mmMsg), "%s: J, wish %s", who, MM_SN[(m >> 5) & 3]);
  else snprintf(mmMsg, sizeof(mmMsg), "%s: %s of %s", who, MM_RN[MauMau::rank(m & 31)], MM_SN[MauMau::suit(m & 31)]);
}

/* A move with its card flying: played cards go to the open pile, drawn
   ones come from the draw pile. false = left the game.             */
static bool mmPlay(uint8_t who, uint8_t m) {
  int16_t hx, hy;                                               // the player's place on the table
  if (who == mmMe) {
    uint8_t cards[MM_CARDS], n = mmHand(mm, mmMe, cards), first, slot = n;
    int16_t step;
    if (m != MM_DRAW && m != MM_PASS) for (uint8_t i = 0; i < n; i++) if (cards[i] == (m & 31)) slot = i + 1;
    mmLayout(n + 1, (int8_t)slot, first, step);
    hx = 4 + (slot - first) * step; hy = MM_HY;
  } else { hx = mmZoneX((uint8_t)((who + mm.players - mmMe) % mm.players), mm.players) - MM_HW / 2; hy = 36; }
  if (m == MM_DRAW && mm.nDeck + mm.nPile > 1) {
    if (!mmFlyCard(0xFF, MM_DX, MM_DY, hx, hy, 380)) return false;
  } else if (m != MM_DRAW && m != MM_PASS) {
    if (!mmFlyCard(m & 31, hx, hy, MM_PX + (MM_BW - MM_HW) / 2, MM_PY + (MM_BH - MM_HH) / 2, 420)) return false;
  }
  mm.move(m);
  mmSay(mmWho[who], m);
  sfx(m == MM_DRAW ? 400 : 800, 25);
  return true;
}

void maumauRun() {
  static const char *const MODES[3] = { "vs 1 CPU", "vs 2 CPUs", "vs 3 CPUs" };
  bool again = true;
  while (again) {
    again = false;
    uint8_t mode = chooseMode("MAU-MAU", MODES, 3, C::RED);
    if (mode == 255) return;
    mm.begin(millis() * 2654435761u, mode + 2);
    mmCur = 1;
    mmScore = 0;
    mmFly.on = false;
    strcpy(mmMsg, "dealing...");
    memset(mmShown, 0, sizeof(mmShown));                        // deal: one card after the other
    for (uint8_t k = 0; k < MM_HAND; k++)
      for (uint8_t p = 0; p < mm.players; p++) {
        int16_t x = p ? mmZoneX(p, mm.players) - MM_HW / 2 : 4 + (k + 1) * (MM_HW + 4), y = p ? 36 : MM_HY;
        if (!mmFlyCard(0xFF, MM_DX, MM_DY, x, y, 160)) return;
        mmShown[p]++;
        sfx(500 + k * 60, 10);
      }
    memset(mmShown, 255, sizeof(mmShown));
    strcpy(mmMsg, "your go");
    uint32_t cpuAt = 0;
    btnClear();
    while (!mm.winner) {
      if (!poll()) return;
      if (mm.turn == 0) {
        int16_t m = mmInput();
        if (m == -2) return;
        if (m >= 0) {
          MauMau test = mm;                                     // allowed? (draw after drawing is not)
          if (test.move((uint8_t)m)) {
            if (!mmPlay(0, (uint8_t)m)) return;
            if (mm.drew && mm.turn == 0) {                      // the cursor goes to the new card
              uint8_t cards[MM_CARDS], n = mmHand(mm, 0, cards);
              for (uint8_t i = 0; i < n; i++) if (cards[i] == mm.drawn) mmCur = i + 1;
            }
            cpuAt = gameMillis() + 900;
            btnClear();
          }
        }
      } else if (gameMillis() >= cpuAt) {                       // the CPUs take their time
        uint8_t who = mm.turn, m = mm.cpuMove();
        if (!mmPlay(who, m)) return;
        cpuAt = gameMillis() + 900;
        if (mm.turn == 0) btnClear();
      }
      mmDraw(mm, mm.turn == 0 && !mm.winner ? (int8_t)mmCur : -1, NULL);
      gfx.send();
    }
    bool won = mm.winner == 1;
    if (won) { mmScore = 50; for (uint8_t p = 1; p < mm.players; p++) mmScore += 10 * MauMau::count(mm.hand[p]); }
    snprintf(mmMsg, sizeof(mmMsg), "%s", won ? "you have no card left" : "out of cards");
    if (!won) snprintf(mmMsg, sizeof(mmMsg), "%s has no card left", MM_NAMES[mm.winner - 1]);
    sfx(won ? 1400 : 250, 250);
    uint32_t t0 = gameMillis();
    btnClear();
    while (gameMillis() - t0 < 2500) {
      if (!poll()) return;
      if (btn(B_OK) && gameMillis() - t0 > 600) break;
      mmDraw(mm, -1, won ? "MAU-MAU!" : "YOU LOST");
      gfx.send();
    }
    again = gameOver(mmScore);
  }
}

static const char *const IC_MAUMAU[12] = {
  "sssssss.....", "swwwwws.....", "swkwwsssssss", "skkkwswwwwws", "skkkwswwwwws", "swkwwsrrwrrs",
  "skkkwsrrrrrs", "swwwwswrrrws", "swwwwswwrwws", "sssssswwwwws", ".....swwwwws", ".....sssssss" };
