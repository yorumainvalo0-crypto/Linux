// MiniArcade Color: Skat - you against two CPU players, by the German
// Skat Order. Part of console.cpp, which includes it after game_maumau.h
// (the cards are drawn the same way). 32 French cards, 10 each and 2 in
// the Skat; the dealer goes round (Vorhand, Mittelhand, Hinterhand).
// Bidding (Reizen) 18, 20, 22, 23 ...: UP/DOWN the value, LEFT/RIGHT bid
// or "passe", OK. The declarer takes the Skat and pushes 2 (OK marks a
// card, OK on "push" ends it) or plays Hand, then announces: Kreuz, Pik,
// Herz, Karo, Grand or Null; in a Hand game also Schneider, Schwarz or
// Ouvert (LEFT/RIGHT). In a trick LEFT/RIGHT picks a card, OK plays it;
// cards you may not play are dimmed. A Liste of 12, 24 or 36 games, scored
// as in the Seeger list: the declarer gets the value when he wins and
// loses twice the value when he loses. Best: your total at the end.

// =========================================================
//  RULES   (no screen, no keys: the simulator tests them alone)
// =========================================================
/* A card is suit * 8 + rank, suits Kreuz, Pik, Herz, Karo (as Mau-Mau),
   ranks 7 8 9 10 B D K A. A hand is a 32 bit mask.                   */
#define SKT_JACKS 0x10101010UL                 // the four Buben
#define SKT_TR    4                            // the "suit" of all trumps
#define SKT_NONE  255
#define SKF_HAND 1                            // game flags
#define SKF_SCHN 2                            // Schneider announced
#define SKF_SCHW 4                            // Schwarz announced
#define SKF_OUV  8                            // Ouvert
enum { SG_KREUZ, SG_PIK, SG_HERZ, SG_KARO, SG_GRAND, SG_NULL };
enum { SR_7, SR_8, SR_9, SR_10, SR_B, SR_D, SR_K, SR_A };

static const uint8_t SKT_PTS[8] = { 0, 0, 0, 10, 2, 3, 4, 11 };
static const uint8_t SKT_ORD[8] = { 0, 1, 2, 5, 0, 3, 4, 6 };    // A 10 K D 9 8 7 (Buben are trumps)

static inline uint8_t  sktSuit(uint8_t c) { return c >> 3; }
static inline uint8_t  sktRank(uint8_t c) { return c & 7; }
static inline uint32_t sktBit(uint8_t c)  { return 1UL << c; }
static inline uint32_t sktSuitMask(uint8_t s) { return 0xFFUL << (s * 8); }
static uint8_t sktCount(uint32_t m) { uint8_t n = 0; while (m) { m &= m - 1; n++; } return n; }
static uint8_t sktPoints(uint32_t m) {
  uint8_t s = 0;
  for (uint8_t c = 0; c < 32; c++) if (m >> c & 1) s += SKT_PTS[c & 7];
  return s;
}
static uint8_t  sktBase(uint8_t g)   { return g == SG_GRAND ? 24 : 12 - g; }    // Kreuz 12 .. Karo 9
static uint32_t sktTrumps(uint8_t g) { return g == SG_NULL ? 0 : (SKT_JACKS | (g < 4 ? sktSuitMask(g) : 0)); }
static uint8_t  sktEff(uint8_t g, uint8_t c) { return (sktTrumps(g) >> c & 1) ? SKT_TR : sktSuit(c); }
static uint32_t sktEffMask(uint8_t g, uint8_t e) { uint32_t t = sktTrumps(g); return e == SKT_TR ? t : sktSuitMask(e) & ~t; }
// strength within its kind: Null 7 8 9 10 B D K A; else Buben over the trump suit over A 10 K D 9 8 7
static uint8_t sktPower(uint8_t g, uint8_t c) {
  if (g == SG_NULL) return sktRank(c);
  if (sktRank(c) == SR_B) return 23 - sktSuit(c);
  return SKT_ORD[sktRank(c)] + (sktSuit(c) == g ? 10 : 0);
}
// does a beat b, the card that has the trick so far?
static bool sktBeats(uint8_t g, uint8_t a, uint8_t b) {
  uint8_t ea = sktEff(g, a), eb = sktEff(g, b);
  return ea == eb ? sktPower(g, a) > sktPower(g, b) : ea == SKT_TR;
}

// the values that can be bid: what some game is worth (18, 20, 22, 23, 24, 27 ...)
static bool sktBidOk(uint16_t v) {
  if (v == 23 || v == 35 || v == 46 || v == 59) return true;
  static const uint8_t B[5] = { 9, 10, 11, 12, 24 };
  for (uint8_t i = 0; i < 5; i++)
    if (v % B[i] == 0 && v / B[i] >= 2 && v / B[i] <= (i == 4 ? 11 : 18)) return true;
  return false;
}
static uint16_t sktNextBid(uint16_t v) { do v++; while (v <= 264 && !sktBidOk(v)); return v <= 264 ? v : 0; }
static uint16_t sktPrevBid(uint16_t v) { do v--; while (v > 18 && !sktBidOk(v)); return v < 18 ? 18 : v; }

// Spitzen: + "mit n", - "ohne n" (Buben, then A 10 K ... of the trump suit)
static int8_t sktSpitzen(uint32_t h, uint8_t g) {
  static const uint8_t R[7] = { SR_A, SR_10, SR_K, SR_D, SR_9, SR_8, SR_7 };
  uint8_t n = g < 4 ? 11 : 4;
  bool mit = h >> SR_B & 1;
  int8_t k = 0;
  while (k < n) {
    uint8_t c = k < 4 ? k * 8 + SR_B : g * 8 + R[k - 4];
    if ((bool)(h >> c & 1) != mit) break;
    k++;
  }
  return mit ? k : -k;
}

struct SkResult { bool won, overbid, schneider, schwarz; int8_t spitzen; uint8_t mult, pts; uint16_t value; int16_t score; };

/* The value of a game: cards = the declarer's 12 (for the Spitzen), pts
   his card points (the Skat counts for him), tDecl / tDef the tricks.
   Overbid: lost, with the next multiple of the base value from the bid. */
static SkResult sktValue(uint32_t cards, uint8_t g, uint8_t fl, uint8_t pts, uint8_t tDecl, uint8_t tDef, uint16_t bid) {
  SkResult r;
  memset(&r, 0, sizeof(r));
  r.pts = pts;
  if (g == SG_NULL) {
    static const uint8_t NV[4] = { 23, 35, 46, 59 };
    r.value = NV[(fl & SKF_HAND ? 1 : 0) + (fl & SKF_OUV ? 2 : 0)];
    r.won = tDecl == 0;
    if (r.value < bid) {
      r.overbid = true; r.won = false;
      uint16_t v = bid;
      for (int8_t k = 3; k >= 0; k--) if (NV[k] >= bid) v = NV[k];
      r.value = v;
    }
  } else {
    r.spitzen = sktSpitzen(cards, g);
    uint8_t m = (uint8_t)((r.spitzen < 0 ? -r.spitzen : r.spitzen) + 1);
    r.schneider = pts >= 90 || pts <= 30;
    r.schwarz = tDef == 0 || tDecl == 0;
    if (fl & SKF_HAND) m++;
    if (r.schneider || (fl & SKF_SCHN)) m++;
    if (fl & SKF_SCHN) m++;
    if (r.schwarz || (fl & SKF_SCHW)) m++;
    if (fl & SKF_SCHW) m++;
    if (fl & SKF_OUV) m++;
    r.mult = m;
    r.value = (uint16_t)sktBase(g) * m;
    r.won = pts > 60 && (!(fl & SKF_SCHN) || pts >= 90) && (!(fl & SKF_SCHW) || tDef == 0);
    if (r.value < bid) {
      uint8_t b = sktBase(g);
      r.overbid = true; r.won = false;
      r.value = (uint16_t)((bid + b - 1) / b * b);
    }
  }
  r.score = r.won ? (int16_t)r.value : -2 * (int16_t)r.value;
  return r;
}

