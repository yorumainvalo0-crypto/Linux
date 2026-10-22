// MiniArcade Color: Kart GP. Part of console.cpp, which includes it.
// A kart race in the style of the 16-bit consoles: the track is a floor
// seen in perspective ("mode 7", kart_draw.h), six karts, three laps.
// Six tracks in two cups - Meadow Ring, Dune Dash, Coral Bay (Sun Cup),
// Frost Peak, Neon City, Magma Core (Star Cup) - each its own world:
// grass, sand, the sea, ice, a city at night, lava.
//   Grand Prix   a cup of three races against five CPU drivers, points
//                9-6-4-3-2-1, a trophy for the top three (50 / 100 / 150cc);
//                the Star Cup opens with a trophy in the Sun Cup
//   Single race  any open track
//   Time trial   alone, three turbos; the best lap comes back as a ghost
//                (kept in flash, about 500 bytes a track)
// Items from the boxes: turbo, peel (dropped behind), rocket (flies to the
// kart ahead), shield. Coins: 1 % top speed each, ten at most; a hit
// loses some. Drift: hold DOWN while steering - the sparks go blue, then
// orange; let go for a mini turbo. Online (kart_online.h): two consoles,
// over the radio or the internet.
//
//   LEFT/RIGHT (or tilt)  steer
//   DOWN held             drift in a bend, brake otherwise (backwards when stopped)
//   UP / OK               use the item (gas is automatic; "gas: UP" in the menu: UP = gas, OK = item)
//   hold OK               pause

#include "kart_data.h"
#include "kart_world.h"
#include "kart_draw.h"
#include "link.h"
#include "kart_net.h"

// ---------------- what is kept in flash ----------------
struct KgSave {
  uint8_t  ver, drv, cc, gas, open;    // open: bit 1 = the Star Cup
  uint8_t  trophy[2][3];               // per cup and class: 0 none, 1 bronze, 2 silver, 3 gold
  uint16_t best[6], lap[6];            // best race and best lap per track, 1/100 s (0 = none)
};
static KgSave kgSv;
#define KG_SAVE_VER 1

static void kgSvLoad() {
  prefs.begin("arcade", true);
  bool ok = prefs.getBytes("KGPsv", &kgSv, sizeof(kgSv)) == sizeof(kgSv) && kgSv.ver == KG_SAVE_VER;
  prefs.end();
  if (!ok) { memset(&kgSv, 0, sizeof(kgSv)); kgSv.ver = KG_SAVE_VER; kgSv.cc = 1; }
  if (kgSv.drv >= KG_KARTS) kgSv.drv = 0;
  if (kgSv.cc > 2) kgSv.cc = 1;
}
static void kgSvSave() { prefs.begin("arcade", false); prefs.putBytes("KGPsv", &kgSv, sizeof(kgSv)); prefs.end(); }
static uint16_t kgTrophyPoints() { uint16_t p = 0; for (auto &c : kgSv.trophy) for (uint8_t t : c) p += t; return p; }
static bool kgTrackOpen(uint8_t t) { return t < 3 || (kgSv.open & 2); }

// ---------------- the ghost of the time trial ----------------
/* A sample every 200 ms: x and y in 1/4 px (12 bits each) and the
   heading (8 bits). In flash as "KGPg<track>": the lap time (1/100 s),
   the number of samples, the samples.                                 */
#define KG_GHOST_MS 200
#define KG_GHOSTN   450                // 90 s
struct KgGhost { uint16_t cs, n; uint32_t s[KG_GHOSTN]; };

static void kgGhostKey(char *k, uint8_t t) { snprintf(k, 8, "KGPg%u", t); }
static bool kgGhostLoad(KgGhost &g, uint8_t t) {
  char k[8];
  kgGhostKey(k, t);
  prefs.begin("arcade", true);
  size_t n = prefs.getBytes(k, &g, sizeof(g));
  prefs.end();
  if (n < 4 || g.n > KG_GHOSTN || n != 4 + (size_t)g.n * 4) { g.n = 0; g.cs = 0; return false; }
  return true;
}
static void kgGhostSave(const KgGhost &g, uint8_t t) {
  char k[8];
  kgGhostKey(k, t);
  prefs.begin("arcade", false);
  prefs.putBytes(k, &g, 4 + (size_t)g.n * 4);
  prefs.end();
}
static uint32_t kgGhostPack(const KgKart &k) {
  uint32_t x = (uint32_t)(k.x >> 6) & 0xFFF, y = (uint32_t)(k.y >> 6) & 0xFFF;
  return x | y << 12 | (uint32_t)(k.a >> 8) << 24;
}
// the ghost at time ms of its lap: between two samples
static bool kgGhostAt(const KgGhost &g, uint32_t ms, KgKart &k) {
  uint32_t i = ms / KG_GHOST_MS, f = ms % KG_GHOST_MS;
  if (!g.n || i + 1 >= g.n) return false;
  uint32_t a = g.s[i], b = g.s[i + 1];
  int32_t ax = a & 0xFFF, ay = (a >> 12) & 0xFFF, bx = b & 0xFFF, by = (b >> 12) & 0xFFF;
  k.x = (ax + (bx - ax) * (int32_t)f / KG_GHOST_MS) << 6;
  k.y = (ay + (by - ay) * (int32_t)f / KG_GHOST_MS) << 6;
  int16_t da = (int16_t)((uint16_t)((b >> 24) << 8) - (uint16_t)((a >> 24) << 8));
  k.a = (uint16_t)(((a >> 24) << 8) + da * (int32_t)f / KG_GHOST_MS);
  return true;
}

// ---------------- the game's state, on the heap while it runs ----------------
struct KgGame {
  KgTrack  T;
  KgRace   R;
  KgCam    cam;
  KgGhost *gh, *rec;                   // time trial: the best lap, the lap being driven
  bool     ghostNew;
  uint8_t  cup, race, pts[KG_KARTS];   // grand prix: the cup, its race, the points (by driver)
  uint8_t  grid[KG_KARTS];             // drivers in the order of the start grid
  const uint16_t *tune;                // a few notes after each other
  uint32_t tuneAt;
  uint8_t  roulTick;
  uint16_t hitsShown;                  // R.hits already shown as a bang
  const char *say;                     // a banner for a moment ("FINAL LAP!")
  uint32_t sayUntil;
  uint8_t  mode;                       // KG_GP, KG_SINGLE, KG_TT, KG_NET
  uint32_t lastFrame;
  uint32_t raceShown;                  // millis() of the last picture of a race (the host tests)
  KgDuel   duel;                       // a race of two (kart_online.h)
};
enum { KG_GP, KG_SINGLE, KG_TT, KG_NET, KG_ATTRACT };
static KgGame *kg = NULL;
#ifdef ARCADE_TRACE
static bool kgRaceDrawn = false;                                // this frame is a race's picture (host tests)
#endif

