// Tests for the games of 10.0: bots that play them through the real UI,
// and logic checks (puzzles have one solution, golf holes can be played,
// ...). Included by run.cpp after the sketch and the test helpers.
#pragma once

static uint32_t g2Start = 0;                      // the game is open from here on

// ---------------- key presses from a bot ----------------
/* A bot decides every "gap" ms and holds its key for 40 ms. "held" keys
   (like thrust) stay down across decisions.                           */
static uint32_t bkNext = 0, bkUp = 0;
static uint8_t  bkHeld = 0, bkKey = 0;
static void bkPress(uint32_t t, uint8_t key, uint32_t gap) {
  bkKey = key; applyMask(bkHeld | key); bkUp = t + 40; bkNext = t + gap;
}
static bool bkBusy(uint32_t t) {                  // true while a press runs or it is not time yet
  if (bkUp && t >= bkUp) { applyMask(bkHeld); bkUp = 0; bkKey = 0; }
  return bkUp || t < bkNext;
}

// ---------------- Sudoku ----------------
static int suBotWrong = 2;                        // the bot makes this many mistakes first
static void sudokuBot(uint32_t t) {
  if (t < g2Start || bkBusy(t)) return;
  if (!suPicking) {
    int target = -1;
    for (int i = 0; i < 81 && target < 0; i++) if (!suGrid[i]) target = i;   // the first empty field
    if (target < 0) return;
    if (target == suCur) bkPress(t, 16, 90);
    else if (target / 9 != suCur / 9) bkPress(t, 2, 90);
    else bkPress(t, 8, 90);
  } else {
    uint8_t want = suBotWrong > 0 ? suSol[suCur] % 9 + 1 : suSol[suCur];
    if (suPick != want) bkPress(t, 1, 90);
    else { bkPress(t, 16, 90); if (suBotWrong > 0) suBotWrong--; }
  }
}

// ---------------- Lights Out ----------------
// light chasing: try all 32 first rows, the rest follows; the shortest solution
static int loSolve(uint32_t b, uint8_t *out) {
  int bestN = 99;
  for (int m = 0; m < 32; m++) {
    uint32_t x = b;
    uint8_t pr[25]; int n = 0;
    for (int c = 0; c < 5; c++) if (m & (1 << c)) { x = loPress(x, 0, c); pr[n++] = c; }
    for (int r = 1; r < 5; r++) for (int c = 0; c < 5; c++)
      if (x & (1UL << ((r - 1) * 5 + c))) { x = loPress(x, r, c); pr[n++] = r * 5 + c; }
    if (!x && n < bestN) { bestN = n; memcpy(out, pr, n); }
  }
  return bestN == 99 ? -1 : bestN;
}
static uint32_t loBotStop = 0;
static void lightsBot(uint32_t t) {
  if (t < g2Start || bkBusy(t)) return;
  if (t > loBotStop) { static const uint8_t K[5] = { 1, 2, 4, 8, 16 }; bkPress(t, K[rand() % 5], 120); return; }
  uint8_t pr[25];
  int n = loSolve(loBoard, pr);
  if (n <= 0) return;
  int r = pr[0] / 5, c = pr[0] % 5;
  if (r != loR) bkPress(t, r > loR ? 2 : 1, 80);
  else if (c != loC) bkPress(t, c > loC ? 8 : 4, 80);
  else bkPress(t, 16, 80);
}

// ---------------- Match 3 ----------------
static void match3Bot(uint32_t t) {
  if (t < g2Start || bkBusy(t)) return;
  bool mark[M3_H][M3_W]; uint8_t best;
  for (int r = 0; r < M3_H; r++) for (int c = 0; c < M3_W; c++)
    for (int d = 0; d < 2; d++) {
      int r2 = r + d, c2 = c + 1 - d;
      if (r2 >= M3_H || c2 >= M3_W) continue;
      m3Swap(r, c, r2, c2);
      bool ok = m3Find(mark, &best) > 0;
      m3Swap(r, c, r2, c2);
      if (!ok) continue;
      if (m3Held) { bkPress(t, d ? 2 : 8, 150); return; }              // swap it
      if (r != m3R) bkPress(t, r > m3R ? 2 : 1, 70);
      else if (c != m3C) bkPress(t, c > m3C ? 8 : 4, 70);
      else bkPress(t, 16, 70);
      return;
    }
}