struct Skat {
  uint32_t hand[3], skat, won[3], played;
  uint32_t cards;                       // the declarer's 12 (hand + Skat), for the Spitzen
  uint8_t  dealer, decl, game, flags;
  uint16_t bid;
  uint8_t  trick[3], who[3], nTrick, turn, done, tricks[3];
  uint8_t  voids[3];                    // bit e: player p has shown he has no card of kind e
  uint8_t  bidder, holder, stage;       // bidding: who names values, who answers; stage 3 = over
  bool     toSay;                       // the bidder's turn (else the holder's)
  uint8_t  word[3];                     // said last: 0 nothing, 1 a value, 2 "ja", 3 "passe"
  uint16_t wordV[3];

  uint8_t fore() const { return (dealer + 1) % 3; }   // Vorhand; Mittelhand +1, Hinterhand = dealer

  // 3 each, 2 in the Skat, 4 each, 3 each, starting with Vorhand
  void deal(uint32_t seed, uint8_t dl) {
    memset(this, 0, sizeof(*this));
    uint32_t rnd = seed | 1;
    uint8_t deck[32];
    for (uint8_t i = 0; i < 32; i++) deck[i] = i;
    for (uint8_t i = 32; i > 1; i--) { uint8_t j = bgRand(rnd) % i, t = deck[i - 1]; deck[i - 1] = deck[j]; deck[j] = t; }
    dealer = dl;
    uint8_t k = 0;
    static const uint8_t ROUND[3] = { 3, 4, 3 };
    for (uint8_t r = 0; r < 3; r++) {
      for (uint8_t p = 0; p < 3; p++)
        for (uint8_t j = 0; j < ROUND[r]; j++) hand[(fore() + p) % 3] |= sktBit(deck[k++]);
      if (!r) { skat = sktBit(deck[k]) | sktBit(deck[k + 1]); k += 2; }
    }
    decl = SKT_NONE;
    game = SG_GRAND;
    bidder = (dealer + 2) % 3;          // Mittelhand says, Vorhand hears
    holder = fore();
    toSay = true;
  }

  // ---- bidding ----
  uint8_t  bidActor() const { return stage == 3 ? SKT_NONE : (toSay ? bidder : holder); }
  uint16_t bidAsk() const   { return toSay ? sktNextBid(bid) : bid; }       // 0: no value left
  // the one to act says yes (a value: v, 0 = the next one) or passe
  void bidAct(bool yes, uint16_t v = 0) {
    uint8_t p = bidActor();
    if (p == SKT_NONE) return;
    if (yes && toSay && !v) v = sktNextBid(bid);
    if (yes && toSay && v <= bid) yes = false;
    if (yes) {
      if (toSay) { bid = v; word[p] = 1; wordV[p] = bid; }
      else { word[p] = 2; wordV[p] = bid; }
      if (stage == 2) { setDecl(p); return; }
      toSay = !toSay;
      return;
    }
    word[p] = 3;
    uint8_t other = p == bidder ? holder : bidder;
    if (stage == 0) { stage = 1; holder = other; bidder = dealer; toSay = true; }   // Hinterhand goes on
    else if (stage == 1) {
      if (bid) setDecl(other);
      else { stage = 2; bidder = holder; toSay = true; }   // nobody bid: Vorhand may play for 18
    } else { stage = 3; decl = SKT_NONE; }                  // all passed
  }
  void setDecl(uint8_t p) { stage = 3; decl = p; cards = hand[p] | skat; }

  // ---- declaring ----
  void takeSkat() { hand[decl] |= skat; skat = 0; }
  void push(uint32_t two) { hand[decl] &= ~two; skat = two; }
  void declare(uint8_t g, uint8_t fl) {
    if (g != SG_NULL) {                    // Ouvert means Schwarz means Schneider, all only with Hand
      if (fl & SKF_OUV) fl |= SKF_SCHW;
      if (fl & SKF_SCHW) fl |= SKF_SCHN;
      if (fl & SKF_SCHN) fl |= SKF_HAND;
    }
    game = g; flags = fl;
    turn = fore(); nTrick = 0;
  }

  // ---- tricks ----
  uint32_t legal(uint8_t p) const {
    if (!nTrick) return hand[p];
    uint32_t f = hand[p] & sktEffMask(game, sktEff(game, trick[0]));
    return f ? f : hand[p];
  }
  void play(uint8_t c) {
    uint8_t p = turn;
    if (nTrick && sktEff(game, c) != sktEff(game, trick[0])) voids[p] |= (uint8_t)(1 << sktEff(game, trick[0]));
    hand[p] &= ~sktBit(c);
    played |= sktBit(c);
    trick[nTrick] = c; who[nTrick] = p; nTrick++;
    turn = nTrick == 3 ? SKT_NONE : (p + 1) % 3;
  }
  uint8_t trickWin() const {                   // the index in trick[] that has it
    uint8_t w = 0;
    for (uint8_t i = 1; i < nTrick; i++) if (sktBeats(game, trick[i], trick[w])) w = i;
    return w;
  }
  uint8_t trickPts() const { uint8_t s = 0; for (uint8_t i = 0; i < nTrick; i++) s += SKT_PTS[sktRank(trick[i])]; return s; }
  void collect() {
    uint8_t w = who[trickWin()];
    for (uint8_t i = 0; i < 3; i++) won[w] |= sktBit(trick[i]);
    tricks[w]++; done++;
    nTrick = 0; turn = w;
  }
  // Null ends with the declarer's first trick
  bool over() const { return done == 10 || (game == SG_NULL && tricks[decl]); }
  uint8_t declPts() const { return sktPoints(won[decl] | skat); }
  SkResult result() const {
    return sktValue(cards, game, flags, declPts(), tricks[decl], (uint8_t)(done - tricks[decl]), bid);
  }
};

// =========================================================
//  THE CPU PLAYERS
// =========================================================
/* Bidding and choosing a game: a score for a hand of 10 in a suit game
   or Grand - trumps, Buben, aces, tens beside them, empty suits. From
   SKT_TH (Grand SKT_THG) on it is a game; Null needs every suit safe
   (its i-th lowest card at most 7 9 B K: the others cannot force it). */
#define SKT_TH  28
#define SKT_THG 30
static int16_t sktEval(uint32_t h, uint8_t g) {
  int16_t s;
  if (g == SG_GRAND) {
    uint8_t j = sktCount(h & SKT_JACKS);
    s = 6 * j + (h >> SR_B & 1 ? 3 : 0) + (h >> (8 + SR_B) & 1 ? 1 : 0) - (j <= 1 ? 8 : 0);
    for (uint8_t q = 0; q < 4; q++) {
      uint32_t m = h & sktSuitMask(q) & ~SKT_JACKS;
      uint8_t n = sktCount(m);
      bool a = m >> (q * 8 + SR_A) & 1, x = m >> (q * 8 + SR_10) & 1, k = m >> (q * 8 + SR_K) & 1;
      if (a) { s += 6; if (x) s += 5 + (k ? 2 : 0) + (n > 3 ? 2 * (n - 3) : 0); }
      else if (x) s += n >= 3 ? 1 : -2;
      else s -= n;                                       // losers
    }
    return s;
  }
  uint8_t t = sktCount(h & sktTrumps(g));
  s = 4 * t + (h >> SR_B & 1 ? 3 : 0) + (h >> (8 + SR_B) & 1 ? 2 : 0) + (h >> (16 + SR_B) & 1 ? 1 : 0)
      + (h >> (24 + SR_B) & 1 ? 1 : 0) + (h >> (g * 8 + SR_A) & 1 ? 1 : 0) + (h >> (g * 8 + SR_10) & 1 ? 1 : 0) - (t < 4 ? 10 : 0);
  for (uint8_t q = 0; q < 4; q++) {
    if (q == g) continue;
    uint32_t m = h & sktSuitMask(q) & ~SKT_JACKS;
    uint8_t n = sktCount(m);
    bool a = m >> (q * 8 + SR_A) & 1, x = m >> (q * 8 + SR_10) & 1;
    if (!n) s += t >= 5 ? 3 : 1;
    else if (a) s += 4 + (x ? 3 : 0);
    else s += (n == 1 ? 1 : 0) - (x && n == 1 ? 2 : 0);
  }
  return s;
}
// Null: how many suits are not safe; *cnt = cards in them
static uint8_t sktNullRisk(uint32_t h, uint8_t *cnt = NULL) {
  uint8_t risk = 0, n = 0;
  for (uint8_t q = 0; q < 4; q++) {
    uint8_t i = 0;
    for (uint8_t r = 0; r < 8; r++)
      if (h >> (q * 8 + r) & 1) {
        if (r > 2 * i) { risk++; n += sktCount(h & sktSuitMask(q)); break; }
        i++;
      }
  }
  if (cnt) *cnt = n;
  return risk;
}

