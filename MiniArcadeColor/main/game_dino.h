// MiniArcade Color: Dino. Part of console.cpp, which includes it.
// The offline runner of the browser, in a desert with parallax hills and
// a day that turns to night every 350 points. UP or OK jumps - a tap
// hops, holding the key jumps high - DOWN ducks, or drops fast in the
// air. Low birds are jumped over, middle ones ducked under, high ones fly
// past. It gets a little faster with the score.

#define DNO_GY     206                 // ground line (the dino's feet)
#define DNO_X      30                  // dino left
#define DNO_OBS    3
#define DNO_CLOUDS 4
#define DNO_TICK   15                  // ms per step
#define DNO_HS     35                  // height standing / ducking
#define DNO_HD     17
#define DNO_VMIN   26                  // speed: 26 = about 175 px/s ...
#define DNO_VMAX   56                  // ... 56 = about 380 px/s
#define DNO_MOVE   13                  // 1/128 px per step and speed unit

/* The jump of the OLED console, 2.5 times as big and in steps of 15 ms
   (all in 1/128 px): a tap gives a short hop, holding the key a high
   jump (about 60 px), DOWN in the air drops fast. Holding the key on the
   ground jumps again at once.                                          */
#define DNO_V0     780                 // speed at take-off (a little more at high speed)
#define DNO_G      45                  // gravity per step
#define DNO_DROP   360                 // let go: the climb slows to this at once
#define DNO_MINH   (45 * 64)           // a jump always rises this far (22.5 px)
#define DNO_MAXH   (95 * 64)           // from here on it ends like a let-go jump

struct DnoJump {
  int32_t y, vy;                       // height (negative = up) and speed, 1/128 px
  bool    air, reached, drop;          // in the air, past the minimum, fast drop
};

// one step of the dino: held = jump key down, down = DOWN key down
static void dnoStep(DnoJump &j, bool held, bool down, uint16_t spd) {
  if (!j.air) {
    if (!held) return;
    j.air = true; j.reached = j.drop = false;
    j.vy = -(DNO_V0 + spd / 6 * 10);
  }
  if (down && !j.drop) { j.drop = true; j.vy = 160; }       // speed drop
  j.y += j.drop ? j.vy * 3 : j.vy;
  j.vy += DNO_G;
  if (-j.y >= DNO_MINH || j.drop) j.reached = true;
  if (((!held && j.reached) || -j.y >= DNO_MAXH) && j.vy < -DNO_DROP) j.vy = -DNO_DROP;
  if (j.y >= 0) { j.y = 0; j.vy = 0; j.air = false; }
}

/* obstacle kinds: 0 = one small cactus, 1 = two small, 2 = big cactus,
   3 = pterodactyl (only once the score got past 30), at three heights:
   low ones must be jumped over, middle ones ducked under, high ones fly
   past.                                                               */
static int16_t dnoW(uint8_t t) { return t == 1 ? 32 : (t == 0 ? 15 : 22); }
static int16_t dnoH(uint8_t t) { return t == 2 ? 32 : (t == 3 ? 15 : 22); }
static int16_t dnoTop(uint8_t t, uint8_t h) {
  if (t != 3) return DNO_GY - dnoH(t);
  return h == 0 ? DNO_GY - 30 : (h == 1 ? DNO_GY - 42 : DNO_GY - 70);
}

// does the dino touch an obstacle (x in 1/128 px)?
static bool dnoHit(const DnoJump &j, bool duck, int32_t oxq, uint8_t t, uint8_t h) {
  int16_t dh = duck ? DNO_HD : DNO_HS;
  int16_t top = (int16_t)(DNO_GY - dh + j.y / 128);
  int16_t ox = (int16_t)(oxq >> 7), oy = dnoTop(t, h);
  return ox < DNO_X + (duck ? 30 : 20) && ox + dnoW(t) > DNO_X + 3 && oy < top + dh && oy + dnoH(t) > top;
}

/* Distance between two obstacles (px, the game adds up to 99 more).
   The "dinofair" test tries every way to press the keys for every pair
   and every gap at every speed - all can be passed.                  */
static int16_t dnoGap(uint16_t spd) {
  static const uint8_t g[8] = { 100, 100, 100, 100, 108, 118, 130, 140 };
  uint8_t v = (uint8_t)(spd / 8);
  return g[v > 7 ? 7 : v];
}