// ---------------- Stack ----------------
static int stkBotFloors = 0;
static void stackBot(uint32_t t) {
  if (t < g2Start || bkBusy(t)) return;
  int d = (stkX >> 2) - stkTop.x;
  if (stkBotFloors < 18 ? (d >= -1 && d <= 1) : rand() % 400 == 0) { bkPress(t, 2, 200); stkBotFloors++; }
}

// ---------------- Cave ----------------
static uint32_t cvBotStop = 0;
static int16_t  cvPrev = 0, cvV = 0;
static void caveBot(uint32_t t) {
  if (t < g2Start) return;
  if (t > cvBotStop) { applyMask(0); return; }
  if (t % 25 == 0) { cvV = cvY - cvPrev; cvPrev = cvY; }        // speed, Q4 per tick
  int top = 0, bot = 99;                                           // the tightest walls just ahead
  for (int k = CV_X; k < CV_X + 14; k++) { if (cvTop[k] > top) top = cvTop[k]; if (cvBot[k] < bot) bot = cvBot[k]; }
  int target = (top + bot) / 2 - 2;
  for (int k = CV_X; k < CV_X + 26 && k < SCR_W; k++)             // a rock ahead: pass on the wider side
    if (cvRock[k]) {
      int rt = cvRock[k], above = rt - top, below = bot - (rt + CV_ROCK);
      bool up = above > below;
      if (k < CV_X + 10) up = (cvY >> 4) + 2 < rt;                 // too close to change sides
      target = up ? (top + rt) / 2 - 2 : (rt + CV_ROCK + bot) / 2 - 2;
      break;
    }
  int predicted = (cvY + cvV * 4) >> 4;                            // where it will be in 4 ticks
  applyMask(predicted > target ? 1 : 0);
}

// ---------------- Minigolf ----------------
struct GolfShot { uint8_t ang, pow; };
// where one shot ends: 1 hole, 2 water, 0 rest at *x, *y
static uint8_t golfShoot(int32_t *x, int32_t *y, uint8_t ang, uint8_t pow) {
  int32_t vx = mgCos(ang) * (130 * pow) / 256, vy = mgSin(ang) * (130 * pow) / 256;
  for (int s = 0; s < 3000 && (vx || vy); s++) {
    uint8_t r = mgStep(x, y, &vx, &vy, mgHx, mgHy);
    if (r) return r;
  }
  return 0;
}
/* Breadth first over resting places: the fewest strokes into the hole
   from (x, y), and the first shot of such a way. -1 = no way.         */
static int golfPlan(int32_t x, int32_t y, GolfShot *first) {
  struct Node { int32_t x, y; int depth; GolfShot first; };
  std::vector<Node> q;
  std::set<std::pair<int,int>> seen;
  q.push_back({ x, y, 0, { 0, 0 } });
  seen.insert({ (int)(x >> 8), (int)(y >> 8) });
  for (size_t h = 0; h < q.size(); h++) {
    Node n = q[h];
    if (n.depth >= 5) break;
    for (uint8_t a = 0; a < 32; a++)
      for (uint8_t p = 1; p <= 10; p++) {
        int32_t ex = n.x, ey = n.y;
        uint8_t r = golfShoot(&ex, &ey, a, p);
        GolfShot f = n.depth ? n.first : GolfShot{ a, p };
        if (r == 1) { *first = f; return n.depth + 1; }
        if (r == 2) continue;
        if (seen.insert({ (int)(ex >> 8), (int)(ey >> 8) }).second) q.push_back({ ex, ey, n.depth + 1, f });
      }
  }
  return -1;
}
static GolfShot golfNext = { 255, 0 };
static uint8_t  golfFor = 255;
static int32_t  golfAtX = -1, golfAtY = -1;
static void golfBot(uint32_t t) {
  if (t < g2Start || bkBusy(t)) return;
  if (mgVx || mgVy) return;
  if (golfFor != mgHole || golfAtX != mgBx || golfAtY != mgBy) {   // a new resting place: plan
    golfFor = mgHole; golfAtX = mgBx; golfAtY = mgBy;
    if (golfPlan(mgBx, mgBy, &golfNext) < 0) golfNext = { (uint8_t)(rand() % 32), 5 };
  }
  if (mgAng != golfNext.ang) bkPress(t, ((golfNext.ang - mgAng) & 31) < 16 ? 8 : 4, 60);
  else if (mgPow != golfNext.pow) bkPress(t, golfNext.pow > mgPow ? 1 : 2, 60);
  else { bkPress(t, 16, 400); golfFor = 255; }
}