// ---------------- sound ----------------
static const uint16_t KG_T_GO[]     = { 1320, 260, 0 };
static const uint16_t KG_T_LAP[]    = { 990, 70, 1320, 110, 0 };
static const uint16_t KG_T_FINAL[]  = { 880, 90, 1100, 90, 1320, 90, 1760, 200, 0 };
static const uint16_t KG_T_FINISH[] = { 1047, 120, 1319, 120, 1568, 120, 2093, 300, 1568, 100, 2093, 400, 0 };
static const uint16_t KG_T_TURBO[]  = { 300, 40, 450, 40, 650, 40, 900, 80, 0 };
static const uint16_t KG_T_FALL[]   = { 900, 60, 700, 60, 500, 60, 300, 120, 0 };
static const uint16_t KG_T_ITEM[]   = { 1500, 40, 2000, 60, 0 };
static const uint16_t KG_T_WIN[]    = { 784, 150, 784, 150, 784, 150, 1047, 450, 988, 150, 1047, 600, 0 };

static void kgPlay(const uint16_t *t) { kg->tune = t; kg->tuneAt = 0; }
static void kgTuneTick() {
  if (!kg->tune || !kg->tune[0]) { kg->tune = NULL; return; }
  uint32_t now = millis();
  if (kg->tuneAt && (int32_t)(now - kg->tuneAt) < 0) return;
  sfx(kg->tune[0], kg->tune[1]);
  kg->tuneAt = now + kg->tune[1] + 15;
  kg->tune += 2;
}

// ---------------- the camera ----------------
static void kgCamBehind(KgCam &c, const KgKart &k, uint16_t a, int32_t dist = KG_CAMD) {
  kgCamSet(c, k.x - (kgCos(a) * dist >> 6), k.y - (kgSin(a) * dist >> 6), a);
}
// follows the kart's heading with a little lag (a drift shows the kart turned); per 10 ms step
static void kgCamFollow(KgCam &c, const KgKart &k) {
  uint16_t want = (uint16_t)(k.a - k.drift * 3800);
  int16_t d = kgAngDiff(want, c.a);
  uint16_t a = (uint16_t)(c.a + d / 7);
  kgCamBehind(c, k, a);
}

// ---------------- banners ----------------
static void kgBanner(int16_t y, const char *s, col c, col edge, Font f = F_BOLD) {
  int16_t w = Gfx::width(s, f) + 30, h = Gfx::height(f) + 12, x = (SCR_W - w) / 2;
  gfx.rbox(x + 3, y + 3, w, h, 8, C::BLACK);
  gfx.rbox(x, y, w, h, 8, colDark(edge, 2));
  gfx.rframe(x, y, w, h, 8, edge);
  gfx.textShadow(x + 15, y + 6, s, c, C::BLACK, f);
}

// the start lights: three red, then all green
static void kgLights(uint8_t lit, bool go) {
  int16_t x = SCR_W / 2 - 46, y = 30;
  gfx.rbox(x + 3, y + 3, 92, 30, 8, C::BLACK);
  gfx.rbox(x, y, 92, 30, 8, rgb(30, 30, 40));
  gfx.rframe(x, y, 92, 30, 8, GREY3);
  for (uint8_t i = 0; i < 3; i++) {
    int16_t cx = x + 18 + i * 28, cy = y + 15;
    col c = go ? C::LIME : (i < lit ? C::RED : rgb(60, 20, 20));
    if (go || i < lit) fxGlow(cx, cy, 11, colDark(c, 2), c, 3);
    gfx.disc(cx, cy, 8, c);
    gfx.disc(cx - 2, cy - 3, 2, go || i < lit ? C::WHITE : rgb(90, 40, 40));
  }
}

// the countdown: the lights, the number popping up big and settling (a = ms since it came)
static void kgCountdown(uint8_t lit, uint32_t a, bool go) {
  kgLights(go ? 3 : lit, go);
  if (!go && !lit) return;
  int32_t sc = a < 200 ? 256 + (int32_t)(200 - a) * 2 : 256;
  if (go) kgBig(KG_COUNT[3], SCR_W / 2, 112, KP_TGREEN, sc);
  else kgBig(KG_COUNT[3 - lit], SCR_W / 2, 112, lit == 3 ? KP_TRED : KP_TWHITE, sc);
}

// ---------------- one race ----------------
/* The player's keys as this frame has them: steering stepless (keys or
   tilt), DOWN held for a drift or the brake, the item on UP or OK. Tilting
   forwards and back does nothing here.                               */
static void kgInput(KgIn &in) {
  memset(&in, 0, sizeof(in));
  in.steer = keyAxis(B_LEFT, B_RIGHT);
  bool down = btnHeld(B_DOWN) && !tiltKey[B_DOWN];
  in.drift = in.brake = down;
  if (kgSv.gas) in.gas = btnHeld(B_UP) && !tiltKey[B_UP];
  else in.gas = true;
  bool up = btn(B_UP) && !tiltKey[B_UP];
  bool ok = btn(B_OK);
  in.use = ok || (!kgSv.gas && up);
}

// sounds and effects for what happened to the player's kart in a step
static void kgEvents(KgRace &R, const KgCam &c, uint32_t ms) {
  uint16_t e = R.ev;
  R.ev = 0;
  if (!e) return;
  const KgKart &k = R.k[R.me];
  int16_t sx = SCR_W / 2, sy = 196;
  if (e & KE_BOX) { sfx(1500, 40); }
  if (e & KE_GOT) kgPlay(KG_T_ITEM);
  if (e & KE_COIN) { sfx(2100, 50); fxText(sx, sy - 40, "+1", C::YELLOW); fxBurst(sx, sy - 24, 6, C::YELLOW, C::ORANGE, 90, 300); }
  if (e & (KE_TURBO | KE_BOOST)) { kgPlay(KG_T_TURBO); fxFlash(C::WHITE, 120, 60); }
  if (e & KE_MINI) { sfx(k.boost > 60 ? 1700 : 1300, 90); fxBurst(sx, sy, 10, k.boost > 60 ? C::ORANGE : C::CYAN, C::WHITE, 140, 300, 0, 2, FX_STREAK); }
  if (e & KE_SPIN) { sfx(260, 220); fxShake(3, 300); fxBurst(sx, sy - 10, 8, C::YELLOW, C::BROWN, 110, 400, 20); }
  if (e & KE_HIT) { sfx(110, 350); fxShake(7, 500); fxExplode(sx, sy - 20, 2); fxFlash(C::ORANGE, 250, 120); }
  if (e & KE_SHIELD) { sfx(1900, 80); fxRing(sx, sy - 14, 30, C::WHITE, C::CYAN, 300); }
  if (e & KE_BUMP) { sfx(220, 30); fxShake(2, 120); }
  if (e & KE_WALL) { sfx(150, 50); fxShake(3, 150); fxBurst(sx, sy, 5, C::WHITE, GREY3, 90, 200, 0, 1, FX_STREAK); }
  if (e & KE_FALL) {                                              // a splash (or a burst of embers)
    kgPlay(KG_T_FALL);
    bool lava = R.T->theme == KW_VOLCANO;
    fxBurst(sx, sy - 6, 16, lava ? C::YELLOW : C::WHITE, lava ? C::RED : C::CYAN, 150, 600, 40, 2);
    fxRing(sx, sy - 2, 26, C::WHITE, lava ? C::ORANGE : C::BLUE, 400);
  }
  if (e & KE_FIRE) sfx(700, 120);
  if (e & KE_LAP) {
    if (k.lap == KG_LAPS - 1) { kgPlay(KG_T_FINAL); kg->say = "FINAL LAP!"; kg->sayUntil = millis() + 1800; }
    else if (k.lap < KG_LAPS) { kgPlay(KG_T_LAP); kg->say = k.lap == 1 ? "LAP 2" : NULL; kg->sayUntil = millis() + 1200; }
  }
  if (e & KE_FINISH) kgPlay(KG_T_FINISH);
  (void)ms;
}