// ---------------- drawing ----------------
static const char *const DNO_BODY[15] = {
  "......dggggg", ".....dgwkggg", ".....dgggggg", ".....dgggggg", ".....dggg...",
  ".....dgggrr.", "d...dgggg...", "d..dggggggg.", "dd.dgggggg.g", "ddddgllgg...",
  ".dddglllg...", "..ddglllg...", "...dggggg...", "....dgggg...", "....dg.dg..." };
static const char *const DNO_LEGS[3][3] = {
  { "....dg..g...", "....dg..gg..", "....dgg....." },
  { "....d...dg..", "...dg...dg..", "........dgg." },
  { "....dg..dg..", "....dg..dg..", "....dgg.dgg." } };
static const char *const DNO_DUCK[2][9] = {
  { "..........dggggg", "d.......ddgwkggg", "dd.dddddgggggggg", "ddddgggggggggrr.",
    ".dddglllllggg...", "..dgglllllggg.g.", "...dggggggg.....", "....dg..dg......", "....dgg........." },
  { "..........dggggg", "d.......ddgwkggg", "dd.dddddgggggggg", "ddddgggggggggrr.",
    ".dddglllllggg...", "..dgglllllggg.g.", "...dggggggg.....", "....d...dg......", "...dg....dgg...." } };
static const char DNO_KEYS[] = "gdlwkr";

static const char *const DNO_BIRD[2][8] = {
  { ".....ppp....", ".....pppp...", "..r..ppppp..", ".rppkpppppp.", "yyppppppppp.",
    "...ppppp....", "............", "............" },
  { "............", "............", "..r.........", ".rppkpppppp.", "yypppppppppp",
    "....ppppp...", ".....pppp...", ".....ppp...." } };
static const char DNO_BKEYS[] = "pkry";

// a ridge line: the height at x from two triangle waves
static int16_t dnoRidge(int32_t x, int16_t p1, int16_t a1, int16_t p2, int16_t a2) {
  int32_t u = x % p1, v = x % p2;
  return (int16_t)(labs(u * 2 - p1) * a1 / p1 + labs(v * 2 - p2) * a2 / p2);
}

static void dnoCactus(int16_t x, int16_t w, int16_t h, uint8_t night) {
  col c = colLerp(rgb(40, 170, 60), rgb(20, 70, 50), night), dk = colDark(c), lt = colLight(c);
  int16_t tw = w >= 20 ? 8 : 6, tx = x + (w - tw) / 2, top = DNO_GY - h;
  gfx.rbox(x, top + h / 5, 4, h / 3, 2, c);                     // left arm
  gfx.fill(x, top + h / 5 + h / 3 - 4, tx - x + 1, 4, c);
  gfx.rbox(x + w - 4, top + h / 3, 4, h / 4 + 2, 2, c);         // right arm
  gfx.fill(tx + tw - 1, top + h / 3 + h / 4 - 2, x + w - 4 - (tx + tw - 1) + 1, 4, c);
  gfx.rbox(tx, top, tw, h + 2, tw / 2, c);                      // trunk
  gfx.vline(tx + 2, top + 3, h - 4, lt);
  gfx.vline(tx + tw - 2, top + 3, h - 3, dk);
  for (int16_t y = top + 5; y < DNO_GY - 2; y += 5) gfx.pixel(tx + (y & 4 ? 1 : tw - 3), y, dk);
}

static void dnoCloud(int16_t x, int16_t y, col c) {
  gfx.disc(x + 10, y + 8, 8, c); gfx.disc(x + 24, y + 4, 11, c); gfx.disc(x + 38, y + 8, 8, c);
  gfx.rbox(x + 2, y + 8, 44, 9, 4, c);
}