// ---------------- Lander ----------------
static void landerBot(uint32_t t) {
  if (t < g2Start) return;
  if (t % 40) { if (t % 40 == 20) applyMask(bkHeld); return; }
  int px = ldX >> 4;
  int target = -1, len = 0;                               // the wide pad
  for (int i = 0; i + 1 < LD_PTS; i++) if (ldPad[i] == 2) { target = i * 8 + (16 - 5) / 2; len = 16; }
  if (target < 0) for (int i = 0; i + 1 < LD_PTS; i++) if (ldPad[i]) { target = i * 8 + 1; len = 8; }
  (void)len;
  int dx = target - px;
  int height = ldHeight(px + 3) - ((ldY >> 4) + 7);
  for (int k = 0; k < 7; k++) { int h = ldHeight(px + k) - ((ldY >> 4) + 7); if (h < height) height = h; }
  int wantVx = dx > 3 ? (dx > 20 ? 12 : 5) : (dx < -3 ? (dx < -20 ? -12 : -5) : 0);
  int err = wantVx - ldVx;
  uint8_t want = err > 3 ? 2 : (err < -3 ? 14 : 0);
  if (height < 14 && (dx > -3 && dx < 3)) want = 0;
  bool thrust = ldVy > (height > 25 ? 9 : 4) || (want != 0 && ldVy > 0) || (height < 12 && (dx < -3 || dx > 3));
  if (t > g2Start + 60000) thrust = false;                        // then let it fall
  bkHeld = thrust ? 1 : 0;
  uint8_t rot = 0;
  if (ldA != want) rot = ((want - ldA) & 15) < 8 ? 8 : 4;
  applyMask(bkHeld | rot);
}

// ---------------- Checkers (the console's own player, offline and online) ----------------
static uint8_t ckToScreen(uint8_t i, uint8_t me) {             // board square -> screen square
  int8_t c, r = Checkers::rc(i, &c);
  if (me) { r = 7 - r; c = 7 - c; }
  return r * 8 + c;
}
static int ckBotDepth = 3;
static void ckBotMove(const Checkers &b, uint8_t me, uint32_t t) {
  if (b.winner || b.turn != me || bkBusy(t)) return;
  uint32_t r = 12345 + b.quiet * 7 + b.count(0) * 131;          // a fixed choice for a given board
  CkStep st = b.best(ckBotDepth, r);
  int8_t want = (ckSel == st.from) ? Checkers::next(st.from, st.dir, st.jump ? 2 : 1) : st.from;
  uint8_t target = ckToScreen(want, me);
  int tr = target / 8, tc = target % 8, cr = ckCur / 8, cc = ckCur % 8;
  if (tr != cr) bkPress(t, tr > cr ? 2 : 1, 70);
  else if (tc != cc) bkPress(t, tc > cc ? 8 : 4, 70);
  else bkPress(t, 16, 150);
}
static void checkersBot(uint32_t t) { if (t >= g2Start) ckBotMove(ck, 0, t); }