// the highest value the CPU bids with these 10 cards (bias < 0: bolder)
static uint16_t sktMaxBid(uint32_t h, int8_t bias = 0) {
  uint16_t best = 0;
  for (uint8_t g = 0; g <= SG_GRAND; g++) {
    int16_t e = sktEval(h, g), th = (g == SG_GRAND ? SKT_THG : SKT_TH) + bias;
    if (e < th) continue;
    int8_t sp = sktSpitzen(h, g);
    uint8_t k = sp > 0 ? sp : (-sp > 3 ? 3 : -sp);       // "ohne" counts at most 3: the Skat may hold a Bube
    uint16_t v = (uint16_t)sktBase(g) * (k + 1 + (e >= th + 14 ? 1 : 0));
    if (v > best) best = v;
  }
  uint8_t cnt, risk = sktNullRisk(h, &cnt);
  if (risk == 0 && best < 35) best = 35;                 // Null Hand
  else if (risk == 1 && cnt <= 2 && best < 23) best = 23; // the weak suit can go into the Skat
  return best;
}

struct SkPlan { uint8_t game, flags; uint32_t push; };

// before the Skat: a game strong enough to play Hand? (Spitzen: "ohne" may turn into "mit 1")
static bool sktCpuHand(uint32_t h, uint16_t bid, SkPlan &pl) {
  int16_t bestM = -1;
  for (uint8_t g = 0; g <= SG_GRAND; g++) {
    int16_t m = sktEval(h, g) - (g == SG_GRAND ? SKT_THG : SKT_TH) - 10;
    int8_t sp = sktSpitzen(h, g);
    if (m < 0 || (uint16_t)sktBase(g) * ((sp > 0 ? sp : 1) + 2) < bid || m <= bestM) continue;
    bestM = m;
    pl.game = g; pl.flags = SKF_HAND; pl.push = 0;
  }
  if (bestM < 0 && bid <= 35 && sktNullRisk(h) == 0) { pl.game = SG_NULL; pl.flags = SKF_HAND; pl.push = 0; bestM = 0; }
  return bestM >= 0;
}

// with the Skat taken (12 cards): the game and the 2 cards to push
static SkPlan sktCpuPick(uint32_t h12, uint16_t bid) {
  SkPlan best = { SG_GRAND, 0, 0 };
  int16_t bestM = -32000;
  uint16_t bestV = 0;
  bool bestFits = false;
  uint8_t cs[12], n = 0;
  for (uint8_t c = 0; c < 32 && n < 12; c++) if (h12 >> c & 1) cs[n++] = c;
  for (uint8_t g = 0; g <= SG_NULL; g++) {
    int16_t top = -32000;
    uint32_t push = 0;
    for (uint8_t i = 0; i < n; i++)
      for (uint8_t j = i + 1; j < n; j++) {
        uint32_t two = sktBit(cs[i]) | sktBit(cs[j]), rest = h12 & ~two;
        int16_t e = g == SG_NULL ? -12 * sktNullRisk(rest) + sktRank(cs[i]) + sktRank(cs[j])     // high cards away
                                 : sktEval(rest, g) + (SKT_PTS[sktRank(cs[i])] + SKT_PTS[sktRank(cs[j])]) / 4;
        if (e > top) { top = e; push = two; }
      }
    uint8_t fl = 0;
    uint16_t v;
    int16_t m;
    if (g == SG_NULL) {
      uint8_t r = sktNullRisk(h12 & ~push);
      m = r == 0 ? 8 : (r == 1 ? -6 : -30);
      v = 23;
      if (r == 0 && bid > 23) { fl = SKF_OUV; v = 46; }
    } else {
      int8_t sp = sktSpitzen(h12, g);
      v = (uint16_t)sktBase(g) * ((sp < 0 ? -sp : sp) + 1);
      m = top - (g == SG_GRAND ? SKT_THG : SKT_TH);
    }
    bool fits = v >= bid;
    bool better = fits != bestFits ? fits : (fits ? m > bestM : (v > bestV || (v == bestV && m > bestM)));
    if (better) { best.game = g; best.flags = fl; best.push = push; bestM = m; bestV = v; bestFits = fits; }
  }
  return best;
}

template <class F> static uint8_t sktBest(uint32_t m, F key) {
  uint8_t bc = SKT_NONE;
  int16_t bv = 0;
  for (uint8_t c = 0; c < 32; c++)
    if (m >> c & 1) { int16_t v = key(c); if (bc == SKT_NONE || v > bv) { bv = v; bc = c; } }
  return bc;
}

/* The card the CPU plays - always one of s.legal(p). It knows its hand,
   the cards played, who could not follow, the Skat it pushed and, in an
   Ouvert game, the declarer's cards.
   Suit / Grand: the declarer draws trumps while he has the best one and
   plays his aces; the defenders cash sure cards, lead to a partner who can
   trump, put points on a trick the partner surely wins (schmieren) and
   take the declarer's points with the cheapest card that does it.
   Null: the declarer stays under the card that has the trick and leads
   where the others must go over; the defenders lead low where he has to
   take it and never go over him.                                      */
