// Host tests of Kart GP (main/game_kart.h): a driver on the keys or the
// tilt sensor, the rules without a screen, every track driven, the frame
// rate model, and ANNA's kart for the races of two. Included by sim.cpp.

// ---------------- the frame rate model of the ESP32-C3 ----------------
/* What a frame costs on the C3 at 160 MHz: the drawing as counted by the
   game (KG_COST, about the cycles of its loops), plus the frame's fixed
   work (the HUD, effects, physics: 240k cycles; send()'s block hashes:
   100k), plus the SPI transfer of the 32-px blocks that changed - the
   same windows Gfx::send() opens, 16 bits a pixel, a few microseconds a
   window and a DMA chunk. While a kart scenario runs, the simulated clock
   moves on by the 40 MHz cost of every frame, so the game runs at the
   frame rate it would have.                                         */
static bool     kcOn = false;                  // a kart scenario: the model runs
static uint32_t kcMhz = 40;                    // the clock the simulated time follows
static double   kcCpu = 0, kcSpi20 = 0, kcSpi40 = 0, kcWorst40 = 0;
static long     kcN = 0;

static double kcSpiUs(const uint8_t *fb, const uint8_t *prev, double mhz, long *pxOut = NULL) {
  double us = 0;
  long px = 0;
  auto mask = [&](int y) {
    uint16_t m = 0;
    for (int b = 0; b < SCR_W / 32; b++) if (memcmp(fb + y * SCR_W + b * 32, prev + y * SCR_W + b * 32, 32)) m |= (uint16_t)(1 << b);
    return m;
  };
  int y = 0;
  while (y < SCR_H) {
    uint16_t m = mask(y);
    if (!m) { y++; continue; }
    int y0 = y;
    uint16_t span = m;
    for (y++; y < SCR_H; y++) { m = mask(y); if (!m) break; span |= m; }
    int b0 = 0, b1 = 9;
    while (!(span >> b0 & 1)) b0++;
    while (!(span >> b1 & 1)) b1--;
    int w = (b1 - b0 + 1) * 32, rows = y - y0, per = 3840 / w;
    px += (long)w * rows;
    us += 25 + (double)w * rows * 16 / mhz + ((rows + per - 1) / per) * 6;
  }
  if (pxOut) *pxOut = px;
  return us;
}
static double kcCpuUs(uint32_t cost) { return (cost + 240000 + 100000) / 160.0; }