// a hit somewhere in sight (another kart thrown, a rocket into a wall): a bang where it is
static void kgBangs(KgRace &R, const KgCam &c) {
  if (R.hits == kg->hitsShown) return;
  kg->hitsShown = R.hits;
  if (R.hitKart == R.me) return;                                  // ours: kgEvents
  int16_t x, y; int32_t z;
  if (!kgProj(c, (int32_t)R.hitX << 8, (int32_t)R.hitY << 8, 512, x, y, z) || z > 260 * 256 || x < -20 || x > SCR_W + 20) return;
  uint8_t p = z < 80 * 256 ? 2 : 1;
  fxExplode(x, (int16_t)(y - 4), p);
  sfx(160, 120);
}

// effects that keep going: dust and drift sparks of the player's kart as particles
static void kgPlayerFx(const KgRace &R, uint32_t ms) {
  const KgKart &k = R.k[R.me];
  if (k.fall) return;
  int32_t v2 = (k.vx >> 4) * (k.vx >> 4) + (k.vy >> 4) * (k.vy >> 4);
  uint8_t m = k.surf;
  bool off = m <= KM_OUTB || m == KM_RUN || m == KM_RUN2 || m == KM_SHORE;
  if (off && v2 > 60 && fxRand(3) == 0) {
    col dc = KG_THEME[R.T->theme].mat[m][1];
    fxEmit((int16_t)(SCR_W / 2 + fxRange(-14, 14)), (int16_t)(200 + fxRange(-2, 4)), fxRange(-40, 40), fxRange(-60, -20), 500, dc,
           colLerp(dc, C::WHITE, 120), 4, -2, FX_GROW | FX_DRAG);
  }
  if (k.drift && k.charge >= KG_DRIFT1) {
    col c = k.charge >= KG_DRIFT2 ? C::ORANGE : C::CYAN;
    for (uint8_t i = 0; i < 2; i++) {
      int16_t x = (int16_t)(SCR_W / 2 + (fxRand(2) ? -15 : 15));
      fxEmit(x, (int16_t)(208 + fxRange(-2, 2)), (int16_t)(fxRange(-120, 120) - k.drift * 60), fxRange(-160, -50), 260, C::WHITE, c, 1, 40, FX_STREAK);
    }
  }
  (void)ms;
}

static void kgDrawRace(KgGame &G, uint32_t ms, bool hud, const char *tag = NULL) {
  KgRace &R = G.R;
  G.raceShown = millis();
#ifdef ARCADE_TRACE
  kgRaceDrawn = true;
#endif
  kgFrame(R, G.cam, ms, tag);
  fxDraw(0, 0, KG_HZ - 30, SCR_H);
  if (R.k[R.me].boost && R.phase == KS_RACE) kgSpeedLines(ms);
  if (hud) kgHud(R, ms, G.mode == KG_TT);
  if (G.say && (int32_t)(millis() - G.sayUntil) < 0 && (millis() / 150) % 4) kgBanner(70, G.say, C::YELLOW, C::ORANGE, F_BOLD);
  fxShakeApply(0, SCR_H);
  fxFlashApply(0, 0, SCR_W, SCR_H);
}

// the race's karts: the player (driver d) at grid slot `slot`, the others by the grid list
static void kgSetupKarts(KgGame &G, bool cpus) {
  KgRace &R = G.R;
  R.nk = cpus ? KG_KARTS : 1;
  for (uint8_t s = 0; s < R.nk; s++) {
    uint8_t drv = cpus ? G.grid[s] : kgSv.drv;
    bool me = drv == kgSv.drv;
    KgKart &k = R.k[s];
    kgKartReset(k, drv, me ? 0 : KK_CPU);
    if (me) R.me = s;
    kgGrid(R, k, s);
    k.skill = (uint8_t)(120 + ((drv * 37 + G.race * 11) % 6) * 22);
    k.lineOff = (int8_t)((int)(kgHash(drv * 13u + G.race) % 9) - 4);
  }
  kgKartReset(R.k[KG_KARTS], kgSv.drv, KK_GHOST);
  R.k[KG_KARTS].flags = 0;
}

static void kgTitleCard(KgGame &G, uint32_t a) {
  const KgTrackDef &d = KG_TRACK[G.T.id];
  char b[32];
  int16_t slide = a < 300 ? (int16_t)((300 - a) * 300 / 300) : 0;
  gfx.fill(-slide, 150, SCR_W, 46, rgb(0, 0, 40));
  gfx.hline(-slide, 150, SCR_W, C::YELLOW);
  gfx.hline(-slide, 195, SCR_W, C::YELLOW);
  gfx.textShadow(20 - slide, 156, d.name, C::WHITE, C::BLACK, F_BOLD);
  if (G.mode == KG_GP) snprintf(b, sizeof(b), "%s  RACE %u/3", KG_CUP_NAME[G.cup], G.race + 1);
  else if (G.mode == KG_TT) snprintf(b, sizeof(b), "TIME TRIAL");
  else snprintf(b, sizeof(b), "%ucc  %u LAPS", 50 + kgSv.cc * 50, KG_LAPS);
  gfx.text(22 - slide, 182, b, C::YELLOW, F_SMALL);
}