// ---------------- Mau-Mau ----------------
static uint8_t mmBotWish = 0;
static uint32_t mmBotAgain = 0;
static void mmBotMove(const MauMau &g, uint8_t me, uint32_t t) {
  if (bkBusy(t)) return;
  if (mmWishing) { bkPress(t, mmWishSel == mmBotWish ? 16 : 8, 90); return; }
  if (g.winner || g.turn != me) return;
  MauMau c = g;
  uint8_t m = c.cpuMove(), slot = 0;
  if (m != MM_DRAW && m != MM_PASS) {
    uint8_t cards[MM_CARDS], n = mmHand(g, me, cards);
    for (uint8_t i = 0; i < n; i++) if (cards[i] == (m & 31)) slot = i + 1;
    mmBotWish = (m >> 5) & 3;
  }
  if (mmCur != slot) bkPress(t, slot > mmCur ? 8 : 4, 70);
  else bkPress(t, 16, 150);
}
static void maumauBot(uint32_t t) {
  if (t < g2Start) return;
  if (mm.winner) {                                               // next game: OK through "again" and the mode
    if (!bkBusy(t) && t > mmBotAgain) { bkPress(t, 16, 100); mmBotAgain = t + 1200; }
    return;
  }
  mmBotMove(mm, 0, t);
}

// online: our console plays through the same UI
static void mpBoardBot(uint32_t t) {
  if (t < g2Start || linkCore().state != LK_PLAYING) return;
  if (linkCore().game == LKG_DAME) ckBotMove(mpCk, mpCkMe, t);
  else mmBotMove(mpMm, mpMmMe, t);
}

// ---------------- Bomberman: our player is steered by the game's own CPU player ----------------
static void rbKeys(RtBomb &g, uint8_t me, uint32_t t) {
  if (t % 20) { if (t % 20 == 10) applyMask(bkHeld); return; }  // a bomb press lasts 10 ms
  uint8_t in = g.ai(me), d = in & 7;
  static const uint8_t K[5] = { 0, 1, 2, 4, 8 };
  bkHeld = d <= 4 ? K[d] : 0;
  applyMask(bkHeld | ((in & 8) ? 16 : 0));
}
static void bomberBot(uint32_t t) { if (t >= g2Start && !rb.winner && rb.alive[0]) rbKeys(rb, 0, t); else if (t >= g2Start) applyMask(0); }
static void mpBombBot(uint32_t t) {
  if (t < g2Start) return;
  if (linkCore().state != LK_PLAYING || mpRb.winner) { applyMask(0); return; }
  rbKeys(mpRb, mpMe, t);
}