static void kcWrite(const char *name, const uint8_t *px) {      // frames/<scenario>_<name>.ppm
  char fn[128];
  snprintf(fn, sizeof(fn), "frames/%s_%s.ppm", scenario.c_str(), name);
  FILE *f = fopen(fn, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
  for (int i = 0; i < SCR_W * SCR_H; i++) { uint8_t v[3]; colToRgb(px[i], v[0], v[1], v[2]); fwrite(v, 1, 3, f); }
  fclose(f);
}
static std::set<std::string> kcShots;
static void kcSnap(const char *name, const uint8_t *fb) { if (kcShots.insert(name).second) kcWrite(name, fb); }

static bool kbTilt = false;                    // the key driver steers by tilting (below)

/* What the races of a scenario did (added up when a new race begins),
   and named pictures of the moments worth seeing.                   */
static KgStats  kcSt, kcLastSt;
static uint32_t kcLastT = 0;
static int      kcRaces = 0, kcAllDone = 0, kcGhostSeen = 0, kcRemoteSeen = 0, kcCountFrames = 0;
static double   kcErrSum = 0, kcErrMax = 0;                    // online: our picture of ANNA's kart against hers (px)
static long     kcErrN = 0;
struct KcPeer { bool on; uint16_t sid; KgTrack T; KgRace R; KgDuel D; uint32_t last, acc; bool started, done; uint32_t doneAt; };
static KcPeer  *kcp = NULL;
static bool     kcInRace = false;
static void kcAdd(const KgStats &s) {
  kcSt.boxes += s.boxes; kcSt.coins += s.coins; kcSt.rocketHits += s.rocketHits; kcSt.peelHits += s.peelHits; kcSt.blocked += s.blocked;
  kcSt.falls += s.falls; kcSt.minis += s.minis; kcSt.drifts += s.drifts; kcSt.walls += s.walls; kcSt.bumps += s.bumps;
  for (int i = 0; i < KI_N; i++) kcSt.used[i] += s.used[i];
}
static void kcLook(const uint8_t *fb, bool drawn) {
  KgRace &R = kg->R;
  if (kg->mode == KG_ATTRACT && frames > 150) kcSnap("title", fb);
  if (kg->R.T != &kg->T || kg->mode == KG_ATTRACT || !drawn) {
    static uint32_t resAt = 0;
    if (recent("RESULTS")) { if (!resAt) resAt = millis(); if (millis() - resAt > 1600) kcSnap("results", fb); }
    if (recent("GOLD TROPHY!") || recent("SILVER TROPHY!") || recent("BRONZE TROPHY!") || recent("NO TROPHY THIS TIME")) {
      static uint32_t podAt = 0;
      if (!podAt) podAt = millis();
      if (millis() - podAt > 1300) kcSnap("podium", fb);
    }
    if (recent("CHOOSE YOUR DRIVER")) kcSnap("driver", fb);
    if (recent("SUN CUP") && recent("STAR CUP")) kcSnap("cup", fb);
    if (recent("MEADOW RING") && recent("CORAL BAY") && recent("best --")) kcSnap("tracks", fb);
    return;
  }
  const KgKart &me = R.k[R.me];
  if (R.phase == KS_COUNT && ++kcCountFrames == 110) kcSnap("start", fb);
  if (R.phase == KS_RACE && (R.t > 12000 || kg->mode == KG_NET)) {
    int near = 0;
    for (uint8_t i = 0; i < R.nk; i++) {
      if (i == R.me) continue;
      int16_t sx, sy; int32_t z;
      if (kgProj(kg->cam, R.k[i].x, R.k[i].y, 0, sx, sy, z) && z < 130 * 256 && sx > 20 && sx < 300) near++;
    }
    if (near >= 2 && !me.drift && !me.boost) kcSnap("race", fb);
    if (me.drift && me.charge >= KG_DRIFT2 && !me.hop && near >= 1) kcSnap("drift", fb);
    for (const KgRocket &q : R.rk) {
      int16_t sx, sy; int32_t z;
      if (q.on && q.life < 560 && kgProj(kg->cam, q.x, q.y, 0, sx, sy, z) && z < 110 * 256 && sx > 40 && sx < 280) kcSnap("item", fb);
    }
    if (kg->mode == KG_TT && (R.k[KG_KARTS].flags & KK_GHOST)) {
      int16_t sx, sy; int32_t z;
      const KgKart &g = R.k[KG_KARTS];
      if (kgProj(kg->cam, g.x, g.y, 0, sx, sy, z) && z < 100 * 256 && sx > 40 && sx < 280) { kcGhostSeen++; kcSnap("ghost", fb); }
    }
    if (kg->mode == KG_NET && !(R.k[1].flags & KK_GONE) && kcp && kcp->on && kcp->started) {
      // where we show ANNA's kart, against where her console has it
      int32_t dx = (R.k[1].x - kcp->R.k[0].x) >> 8, dy = (R.k[1].y - kcp->R.k[0].y) >> 8;
      double e = sqrt((double)(dx * dx + dy * dy));
      kcErrSum += e; kcErrN++;
      if (e > kcErrMax) kcErrMax = e;
      int16_t sx, sy; int32_t z;
      if (kgProj(kg->cam, R.k[1].x, R.k[1].y, 0, sx, sy, z) && z < 160 * 256 && sx > 30 && sx < 290) { kcRemoteSeen++; kcSnap("online", fb); }
    }
    if (kbTilt && R.t > 20000 && me.steer > 100) kcSnap("tilt", fb);
  }
  if (R.phase == KS_OVER && R.t > 0) kcSnap("finish", fb);
}

static void kcFrame(const uint8_t *fb, const uint8_t *prev) {
  uint32_t cost = kgCost;
  kgCost = 0;
  bool drawn = kg && kgRaceDrawn;
  kgRaceDrawn = false;
  if (!kcOn) return;
  if (kg) kcLook(fb, drawn);
  else if (recent("GOLD TROPHY!") || recent("SILVER TROPHY!") || recent("BRONZE TROPHY!")) {}
  if (drawn && kg->R.T == &kg->T && kg->mode != KG_ATTRACT && (kg->R.phase == KS_RACE || kg->R.phase == KS_OVER)) {
    KgRace &R = kg->R;
    if (!kcInRace || R.t < kcLastT) { if (kcInRace) kcAdd(kcLastSt); kcRaces++; kcInRace = true; }
    kcLastSt = R.st; kcLastT = R.t;
    uint8_t done = 0;
    for (uint8_t i = 0; i < R.nk; i++) if (R.k[i].finish) done++;
    if (done == R.nk && R.nk == KG_KARTS) kcAllDone = kcRaces;
  }
  if (!drawn || kg->R.T != &kg->T || kg->mode == KG_ATTRACT || (kg->R.phase != KS_RACE && kg->R.phase != KS_OVER)) return;
  double cpu = kcCpuUs(cost), s20 = kcSpiUs(fb, prev, 20), s40 = kcSpiUs(fb, prev, 40);
  kcCpu += cpu; kcSpi20 += s20; kcSpi40 += s40; kcN++;
  if (cpu + s40 > kcWorst40) kcWorst40 = cpu + s40;
  double us = cpu + (kcMhz == 20 ? s20 : s40);
  if (us > 1000) simDelayMs((uint32_t)(us / 1000));               // the frame took this long
}
static void kcReport(const char *what) {
  if (kcInRace) { kcAdd(kcLastSt); kcInRace = false; }
  if (!kcN) return;
  printf("      C3 model, %s: %ld frames, drawing %.1f ms, SPI %.1f ms (20 MHz) / %.1f ms (40 MHz)\n", what, kcN, kcCpu / kcN / 1000,
         kcSpi20 / kcN / 1000, kcSpi40 / kcN / 1000);
  printf("      -> %.1f fps at 20 MHz, %.1f fps at 40 MHz (slowest frame %.1f ms)\n", 1e6 * kcN / (kcCpu + kcSpi20), 1e6 * kcN / (kcCpu + kcSpi40), kcWorst40 / 1000);
}

// ---------------- the driver on the keys ----------------
/* Looks where the CPU drivers would go (kgAutopilot) and turns that into
   LEFT / RIGHT held, DOWN held through the long bends (a drift: let go
   for the mini turbo) and a tap on UP for an item. kbTilt: steers by
   tilting the console instead, as far as the steering wants. Between the
   races it presses OK on the screens that wait for it.               */
static uint32_t kbNext = 0, kbUseAt = 0, kbOkAt = 0, kbTiltSteer = 0, kbKeySteer = 0;
static int      kbMaxTilt = 0;
static bool     kbMenus = true;                // OK on the results, the standings, the podium
static bool     kbQuit = false, kbLeft = false; // back at the title: leave the game (once), then the end
static int      kbTT = 1;                      // time trial runs to start again
static void kartKeyBot(uint32_t t) {
  if (t < kbNext) return;
  kbNext = t + 15;
  if (!kg) { if (kbLeft && (!simEnd || simEnd > t + 1500)) simEnd = t + 1500; return; }
  KgRace &R = kg->R;
  bool racing = kg->mode != KG_ATTRACT && R.T == &kg->T && (R.phase == KS_RACE || R.phase == KS_OVER) && kg->raceShown && millis() - kg->raceShown < 100;
  if (!racing) {
    if (kbMenus && t >= kbOkAt && (recent("OK = on") || recent("OK = race"))) { press(t + 1, OK, 80); kbOkAt = t + 1200; }
    static uint32_t boxSeen = 0;
    if (recent("OK = again   hold OK = back")) {                // the time trial: once more, then back
      if (!boxSeen) boxSeen = t;
      if (t - boxSeen > 900 && t >= kbOkAt) {
        bool again = kbTT-- > 0;
        press(t + 1, OK, again ? 80 : 1100); kbOkAt = t + 2500; boxSeen = 0;
        if (!again) kbQuit = true;
      }
    } else boxSeen = 0;
    if (kbQuit && t >= kbOkAt && recent("GRAND PRIX")) { kbQuit = false; press(t + 1, OK, 1100); kbOkAt = t + 3000; kbLeft = true; }
    return;
  }
  KgKart &k = R.k[R.me];
  if (k.finish) { applyMask(0); simAcc[0] = 0; return; }
  KgIn in;
  uint16_t stuck = k.stuck, ai = k.aiItem;
  kgAutopilot(R, R.me, in);
  k.stuck = stuck; k.aiItem = ai;
  int32_t bend = 0;
  for (uint16_t j = 2; j < 10; j++) bend += R.T->pt[(k.idx + j) % R.T->n].curv;
  int32_t v = (k.vx * kgCos(k.a) + k.vy * kgSin(k.a)) >> 14;
  bool drift = (k.drift && (bend * k.drift > 60 || k.charge < KG_DRIFT1)) || (!k.drift && (bend > 165 || bend < -165) && v > R.vmax * 7 / 10);
  if (k.drift && k.charge >= KG_DRIFT2) drift = false;
  uint8_t m = 0;
  if (kbTilt) {
    int st = in.steer;
    simAcc[0] = (int16_t)(st > 0 ? 1640 + st * 6560 / 256 : st < 0 ? -1640 + st * 6560 / 256 : 0);
    if (st > 20 || st < -20) kbTiltSteer++;
    if (abs(simAcc[0]) > kbMaxTilt) kbMaxTilt = abs(simAcc[0]);
  } else if (in.steer > 40) { m |= RT; kbKeySteer++; }
  else if (in.steer < -40) { m |= LT; kbKeySteer++; }
  if (drift) m |= DN;
  if (k.item && !k.roul && t >= kbUseAt && !(m & DN) && R.phase == KS_RACE) { m |= UP; kbUseAt = t + 1500; }
  applyMask(m);
}

// ---------------- the rules without a screen ----------------
static KgRace *kcRace() { static KgRace *r = (KgRace *)calloc(1, sizeof(KgRace)); return r; }
static void kcSteps(KgRace &R, int n, const KgIn *in = NULL) {
  KgIn none;
  memset(&none, 0, sizeof(none));
  for (int i = 0; i < n; i++) { kgRaceStep(R, in ? *in : none); R.ev = 0; }
}
static void kcSolo(KgRace &R, KgTrack *T, uint8_t karts, bool cpu) {
  kgRaceInit(R, T, 1, 4242);
  R.nk = karts;
  for (uint8_t i = 0; i < karts; i++) {
    kgKartReset(R.k[i], i, cpu ? KK_CPU : 0);
    kgGrid(R, R.k[i], i);
    R.k[i].skill = (uint8_t)(cpu ? 140 + i * 20 : 200);
  }
  R.me = 0;
  R.phase = KS_RACE;
}
static int32_t kcSpeed(const KgKart &k) { return (k.vx * kgCos(k.a) + k.vy * kgSin(k.a)) >> 14; }

static void kartItemsLogic(KgTrack *T) {
  KgRace &R = *kcRace();
  KgIn gas;
  memset(&gas, 0, sizeof(gas));
  gas.gas = true;
  // an item box: the roulette, then an item
  kcSolo(R, T, 1, false);
  KgKart &a = R.k[0];
  a.x = (int32_t)T->box[0].x << 8; a.y = (int32_t)T->box[0].y << 8;
  kcSteps(R, 1);
  check("kart: an item box starts the roulette", a.roul > 0 && T->box[0].off > 0 && R.st.boxes == 1);
  kcSteps(R, KG_ROUL + 1);
  check("kart: ... and gives an item", a.item > KI_NONE && a.item < KI_N && !a.roul);
  // a turbo: faster than the top speed for a while (the CPU drives up to speed on the road first)
  kcSolo(R, T, 1, true);
  KgKart &b = R.k[0];
  b.skill = 255;
  kcSteps(R, 500);
  b.flags = 0;
  int32_t v0 = kcSpeed(b);
  b.item = KI_TURBO;
  KgIn use = gas;
  use.use = true;
  kcSteps(R, 1, &use);
  kcSteps(R, 60, &gas);
  int32_t v1 = kcSpeed(b);
  printf("      turbo: %d -> %d (1/256 px a step)\n", (int)v0, (int)v1);
  check("kart: a turbo makes the kart faster than its top speed", b.boost > 0 && v1 > v0 * 11 / 10 && R.st.used[KI_TURBO] == 1);
  // a peel dropped behind: the kart after it spins
  kcSolo(R, T, 2, false);
  KgKart &p0 = R.k[0], &p1 = R.k[1];
  kcSteps(R, 300, &gas);
  p0.item = KI_PEEL;
  kcSteps(R, 1, &use);
  KgPeel *pl = NULL;
  for (auto &q : R.peel) if (q.on) pl = &q;
  check("kart: a peel lies behind the kart that dropped it", pl && pl->owner == 0);
  if (pl) {
    p1.x = ((int32_t)pl->x << 8) - (kgCos(p1.a) * 8 >> 6); p1.y = ((int32_t)pl->y << 8) - (kgSin(p1.a) * 8 >> 6);
    p1.vx = kgCos(p1.a) >> 6; p1.vy = kgSin(p1.a) >> 6;
    p1.flags = KK_CPU; p1.skill = 0;
    kcSteps(R, 20, &gas);
    check("kart: a kart running over it spins out, the peel is gone", R.st.peelHits == 1 && !pl->on);
  }
  // a rocket flies to the kart ahead and throws it; a shield stops the next one
  kcSolo(R, T, 2, true);
  KgKart &r0 = R.k[0], &r1 = R.k[1];
  r0.flags = r1.flags = 0;
  kgPlace(R, r1, 40, 0); r1.prog = 40;
  kgPlace(R, r0, 10, 0); r0.prog = 10;
  kcSteps(R, 1);
  r0.item = KI_ROCKET;
  kcSteps(R, 1, &use);
  check("kart: the rocket is on its way to the kart ahead", R.rk[0].on ? R.rk[0].target == 1 : false);
  int st = 0;
  for (; st < 400 && !R.st.rocketHits; st++) kcSteps(R, 1);
  printf("      rocket: hit after %d steps (240 px ahead)\n", st);
  check("kart: the rocket hits it, it tumbles", R.st.rocketHits == 1 && r1.tumble > 0);
  kcSteps(R, 200);
  r1.shield = 600;
  r0.item = KI_ROCKET;
  kcSteps(R, 1, &use);
  for (st = 0; st < 400 && !R.st.blocked; st++) kcSteps(R, 1);
  check("kart: a shield stops a rocket", R.st.blocked == 1 && !r1.shield && r1.tumble == 0 && R.st.rocketHits == 1);
  // coins: one more percent top speed each
  kcSolo(R, T, 1, false);
  KgKart &c = R.k[0];
  int32_t vm = kgVmax(R, c);
  c.x = (int32_t)T->coin[0].x << 8; c.y = (int32_t)T->coin[0].y << 8;
  kcSteps(R, 1);
  check("kart: a coin is picked up and makes it faster", c.coins == 1 && kgVmax(R, c) > vm && R.st.coins == 1);
  // a drift: hop, sparks charging, a mini turbo when let go
  kcSolo(R, T, 1, true);
  KgKart &d = R.k[0];
  d.skill = 255;
  kcSteps(R, 300);
  d.flags = 0;
  KgIn dr = gas;
  dr.steer = 256; dr.drift = dr.brake = true;
  kcSteps(R, 1, &dr);
  bool hopped = d.drift == 1 && d.hop > 0;
  kcSteps(R, 160, &dr);
  uint16_t ch = d.charge;
  kcSteps(R, 1, &gas);
  printf("      drift: charge %u after 1.6 s, boost %u steps\n", ch, d.boost);
  check("kart: DOWN while steering hops into a drift that charges a mini turbo", hopped && ch >= KG_DRIFT2 && d.boost > 60 && R.st.minis == 1);
}

// KARTMAP=1: the map of every track from above (one pixel a cell), the CPU's lap on it in red
static uint8_t *kcMapImg = NULL;
static void kcMapDot(int32_t x, int32_t y, uint8_t r, uint8_t g, uint8_t b) {
  if (!kcMapImg || (uint32_t)x >= KG_WORLD || (uint32_t)y >= KG_WORLD) return;
  uint8_t *p = kcMapImg + ((y / 4) * 256 + x / 4) * 3;
  p[0] = r; p[1] = g; p[2] = b;
}
static void kcMapStart(const KgTrack &T) {
  if (!getenv("KARTMAP")) return;
  if (!kcMapImg) kcMapImg = (uint8_t *)malloc(256 * 256 * 3);
  const KgTheme &th = KG_THEME[T.theme];
  for (int y = 0; y < 256; y++)
    for (int x = 0; x < 256; x++) { uint8_t *p = kcMapImg + (y * 256 + x) * 3; colToRgb(th.mat[kgMat(T, x * 4 + 2, y * 4 + 2)][0], p[0], p[1], p[2]); }
}
static void kcMapEnd(uint8_t t) {
  if (!kcMapImg) return;
  char fn[64];
  snprintf(fn, sizeof(fn), "frames/kartmap_%u.ppm", t);
  FILE *f = fopen(fn, "wb");
  if (!f) return;
  fprintf(f, "P6\n256 256\n255\n");
  fwrite(kcMapImg, 1, 256 * 256 * 3, f);
  fclose(f);
}

static void kartLogic() {
  KgTrack *T = (KgTrack *)calloc(1, sizeof(KgTrack));
  KgRace &R = *kcRace();
  printf("      track          length  tiles  heap   lap (CPU)  falls walls  3 laps x 6   fps 20/40 MHz\n");
  kcCpu = kcSpi20 = kcSpi40 = kcWorst40 = 0; kcN = 0;
  static uint8_t prev[SCR_W * SCR_H];
  for (uint8_t t = 0; t < 6; t++) {
    bool built = kgBuild(*T, t);
    check("kart: the track is built", built);
    if (!built) continue;
    // the centre line, the start grid, boxes and coins lie on the road
    bool road = true;
    for (uint16_t i = 0; i < T->n; i++) if (KM_RANK[kgMat(*T, T->pt[i].x / 4, T->pt[i].y / 4)] > 3) road = false;
    kcSolo(R, T, 6, true);
    for (uint8_t i = 0; i < 6; i++) if (KM_RANK[kgMat(*T, R.k[i].x >> 8, R.k[i].y >> 8)] > 3) road = false;
    for (uint8_t i = 0; i < T->nbox; i++) if (KM_RANK[kgMat(*T, T->box[i].x, T->box[i].y)] > 3) road = false;
    for (uint8_t i = 0; i < T->ncoin; i++) if (KM_RANK[kgMat(*T, T->coin[i].x, T->coin[i].y)] > 3) road = false;
    char b[96];
    snprintf(b, sizeof(b), "kart: %s - the centre line, the grid, boxes and coins on the road", KG_TRACK[t].name);
    check(b, road && T->nbox >= 8 && T->ncoin >= 15 && T->ndec >= 10);
    // a driver like the player's autopilot does a lap; the camera rides along, frames to the model
    kcSolo(R, T, 1, true);
    R.k[0].skill = 200;
    KgCam cam;
    memset(&cam, 0, sizeof(cam));
    uint32_t steps = 0, lapMs = 0, shot = 0;
    gfx.clear();
    memcpy(prev, gfx.buffer(), sizeof(prev));
    long n0 = kcN;
    double c0 = kcCpu, s20 = kcSpi20, s40 = kcSpi40;
    kcMapStart(*T);
    while (steps < 9000 && !R.k[0].lap) {
      kcSteps(R, 1);
      steps++;
      kcMapDot(R.k[0].x >> 8, R.k[0].y >> 8, 255, 0, 0);
      if (steps % 3 == 0) {                                    // a frame every 30 ms
        kgCamFollow(cam, R.k[0]);
        kgCost = 0;
        kgFrame(R, cam, steps * 10);
        kgHud(R, steps * 10, false);
        uint32_t cost = kgCost;
        kgCost = 0;
        kcCpu += kcCpuUs(cost); kcSpi20 += kcSpiUs(gfx.buffer(), prev, 20); kcSpi40 += kcSpiUs(gfx.buffer(), prev, 40); kcN++;
        memcpy(prev, gfx.buffer(), sizeof(prev));
        if (steps > 600 && shot < 2 && steps % (T->n / 2 * 3) == 0) {
          char nm[16];
          snprintf(nm, sizeof(nm), "%u%c", t, 'a' + shot++);
          kcWrite(nm, gfx.buffer());
        }
      }
    }
    lapMs = R.k[0].lap ? R.k[0].lastLap : 0;
    kcMapEnd(t);
    uint16_t falls = R.st.falls, walls = R.st.walls;
    double fps20 = 1e6 * (kcN - n0) / ((kcCpu - c0) + (kcSpi20 - s20)), fps40 = 1e6 * (kcN - n0) / ((kcCpu - c0) + (kcSpi40 - s40));
    // six CPU drivers, three laps
    kcSolo(R, T, 6, true);
    uint32_t s6 = 0;
    bool all = false;
    while (s6 < 40000 && !all) {
      kcSteps(R, 1); s6++;
      all = true;
      for (uint8_t i = 0; i < 6; i++) if (!R.k[i].finish) all = false;
    }
    uint32_t last = 0;
    for (uint8_t i = 0; i < 6; i++) if (R.k[i].finish > last) last = R.k[i].finish;
    printf("      %-14s %5u px %5u %5u B  %2u.%02u s    %3u %5u     %s %3u.%u s    %4.1f / %4.1f\n", KG_TRACK[t].name, (unsigned)T->len, T->ntiles,
           (unsigned)(sizeof(KgTrack) + T->ntiles * 32), (unsigned)(lapMs / 1000), (unsigned)(lapMs / 10 % 100), falls, walls,
           all ? "all" : "NOT", (unsigned)(last / 1000), (unsigned)(last / 100 % 10), fps20, fps40);
    snprintf(b, sizeof(b), "kart: %s - a lap driven in under 50 s", KG_TRACK[t].name);
    check(b, lapMs > 15000 && lapMs < 50000);
    snprintf(b, sizeof(b), "kart: %s - six CPU drivers finish three laps", KG_TRACK[t].name);
    check(b, all);
    if (t == 0) kartItemsLogic(T);
  }
  kcReport("all six tracks, one lap each");
  check("kart: at least 20 fps at 40 MHz on the C3 (model)", 1e6 * kcN / (kcCpu + kcSpi40) >= 20);
  // walls hold: full speed into the city's barriers for a while, never inside one
  kgBuild(*T, 4);
  kcSolo(R, T, 1, false);
  bool inWall = false;
  KgIn in;
  memset(&in, 0, sizeof(in));
  in.gas = true;
  for (int i = 0; i < 3000; i++) {
    in.steer = (int16_t)((i / 300) % 2 ? 256 : -256);
    kcSteps(R, 1, &in);
    if (kgMat(*T, R.k[0].x >> 8, R.k[0].y >> 8) == KM_WALL) inWall = true;
  }
  check("kart: the city's barriers stop the kart, it never ends up inside one", !inWall && R.st.walls > 0);
  // the sea: a kart that drives into it is put back on the road
  kgBuild(*T, 2);
  kcSolo(R, T, 1, false);
  in.steer = 256;
  int fell = 0;
  for (int i = 0; i < 2000 && !fell; i++) { kcSteps(R, 1, &in); if (R.k[0].fall) fell = i; }
  kcSteps(R, 80);
  check("kart: off into the sea, back on the road a moment later", fell && !R.k[0].fall && KM_RANK[kgMat(*T, R.k[0].x >> 8, R.k[0].y >> 8)] <= 3);
  // the ghost: packed and back within a pixel
  KgKart g, h;
  memset(&g, 0, sizeof(g)); memset(&h, 0, sizeof(h));
  g.x = 345 << 8 | 77; g.y = 801 << 8; g.a = 40000;
  static KgGhost gh;
  gh.n = 2; gh.s[0] = gh.s[1] = kgGhostPack(g);
  kgGhostAt(gh, 50, h);
  check("kart: a ghost sample keeps the place and the heading", abs((int)(h.x - g.x)) < 128 && abs((int)(h.y - g.y)) < 128 && abs(kgAngDiff(h.a, g.a)) < 300);
  free(T->tdat);
  free(T);
}

// ---------------- ANNA's kart, the other console of a race of two ----------------
/* Her own world on our track's map (a copy of it: her boxes and coins are
   her own), her kart driven by the CPU, ours shown as our packets say -
   the same kart_net.h as ours. kcpLeaveAt: she gives up then.        */
static uint32_t kcpLeaveAt = 0, kcpPkts = 0, kcpEvIn = 0, kcpEvOut = 0;
static int kcpResult = -1;
static void kcpNet(uint8_t type, uint8_t a, uint8_t b, uint8_t c) {
  KgDuel *keep = kgDuelNow;
  kgDuelNow = &kcp->D;
  kgDuelNet(type, a, b, c);
  kgDuelNow = keep;
}
static void kartPeer(uint32_t now) {
  LinkCore &B = simPeer;
  if (!kcp) kcp = (KcPeer *)calloc(1, sizeof(KcPeer));
  KcPeer &P = *kcp;
  if (B.state != LK_PLAYING) { P.on = false; return; }
  if (!P.on || P.sid != B.session()) {                           // a new race: wait for our console's track
    uint8_t track = (uint8_t)(B.session() % 6);
    if (!kg || !kg->T.tdat || kg->T.id != track) return;
    memset(&P, 0, sizeof(P));
    P.on = true; P.sid = B.session();
    memcpy(&P.T, &kg->T, sizeof(KgTrack));                        // the map is shared, boxes and coins her own
    kgRaceInit(P.R, &P.T, 1, 777);
    P.R.online = true; P.R.nk = 2; P.R.me = 0;
    kgKartReset(P.R.k[0], 3, KK_CPU);
    P.R.k[0].skill = 230;
    kgKartReset(P.R.k[1], 0, KK_REMOTE);
    kgGrid(P.R, P.R.k[0], B.iStart ? 0 : 1);
    kgGrid(P.R, P.R.k[1], B.iStart ? 1 : 0);
    P.R.net = kcpNet;
    kgDuelInit(P.D, B.iStart);
    P.D.base = annaOn ? 80 : 3;
    P.last = now;
  }
  if (!kg || kg->T.tdat != P.T.tdat) { P.on = false; return; }   // our race is over: the map is gone
  if (kcpLeaveAt && now >= kcpLeaveAt) { kcpLeaveAt = 0; printf("      ANNA gives up at %u ms\n", now); B.cancel(); P.on = false; return; }
  uint8_t got = kgDuelTick(P.D, P.R, 0, 1, B, now);
  if (!P.started) {
    if (P.D.host && P.D.seen && !P.D.countAt) { kgDuelPush(P.D, KV_START, 0, 0, 0); P.D.countAt = now; }
    if (!P.D.host && (got & KV_START)) P.D.countAt = now;
    if (P.D.countAt && now - P.D.countAt >= 3200) { P.started = true; P.R.phase = KS_RACE; P.D.phase = KP_RACE; P.last = now; }
    return;
  }
  KgIn none;
  memset(&none, 0, sizeof(none));
  while (now - P.last >= KG_STEP) {
    P.last += KG_STEP;
    kgDuelRemote(P.D, P.R.k[1], now, P.T);
    kgRaceStep(P.R, none);
    P.R.ev = 0;
  }
  if (P.R.k[0].finish && !P.D.myTime) { P.D.myTime = P.R.k[0].finish; P.D.doneAt = now; P.D.phase = KP_DONE; }
  kgDuelDecide(P.D, now, false);
  if (!P.D.host && P.D.result < 0 && P.D.myTime && P.D.theirDone && now - P.D.doneAt > 6000) P.D.result = P.D.myTime <= P.D.theirTime ? 0 : 1;
  if (P.D.result >= 0 && !P.done) { P.done = true; P.doneAt = now; }
  kcpPkts = P.D.pktIn; kcpEvIn = P.D.evIn; kcpEvOut = P.D.evOut; kcpResult = P.D.result;
  if (P.done && !P.D.qn && now - P.doneAt > 3000) {             // all said: the end
    simBotGames++;
    simBotResult = P.D.result == 0 ? 1 : 2;
    B.finish();
    P.on = false;
  }
}

// the checks at the end of a kart scenario
static void kartChecks() {
  if (scenario == "kart") {
    kcReport("the Sun Cup");
    printf("      %d races; the player: %u boxes, %u coins, %u drifts, %u mini turbos, %u walls; items used %u/%u/%u/%u; hits: %u rocket, %u peel, %u blocked\n",
           kcRaces, kcSt.boxes, kcSt.coins, kcSt.drifts, kcSt.minis, kcSt.walls, kcSt.used[1], kcSt.used[2], kcSt.used[3], kcSt.used[4],
           kcSt.rocketHits, kcSt.peelHits, kcSt.blocked);
    check("kart: the three races of the Sun Cup were driven, on the keys", kcRaces == 3 && kbKeySteer > 500 && seen("SUN CUP  after race 3"));
    check("kart: every CPU driver came over the line", kcAllDone >= 1 && seen("RESULTS"));
    check("kart: boxes, items, coins, drifts and mini turbos happened", kcSt.boxes > 2 && kcSt.coins > 0 && kcSt.drifts > 3 && kcSt.minis > 0 &&
          kcSt.used[KI_TURBO] + kcSt.used[KI_PEEL] + kcSt.used[KI_ROCKET] + kcSt.used[KI_SHIELD] > 6 && kcSt.rocketHits + kcSt.peelHits > 0);
    bool trophy = seen("GOLD TROPHY!") || seen("SILVER TROPHY!") || seen("BRONZE TROPHY!");
    check("kart: the podium was shown", trophy || seen("NO TROPHY THIS TIME"));
    KgSave sv;
    bool saved = simNvsBlob.count("KGPsv") && simNvsBlob["KGPsv"].size() == sizeof(sv);
    if (saved) memcpy(&sv, simNvsBlob["KGPsv"].data(), sizeof(sv));
    check("kart: a trophy is kept and opens the Star Cup", saved && (!trophy || (sv.trophy[0][1] > 0 && (sv.open & 2))));
    check("kart: 20 fps or more at 40 MHz on the C3 (model)", kcN > 500 && 1e6 * kcN / (kcCpu + kcSpi40) >= 20);
  }
  if (scenario == "karttilt") {
    kcReport("Coral Bay on the tilt sensor");
    printf("      %d races, tilt steering %u, keys %u, max tilt %d\n", kcRaces, kbTiltSteer, kbKeySteer, kbMaxTilt);
    check("kart: steered by tilting, stepless (no key held)", kbTiltSteer > 300 && kbKeySteer == 0 && kbMaxTilt > 2000);
    check("kart: the race on the tilt sensor went over the line", kcRaces == 1 && seen("RESULTS") && seen("FINISH!"));
  }
  if (scenario == "kartghost") {
    kcReport("time trial");
    bool g = simNvsBlob.count("KGPg0") > 0;
    size_t gn = g ? simNvsBlob["KGPg0"].size() : 0;
    printf("      ghost in flash: %zu bytes; seen on the track in %d frames\n", gn, kcGhostSeen);
    check("kart: the time trial keeps the best lap as a ghost (small)", g && gn > 100 && gn < 2000 && seen("NEW RECORD!"));
    check("kart: the second run drives against the ghost", kcRaces == 2 && kcGhostSeen > 20);
  }
  if (scenario == "kartmenu") {
    check("kart: the menus - class, gas, tracks (a locked one), drivers", seen("CLASS: 150cc") && seen("GAS: HOLD UP") && seen("SINGLE RACE") &&
          seen("? ? ?") && seen("CHOOSE YOUR DRIVER") && seen("TIME TRIAL"));
  }
  if (scenario == "mpkart" || scenario == "inetkart" || scenario == "mpkartgone") {
    kcReport(scenario == "inetkart" ? "online race (internet)" : "online race (radio)");
    printf("      we got %u packets, %u events, sent %u events; ANNA got %u packets, %u events, sent %u; result %d (ANNA: %d)\n",
           kgNetSeen.pktIn, kgNetSeen.evIn, kgNetSeen.evOut, kcpPkts, kcpEvIn, kcpEvOut, kgNetSeen.result, kcpResult);
    if (scenario == "mpkartgone") {
      check("kart online: ANNA gives up halfway - her kart goes, the race runs on to the line", kgNetSeen.gone && seen("ANNA left") && seen("YOU WIN"));
      check("kart online: back in the lobby it says so", seen("ANNA left the game"));
    } else {
      check("kart online: the race was driven to the end on both consoles", seen("YOU WIN") || seen("YOU LOST"));
      check("kart online: both agree who won", kgNetSeen.result >= 0 && kcpResult >= 0 && kgNetSeen.result != kcpResult);
      check("kart online: states came through all the race (10 a second or more)", kgNetSeen.pktIn > 400 && kcpPkts > 400);
      check("kart online: events both ways (start, boxes, items, the finish)", kgNetSeen.evIn >= 3 && kcpEvIn >= 3 && kgNetSeen.evIn + kcpEvIn >= 8);
      printf("      ANNA's kart as we show it: %.1f px off on average, %.1f px at the most (%ld frames); on our screen in %d frames\n",
             kcErrN ? kcErrSum / kcErrN : 0.0, kcErrMax, kcErrN, kcRemoteSeen);
      check("kart online: the other kart is shown where it is (within 12 px on average)", kcErrN > 500 && kcErrSum / kcErrN < 12);
    }
  }
}

static void colourPeer(uint32_t now) {                          // ANNA's side of the colour console's own games
  if (simPeer.game == LKG_KART) kartPeer(now);
  else chessPeer(now);
}