// the results of the race: places, times and (grand prix) points
static void kgResults(KgGame &G, bool wait) {
  KgRace &R = G.R;
  NoPause np;
  gfx.dim(0, 0, SCR_W, SCR_H);
  btnClear();
  uint32_t t0 = millis();
  uint8_t shown = 0;
  while (poll(false)) {
    uint32_t a = millis() - t0;
    if (btn(B_OK) && a > 400) break;
    if (!wait && a > 4000) break;
    panel(30, 20, 260, 200, "RESULTS", rgb(40, 110, 230));
    for (uint8_t p = 0; p < R.nk; p++) {
      if (a < 200u + p * 180u) break;
      if (p + 1 > shown) { shown = (uint8_t)(p + 1); sfx((uint16_t)(700 + p * 90), 30); }
      const KgKart &k = R.k[R.order[p]];
      int16_t y = (int16_t)(56 + p * 24);
      bool me = R.order[p] == R.me;
      if (me) gfx.rbox(36, y - 3, 248, 21, 5, rgb(60, 60, 140));
      char b[24];
      snprintf(b, sizeof(b), "%u%s", p + 1, kgOrd((uint8_t)(p + 1)));
      gfx.text(44, y, b, p == 0 ? C::YELLOW : C::WHITE);
      col pal[16];
      kgKartPal(k.drv, pal);
      kgBlit(KG_KART[8], 92, (int16_t)(y + 16), 150, pal);
      gfx.text(104, y, KG_DRV[k.drv].name, me ? C::YELLOW : C::WHITE);
      if (k.finish) { kgTime(b, sizeof(b), k.finish); gfx.text(176, y + 4, b, C::LTGREY, F_SMALL); }
      else gfx.text(176, y + 4, "--", C::GREY, F_SMALL);
      if (G.mode == KG_GP) { snprintf(b, sizeof(b), "+%u", KG_POINTS[p]); gfx.right(276, y, b, C::LIME); }
    }
    gfx.center(204, "OK = on", C::GREY, F_SMALL);
    gfx.send();
  }
}

// one race; 0 = done, 1 = again (pause menu), 2 = quit to the library
static uint8_t kgRace(KgGame &G, uint8_t track) {
  KgRace &R = G.R;
  if (G.T.id != track || !G.T.tdat) {
    gfx.fill(0, 0, SCR_W, SCR_H, C::BLACK);
    gfx.center(110, "LOADING...", C::WHITE, F_BOLD);
    gfx.send();
    if (!kgBuild(G.T, track)) return 2;
  }
  kgRaceInit(R, &G.T, kgSv.cc, (uint32_t)random(0x7FFFFFFF) + 1);
  bool tt = G.mode == KG_TT;
  R.items = !tt;
  kgSetupKarts(G, !tt);
  kgProgress(R);
  if (tt) R.k[R.me].item = KI_TURBO;
  uint8_t turbos = tt ? 2 : 0;                                    // three in the time trial: one held, two more
  // the ghost: the best lap there is
  if (tt && G.gh && kgGhostLoad(*G.gh, track)) R.k[KG_KARTS].flags = KK_GHOST;
  if (tt && G.rec) G.rec->n = 0;
  uint32_t recNext = 0;
  G.ghostNew = false;
  fxReset();
  tiltGame = true;
  tiltGameStart();
  R.phase = KS_INTRO;
  uint32_t phaseAt = gameMillis(), last = gameMillis(), acc = 0, finishAt = 0;
  uint8_t lit = 0;
  KgKart &me = R.k[R.me];
  uint16_t startA = me.a;
  G.cam.turn = 0;
  kgCamBehind(G.cam, me, (uint16_t)(startA + 32768), 70);
  btnClear();
  for (;;) {
    if (!poll(false)) { tiltGame = false; if (wantRestart) { wantRestart = false; return 1; } return 2; }
    uint32_t now = gameMillis(), dt = now - last;
    last = now;
    if (dt > 80) dt = 80;
    uint32_t pa = now - phaseAt;
    KgIn in;
    kgInput(in);
    // ---- the phases ----
    if (R.phase == KS_INTRO) {
      uint32_t f = pa > 2200 ? 2200 : pa;                         // the camera flies round to behind the kart
      int32_t ease = (int32_t)(f * f / 2200);
      kgCamBehind(G.cam, me, (uint16_t)(startA + 32768 - ease * 32768 / 2200), 70 - ease * 35 / 2200);
      if (pa > 2600 || (pa > 600 && in.use)) { R.phase = KS_COUNT; phaseAt = now; lit = 0; kgCamBehind(G.cam, me, startA); }
    } else if (R.phase == KS_COUNT) {
      uint8_t l = (uint8_t)(pa / 800);
      if (l > lit && l <= 3) { lit = l; sfx(660, 140); }
      if (pa >= 3200) { R.phase = KS_RACE; phaseAt = now; kgPlay(KG_T_GO); last = now; acc = 0; }
    }
    if (R.phase == KS_RACE || R.phase == KS_OVER) {
      for (acc += dt; acc >= KG_STEP; acc -= KG_STEP) {
        if (tt && G.rec && !me.finish && (int32_t)(R.t - recNext) >= 0 && G.rec->n < KG_GHOSTN) {   // record the lap
          G.rec->s[G.rec->n++] = kgGhostPack(me);
          recNext += KG_GHOST_MS;
        }
        uint8_t lapBefore = me.lap;
        kgRaceStep(R, in);
        in.use = false;
        kgCamFollow(G.cam, me);
        if (tt && me.lap != lapBefore && G.rec) {                // a lap done: the ghost if it was the best
          uint16_t cs = (uint16_t)(me.lastLap / 10);
          if (!G.gh->n || cs < G.gh->cs || !(R.k[KG_KARTS].flags & KK_GHOST)) {
            if (G.rec->n > 2) {
              memcpy(G.gh->s, G.rec->s, G.rec->n * 4); G.gh->n = G.rec->n; G.gh->cs = cs;
              G.ghostNew = true;
              R.k[KG_KARTS].flags = KK_GHOST;
            }
          }
          G.rec->n = 0;
          recNext = R.t;
        }
        if (tt && me.item == KI_NONE && turbos && !me.boost) { me.item = KI_TURBO; turbos--; }
      }
      if (tt && (R.k[KG_KARTS].flags & KK_GHOST)) {
        KgKart &g = R.k[KG_KARTS];
        if (!kgGhostAt(*G.gh, R.t - me.lapStart, g)) g.x = -100000;
      }
      if (me.finish && R.phase == KS_RACE) { R.phase = KS_OVER; phaseAt = now; finishAt = now; }
    } else if (R.phase == KS_COUNT) kgCamBehind(G.cam, me, startA);
    if (R.phase == KS_OVER) {                                     // after the line: the camera turns round the kart
      int32_t ca = (int32_t)(now - finishAt);
      kgCamBehind(G.cam, me, (uint16_t)(me.a + (ca > 2500 ? 2500 : ca) * 13), 35 + (ca > 2500 ? 2500 : ca) / 100);
    }
    // the item roulette ticks
    if (me.roul && (++G.roulTick & 3) == 0) sfx((uint16_t)(900 + (me.roul % 5) * 120), 12);
    kgEvents(R, G.cam, now);
    kgBangs(R, G.cam);
    kgTuneTick();
    fxUpdate();
    if (R.phase == KS_RACE) kgPlayerFx(R, now);
    // ---- the picture ----
    kgDrawRace(G, now, R.phase != KS_INTRO);
    if (R.phase == KS_INTRO) kgTitleCard(G, pa);
    if (R.phase == KS_COUNT) kgCountdown(lit, (pa - lit * 800) % 800, false);
    if (R.phase == KS_RACE && pa < 900) kgCountdown(3, pa, true);
    if (R.phase == KS_OVER) {
      uint32_t a = now - finishAt;
      if (a < 2500 || (a / 400) % 2) kgBig(KG_FINISH, SCR_W / 2, 112, KP_TGOLD, a < 250 ? 256 + (int32_t)(250 - a) : 256);
      bool all = true;
      for (uint8_t i = 0; i < R.nk; i++) if (!R.k[i].finish) all = false;
      if (a > 3500 && (all || a > (tt ? 3500u : 15000u) || (btn(B_OK) && a > 1200))) break;
    }
    gfx.send();
  }
  tiltGame = false;
  // ---- the end: places for those still on the way, records ----
  kgProgress(R);
  if (tt) {
    uint8_t t = track;
    uint16_t race = (uint16_t)(me.finish / 10), lap = (uint16_t)(me.bestLap / 10);
    bool rec = !kgSv.best[t] || race < kgSv.best[t], recLap = !kgSv.lap[t] || lap < kgSv.lap[t];
    if (rec) kgSv.best[t] = race;
    if (recLap) kgSv.lap[t] = lap;
    if (rec || recLap) kgSvSave();
    if (G.ghostNew && G.gh->n && recLap) kgGhostSave(*G.gh, t);
    // the time trial's box
    NoPause np2;
    gfx.dim(0, 0, SCR_W, SCR_H);
    btnClear();
    uint32_t t0 = millis();
    while (poll(false)) {
      uint32_t a = millis() - t0;
      if (btn(B_OK) && a > 500) return 1;                         // once more, against the ghost
      panel(40, 30, 240, 180, "TIME TRIAL", rgb(40, 110, 230));
      char b[32], c[20];
      kgTime(c, sizeof(c), me.finish);
      snprintf(b, sizeof(b), "TOTAL  %s", c);
      gfx.center(70, b, C::WHITE, F_TEXT);
      kgTime(c, sizeof(c), me.bestLap);
      snprintf(b, sizeof(b), "BEST LAP  %s", c);
      gfx.center(96, b, C::YELLOW, F_TEXT);
      if (rec && (a / 300) % 2) gfx.center(124, "NEW RECORD!", C::LIME, F_BOLD);
      if (recLap && G.gh->n) gfx.center(152, "the ghost of this lap is saved", C::CYAN, F_SMALL);
      kgTime(c, sizeof(c), (uint32_t)kgSv.best[t] * 10);
      snprintf(b, sizeof(b), "record %s", c);
      gfx.center(170, b, C::LTGREY, F_SMALL);
      gfx.center(192, "OK = again   hold OK = back", C::GREY, F_SMALL);
      gfx.send();
    }
    return 0;
  }
  kgResults(G, true);
  return 0;
}