// ---------------- scenarios ----------------
static bool games2Setup() {
  auto open = [](int downsN, uint32_t extra = 0) {              // down the library, OK
    downs(downsN, 1300, 180);
    uint32_t okAt = 1300 + downsN * 180 + 200;
    script.push_back({ okAt, 16 }); script.push_back({ okAt + 60, 0 });
    if (extra) { script.push_back({ okAt + 500, 16 }); script.push_back({ okAt + 560, 0 }); }   // first choice
    g2Start = okAt + (extra ? 1100 : 600);
  };
  if (scenario == "sudoku")    { simEnd = 90000;  open(22, 1); simTimeHook = sudokuBot; captureAt = { 1500, 3000 }; return true; }
  if (scenario == "lightsout") { simEnd = 70000;  loBotStop = 40000; open(23); simTimeHook = lightsBot; captureAt = { 1500 }; return true; }
  if (scenario == "match3")    { simEnd = 90000;  open(24); simTimeHook = match3Bot; captureAt = { 1500, 2500 }; return true; }
  if (scenario == "cave")      { simEnd = 60000;  cvBotStop = 40000; open(28); simTimeHook = caveBot; captureAt = { 1500 }; return true; }
  if (scenario == "stack")     { simEnd = 90000;  open(29); simTimeHook = stackBot; captureAt = { 1500, 2500 }; return true; }
  if (scenario == "minigolf")  { simEnd = 240000; open(30); simTimeHook = golfBot; captureAt = { 1500, 4000 }; return true; }
  if (scenario == "checkers")  { simEnd = 240000; open(25, 1); simTimeHook = checkersBot; captureAt = { 1500, 3000 }; return true; }
  if (scenario == "maumau")    { simEnd = 300000; open(26, 1); simTimeHook = maumauBot; captureAt = { 1500, 3000 }; return true; }
  if (scenario == "mpdame" || scenario == "mpmau") {           // against ANNA next door
    bool dame = scenario == "mpdame";
    simEnd = dame ? 300000 : 200000;
    ups(3, 1300, 180);
    script.push_back({ 4300, 16 }); script.push_back({ 4360, 0 });     // open multiplayer
    script.push_back({ 6500, 2 });  script.push_back({ 6560, 0 });     // ANNA's row
    script.push_back({ 6900, 16 }); script.push_back({ 6960, 0 });     // challenge ...
    for (int i = 0; i < (dame ? 6 : 7); i++) { script.push_back({ 7200 + i * 230u, 2 }); script.push_back({ 7260 + i * 230u, 0 }); }
    script.push_back({ 9000, 16 }); script.push_back({ 9060, 0 });     // ... to checkers / mau-mau
    g2Start = 10000;
    simTimeHook = mpBoardBot;
    captureAt = { 2200, 2600 };
    return true;
  }
  if (scenario == "bomber")    { simEnd = 240000; open(27, 1); simTimeHook = bomberBot; captureAt = { 1500, 3000 }; return true; }
  if (scenario == "mpbomb") {
    simEnd = 240000;
    ups(3, 1300, 180);
    script.push_back({ 4300, 16 }); script.push_back({ 4360, 0 });     // open multiplayer
    script.push_back({ 6500, 2 });  script.push_back({ 6560, 0 });     // ANNA's row
    script.push_back({ 6900, 16 }); script.push_back({ 6960, 0 });     // challenge ...
    for (int i = 0; i < 8; i++) { script.push_back({ 7200 + i * 230u, 2 }); script.push_back({ 7260 + i * 230u, 0 }); }
    script.push_back({ 9200, 16 }); script.push_back({ 9260, 0 });     // ... to bomberman
    g2Start = 10000;
    simTimeHook = mpBombBot;
    captureAt = { 2200, 2600 };
    return true;
  }
  if (scenario == "stats2") {                                  // 9.11 stored 32 award bits: kept
    simEnd = 9000; ups(2, 1300, 200);
    StatBlob2 b; memset(&b, 0, sizeof(b)); b.ver = 2;
    b.plays[0] = 3; b.secs[0] = 3725; b.plays[1] = 7; b.awards = (1u << 0) | (1u << 29);
    simNvsBlob["stats"] = std::string((const char *)&b, sizeof(b));
    script.push_back({ 5000, 16 }); script.push_back({ 5060, 0 });     // open stats
    script.push_back({ 6500, 8 });  script.push_back({ 6560, 0 });     // awards page
    return true;
  }
  if (scenario == "lander")    { simEnd = 120000; open(31); simTimeHook = landerBot; captureAt = { 1500 }; return true; }
  return false;
}