static uint8_t sktCpuCard(const Skat &s, uint8_t p) {
  uint32_t L = s.legal(p);
  if (!L) return SKT_NONE;
  if (!(L & (L - 1))) return sktBest(L, [](uint8_t) { return (int16_t)0; });
  uint8_t g = s.game, pos = s.nTrick, d = s.decl;
  uint32_t T = sktTrumps(g), h = s.hand[p];
  uint32_t unseen = ~h & ~s.played;
  if (p == d && !(s.flags & SKF_HAND)) unseen &= ~s.skat;
  auto higher = [&](uint8_t c) {                         // unseen cards of its kind that beat it
    uint32_t m = unseen & sktEffMask(g, sktEff(g, c)), r = 0;
    for (uint8_t k = 0; k < 32; k++) if ((m >> k & 1) && sktPower(g, k) > sktPower(g, c)) r |= sktBit(k);
    return r;
  };
  auto rankHi = [&](uint8_t c) { return (int16_t)sktRank(c); };
  auto rankLo = [&](uint8_t c) { return (int16_t)-sktRank(c); };

  if (g == SG_NULL) {
    auto lower = [&](uint8_t c) { return sktCount(unseen & sktSuitMask(sktSuit(c)) & (sktBit(c) - 1)); };
    if (p == d) {
      if (!pos)                                          // where the others have to go over
        return sktBest(L, [&](uint8_t c) {
          uint8_t lo = lower(c), hi = sktCount(higher(c));
          return (int16_t)((lo == 0 && hi ? 200 : 0) + 4 * (hi - lo) - sktRank(c)); });
      uint8_t W = s.trick[s.trickWin()], E = sktSuit(s.trick[0]);
      if (L & sktSuitMask(E)) {
        uint32_t under = L & sktSuitMask(E) & (sktBit(W) - 1);
        if (under) return sktBest(under, rankHi);         // the highest that stays under
        return pos == 2 ? sktBest(L, rankHi) : sktBest(L, rankLo);
      }
      return sktBest(L, [&](uint8_t c) { return (int16_t)(4 * lower(c) + sktRank(c)); });   // the most dangerous away
    }
    if (!pos)
      return sktBest(L, [&](uint8_t c) {
        uint8_t q = sktSuit(c);
        int16_t v = -4 * sktRank(c);
        if (s.flags & SKF_OUV) {
          uint32_t dm = s.hand[d] & sktSuitMask(q);
          if (!dm) v -= 100;
          else if (!(dm & (sktBit(c) - 1))) v += 100;     // he has nothing below: he must take it
        } else if (s.voids[d] >> q & 1) v -= 100;
        return v; });
    uint8_t wi = s.trickWin(), W = s.trick[wi], E = sktSuit(s.trick[0]);
    bool follows = L & sktSuitMask(E);
    if (s.who[wi] == d) {                                // he has it: do not take it off him
      uint32_t under = L & sktSuitMask(E) & (sktBit(W) - 1);
      if (under) return sktBest(under, rankHi);
      return follows ? sktBest(L, rankLo) : sktBest(L, rankHi);
    }
    if (pos == 2 || !follows) return sktBest(L, rankHi);  // keep the low cards for later
    return sktBest(L, rankLo);                            // he comes next: keep the trick low
  }

  bool isD = p == d;
  uint8_t mate = isD ? SKT_NONE : 3 - p - d;
  auto master = [&](uint8_t c) { return !higher(c); };
  auto low = [&](uint8_t c) { return (int16_t)-(SKT_PTS[sktRank(c)] * 4 + sktPower(g, c) + ((T >> c & 1) ? 40 : 0)); };
  auto pts = [&](uint8_t c) { return (int16_t)(SKT_PTS[sktRank(c)] * 4 - sktPower(g, c)); };
  auto weakest = [&](uint8_t c) { return (int16_t)-sktPower(g, c); };
  uint32_t oppT = unseen & T;

  if (!pos) {
    uint32_t side = h & ~T, myT = h & T, safeM = 0;
    for (uint8_t c = 0; c < 32; c++) {                  // side cards nobody beats or trumps
      if (!(side >> c & 1) || !master(c)) continue;
      uint8_t q = sktSuit(c);
      bool risk = false;
      for (uint8_t o = 0; o < 3; o++)
        if (o != p && o != mate && (s.voids[o] >> q & 1)) risk = true;
      if (!risk || !oppT) safeM |= sktBit(c);
    }
    if (isD) {
      uint8_t top = myT ? sktBest(myT, [&](uint8_t c) { return (int16_t)sktPower(g, c); }) : SKT_NONE;
      if (myT && oppT && master(top)) return top;        // draw trumps
      if (safeM) return sktBest(safeM, pts);
      if (myT && oppT && g != SG_GRAND) return sktBest(myT, low);
      if (side)                                          // low from a long suit, no ten in danger
        return sktBest(side, [&](uint8_t c) {
          int16_t n = sktCount(h & sktEffMask(g, sktSuit(c)));
          bool bare10 = sktRank(c) == SR_10 && !master(c);
          return (int16_t)(n * 8 - SKT_PTS[sktRank(c)] * 2 - (bare10 ? 30 : 0)); });
      return top;
    }
    if (safeM) return sktBest(safeM, pts);
    for (uint8_t q = 0; q < 4; q++) {                    // the partner trumps it
      uint32_t m = side & sktEffMask(g, q);
      if (m && (s.voids[mate] >> q & 1) && !(s.voids[mate] >> SKT_TR & 1) && !(s.voids[d] >> q & 1) && oppT)
        return sktBest(m, low);
    }
    if (side)
      return sktBest(side, [&](uint8_t c) {
        uint8_t q = sktSuit(c);
        int16_t n = sktCount(h & sktEffMask(g, q));
        bool bare10 = sktRank(c) == SR_10 && !master(c);
        return (int16_t)(n * 4 - SKT_PTS[sktRank(c)] * 3 - (bare10 ? 30 : 0) - ((s.voids[d] >> q & 1) ? 10 : 0)); });
    return sktBest(L, low);
  }

  uint8_t wi = s.trickWin(), W = s.trick[wi], wp = s.who[wi], E = sktEff(g, s.trick[0]);
  int16_t tp = s.trickPts();
  uint8_t nx = (p + 1) % 3;
  bool follows = L & sktEffMask(g, E);
  uint32_t win = 0;
  for (uint8_t c = 0; c < 32; c++) if ((L >> c & 1) && sktBeats(g, c, W)) win |= sktBit(c);
  auto sure = [&](uint8_t c, uint8_t o) {               // o cannot beat c any more (as far as known)
    bool ruff = sktEff(g, c) != SKT_TR && (s.voids[o] >> sktEff(g, c) & 1) && oppT;
    return master(c) && !ruff;
  };

  if (!isD && wp != d) {                                 // the partner has it
    bool safe = pos == 2 || sure(W, d);
    if (safe)                                            // schmieren: points on it, the ten first, trumps kept
      return sktBest(L, [&](uint8_t c) {
        if (T >> c & 1) return (int16_t)(-20 - sktPower(g, c));
        return (int16_t)(SKT_PTS[sktRank(c)] * 4 + (sktRank(c) == SR_10 ? 6 : 0) - sktPower(g, c)); });
    uint32_t sw = 0;
    for (uint8_t c = 0; c < 32; c++) if ((win >> c & 1) && sure(c, nx)) sw |= sktBit(c);
    if (sw && tp >= 10) return sktBest(sw, weakest);
    return sktBest(L, low);
  }
  if (!win) return sktBest(L, low);
  if (pos == 2) {                                        // last: take it as cheaply as possible
    uint8_t c = sktBest(win, weakest);
    if (tp + SKT_PTS[sktRank(c)] > 0 && (tp >= 3 || sktRank(c) != SR_B)) return c;
    return sktBest(L, low);
  }
  bool nxFriend = !isD && nx != d;
  uint32_t sw = 0;
  for (uint8_t c = 0; c < 32; c++) if ((win >> c & 1) && sure(c, nx)) sw |= sktBit(c);
  if (sw) return sktBest(sw, pts);
  if (!follows && tp >= 10) {                            // trump it in
    if (nxFriend || tp < 14) return sktBest(win, weakest);
    return sktBest(win, [&](uint8_t c) { return (int16_t)sktPower(g, c); });
  }
  return sktBest(L, low);
}

// =========================================================
//  THE TABLE
// =========================================================
#define SKT_HW  34                       // a card in the hand
#define SKT_HH  52
#define SKT_HY  184
#define SKT_CW  44                       // a card in the trick
#define SKT_CH  64
#define SKT_ACC rgb(0, 140, 70)
enum { SKP_NONE, SKP_BID, SKP_TAKE, SKP_PUSH, SKP_GAME, SKP_PLAY, SKP_RESULT, SKP_LIST };

static Skat     skt;
static uint8_t  sktPhase;                // what the keys do now (the simulator's bot reads it)
static uint8_t  sktCur, sktSel, sktAnn;    // cursor in the hand, choice in a panel, announcement
static uint16_t sktVal;                  // the value you would say
static uint32_t sktMark;                 // cards marked to push
static bool     sktSkatOn;               // the Skat lies face down in the middle
static char     sktMsg[40];
static int16_t  sktTotal[3];
static uint8_t  sktGames, sktRows;
static struct { uint8_t decl; int16_t score; } sktList[36];
static struct { bool on; uint8_t c; int16_t x, y; } sktFly;

static const char *const SKT_NAME[3] = { "YOU", "ANNA", "BEN" };
static const char *const SKT_GN[6]   = { "Kreuz", "Pik", "Herz", "Karo", "Grand", "Null" };
static const char *const SKT_RN[8]   = { "7", "8", "9", "10", "B", "D", "K", "A" };
static const char *const SKT_RL[8]   = { "7", "8", "9", "10", "Bube", "Dame", "Koenig", "As" };
static const int16_t SKT_TX[3] = { 138, 86, 190 }, SKT_TY[3] = { 96, 56, 56 };   // trick places

static void sktGameName(char *b, size_t n, uint8_t g, uint8_t fl) {
  if (g == SG_NULL) snprintf(b, n, "Null%s%s", fl & SKF_OUV ? " Ouvert" : "", fl & SKF_HAND ? " Hand" : "");
  else snprintf(b, n, "%s%s%s", SKT_GN[g], fl & SKF_HAND ? " Hand" : "",
                fl & SKF_OUV ? " Ouvert" : (fl & SKF_SCHW ? " Schwarz" : (fl & SKF_SCHN ? " Schneider" : "")));
}

/* The four-colour deck of the Skat tournaments: Kreuz black, Pik green,
   Herz red, Karo orange - Kreuz and Pik look alike in black on a small
   screen. The corner sign is bigger than Mau-Mau's too.               */
static col sktSuitCol(uint8_t s) {
  static const col SC[4] = { C::BLACK, rgb(0, 140, 40), rgb(220, 0, 0), rgb(235, 130, 0) };
  return SC[s & 3];
}