// ---------------- the podium after a cup ----------------
static void kgPodiumBlock(int16_t x, int16_t top, int16_t w, uint8_t place, col c) {
  int16_t bot = 228;
  gfx.fill(x, top + 8, w, bot - top - 8, c);
  gfx.fill(x, top, w, 8, colLight(c));
  gfx.vline(x, top, bot - top, colLight(c, 2));
  gfx.vline(x + w - 1, top, bot - top, colDark(c));
  char b[4] = { (char)('0' + place), 0 };
  gfx.textShadow(x + (w - Gfx::width(b, F_BIG)) / 2, top + 22, b, C::WHITE, colDark(c, 2), F_BIG);
}

static void kgPodium(KgGame &G, const uint8_t *order, uint8_t myPlace) {
  NoPause np;
  fxReset();
  btnClear();
  uint32_t t0 = millis();
  KgCam c;
  memset(&c, 0, sizeof(c));
  const KgTrack &T = G.T;
  int32_t sx = (int32_t)T.pt[0].x << 6, sy = (int32_t)T.pt[0].y << 6;
  if (myPlace <= 3) kgPlay(KG_T_WIN);
  while (poll(false)) {
    uint32_t a = millis() - t0;
    if (btn(B_OK) && a > 1500) break;
    kgCamSet(c, sx, sy, (uint16_t)(T.pt[0].a + a * 3));
    kgTexAnim(G.T, a);
    kgSky(G.T, c, a);
    kgFloor(G.T, c);
    gfx.dim(0, KG_HZ, SCR_W, SCR_H - KG_HZ);
    // the blocks: 2nd left, 1st in the middle, 3rd right
    static const int16_t BX[3] = { 116, 30, 202 }, BT[3] = { 150, 172, 186 };
    static const col BC[3] = { rgb(220, 170, 20), rgb(170, 175, 190), rgb(180, 110, 60) };
    for (uint8_t p = 0; p < 3; p++) kgPodiumBlock(BX[p], BT[p], 88, (uint8_t)(p + 1), BC[p]);
    for (uint8_t p = 0; p < 3; p++) {
      uint8_t drv = order[p];
      col pal[16];
      kgKartPal(drv, pal);
      int16_t x = BX[p] + 44, y = BT[p] + 2;
      uint8_t fr = (uint8_t)((8 + (int)(kgSin((uint16_t)(a * 30 + p * 20000)) / 6000)) & 15);
      int16_t hop = p == 0 ? (int16_t)(abs(kgSin((uint16_t)(a * 60))) / 3000) : 0;
      kgBlit(KG_KART[fr], x, (int16_t)(y - hop), 400, pal);
      int16_t w = Gfx::width(KG_DRV[drv].name, F_SMALL);
      gfx.textShadow(x - w / 2, y + 40, KG_DRV[drv].name, drv == kgSv.drv ? C::YELLOW : C::WHITE, C::BLACK, F_SMALL);
    }
    if (myPlace <= 3) {
      const col *tp = myPlace == 1 ? KP_GOLD : myPlace == 2 ? KP_SILVER : KP_BRONZE;
      kgSpr(KG_TROPHY, 160, 120, tp, 2);
    }
    if (fxRand(2) == 0) {                                        // confetti
      static const col CF[6] = { C::RED, C::YELLOW, C::LIME, C::CYAN, C::PINK, C::ORANGE };
      col cc = CF[fxRand(6)];
      fxEmit(fxRange(0, SCR_W - 1), -2, fxRange(-20, 20), fxRange(20, 60), 2600, cc, colDark(cc), 2, 4, 0);
    }
    fxUpdate();
    fxDraw(0, 0, 0, SCR_H);
    char b[40];
    snprintf(b, sizeof(b), "%s %ucc", KG_CUP_NAME[G.cup], 50 + kgSv.cc * 50);
    gfx.textShadow((SCR_W - Gfx::width(b, F_BOLD)) / 2, 8, b, C::WHITE, C::BLACK, F_BOLD);
    const char *res = myPlace == 1 ? "GOLD TROPHY!" : myPlace == 2 ? "SILVER TROPHY!" : myPlace == 3 ? "BRONZE TROPHY!" : "NO TROPHY THIS TIME";
    gfx.textShadow((SCR_W - Gfx::width(res, F_TEXT)) / 2, 36, res, myPlace <= 3 ? C::YELLOW : C::LTGREY, C::BLACK, F_TEXT);
    if (a > 1500) gfx.textShadow(SCR_W - 4 - Gfx::width("OK = on", F_SMALL), SCR_H - 11, "OK = on", C::LTGREY, C::BLACK, F_SMALL);
    kgTuneTick();
    gfx.send();
  }
}