static bool games2Check() {
  auto hs = [](int g, const char *name) {
    printf("      best %u, stored hs%d %u\n", maxScore("SCORE "), g, simNvsU16["hs" + std::to_string(g)]);
    check((std::string(name) + ": high score consistent").c_str(), simNvsU16["hs" + std::to_string(g)] == maxScore("SCORE ") && maxScore("SCORE ") > 0);
  };
  if (scenario == "sudoku") {
    check("sudoku drawn with the picker", saw("SUDOKU") && saw("OK = write") && saw("< "));
    check("mistakes counted, puzzle solved", saw("X") && saw("SOLVED"));
    hs(22, "Sudoku");
  } else if (scenario == "lightsout") {
    check("levels solved", saw("DONE!") && saw("LEVEL 4"));
    check("the move limit ends it", saw("LIMIT") && (saw("GAME OVER") || saw("NEW RECORD!")));
    hs(23, "Lights Out");
  } else if (scenario == "match3") {
    check("board and moves shown", saw("MATCH 3") && saw("MOVES"));
    check("chains happen", saw("x2"));
    check("game ends when the moves are used up", saw("END") && (saw("GAME OVER") || saw("NEW RECORD!")));
    hs(24, "Match 3");
  } else if (scenario == "cave") {
    check("cave flown and crashed", saw("CAVE") && saw("hold UP to fly") && (saw("GAME OVER") || saw("NEW RECORD!")));
    check("the bot got far (score 50+)", maxScore("SCORE ") >= 50);
    hs(28, "Cave");
  } else if (scenario == "stack") {
    check("tower built", saw("STACK") && maxScore("SCORE ") >= 18);
    check("a miss ends it", saw("GAME OVER") || saw("NEW RECORD!"));
    hs(29, "Stack");
  } else if (scenario == "minigolf") {
    check("all 9 holes played", saw("HOLE 9/9"));
    check("holed out", saw("PAR +") || saw("BIRDIE! +") || saw("HOLE IN ONE! +"));
    check("round ends", saw("GAME OVER") || saw("NEW RECORD!"));
    hs(30, "Minigolf");
  } else if (scenario == "checkers") {
    check("board, CPU and turns shown", saw("CHECKERS") && saw("your go") && saw("thinking..."));
    check("the game was won against the CPU", saw("YOU WIN"));
    hs(25, "Checkers");
  } else if (scenario == "maumau") {
    check("table shown, CPU plays", saw("MAU-MAU") && saw("CPU1 "));
    check("a round was won", saw("MAU-MAU!"));
    hs(26, "Mau-Mau");
  } else if (scenario == "mpdame" || scenario == "mpmau") {
    bool dame = scenario == "mpdame";
    check("challenge list offers the new game", dame ? saw("Checkers") : saw("Mau-Mau"));
    check("game against ANNA shown", dame ? saw("vs ANNA") : saw("ANNA"));
    bool same = (simBotResult == 1 && saw("LOST")) || (simBotResult == 2 && saw("YOU WIN")) || (simBotResult == 3 && saw("DRAW"));
    printf("      bot games %d, bot result %d\n", simBotGames, simBotResult);
    check("both consoles saw the same end", simBotGames >= 1 && same);
  } else if (scenario == "bomber") {
    check("field shown", saw("BOMBER") && saw("CPU1") && saw("B1 F2"));
    check("a round ended", saw("YOU WIN") || saw("CAUGHT") || saw("TIME UP"));
    hs(27, "Bomberman");
  } else if (scenario == "mpbomb") {
    check("challenge list offers Bomberman", saw("Bomberman"));
    check("field against ANNA shown", saw("ANNA") && saw("B1 F2"));
    bool same = (simBotResult == 1 && saw("LOST")) || (simBotResult == 2 && saw("YOU WIN")) || (simBotResult == 3 && saw("DRAW"));
    printf("      bot games %d, bot result %d\n", simBotGames, simBotResult);
    check("both consoles saw the same end", simBotGames >= 1 && same);
  } else if (scenario == "stats2") {
    check("awards of 9.11 kept, 40 in all", saw("2/40  <STATS"));
    check("play counts kept", saw("Snake         7x"));
  } else if (scenario == "lander") {
    check("landed at least once", saw("LANDED!"));
    check("crash ends it", saw("CRASH") && (saw("GAME OVER") || saw("NEW RECORD!")));
    hs(31, "Lander");
  } else return false;
  return true;
}

// ---------------- logic without the UI ----------------
static bool suValid(const uint8_t *g) {
  for (int k = 0; k < 9; k++) {
    int r = 0, c = 0, b = 0;
    for (int i = 0; i < 9; i++) {
      r |= 1 << g[k * 9 + i];
      c |= 1 << g[i * 9 + k];
      b |= 1 << g[(k / 3 * 3 + i / 3) * 9 + k % 3 * 3 + i % 3];
    }
    if (r != 0x3FE || c != 0x3FE || b != 0x3FE) return false;
  }
  return true;
}