// a card face up (as Mau-Mau's, with the German letters B D K)
static void sktCard(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c, bool hi = false) {
  uint8_t s = sktSuit(c), r = sktRank(c);
  col sc = sktSuitCol(s);
  gfx.rbox(x + 2, y + 2, w, h, 4, rgb(0, 40, 0));
  gfx.rbox(x, y, w, h, 4, C::WHITE);
  gfx.rframe(x, y, w, h, 4, hi ? C::YELLOW : C::GREY);
  if (hi) gfx.rframe(x + 1, y + 1, w - 2, h - 2, 4, C::YELLOW);
  gfx.text(x + 3, y + 3, SKT_RN[r], sc, F_BOLD);
  mmSuit(x + 10, y + 35, s, 6, sc);
  if (r >= SR_B && r <= SR_K) {
    bool big = w >= 40;
    int16_t bw = big ? 22 : 16, bh = big ? 30 : 22, bx = x + w - bw - 3, by = y + h - bh - 3;
    gfx.rbox(bx, by, bw, bh, 3, r == SR_B ? rgb(255, 204, 102) : rgb(255, 230, 153));
    gfx.rframe(bx, by, bw, bh, 3, sc);
    Font f = big ? F_BOLD : F_TEXT;
    gfx.text(bx + (bw - Gfx::width(SKT_RN[r], f)) / 2 + 1, by + (bh - Gfx::height(f)) / 2 + 1, SKT_RN[r], sc, f);
  } else mmSuit(x + w / 2 + 5, y + h / 2 + 10, s, r == SR_A ? 9 : 7, sc);
}
static void sktMini(int16_t x, int16_t y, uint8_t c) {
  col sc = sktSuitCol(sktSuit(c));
  gfx.rbox(x, y, 18, 26, 3, C::WHITE);
  gfx.rframe(x, y, 18, 26, 3, C::GREY);
  gfx.text(x + 2, y + 2, SKT_RN[sktRank(c)], sc, F_SMALL);
  mmSuit(x + 9, y + 18, sktSuit(c), 4, sc);
}

// your cards in order: Buben, the trump suit, the others (Null: by suit, A K D B 10 9 8 7)
static uint8_t sktSortGame() {
  if (sktPhase == SKP_GAME) return sktSel <= SG_GRAND ? sktSel : (uint8_t)SG_NULL;
  return skt.game;
}
static int16_t sktSortKey(uint8_t c, uint8_t g) {
  if (g == SG_NULL) return sktSuit(c) * 8 + 7 - sktRank(c);
  if (sktRank(c) == SR_B) return sktSuit(c);
  uint8_t grp = (g < 4 && sktSuit(c) == g) ? 0 : sktSuit(c) + 1;
  return 4 + grp * 8 + 7 - SKT_ORD[sktRank(c)];
}
static uint8_t sktSorted(uint32_t h, uint8_t *out) {
  uint8_t n = 0, g = sktSortGame();
  for (uint8_t c = 0; c < 32; c++)
    if (h >> c & 1) {
      uint8_t i = n++;
      while (i && sktSortKey(out[i - 1], g) > sktSortKey(c, g)) { out[i] = out[i - 1]; i--; }
      out[i] = c;
    }
  return n;
}
static uint8_t sktHandList(uint8_t *out) { return sktSorted(skt.hand[0], out); }

static int16_t sktSlotX(uint8_t i, uint8_t slots) {
  int16_t st = slots > 1 ? (int16_t)((SCR_W - 8 - SKT_HW) / (slots - 1)) : 0;
  if (st > SKT_HW + 4) st = SKT_HW + 4;
  return (SCR_W - (st * (slots - 1) + SKT_HW)) / 2 + i * st;
}

static bool sktPlaying() { return skt.stage == 3 && skt.decl != SKT_NONE && !sktSkatOn && sktPhase != SKP_TAKE && sktPhase != SKP_PUSH && sktPhase != SKP_GAME; }

static void sktTop() {
  gfx.gradient(0, 0, SCR_W, TOP_H, colLight(SKT_ACC), colDark(SKT_ACC, 2));
  gfx.hline(0, TOP_H - 1, SCR_W, colLight(SKT_ACC, 2));
  gfx.textShadow(6, 5, "SKAT", C::WHITE, C::BLACK, F_TEXT);
  char b[48], gn[24];
  if (sktPlaying()) {
    sktGameName(gn, sizeof(gn), skt.game, skt.flags);
    snprintf(b, sizeof(b), "%s: %s, bid %u", SKT_NAME[skt.decl], gn, skt.bid);
  } else if (skt.stage < 3) { if (skt.bid) snprintf(b, sizeof(b), "Reizen: %u", skt.bid); else strcpy(b, "Reizen"); }
  else if (skt.decl != SKT_NONE) snprintf(b, sizeof(b), "%s plays for %u", SKT_NAME[skt.decl], skt.bid);
  else b[0] = 0;
  gfx.text(58, 4, b, C::WHITE, F_SMALL);
  int16_t x = gfx.text(58, 15, "game ", C::LTGREY, F_SMALL);
  snprintf(b, sizeof(b), "%u/%u", sktRows + 1 > sktGames ? sktGames : sktRows + 1, sktGames);
  x = gfx.text(x, 15, b, C::LTGREY, F_SMALL);
  if (sktPlaying() && skt.game != SG_NULL) {
    uint8_t dp = sktPoints(skt.won[skt.decl]), all = 0;
    for (uint8_t p = 0; p < 3; p++) all += sktPoints(skt.won[p]);
    snprintf(b, sizeof(b), "   Augen %u : %u", dp, all - dp);
    gfx.text(x, 15, b, C::YELLOW, F_SMALL);
  }
  snprintf(b, sizeof(b), "%+d", sktTotal[0]);
  gfx.right(SCR_W - 34, 5, b, sktTotal[0] < 0 ? rgb(255, 153, 153) : C::YELLOW, F_TEXT);
  batIcon(SCR_W - 28, 7);
}

// the box of a player: name, Vorhand/Mittelhand/Hinterhand or tricks, what he said or plays
static void sktLabel(uint8_t p) {
  int16_t x = p == 2 ? 238 : 4, y = p == 0 ? 114 : 32, w = 78, h = p == 0 ? 46 : 52;
  bool act = skt.stage < 3 ? skt.bidActor() == p : (sktPlaying() && skt.turn == p && !skt.over());
  gfx.rbox(x, y, w, h, 6, skt.decl == p && skt.stage == 3 ? rgb(110, 60, 0) : rgb(0, 70, 30));
  gfx.rframe(x, y, w, h, 6, act ? C::YELLOW : rgb(0, 150, 70));
  gfx.text(x + 5, y + 3, SKT_NAME[p], act ? C::YELLOW : C::WHITE, F_TEXT);
  char b[24];
  if (sktPlaying()) snprintf(b, sizeof(b), "%u", skt.tricks[p]);
  else { static const char RL[3] = { 'V', 'M', 'H' }; snprintf(b, sizeof(b), "%c", RL[(p + 2 - skt.dealer) % 3]); }
  gfx.right(x + w - 5, y + 6, b, C::LTGREY, F_SMALL);
  if (sktPlaying() && skt.decl == p) {
    snprintf(b, sizeof(b), "%s%s", SKT_GN[skt.game], skt.flags & SKF_HAND ? " Hand" : "");
    gfx.text(x + 5, y + 24, b, C::YELLOW, F_SMALL);
    const char *a = skt.flags & SKF_OUV ? "Ouvert" : (skt.flags & SKF_SCHW ? "Schwarz" : (skt.flags & SKF_SCHN ? "Schneider" : ""));
    gfx.text(x + 5, y + 34, a, C::YELLOW, F_SMALL);
  } else if (sktPlaying()) {
    gfx.text(x + 5, y + 24, "defends", C::LTGREY, F_SMALL);
  } else if (skt.word[p]) {
    if (skt.word[p] == 1) snprintf(b, sizeof(b), "%u", skt.wordV[p]);
    else if (skt.word[p] == 2) snprintf(b, sizeof(b), "ja %u", skt.wordV[p]);
    else snprintf(b, sizeof(b), "passe");
    gfx.text(x + 5, y + 26, b, skt.word[p] == 3 ? C::LTGREY : C::WHITE, F_TEXT);
  }
}