// ---------------- the grand prix ----------------
static uint8_t kgCupRun(KgGame &G, uint8_t cup) {
  G.mode = KG_GP;
  G.cup = cup;
  memset(G.pts, 0, sizeof(G.pts));
  // the first grid: the CPUs in a mixed order, the player at the back
  uint8_t n = 0;
  for (uint8_t d = 0; d < KG_KARTS; d++) if (d != kgSv.drv) G.grid[n++] = d;
  G.grid[n] = kgSv.drv;
  for (G.race = 0; G.race < 3; G.race++) {
    uint8_t r;
    while ((r = kgRace(G, (uint8_t)(cup * 3 + G.race))) == 1) {}
    if (r == 2) return 2;
    for (uint8_t p = 0; p < G.R.nk; p++) G.pts[G.R.k[G.R.order[p]].drv] += KG_POINTS[p];
    // the standings
    uint8_t ord[KG_KARTS];
    for (uint8_t d = 0; d < KG_KARTS; d++) ord[d] = d;
    for (uint8_t a = 1; a < KG_KARTS; a++)
      for (uint8_t b = a; b > 0 && G.pts[ord[b]] > G.pts[ord[b - 1]]; b--) { uint8_t t = ord[b]; ord[b] = ord[b - 1]; ord[b - 1] = t; }
    {
      NoPause np;
      btnClear();
      uint32_t t0 = millis();
      while (poll(false)) {
        if (btn(B_OK) && millis() - t0 > 400) break;
        gfx.gradient(0, 0, SCR_W, SCR_H, rgb(0, 30, 90), C::BLACK);
        char b[32];
        snprintf(b, sizeof(b), "%s  after race %u", KG_CUP_NAME[cup], G.race + 1);
        gfx.center(12, b, C::WHITE, F_BOLD);
        for (uint8_t p = 0; p < KG_KARTS; p++) {
          uint8_t d = ord[p];
          int16_t y = (int16_t)(48 + p * 28);
          bool me = d == kgSv.drv;
          gfx.rbox(30, y - 3, 260, 24, 6, me ? rgb(60, 60, 150) : rgb(20, 30, 60));
          snprintf(b, sizeof(b), "%u", p + 1);
          gfx.text(40, y + 1, b, C::YELLOW);
          col pal[16];
          kgKartPal(d, pal);
          kgBlit(KG_KART[(millis() / 150 + p * 3) % 16], 84, (int16_t)(y + 16), 150, pal);
          gfx.text(104, y + 1, KG_DRV[d].name, me ? C::YELLOW : C::WHITE);
          snprintf(b, sizeof(b), "%u", G.pts[d]);
          gfx.right(280, y + 1, b, C::LIME);
        }
        gfx.center(SCR_H - 16, "OK = on", C::GREY, F_SMALL);
        gfx.send();
      }
    }
    // the next grid: the leader in front, the player as the standings say
    for (uint8_t p = 0; p < KG_KARTS; p++) G.grid[p] = ord[p];
    if (G.race == 2) {
      uint8_t place = 0;
      for (uint8_t p = 0; p < KG_KARTS; p++) if (ord[p] == kgSv.drv) place = (uint8_t)(p + 1);
      if (place <= 3) {
        uint8_t tr = (uint8_t)(4 - place);
        if (tr > kgSv.trophy[cup][kgSv.cc]) kgSv.trophy[cup][kgSv.cc] = tr;
        if (cup == 0) kgSv.open |= 2;
        kgSvSave();
        uint16_t tp = kgTrophyPoints();
        if (tp > curHigh) { curHigh = tp; saveHigh(curGame, curHigh); }
      }
      kgPodium(G, ord, place);
    }
  }
  return 0;
}

// ---------------- menus ----------------
// the title's background: the CPUs race round Meadow Ring, the camera rides along
static void kgAttract(KgGame &G, uint32_t ms) {
  KgRace &R = G.R;
  if (R.T != &G.T || G.mode != KG_ATTRACT) {
    if ((G.T.id != 0 || !G.T.tdat) && !kgBuild(G.T, 0)) { gfx.gradient(0, 0, SCR_W, SCR_H, rgb(30, 80, 210), rgb(0, 0, 40)); return; }
    G.mode = KG_ATTRACT;
    kgRaceInit(R, &G.T, 1, 12345);
    for (uint8_t d = 0; d < KG_KARTS; d++) G.grid[d] = d;
    kgSetupKarts(G, true);
    for (uint8_t i = 0; i < KG_KARTS; i++) R.k[i].flags = KK_CPU;
    R.me = 0;
    R.phase = KS_RACE;
    G.lastFrame = ms;
    G.cam.turn = 0;
  }
  uint32_t dt = ms - G.lastFrame;
  G.lastFrame = ms;
  if (dt > 60) dt = 60;
  KgIn none;
  memset(&none, 0, sizeof(none));
  for (uint32_t s = 0; s < dt / KG_STEP + 1; s++) { kgRaceStep(R, none); R.ev = 0; }
  KgKart &k = R.k[R.order[1]];
  uint16_t a = (uint16_t)(k.a + 5000 + kgSin((uint16_t)(ms * 4)) / 4);
  kgCamBehind(G.cam, k, a, 55);
  for (uint8_t i = 0; i < KG_KARTS; i++) if (R.k[i].finish) { G.mode = KG_SINGLE; break; }   // start again
  kgFrame(R, G.cam, ms);
}

static void kgLogo(int16_t y) {
  kgSpr(KG_LOGO, (int16_t)((SCR_W - KG_LOGO.w) / 2), y, KP_LOGO);
}

// a menu over the racing title picture; returns the choice or 255 (hold OK)
static uint8_t kgMenu(KgGame &G, const char *const *opts, uint8_t n, uint8_t sel, const char *foot = NULL) {
  NoPause np;
  btnClear();
  while (poll(false)) {
    if (btn(B_UP))   { sel = sel ? sel - 1 : n - 1; sfx(700, 15); }
    if (btn(B_DOWN)) { sel = (uint8_t)((sel + 1) % n); sfx(700, 15); }
    if (btn(B_OK))   { sfx(1200, 50); return sel; }
    uint32_t ms = millis();
    kgAttract(G, ms);
    kgLogo(6);
    int16_t h = (int16_t)(n * 26 + 10), y0 = (int16_t)(SCR_H - h - 22);
    gfx.dim(60, y0, 200, h);
    gfx.rframe(60, y0, 200, h, 6, GREY3);
    for (uint8_t i = 0; i < n; i++) {
      int16_t y = (int16_t)(y0 + 6 + i * 26);
      if (i == sel) { gfx.rbox(66, y - 2, 188, 24, 6, rgb(230, 60, 30)); gfx.rframe(66, y - 2, 188, 24, 6, C::YELLOW); }
      gfx.textShadow((SCR_W - Gfx::width(opts[i])) / 2, y + 2, opts[i], i == sel ? C::WHITE : C::LTGREY, C::BLACK);
    }
    char hint[64];
    snprintf(hint, sizeof(hint), TR("UP/DOWN choose   %s = select   hold = back", "UP/DOWN wählen   %s = los   halten = zurück"), okKeys());
    gfx.textShadow((SCR_W - Gfx::width(foot ? foot : hint, F_SMALL)) / 2, SCR_H - 14, foot ? foot : hint, C::LTGREY, C::BLACK, F_SMALL);
    gfx.send();
  }
  return 255;
}