void dinoRun() {
  bool again = true;
  while (again) {
    again = false;
    DnoJump  jp = { 0, 0, false, false, false };
    bool     tapped = false, dead = false, duck = false;
    int32_t  ox[DNO_OBS];                                   // obstacles, 1/128 px
    uint8_t  ot[DNO_OBS], oh[DNO_OBS];
    int16_t  cx[DNO_CLOUDS], cy[DNO_CLOUDS];                // clouds, 1/4 px
    uint16_t score = 0, spd = DNO_VMIN, flash = 0, nextFlash = 100;
    uint32_t dist = 0, scroll = 0, tick = 0;                // scroll in 1/128 px
    bool     night = false;
    uint8_t  dusk = 0;                                      // 0 = day .. 255 = night
    for (uint8_t i = 0; i < DNO_OBS; i++) {
      ox[i] = (int32_t)(SCR_W + 80 + i * 170) << 7;
      ot[i] = (uint8_t)random(3);
      oh[i] = 0;
    }
    for (uint8_t i = 0; i < DNO_CLOUDS; i++) { cx[i] = (int16_t)(random(SCR_W) * 4); cy[i] = (int16_t)(40 + random(70)); }
    uint32_t next = gameMillis();
    btnClear();

    while (poll()) {
      if (btnTap(B_UP) || btnTap(B_OK)) tapped = true;     // a quick tap still counts at the next step
      bool hold = btnHeld(B_UP) || btnHeld(B_OK), down = btnHeld(B_DOWN);
      uint32_t now = gameMillis();
      for (uint8_t k = 0; k < 3 && !dead && (int32_t)(now - next) >= 0; k++) {   // at most 45 ms per frame
        next += DNO_TICK;
        tick++;
        bool was = jp.air;
        dnoStep(jp, tapped || hold, down, spd);
        tapped = false;
        if (jp.air && !was) sfx(700, 35);
        duck = down && !jp.air;
        dist += spd;
        score = dist / 200 > 65535 ? 65535 : (uint16_t)(dist / 200);
        if (spd < DNO_VMAX) spd = (uint16_t)(DNO_VMIN + score / 20);
        if (score >= nextFlash) { nextFlash += 100; flash = 70; sfx(1150, 120); }
        if (flash) flash--;
        if (score && score % 350 == 0 && dist / 200 != (dist - spd) / 200) night = !night;
        int32_t mv = (int32_t)spd * DNO_MOVE;
        scroll += (uint32_t)mv;
        for (uint8_t i = 0; i < DNO_OBS; i++) {
          ox[i] -= mv;
          if (ox[i] < -(40 << 7)) {                          // recycle behind the screen
            int32_t far = ox[0];
            for (uint8_t q = 1; q < DNO_OBS; q++) if (ox[q] > far) far = ox[q];
            ox[i] = far + ((int32_t)(dnoGap(spd) + random(100)) << 7);
            ot[i] = (uint8_t)((score > 30 && random(4) == 0) ? 3 : random(3));
            oh[i] = (uint8_t)(ot[i] == 3 ? random(3) : 0);    // three flight heights
          }
          if (dnoHit(jp, duck, ox[i], ot[i], oh[i])) dead = true;
        }
        if (tick % 3 == 0)
          for (uint8_t i = 0; i < DNO_CLOUDS; i++)            // clouds drift slowly
            if (--cx[i] < -60 * 4) { cx[i] = (int16_t)((SCR_W + random(60)) * 4); cy[i] = (int16_t)(40 + random(70)); }
      }
      if ((int32_t)(now - next) > 0) next = now;
      if (dead) sfx(150, 400);
      if (night && dusk < 255) dusk = (uint8_t)(dusk > 250 ? 255 : dusk + 5);
      if (!night && dusk > 0) dusk = (uint8_t)(dusk < 5 ? 0 : dusk - 5);

      // ---------------- draw ----------------
      statusBar("DINO", score, curHigh, C::GREEN);
      int32_t sx = (int32_t)(scroll >> 7);
      gfx.gradient(0, TOP_H, SCR_W, DNO_GY - TOP_H, colLerp(rgb(90, 170, 255), rgb(10, 10, 50), dusk),
                   colLerp(rgb(200, 230, 255), rgb(70, 40, 110), dusk));
      if (dusk > 128)                                         // stars
        for (uint8_t i = 0; i < 40; i++) {
          uint32_t hsh = (uint32_t)(i + 1) * 2654435761u;
          gfx.pixel((int16_t)((hsh >> 8) % SCR_W), (int16_t)(TOP_H + (hsh >> 20) % 110), i % 3 ? C::LTGREY : C::WHITE);
        }
      if (dusk < 128) {                                        // sun goes down ...
        int16_t sy = 62 + dusk / 2;
        gfx.disc(258, sy, 18, rgb(255, 240, 120)); gfx.disc(258, sy, 14, C::YELLOW);
      } else {                                                 // ... the moon comes up
        int16_t my = 62 + (255 - dusk) / 2;
        gfx.disc(258, my, 14, rgb(230, 230, 200));
        gfx.disc(264, my - 4, 12, colLerp(rgb(90, 170, 255), rgb(10, 10, 50), dusk));
      }
      col cl = colLerp(C::WHITE, rgb(80, 80, 120), dusk);
      for (uint8_t i = 0; i < DNO_CLOUDS; i++) dnoCloud(cx[i] / 4, cy[i], cl);
      col far = colLerp(rgb(170, 140, 200), rgb(25, 15, 55), dusk), farTop = colLerp(C::WHITE, rgb(110, 100, 150), dusk);
      col dune = colLerp(rgb(225, 160, 90), rgb(90, 60, 80), dusk);
      for (int16_t i = 0; i < SCR_W; i++) {                    // two layers of hills, slower than the ground
        int16_t h = (int16_t)(26 + dnoRidge(sx / 6 + i, 230, 56, 97, 20));
        gfx.vline(i, DNO_GY - 6 - h, h + 6, far);
        if (h > 72) gfx.vline(i, DNO_GY - 6 - h, (h - 72) / 2 + 1, farTop);   // snow on the high peaks
        int16_t d = (int16_t)(6 + dnoRidge(sx / 2 + i, 150, 22, 61, 9));
        gfx.vline(i, DNO_GY - d, d, dune);
      }
      col sand = colLerp(rgb(245, 215, 150), rgb(90, 80, 100), dusk), grit = colDark(sand);
      gfx.fill(0, DNO_GY, SCR_W, SCR_H - DNO_GY, sand);
      gfx.hline(0, DNO_GY, SCR_W, colDark(sand, 2));
      for (int32_t wx = sx - sx % 12; wx < sx + SCR_W + 12; wx += 12) {   // pebbles roll by with the ground
        uint32_t hsh = (uint32_t)wx * 2654435761u;
        int16_t px = (int16_t)(wx - sx + (hsh >> 29)), py = (int16_t)(DNO_GY + 4 + (hsh >> 24) % 28);
        gfx.hline(px, py, 2 + (int16_t)((hsh >> 20) & 3), grit);
      }

      for (uint8_t i = 0; i < DNO_OBS; i++) {                 // obstacles
        int16_t x = (int16_t)(ox[i] >> 7);
        if (x > SCR_W || x < -40) continue;
        if (ot[i] == 3) {
          col bp[4] = { colLerp(rgb(180, 80, 200), rgb(110, 60, 140), dusk), C::BLACK, C::RED, C::ORANGE };
          gfx.sprite(x - 1, dnoTop(3, oh[i]), DNO_BIRD[(tick / 12) & 1], 8, DNO_BKEYS, bp, 2);
        } else if (ot[i] == 1) {
          dnoCactus(x, 15, 22, dusk); dnoCactus(x + 17, 15, 18, dusk);
        } else dnoCactus(x, dnoW(ot[i]), dnoH(ot[i]), dusk);
      }

      {                                                        // the dino itself
        col dp[6] = { rgb(90, 190, 70), rgb(40, 120, 50), rgb(190, 240, 140), C::WHITE, C::BLACK, C::RED };
        if (dead) dp[4] = C::RED;
        int16_t lift = (int16_t)(jp.y / 128);
        if (duck) gfx.sprite(DNO_X, DNO_GY - 18, DNO_DUCK[(tick / 6) & 1], 9, DNO_KEYS, dp, 2);
        else {
          int16_t top = DNO_GY - 36 + lift;
          gfx.sprite(DNO_X, top, DNO_BODY, 15, DNO_KEYS, dp, 2);
          uint8_t lf = jp.air ? 2 : (uint8_t)((tick / 6) & 1);
          gfx.sprite(DNO_X, top + 30, DNO_LEGS[lf], 3, DNO_KEYS, dp, 2);
        }
      }
      if (flash && (flash / 8) % 2) {                          // every 100 points
        char b[12];
        snprintf(b, sizeof(b), "%u!", (unsigned)(nextFlash - 100));
        gfx.textShadow((SCR_W - Gfx::width(b, F_BOLD)) / 2, TOP_H + 12, b, C::YELLOW, C::BLACK, F_BOLD);
      }
      gfx.send();
      if (dead) { delay(700); break; }
    }
    if (!dead) return;                                         // left from the pause menu
    again = gameOver(score);
  }
}

static const char *const IC_DINO[12] = {
  "......gggg..", ".....gwkggg.", ".....gggggg.", ".....ggg....", "g...ggggg...", "gg.gggggg...",
  ".gggggggg.d.", "..ggggggg.d.", "...gg.gg.dd.", "...g..g...d.", "...gg.gg..d.", "nnnnnnnnnnnn" };