static void sktDraw() {
  gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(0, 110, 40), rgb(0, 50, 15));
  sktTop();
  for (uint8_t p = 0; p < 3; p++) sktLabel(p);
  bool open = sktPlaying() && (skt.flags & SKF_OUV) && skt.decl;
  for (uint8_t p = 1; p < 3; p++) {                       // the CPUs' cards: backs, or open (Ouvert)
    uint8_t n = sktCount(skt.hand[p]);
    if (open && skt.decl == p) continue;
    for (uint8_t i = 0; i < n; i++) mmBack((p == 2 ? 242 : 8) + i * 6, 88, 14, 22);
  }
  if (open) {
    uint8_t cs[12], n = sktSorted(skt.hand[skt.decl], cs);
    for (uint8_t i = 0; i < n; i++) sktMini(86 + i * 14, 28, cs[i]);
  }
  if (sktSkatOn) { mmBack(124, 64, SKT_HW, SKT_HH); mmBack(162, 64, SKT_HW, SKT_HH); gfx.center(120, "Skat", C::LIME, F_SMALL); }
  for (uint8_t i = 0; i < skt.nTrick; i++) {
    uint8_t p = skt.who[i];
    sktCard(SKT_TX[p], SKT_TY[p], SKT_CW, SKT_CH, skt.trick[i], skt.nTrick == 3 && i == skt.trickWin());
  }
  gfx.center(166, sktMsg, C::WHITE, F_SMALL);
  // own cards along the bottom
  uint8_t cs[12], n = sktHandList(cs);
  bool pushSlot = sktPhase == SKP_PUSH && sktCount(sktMark) == 2;
  uint8_t slots = n + (pushSlot ? 1 : 0);
  bool cursor = sktPhase == SKP_PLAY || sktPhase == SKP_PUSH;
  uint32_t L = sktPhase == SKP_PLAY ? skt.legal(0) : 0xFFFFFFFFUL;
  int16_t st = sktSlotX(1, slots) - sktSlotX(0, slots);
  for (uint8_t i = 0; i < n; i++) {
    if (sktFly.on && sktFly.c == cs[i]) continue;
    int16_t x = sktSlotX(i, slots);
    bool on = cursor && i == sktCur, mk = sktMark >> cs[i] & 1;
    int16_t y = SKT_HY - (on ? 8 : 0) - (mk ? 16 : 0);
    sktCard(x, y, SKT_HW, SKT_HH, cs[i], on);
    if (mk) gfx.rframe(x, y, SKT_HW, SKT_HH, 4, C::ORANGE);
    if (!(L >> cs[i] & 1)) gfx.dim(x + 1, y + 1, (i + 1 < n && st < SKT_HW ? st : SKT_HW) - 2, SKT_HH - 2);
  }
  if (pushSlot) {
    int16_t x = sktSlotX(n, slots), y = SKT_HY - (sktCur == n ? 8 : 0);
    gfx.rbox(x, y, SKT_HW, SKT_HH, 4, sktCur == n ? C::ORANGE : C::DKGREY);
    gfx.rframe(x, y, SKT_HW, SKT_HH, 4, C::WHITE);
    gfx.text(x + (SKT_HW - Gfx::width("push", F_SMALL)) / 2, y + 20, "push", C::WHITE, F_SMALL);
  }
  if (sktFly.on) sktCard(sktFly.x, sktFly.y, SKT_CW, SKT_CH, sktFly.c);
}

// the table for ms; false = left the game
static bool sktWait(uint16_t ms) {
  uint32_t t0 = gameMillis();
  while (gameMillis() - t0 < ms) {
    if (!poll()) return false;
    sktDraw();
    gfx.send();
  }
  return true;
}

// card c of player p flies to its place in the trick
static bool sktFlyTo(uint8_t p, uint8_t c) {
  int16_t x0, y0;
  if (p == 0) {
    uint8_t cs[12], n = sktHandList(cs), i = 0;
    while (i < n && cs[i] != c) i++;
    x0 = sktSlotX(i, n); y0 = SKT_HY - 8;
  } else { x0 = p == 2 ? 256 : 20; y0 = 70; }
  const uint16_t ms = 260;
  uint32_t t0 = gameMillis();
  sktFly.on = true; sktFly.c = c;
  while (true) {
    if (!poll()) { sktFly.on = false; return false; }
    uint32_t t = gameMillis() - t0;
    if (t > ms) t = ms;
    int32_t e = (int32_t)t * (2 * ms - t) * 256 / ((int32_t)ms * ms);
    sktFly.x = (int16_t)(x0 + (SKT_TX[p] - x0) * e / 256);
    sktFly.y = (int16_t)(y0 + (SKT_TY[p] - y0) * e / 256);
    sktDraw();
    gfx.send();
    if (t >= ms) break;
  }
  sktFly.on = false;
  return true;
}

// two buttons side by side in a panel, the chosen one bright
static void sktButtons(int16_t cx, int16_t y, const char *a, const char *b, uint8_t sel) {
  const char *t[2] = { a, b };
  for (uint8_t i = 0; i < 2; i++) {
    int16_t x = cx - 66 + i * 70;
    gfx.rbox(x, y, 62, 24, 6, sel == i ? C::YELLOW : C::DKGREY);
    gfx.text(x + (62 - Gfx::width(t[i])) / 2, y + 3, t[i], sel == i ? C::BLACK : C::WHITE);
  }
}

// your call in the bidding; false = left the game
static bool sktHumanBid() {
  bool say = skt.toSay;
  uint16_t ask = skt.bidAsk();
  if (!ask) { skt.bidAct(false); return true; }            // nothing above 264
  sktVal = ask; sktSel = 0; sktPhase = SKP_BID;
  btnClear();
  while (true) {
    if (!poll()) { sktPhase = SKP_NONE; return false; }
    if (say && btn(B_UP)) { uint16_t v = sktNextBid(sktVal); if (v) { sktVal = v; sfx(700, 8); } }
    if (say && btn(B_DOWN) && sktVal > ask) { sktVal = sktPrevBid(sktVal); sfx(700, 8); }
    if (btn(B_LEFT) || btn(B_RIGHT)) { sktSel ^= 1; sfx(700, 8); }
    if (btn(B_OK)) { sfx(1000, 30); skt.bidAct(sktSel == 0, say ? sktVal : 0); sktPhase = SKP_NONE; return true; }
    sktDraw();
    panel(86, 34, 148, 124, "REIZEN", C::RED);
    char b[24];
    if (say) {
      gfx.center(70, skt.stage == 2 ? "all passed - play?" : "you say", C::LTGREY, F_SMALL);
      snprintf(b, sizeof(b), "%u", sktVal);
      gfx.center(84, b, C::YELLOW, F_BOLD);
      gfx.triangle(204, 84, 198, 92, 210, 92, sktNextBid(sktVal) ? C::WHITE : C::DKGREY);
      gfx.triangle(204, 106, 198, 98, 210, 98, sktVal > ask ? C::WHITE : C::DKGREY);
      sktButtons(160, 120, b, "passe", sktSel);
    } else {
      snprintf(b, sizeof(b), "%s says", SKT_NAME[skt.bidder]);
      gfx.center(70, b, C::LTGREY, F_SMALL);
      snprintf(b, sizeof(b), "%u", skt.bid);
      gfx.center(84, b, C::YELLOW, F_BOLD);
      sktButtons(160, 120, "ja", "passe", sktSel);
    }
    gfx.send();
  }
}

// the value a game would have when won as announced (your cards; in Hand without the Skat)
static SkResult sktPreview(uint8_t g, uint8_t fl) {
  uint32_t known = fl & SKF_HAND ? skt.hand[0] : skt.cards;
  if (g != SG_NULL) {
    if (fl & SKF_OUV) fl |= SKF_SCHW;
    if (fl & SKF_SCHW) fl |= SKF_SCHN;
  }
  bool s = fl & SKF_SCHW;
  return sktValue(known, g, fl, fl & SKF_SCHN ? 90 : 61, s ? 10 : 5, s ? 0 : 5, 0);
}
static void sktRowGame(uint8_t row, bool hand, uint8_t ann, uint8_t &g, uint8_t &fl) {
  static const uint8_t ANN[4] = { 0, SKF_SCHN, SKF_SCHW, SKF_OUV };
  g = row <= SG_GRAND ? row : (uint8_t)SG_NULL;
  fl = (hand ? SKF_HAND : 0) | (row == 6 ? SKF_OUV : 0) | (row <= SG_GRAND && hand ? ANN[ann] : 0);
}