static void kgBar(int16_t x, int16_t y, uint8_t v, col c) {
  for (uint8_t i = 0; i < 5; i++) gfx.fill(x + i * 13, y, 11, 7, i < v ? c : rgb(40, 40, 70));
}

// the driver: six of them, the chosen kart turning big; returns false when left
static bool kgPickDriver() {
  NoPause np;
  uint8_t sel = kgSv.drv;
  btnClear();
  uint32_t t0 = millis();
  while (poll(false)) {
    if (btn(B_LEFT))  { sel = (uint8_t)((sel + KG_KARTS - 1) % KG_KARTS); sfx(700, 15); }
    if (btn(B_RIGHT)) { sel = (uint8_t)((sel + 1) % KG_KARTS); sfx(700, 15); }
    if (btn(B_UP) || btn(B_DOWN)) { sel = (uint8_t)((sel + 3) % KG_KARTS); sfx(700, 15); }
    if (btn(B_OK)) { sfx(1200, 50); kgSv.drv = sel; kgSvSave(); return true; }
    uint32_t a = millis() - t0;
    gfx.gradient(0, 0, SCR_W, SCR_H, rgb(40, 0, 80), rgb(0, 0, 20));
    for (uint8_t i = 0; i < 12; i++) {                            // spot light rays
      int16_t x = (int16_t)(220 + kgSin((uint16_t)(a * 8 + i * 5461)) / 1200);
      gfx.line(220, 0, x, 170, rgb(70, 30, 120));
    }
    gfx.textShadow(10, 6, "CHOOSE YOUR DRIVER", C::WHITE, C::BLACK, F_BOLD);
    const KgDriver &D = KG_DRV[sel];
    // the turntable
    for (int16_t r = 0; r < 3; r++) gfx.disc(220, 150, (int16_t)(60 - r * 6), r == 0 ? rgb(60, 60, 90) : r == 1 ? rgb(90, 90, 130) : rgb(120, 120, 170));
    gfx.disc(220, 150, 48, rgb(120, 120, 170));
    col pal[16];
    kgKartPal(sel, pal);
    kgBlit(KG_KART[(a / 110) % 16], 220, 152, 640, pal);
    gfx.textShadow(220 - Gfx::width(D.name, F_BOLD) / 2, 172, D.name, C::YELLOW, C::BLACK, F_BOLD);
    static const char *const ST[4] = { "SPEED", "ACCEL", "HANDLING", "WEIGHT" };
    uint8_t v[4] = { D.speed, D.accel, D.handling, D.weight };
    for (uint8_t i = 0; i < 4; i++) {
      gfx.text(150, (int16_t)(196 + i * 10), ST[i], C::LTGREY, F_SMALL);
      kgBar(200, (int16_t)(197 + i * 10), v[i], D.body[1]);
    }
    // the six cards
    for (uint8_t i = 0; i < KG_KARTS; i++) {
      int16_t x = (int16_t)(8 + (i % 3) * 44), y = (int16_t)(44 + (i / 3) * 70);
      bool on = i == sel;
      gfx.rbox(x, y, 40, 62, 6, on ? KG_DRV[i].body[1] : rgb(30, 20, 60));
      gfx.rframe(x, y, 40, 62, 6, on ? C::WHITE : KG_DRV[i].body[2]);
      col p[16];
      kgKartPal(i, p);
      kgBlit(KG_KART[on ? (a / 110) % 16 : 14], x + 20, y + 40, 256, p);
      gfx.text(x + 20 - Gfx::width(KG_DRV[i].name, F_SMALL) / 2, y + 48, KG_DRV[i].name, on ? C::WHITE : C::LTGREY, F_SMALL);
    }
    gfx.text(10, 192, "arrows choose", C::GREY, F_SMALL);
    char b[24];
    snprintf(b, sizeof(b), "%s = race", okKeys());
    gfx.text(10, 204, b, C::GREY, F_SMALL);
    gfx.text(10, 216, "hold = back", C::GREY, F_SMALL);
    gfx.send();
  }
  return false;
}

// a track from above, from its points (no map needed): the menus' preview - its ground, the road
// with an edge in the kerbs' colour, the start line
static void kgOutline(uint8_t t, int16_t cx, int16_t cy, int16_t size) {
  const KgTrackDef &d = KG_TRACK[t];
  const KgTheme &th = KG_THEME[d.theme];
  int16_t n = (int16_t)(d.n * 8);
  for (uint8_t pass = 0; pass < 2; pass++)
    for (int16_t i = 0; i < n; i++) {
      float x, y;
      kgCurve(d, i / 8, (i % 8) / 8.f, x, y);
      int16_t sx = (int16_t)(cx + (x - 512) * size / 1024), sy = (int16_t)(cy + (y - 512) * size / 1024);
      if (pass == 0) gfx.disc(sx, sy, 4, th.mat[KM_KERBA][0]);
      else gfx.disc(sx, sy, 2, th.mat[KM_ROAD][2]);
    }
  float x, y;
  kgCurve(d, 0, 0, x, y);
  gfx.fill((int16_t)(cx + (x - 512) * size / 1024) - 1, (int16_t)(cy + (y - 512) * size / 1024) - 4, 3, 9, C::WHITE);
}

