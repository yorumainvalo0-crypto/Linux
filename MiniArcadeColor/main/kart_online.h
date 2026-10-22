// MiniArcade Color: Kart GP online - a race of two consoles, over the
// radio or the internet (multiplayer.h, inet.h find the other one and
// start the game; kart_net.h carries the race). Part of console.cpp,
// which includes it after multiplayer.h and inet.h.
/* No CPU drivers: the two karts alone on the track the session picks,
   100cc, three laps, all items. Each console drives its own kart; the
   other one is shown as its state says. Should the other console go
   (left, switched off, the connection lost), its kart vanishes and the
   race goes on to the line.                                         */

struct KgNetSeen { uint32_t pktIn, evIn, evOut; int8_t result; bool gone; };
static KgNetSeen kgNetSeen;                             // how the last race of two went (the host tests look)

static uint8_t mpKart() {
  LinkCore &L = linkCore();
  kg = (KgGame *)calloc(1, sizeof(KgGame));
  if (!kg) { L.cancel(); mpNote("not enough memory", NULL); return 2; }
  KgGame &G = *kg;
  kgSvLoad();
  G.T.id = 255;
  G.mode = KG_NET;
  uint8_t track = (uint8_t)(L.session() % 6);
  gfx.fill(0, 0, SCR_W, SCR_H, C::BLACK);
  gfx.center(110, "LOADING...", C::WHITE, F_BOLD);
  gfx.send();
  if (!kgBuild(G.T, track)) { free(G.T.tdat); free(kg); kg = NULL; L.cancel(); mpNote("not enough memory", NULL); return 2; }
  KgRace &R = G.R;
  bool host = L.iStart;
  kgRaceInit(R, &G.T, 1, L.session() * 2654435761u + (host ? 1 : 2));
  R.online = true;
  R.nk = 2;
  R.me = 0;
  kgKartReset(R.k[0], kgSv.drv, 0);
  kgKartReset(R.k[1], (uint8_t)((kgSv.drv + 1) % KG_KARTS), KK_REMOTE);
  kgGrid(R, R.k[0], host ? 0 : 1);
  kgGrid(R, R.k[1], host ? 1 : 0);
  KgDuel &D = G.duel;
  kgDuelInit(D, host);
  D.base = (uint16_t)(inS ? inS->n.rtt / 2 + 20 : 3);             // the internet: half a round trip and the poll; the radio: at once
  kgDuelNow = &D;
  R.net = kgDuelNet;
  fxReset();
  tiltGame = true;
  tiltGameStart();
  KgKart &me = R.k[0], &them = R.k[1];
  uint16_t startA = me.a;
  G.cam.turn = 0;
  kgCamBehind(G.cam, me, (uint16_t)(startA + 32768), 70);
  R.phase = KS_INTRO;
  uint32_t t0 = millis(), last = gameMillis(), acc = 0, goneAt = 0, resultAt = 0, finishAt = 0;
  uint8_t lit = 0, ret = 0;
  bool gone = false;
  char note[32] = "";
  btnClear();
  for (;;) {
    if (!poll(false)) { L.cancel(); ret = 1; break; }             // hold OK: give up
    uint32_t now = gameMillis(), ms = millis(), dt = now - last;
    last = now;
    if (dt > 80) dt = 80;
    // ---- the other console ----
    if (!gone && L.state != LK_PLAYING && !(L.state == LK_OVER && L.why == LKE_NONE)) {
      gone = true; goneAt = ms;
      them.flags |= KK_GONE;
      snprintf(note, sizeof(note), "%.6s left", L.opp.name);
      if (R.phase == KS_INTRO) break;                              // before the start: back to the lobby (it says why)
    }
    uint8_t got = 0;
    if (L.state == LK_PLAYING || L.state == LK_OVER) got = kgDuelTick(D, R, 0, 1, L, linkNow());
    if (D.theirDrv < KG_KARTS) them.drv = D.theirDrv == me.drv ? (uint8_t)((D.theirDrv + 1) % KG_KARTS) : D.theirDrv;
    // ---- the start: the host gives it as soon as the other one is there ----
    if (R.phase == KS_INTRO) {
      uint32_t f = ms - t0 > 2200 ? 2200 : ms - t0;
      int32_t ease = (int32_t)(f * f / 2200);
      kgCamBehind(G.cam, me, (uint16_t)(startA + 32768 - ease * 32768 / 2200), 70 - ease * 35 / 2200);
      bool go = host ? (D.seen && ms - t0 > 1500) : (got & KV_START) != 0;
      if (host && go) kgDuelPush(D, KV_START, track, 0, 0);
      if (go) { R.phase = KS_COUNT; D.countAt = ms; lit = 0; kgCamBehind(G.cam, me, startA); }
    } else if (R.phase == KS_COUNT) {
      uint8_t l = (uint8_t)((ms - D.countAt) / 800);
      if (l > lit && l <= 3) { lit = l; sfx(660, 140); }
      if (ms - D.countAt >= 3200) { R.phase = KS_RACE; D.phase = KP_RACE; kgPlay(KG_T_GO); acc = 0; }
    }
    // ---- the race ----
    KgIn in;
    kgInput(in);
    if (R.phase == KS_RACE || R.phase == KS_OVER) {
      for (acc += dt; acc >= KG_STEP; acc -= KG_STEP) {
        kgDuelRemote(D, them, ms, G.T);
        kgRaceStep(R, in);
        in.use = false;
        if (R.phase == KS_RACE) kgCamFollow(G.cam, me);
      }
      if (me.finish && !D.myTime) { D.myTime = me.finish; D.doneAt = ms; D.phase = KP_DONE; R.phase = KS_OVER; finishAt = ms; }
    }
    if (R.phase == KS_OVER) {
      int32_t ca = (int32_t)(ms - finishAt);
      kgCamBehind(G.cam, me, (uint16_t)(me.a + (ca > 2500 ? 2500 : ca) * 13), 35 + (ca > 2500 ? 2500 : ca) / 100);
    }
    // ---- who won ----
    kgDuelDecide(D, ms, gone);
    if (!host && D.result < 0 && D.myTime && (gone || (D.theirDone && ms - D.doneAt > 6000))) D.result = (int8_t)((!D.theirDone || D.myTime <= D.theirTime) ? 0 : 1);
    if (D.result >= 0 && !resultAt) { resultAt = ms; if (D.result == 0) kgPlay(KG_T_WIN); else sfx(200, 300); }
    kgEvents(R, G.cam, now);
    kgTuneTick();
    fxUpdate();
    if (R.phase == KS_RACE) kgPlayerFx(R, now);
    // ---- the picture ----
    kgDrawRace(G, now, R.phase != KS_INTRO, L.opp.name);
    if (R.phase == KS_INTRO) {
      char b[32];
      snprintf(b, sizeof(b), "vs %.6s", L.opp.name);
      kgTitleCard(G, ms - t0);
      gfx.textShadow(SCR_W - 12 - Gfx::width(b, F_TEXT), 172, b, C::YELLOW, C::BLACK, F_TEXT);
      if (ms - t0 > 2500) mpBanner(host ? "waiting..." : "waiting for the start", C::YELLOW);
    }
    if (R.phase == KS_COUNT) {
      kgLights(lit, false);
      if (lit) { char b[4] = { (char)('0' + 4 - lit), 0 }; gfx.textShadow((SCR_W - Gfx::width(b, F_BIG)) / 2, 100, b, C::WHITE, C::BLACK, F_BIG); }
    }
    if (R.phase == KS_RACE && D.countAt && ms - D.countAt < 4100) {
      kgLights(3, true);
      gfx.textShadow((SCR_W - Gfx::width("GO!", F_BIG)) / 2, 100, "GO!", C::LIME, C::BLACK, F_BIG);
    }
    if (gone && ms - goneAt < 3000 && note[0]) mpBanner(note, C::ORANGE);
    else if (!gone && D.seen && R.phase == KS_RACE && ms - D.inAt > 2000) mpBanner("waiting...", C::YELLOW);
    if (R.phase == KS_OVER) {
      if (D.result >= 0) {
        const char *res = D.result == 0 ? "YOU WIN" : "YOU LOST";
        kgBanner(90, res, D.result == 0 ? C::YELLOW : C::WHITE, D.result == 0 ? C::YELLOW : C::RED, F_BIG);
        char b[64], t1[16], t2[16];
        kgTime(t1, sizeof(t1), D.myTime);
        if (D.theirDone) { kgTime(t2, sizeof(t2), D.theirTime); snprintf(b, sizeof(b), "you %s  %.6s %s", t1, L.opp.name, t2); }
        else snprintf(b, sizeof(b), "you %s", t1);
        kgBanner(146, b, C::WHITE, C::BLUE, F_SMALL);
        if (ms - resultAt > 4000 && (!D.qn || ms - resultAt > 7000 || gone)) break;
        if (btn(B_OK) && ms - resultAt > 1500 && !D.qn) break;
      } else if (ms - finishAt < 2500 || (ms / 400) % 2) kgBanner(96, "FINISH!", C::YELLOW, C::WHITE, F_BIG);
      else mpBanner(gone ? "the race is yours" : "waiting for the other kart", C::YELLOW);
    }
    gfx.send();
  }
  tiltGame = false;
  kgNetSeen = { D.pktIn, D.evIn, D.evOut, D.result, gone };
  if (ret == 0 && L.state == LK_PLAYING) L.finish();
  if (ret == 0 && (L.state == LK_OVER && L.why == LKE_NONE)) L.toLobby();
  kgDuelNow = NULL;
  free(G.T.tdat);
  free(kg);
  kg = NULL;
  return ret;
}