// you won the bidding: Skat or Hand, push 2, announce; false = left the game
static bool sktHumanDeclare() {
  sktPhase = SKP_TAKE; sktSel = 0;
  btnClear();
  while (true) {
    if (!poll()) { sktPhase = SKP_NONE; return false; }
    if (btn(B_LEFT) || btn(B_RIGHT)) { sktSel ^= 1; sfx(700, 8); }
    if (btn(B_OK)) { sfx(1000, 30); break; }
    sktDraw();
    panel(86, 34, 148, 124, "YOU PLAY", C::RED);
    char b[24];
    snprintf(b, sizeof(b), "for %u", skt.bid);
    gfx.center(72, b, C::YELLOW, F_BOLD);
    gfx.center(104, "take the Skat?", C::LTGREY, F_SMALL);
    sktButtons(160, 120, "Skat", "Hand", sktSel);
    gfx.send();
  }
  bool hand = sktSel == 1;
  if (!hand) {
    sktSkatOn = false;
    skt.takeSkat();
    sktMark = 0; sktCur = 0; sktPhase = SKP_PUSH;
    snprintf(sktMsg, sizeof(sktMsg), "push 2 cards: OK marks, then OK on push");
    btnClear();
    while (true) {
      if (!poll()) { sktPhase = SKP_NONE; return false; }
      uint8_t cs[12], n = sktHandList(cs), slots = n + (sktCount(sktMark) == 2 ? 1 : 0);
      if (sktCur >= slots) sktCur = slots - 1;
      if (btn(B_LEFT) && sktCur > 0) { sktCur--; sfx(700, 8); }
      if (btn(B_RIGHT) && sktCur + 1 < slots) { sktCur++; sfx(700, 8); }
      if (btn(B_OK)) {
        if (sktCur == n) { sfx(1000, 30); skt.push(sktMark); break; }
        uint32_t b = sktBit(cs[sktCur]);
        if ((sktMark & b) || sktCount(sktMark) < 2) { sktMark ^= b; sfx(900, 20); }
        else sfx(200, 120);
      }
      sktDraw();
      gfx.send();
    }
    sktMark = 0;
  }
  sktPhase = SKP_GAME; sktSel = 0; sktAnn = 0;
  snprintf(sktMsg, sizeof(sktMsg), "announce your game");
  btnClear();
  while (true) {
    if (!poll()) { sktPhase = SKP_NONE; return false; }
    if (btn(B_UP))   { sktSel = sktSel ? sktSel - 1 : 6; sfx(700, 8); }
    if (btn(B_DOWN)) { sktSel = (sktSel + 1) % 7; sfx(700, 8); }
    if (hand && sktSel <= SG_GRAND) {
      if (btn(B_RIGHT)) { sktAnn = (sktAnn + 1) % 4; sfx(700, 8); }
      if (btn(B_LEFT))  { sktAnn = (sktAnn + 3) % 4; sfx(700, 8); }
    }
    uint8_t g, fl;
    sktRowGame(sktSel, hand, sktAnn, g, fl);
    SkResult pv = sktPreview(g, fl);
    if (btn(B_OK)) {
      if (g == SG_NULL && pv.value < skt.bid) sfx(200, 120);   // a Null worth less than the bid: not allowed
      else { sfx(1200, 50); skt.declare(g, fl); break; }
    }
    sktDraw();
    panel(40, 30, 240, 150, "YOUR GAME", C::RED);
    for (uint8_t r = 0; r < 7; r++) {
      uint8_t rg, rf;
      sktRowGame(r, hand, r == sktSel ? sktAnn : 0, rg, rf);
      char b[32];
      sktGameName(b, sizeof(b), rg, rf);
      bool on = r == sktSel, bad = rg == SG_NULL && sktPreview(rg, rf).value < skt.bid;
      int16_t y = 62 + r * 13;
      if (on) gfx.rbox(48, y - 2, 224, 13, 3, C::YELLOW);
      if (rg < SG_GRAND) {                                // the suit in its colour, on a white chip
        gfx.rbox(52, y - 1, 11, 11, 2, C::WHITE);
        mmSuit(57, y + 4, rg, 3, sktSuitCol(rg));
      }
      gfx.text(68, y, b, on ? C::BLACK : (bad ? C::GREY : C::WHITE), F_SMALL);
      if (on && hand && r <= SG_GRAND) gfx.text(240, y, "< >", C::BLACK, F_SMALL);
    }
    char b[40];
    if (g == SG_NULL) snprintf(b, sizeof(b), "value %u", pv.value);
    else snprintf(b, sizeof(b), "%s %d: %u x %u = %u", pv.spitzen > 0 ? "mit" : "ohne",
                  pv.spitzen < 0 ? -pv.spitzen : pv.spitzen, pv.mult, sktBase(g), pv.value);
    gfx.center(156, b, C::YELLOW, F_SMALL);
    if (pv.value < skt.bid) {
      snprintf(b, sizeof(b), g == SG_NULL ? "below the bid %u" : "below the bid %u: Schneider needed", skt.bid);
      gfx.center(167, b, rgb(255, 120, 120), F_SMALL);
    }
    gfx.send();
  }
  sktPhase = SKP_NONE;
  return true;
}

static int16_t sktHumanCard() {
  uint8_t cs[12], n = sktHandList(cs);
  uint32_t L = skt.legal(0);
  if (sktCur >= n || !(L >> cs[sktCur] & 1))
    for (uint8_t i = 0; i < n; i++) if (L >> cs[i] & 1) { sktCur = i; break; }
  sktPhase = SKP_PLAY;
  snprintf(sktMsg, sizeof(sktMsg), skt.nTrick ? "your turn" : "your lead");
  btnClear();
  while (true) {
    if (!poll()) { sktPhase = SKP_NONE; return -1; }
    if (btn(B_LEFT) && sktCur > 0) { sktCur--; sfx(700, 8); }
    if (btn(B_RIGHT) && sktCur + 1 < n) { sktCur++; sfx(700, 8); }
    if (btn(B_OK)) {
      if (L >> cs[sktCur] & 1) { sktPhase = SKP_NONE; return cs[sktCur]; }
      sfx(200, 120);
    }
    sktDraw();
    gfx.send();
  }
}

static bool sktPlayTricks() {
  while (true) {
    if (skt.nTrick == 3) {
      uint8_t w = skt.who[skt.trickWin()];
      snprintf(sktMsg, sizeof(sktMsg), "%s %s %u", SKT_NAME[w], w ? "takes" : "take", skt.trickPts());
      if (!sktWait(1100)) return false;
      skt.collect();
      sfx(w == skt.decl ? 500 : 900, 30);
      if (skt.over()) return true;
      continue;
    }
    uint8_t p = skt.turn, c;
    if (p == 0) {
      int16_t r = sktHumanCard();
      if (r < 0) return false;
      c = (uint8_t)r;
    } else {
      if (!sktWait(skt.nTrick ? 600 : 800)) return false;
      c = sktCpuCard(skt, p);
    }
    if (!sktFlyTo(p, c)) return false;
    skt.play(c);
    snprintf(sktMsg, sizeof(sktMsg), "%s: %s %s", SKT_NAME[p], SKT_GN[sktSuit(c)], SKT_RL[sktRank(c)]);
    sfx(800, 20);
  }
}