// a track for a single race or the time trial; 255 = back
static uint8_t kgPickTrack(bool tt) {
  NoPause np;
  static uint8_t sel = 0;
  btnClear();
  while (poll(false)) {
    if (btn(B_UP))   { sel = (uint8_t)((sel + 5) % 6); sfx(700, 15); }
    if (btn(B_DOWN)) { sel = (uint8_t)((sel + 1) % 6); sfx(700, 15); }
    if (btn(B_OK)) {
      if (kgTrackOpen(sel)) { sfx(1200, 50); return sel; }
      sfx(200, 120);
    }
    const KgTheme &th = KG_THEME[KG_TRACK[sel].theme];
    gfx.gradient(0, 0, SCR_W, SCR_H, th.sky[1], th.sky[0]);
    gfx.textShadow(10, 6, tt ? "TIME TRIAL" : "SINGLE RACE", C::WHITE, C::BLACK, F_BOLD);
    for (uint8_t i = 0; i < 6; i++) {
      int16_t y = (int16_t)(36 + i * 32);
      bool on = i == sel, open = kgTrackOpen(i);
      gfx.rbox(6, y, 150, 28, 6, on ? rgb(230, 60, 30) : rgb(0, 0, 40));
      if (on) gfx.rframe(6, y, 150, 28, 6, C::YELLOW);
      gfx.text(14, y + 6, open ? KG_TRACK[i].name : "? ? ?", open ? C::WHITE : C::GREY, F_TEXT);
    }
    bool open = kgTrackOpen(sel);
    gfx.rbox(166, 36, 148, 188, 8, open ? th.mat[KM_OUTA][th.outside == KM_PIT ? 0 : 1] : rgb(0, 0, 30));
    gfx.rframe(166, 36, 148, 188, 8, GREY3);
    if (open) {
      gfx.fill(170, 168, 140, 52, rgb(0, 0, 30));
      kgOutline(sel, 240, 104, 128);
      char b[32], c[20];
      gfx.text(176, 178, th.name, th.mat[KM_KERBA][0], F_SMALL);
      if (kgSv.best[sel]) { kgTime(c, sizeof(c), (uint32_t)kgSv.best[sel] * 10); snprintf(b, sizeof(b), "best %s", c); }
      else snprintf(b, sizeof(b), "best --");
      gfx.text(176, 192, b, C::YELLOW, F_SMALL);
      if (kgSv.lap[sel]) { kgTime(c, sizeof(c), (uint32_t)kgSv.lap[sel] * 10); snprintf(b, sizeof(b), "lap  %s", c); }
      else snprintf(b, sizeof(b), "lap  --");
      gfx.text(176, 206, b, C::LIME, F_SMALL);
    } else {
      gfx.center(100, "", C::WHITE);
      gfx.text(190, 110, "win a trophy", C::LTGREY, F_SMALL);
      gfx.text(190, 122, "in the SUN CUP", C::LTGREY, F_SMALL);
    }
    gfx.send();
  }
  return 255;
}

static uint8_t kgPickCup() {
  NoPause np;
  uint8_t sel = 0;
  btnClear();
  while (poll(false)) {
    if (btn(B_LEFT) || btn(B_RIGHT) || btn(B_UP) || btn(B_DOWN)) { sel ^= 1; sfx(700, 15); }
    if (btn(B_OK)) {
      if (sel == 0 || (kgSv.open & 2)) { sfx(1200, 50); return sel; }
      sfx(200, 120);
    }
    gfx.gradient(0, 0, SCR_W, SCR_H, rgb(0, 40, 120), rgb(0, 0, 30));
    char b[32];
    snprintf(b, sizeof(b), "GRAND PRIX %ucc", 50 + kgSv.cc * 50);
    gfx.textShadow(10, 6, b, C::WHITE, C::BLACK, F_BOLD);
    for (uint8_t c = 0; c < 2; c++) {
      int16_t x = (int16_t)(12 + c * 154);
      bool on = c == sel, open = c == 0 || (kgSv.open & 2);
      gfx.rbox(x, 40, 142, 180, 10, on ? rgb(200, 60, 20) : rgb(20, 20, 60));
      gfx.rframe(x, 40, 142, 180, 10, on ? C::YELLOW : GREY2);
      gfx.textShadow(x + 71 - Gfx::width(KG_CUP_NAME[c], F_BOLD) / 2, 48, KG_CUP_NAME[c], C::WHITE, C::BLACK, F_BOLD);
      uint8_t tr = kgSv.trophy[c][kgSv.cc];
      const col *tp = tr == 3 ? KP_GOLD : tr == 2 ? KP_SILVER : tr == 1 ? KP_BRONZE : NULL;
      if (tp) kgSpr(KG_TROPHY, x + 71, 140, tp, 2);
      else if (open) kgSpr(KG_TROPHY, x + 71, 140, KP_SILVER, 2, KB_GHOST);
      else { gfx.rbox(x + 54, 92, 34, 28, 4, GREY2); gfx.circle(x + 71, 92, 11, GREY2); gfx.circle(x + 71, 92, 10, GREY2); gfx.disc(x + 71, 104, 4, C::BLACK); }
      for (uint8_t t = 0; t < 3; t++)
        gfx.text(x + 71 - Gfx::width(open ? KG_TRACK[c * 3 + t].name : "? ? ?", F_SMALL) / 2, (int16_t)(160 + t * 14),
                 open ? KG_TRACK[c * 3 + t].name : "? ? ?", open ? C::WHITE : C::GREY, F_SMALL);
    }
    gfx.send();
  }
  return 255;
}

static const char *kgCcName(uint8_t c) { return c == 0 ? "50cc" : c == 1 ? "100cc" : "150cc"; }

void kartRun() {
  kg = (KgGame *)calloc(1, sizeof(KgGame));
  if (!kg) return;
  KgGame &G = *kg;
  G.T.id = 255;
  kgSvLoad();
  G.R.T = NULL;
  tiltGame = false;
  uint8_t sel = 0;
  for (;;) {
    char gas[24], cc[24];
    snprintf(gas, sizeof(gas), "GAS: %s", kgSv.gas ? "HOLD UP" : "AUTO");
    snprintf(cc, sizeof(cc), "CLASS: %s", kgCcName(kgSv.cc));
    const char *const OPT[5] = { "GRAND PRIX", "SINGLE RACE", "TIME TRIAL", cc, gas };
    sel = kgMenu(G, OPT, 5, sel);
    if (sel == 255) break;
    if (sel == 3) { kgSv.cc = (uint8_t)((kgSv.cc + 1) % 3); kgSvSave(); continue; }
    if (sel == 4) { kgSv.gas ^= 1; kgSvSave(); continue; }
    if (!kgPickDriver()) continue;
    uint8_t r = 0;
    if (sel == 0) {
      uint8_t cup = kgPickCup();
      if (cup == 255) continue;
      r = kgCupRun(G, cup);
    } else {
      uint8_t t = kgPickTrack(sel == 2);
      if (t == 255) continue;
      if (sel == 2) {
        G.mode = KG_TT;
        G.gh = (KgGhost *)calloc(1, sizeof(KgGhost));
        G.rec = (KgGhost *)calloc(1, sizeof(KgGhost));
        if (G.gh && G.rec) while ((r = kgRace(G, t)) == 1) {}
        free(G.gh); free(G.rec);
        G.gh = G.rec = NULL;
      } else {
        G.mode = KG_SINGLE;
        G.race = 0;
        uint8_t n = 0;
        for (uint8_t d = 0; d < KG_KARTS; d++) if (d != kgSv.drv) G.grid[n++] = d;
        G.grid[n] = kgSv.drv;
        while ((r = kgRace(G, t)) == 1) {}
      }
    }
    G.mode = KG_SINGLE;                                           // the title's race starts anew
    if (r == 2) break;
  }
  tiltGame = true;
  free(G.T.tdat);
  free(kg);
  kg = NULL;
}

static const char *const IC_KART[12] = {
  "............", "...wwww.....", "..wbbbbw....", "..wbccbw....", "...rrrr.....", ".kkrrrrrrkk.",
  ".kkrrrrrrkk.", "..rrrrrrrr..", ".yyrrrrrryy.", ".kkyyyyyykk.", ".kk......kk.", "............" };