static bool games2Logic() {
  if (scenario != "g2logic10") return false;
  fails = 0;
  // Sudoku: valid, consistent, unique, about as many givens as asked
  bool ok = true; int given[3] = { 0, 0, 0 };
  static const uint8_t KEEP[3] = { 40, 32, 26 };
  for (int m = 0; m < 3; m++)
    for (int k = 0; k < 15; k++) {
      int n = suMake(KEEP[m]);
      given[m] += n;
      uint8_t tmp[81]; memcpy(tmp, suGrid, 81);
      if (!suValid(suSol) || suSolve(tmp, 2, false) != 1) ok = false;
      for (int i = 0; i < 81; i++) if (suGrid[i] && suGrid[i] != suSol[i]) ok = false;
    }
  printf("      sudoku givens: easy %d, medium %d, hard %d (average)\n", given[0] / 15, given[1] / 15, given[2] / 15);
  check("sudoku: 45 puzzles valid with exactly one solution", ok);
  check("sudoku: harder levels give fewer numbers", given[0] > given[1] && given[1] > given[2] && given[2] / 15 <= 32);
  // Lights Out: every level solvable
  ok = true;
  for (int par = 3; par <= 15; par++) for (int k = 0; k < 20; k++) { uint8_t pr[25]; uint32_t b = loMake(par); if (!b || loSolve(b, pr) < 0) ok = false; }
  check("lights out: 260 levels, all solvable, none empty", ok);
  // Match 3: fresh boards have no rows and a move
  ok = true;
  for (int k = 0; k < 200; k++) { m3Fill(); bool mark[M3_H][M3_W]; uint8_t best; if (m3Find(mark, &best) || !m3CanMove()) ok = false; }
  check("match 3: 200 new boards without rows, with a move", ok);
  // Stack: cutting
  StkRow a = stkCut(10, 20, 15, 20), b = stkCut(40, 10, 0, 20), c = stkCut(5, 10, 5, 10);
  check("stack: overhang is cut, a miss leaves nothing", a.x == 15 && a.w == 15 && b.w == 0 && c.x == 5 && c.w == 10);
  // Lander: pads flat and reachable
  ok = true;
  for (int lv = 0; lv < 8; lv++) for (int k = 0; k < 20; k++) {
    ldTerrain(lv);
    int pads = 0;
    for (int i = 0; i + 1 < LD_PTS; i++) if (ldPad[i]) {
      pads++;
      int len = ldPad[i] == 2 ? 16 : 8;
      for (int x = i * 8; x <= i * 8 + len; x++) if (ldHeight(x) != ldGround[i] && x < (LD_PTS - 1) * 8) ok = false;
      if (!ldPadAt(i * 8)) ok = false;
    }
    if (pads < 1) ok = false;
  }
  check("lander: every ground has a flat pad", ok);
  // Minigolf: every hole can be played in par
  ok = true;
  for (mgHole = 0; mgHole < 9; mgHole++) {
    int32_t x, y; GolfShot f;
    mgFind('S', &x, &y); mgFind('O', &mgHx, &mgHy);
    int n = golfPlan(x, y, &f);
    printf("      hole %d: par %d, best %d\n", mgHole + 1, MG_PAR[mgHole], n);
    if (n < 0 || n > MG_PAR[mgHole]) ok = false;
  }
  check("minigolf: every hole can be played in par", ok);
  // Bomberman: two copies with the same inputs stay the same; CPU games end
  {
    RtBomb a, b; a.begin(777, 4); b.begin(777, 4);
    bool same = true; int ended = 0, winners = 0, drawn = 0; long ticks = 0;
    for (int g = 0; g < 30; g++) {
      RtBomb x; x.begin(1000 + g * 7919, 4);
      while (!x.winner) { uint8_t in[4]; for (int p = 0; p < 4; p++) in[p] = x.ai(p); x.step(in); }
      ended++; ticks += x.ticks; if (x.winner == RB_DRAW) drawn++; else winners++;
    }
    for (int k = 0; k < 3000 && !a.winner; k++) {
      uint8_t in[4]; for (int p = 0; p < 4; p++) in[p] = a.ai(p);
      a.step(in); b.step(in);
      if (memcmp(a.tile, b.tile, sizeof(a.tile)) || memcmp(a.px, b.px, 4) || memcmp(a.alive, b.alive, 4)) same = false;
    }
    printf("      bomberman: 30 CPU games, %d with a winner, %d draws, %ld s on average\n", winners, drawn, ticks / 30 / 50);
    check("bomberman: same inputs, same game", same);
    check("bomberman: CPU games end, mostly with a winner", ended == 30 && winners >= 20);
    RtBomb f; f.begin(5, 4);
    bool free_ = true;
    static const uint8_t SX[4] = { 1, RB_W - 2, RB_W - 2, 1 }, SY[4] = { 1, RB_H - 2, 1, RB_H - 2 };
    for (int p = 0; p < 4; p++) {
      int opens = 0;
      for (int d = 1; d <= 4; d++) if (f.open(SX[p] + RtBomb::ddx(d), SY[p] + RtBomb::ddy(d))) opens++;
      if (!f.open(SX[p], SY[p]) || opens < 2) free_ = false;
    }
    check("bomberman: every start has room to move", free_);
  }
  // Checkers: forced take, going on with a second jump, crowning
  {
    Checkers c; c.begin();
    memset(c.sq, 0, sizeof(c.sq));
    c.sq[Checkers::at(5, 2)] = CK_MAN0;                    // a man that can take twice
    c.sq[Checkers::at(4, 3)] = CK_MAN1;
    c.sq[Checkers::at(2, 5)] = CK_MAN1;
    c.sq[Checkers::at(6, 7)] = CK_MAN0;                    // one that could just walk
    c.sq[Checkers::at(0, 1)] = CK_MAN1;                    // keeps player 1 in the game
    CkStep st[48]; uint8_t n = c.gen(st);
    check("checkers: taking is a must", n == 1 && st[0].jump && st[0].from == Checkers::at(5, 2));
    bool more = c.apply(st[0].from, st[0].dir);
    check("checkers: a piece that took goes on taking", more && c.chain == Checkers::at(3, 4) && c.turn == 0);
    more = c.apply(c.chain, 1);
    check("checkers: both taken, the turn passes", !more && c.count(1) == 1 && c.turn == 1);
    Checkers k; k.begin(); memset(k.sq, 0, sizeof(k.sq));
    k.sq[Checkers::at(1, 2)] = CK_MAN0; k.sq[Checkers::at(7, 0)] = CK_MAN1;
    k.apply(Checkers::at(1, 2), 1);
    check("checkers: the far row crowns", k.sq[Checkers::at(0, 3)] == CK_KING0);
    int deep = 0, games = 10;                              // depth 3 beats a random player
    for (int g = 0; g < games; g++) {
      Checkers x; x.begin(); uint32_t r = 99 + g;
      while (!x.winner) {
        CkStep s2[48]; uint8_t m = x.gen(s2);
        CkStep s = x.turn == 0 ? x.best(3, r) : s2[bgRand(r) % m];
        x.apply(s.from, s.dir);
      }
      if (x.winner == 1) deep++;
    }
    printf("      checkers: depth 3 won %d of %d against a random player\n", deep, games);
    check("checkers: the CPU plays well", deep >= 9);
  }
  // Mau-Mau: no card gets lost, games end, same seed = same game
  {
    bool keep = true; int ended = 0;
    for (int g = 0; g < 200; g++) {
      MauMau m; m.begin(4242 + g * 31, 2 + g % 3);
      for (int k = 0; k < 2000 && !m.winner; k++) {
        m.move(m.cpuMove());
        uint32_t all = 0; int cnt = 0;
        for (int p = 0; p < m.players; p++) { all |= m.hand[p]; cnt += MauMau::count(m.hand[p]); }
        cnt += m.nDeck + m.nPile;
        if (cnt != MM_CARDS) keep = false;
      }
      if (m.winner) ended++;
    }
    check("mau-mau: 32 cards all the time", keep);
    check("mau-mau: 200 CPU games all end", ended == 200);
    MauMau a, b; a.begin(99, 2); b.begin(99, 2);
    check("mau-mau: the same seed deals the same", !memcmp(a.hand, b.hand, sizeof(a.hand)) && a.top() == b.top());
    MauMau s; s.begin(1, 2);
    s.pile[s.nPile - 1] = 0;                                // top: 7 of clubs
    s.hand[0] = (1UL << 8) | (1UL << 3);                   // 7 of spades, 10 of clubs
    s.move(8);                                             // 7 on 7
    check("mau-mau: a 7 makes the next one draw 2", s.penalty == 2 && s.turn == 1);
    uint8_t before = MauMau::count(s.hand[1]);
    s.move(MM_DRAW);
    check("mau-mau: ... and ends that turn", MauMau::count(s.hand[1]) == before + 2 && s.turn == 0 && !s.penalty);
  }
  printf("%s\n", fails ? "### FAILURES ###" : "all checks passed");
  return true;
}