static bool sktShowResult(const SkResult &r) {
  bool youWin = skt.decl == 0 ? r.won : !r.won;
  sfx(youWin ? 1400 : 250, 250);
  sktPhase = SKP_RESULT;
  uint32_t t0 = gameMillis();
  btnClear();
  while (true) {
    if (!poll()) { sktPhase = SKP_NONE; return false; }
    if (btn(B_OK) && gameMillis() - t0 > 400) break;
    sktDraw();
    panel(30, 30, 260, 176, NULL, r.won ? SKT_ACC : C::RED);
    char b[48], gn[24];
    if (skt.decl == 0) snprintf(b, sizeof(b), r.won ? "YOU WIN" : "YOU LOSE");
    else snprintf(b, sizeof(b), "%s %s", SKT_NAME[skt.decl], r.won ? "WINS" : "LOSES");
    gfx.center(38, b, r.won ? C::YELLOW : C::WHITE, F_BOLD);
    sktGameName(gn, sizeof(gn), skt.game, skt.flags);
    gfx.center(68, gn, C::WHITE, F_TEXT);
    if (skt.game == SG_NULL) snprintf(b, sizeof(b), "value %u, tricks taken %u", r.value, skt.tricks[skt.decl]);
    else snprintf(b, sizeof(b), "%s %d, game %u x %u = %u", r.spitzen > 0 ? "mit" : "ohne",
                  r.spitzen < 0 ? -r.spitzen : r.spitzen, r.mult, sktBase(skt.game), (uint16_t)(r.mult * sktBase(skt.game)));
    gfx.center(92, b, C::LTGREY, F_SMALL);
    if (skt.game != SG_NULL) {
      snprintf(b, sizeof(b), "Augen %u : %u%s", r.pts, 120 - r.pts, r.schwarz ? "  Schwarz" : (r.schneider ? "  Schneider" : ""));
      gfx.center(104, b, C::LTGREY, F_SMALL);
    }
    snprintf(b, sizeof(b), r.overbid ? "bid %u - ueberreizt!" : "bid %u", skt.bid);
    gfx.center(116, b, r.overbid ? rgb(255, 120, 120) : C::LTGREY, F_SMALL);
    gfx.text(118, 136, "Skat", C::LTGREY, F_SMALL);
    uint8_t i = 0;
    for (uint8_t c = 0; c < 32; c++) if (skt.skat >> c & 1) sktMini(146 + 22 * i++, 130, c);
    snprintf(b, sizeof(b), "%+d", r.score);
    gfx.center(160, b, r.won ? C::YELLOW : rgb(255, 120, 120), F_BOLD);
    gfx.center(192, "OK = list", C::GREY, F_SMALL);
    gfx.send();
  }
  sktPhase = SKP_NONE;
  return true;
}

// the list: every game in the declarer's column, the sums below
static bool sktShowList(bool final) {
  sktPhase = SKP_LIST;
  uint32_t t0 = gameMillis();
  btnClear();
  while (true) {
    if (!poll()) { sktPhase = SKP_NONE; return false; }
    if (btn(B_OK) && gameMillis() - t0 > 400) break;
    sktDraw();
    char b[40];
    snprintf(b, sizeof(b), final ? "FINAL LIST" : "LISTE %u/%u", sktRows, sktGames);
    panel(20, 28, 280, 192, b, SKT_ACC);
    static const int16_t CX[3] = { 140, 210, 280 };
    gfx.text(34, 62, "#", C::LTGREY, F_SMALL);
    for (uint8_t p = 0; p < 3; p++) gfx.right(CX[p], 62, SKT_NAME[p], C::WHITE, F_SMALL);
    gfx.hline(28, 73, 264, C::GREY);
    uint8_t first = sktRows > 9 ? sktRows - 9 : 0;
    for (uint8_t i = first; i < sktRows; i++) {
      int16_t y = 77 + (i - first) * 11;
      snprintf(b, sizeof(b), "%u", i + 1);
      gfx.text(34, y, b, C::GREY, F_SMALL);
      snprintf(b, sizeof(b), "%+d", sktList[i].score);
      gfx.right(CX[sktList[i].decl], y, b, sktList[i].score < 0 ? rgb(255, 120, 120) : C::WHITE, F_SMALL);
    }
    gfx.hline(28, 177, 264, C::GREY);
    gfx.text(34, 182, "sum", C::LTGREY, F_SMALL);
    uint8_t best = 0;
    for (uint8_t p = 0; p < 3; p++) {
      snprintf(b, sizeof(b), "%d", sktTotal[p]);
      gfx.right(CX[p], 182, b, C::YELLOW, F_SMALL);
      if (sktTotal[p] > sktTotal[best]) best = p;
    }
    if (final) snprintf(b, sizeof(b), "%s %s the Liste - OK", SKT_NAME[best], best ? "wins" : "win");
    else snprintf(b, sizeof(b), "OK = next game");
    gfx.center(202, b, final ? C::YELLOW : C::GREY, F_SMALL);
    gfx.send();
  }
  sktPhase = SKP_NONE;
  return true;
}

// one deal: -1 = left, 0 = all passed, 1 = played
static int8_t sktOneGame(uint8_t dealer, uint32_t seed) {
  skt.deal(seed, dealer);
  sktMark = 0; sktSkatOn = true; sktFly.on = false; sktCur = 0;
  snprintf(sktMsg, sizeof(sktMsg), "%s %s", SKT_NAME[dealer], dealer ? "deals" : "deal");
  sfx(500, 40);
  if (!sktWait(900)) return -1;
  while (skt.stage != 3) {                                 // Reizen
    uint8_t a = skt.bidActor();
    if (a == 0) { if (!sktHumanBid()) return -1; }
    else {
      if (!sktWait(750)) return -1;
      uint16_t ask = skt.bidAsk();
      skt.bidAct(ask && ask <= sktMaxBid(skt.hand[a]));
    }
    if (skt.word[a] == 1) snprintf(sktMsg, sizeof(sktMsg), "%s: %u", SKT_NAME[a], skt.wordV[a]);
    else snprintf(sktMsg, sizeof(sktMsg), "%s: %s", SKT_NAME[a], skt.word[a] == 2 ? "ja" : "passe");
    sfx(skt.word[a] == 3 ? 400 : 700, 25);
  }
  if (skt.decl == SKT_NONE) {
    snprintf(sktMsg, sizeof(sktMsg), "all passe - new deal");
    return sktWait(1800) ? 0 : -1;
  }
  uint8_t d = skt.decl;
  snprintf(sktMsg, sizeof(sktMsg), "%s %s for %u", SKT_NAME[d], d ? "plays" : "play", skt.bid);
  if (d == 0) { if (!sktHumanDeclare()) return -1; }
  else {
    if (!sktWait(1000)) return -1;
    SkPlan pl;
    if (!sktCpuHand(skt.hand[d], skt.bid, pl)) {
      sktSkatOn = false;
      skt.takeSkat();
      pl = sktCpuPick(skt.hand[d], skt.bid);
      skt.push(pl.push);
    }
    skt.declare(pl.game, pl.flags);
  }
  sktSkatOn = false;
  char gn[24];
  sktGameName(gn, sizeof(gn), skt.game, skt.flags);
  snprintf(sktMsg, sizeof(sktMsg), "%s: %s", SKT_NAME[d], gn);
  sfx(1100, 60);
  if (!sktWait(d ? 1600 : 700)) return -1;
  if (!sktPlayTricks()) return -1;
  SkResult r = skt.result();
  sktTotal[d] += r.score;
  sktList[sktRows].decl = d; sktList[sktRows].score = r.score;
  sktRows++;
  if (!sktShowResult(r)) return -1;
  return sktShowList(false) ? 1 : -1;
}

void skatRun() {
  static const char *const MODES[3] = { "Liste: 12 games", "Liste: 24 games", "Liste: 36 games" };
  bool again = true;
  while (again) {
    sktPhase = SKP_NONE;
    uint8_t mode = chooseMode("SKAT", MODES, 3, SKT_ACC);
    if (mode == 255) return;
    sktGames = 12 * (mode + 1);
    sktRows = 0;
    memset(sktTotal, 0, sizeof(sktTotal));
    memset(&skt, 0, sizeof(skt));
    uint8_t dealer = 2;                                    // you are Vorhand first
    uint32_t seed = millis() * 2654435761u;
    while (sktRows < sktGames) {
      if (sktOneGame(dealer, seed) < 0) { sktPhase = SKP_NONE; return; }
      seed = seed * 1664525u + 1013904223u + millis();
      dealer = (dealer + 1) % 3;
    }
    if (!sktShowList(true)) return;
    sktPhase = SKP_NONE;
    again = gameOver(sktTotal[0] > 0 ? (uint16_t)sktTotal[0] : 0);
  }
}

static const char *const IC_SKAT[12] = {
  "sssssss.....", "swwwwws.....", "swwkwws.....", "swkkksssssss", "skkkkswwwwws", "swwkwswrwrws",
  "swwwwsrrrrrs", "swwwwswrrrws", "sssssswwrwws", ".....swwwwws", ".....swwwwws", ".....sssssss" };
