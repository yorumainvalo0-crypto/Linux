// MiniArcade Color: Fruit Pot. Part of console.cpp, which includes it.
// Fruits drop into a pot: LEFT / RIGHT (or tilting) move the dropper, OK
// or DOWN lets go. Two equal fruits that touch become the next bigger one
// at their middle - cherry, strawberry, grape, lemon, orange, apple,
// peach, pineapple, coconut, melon; two melons vanish. Only the five
// smallest come from the dropper. Over when a fruit stays above the red
// line for two seconds.
// Physics in whole numbers (1/256 px), in fixed 10 ms steps whatever the
// frame rate: Verlet movement, then several passes that push overlapping
// circles apart (the lighter one more) and keep them inside the walls.
// A push is limited per pass, so overlaps resolve without explosions;
// contact damping and a rest snap let a stack settle still.

#define FR_MAX    40
#define FR_KINDS  10
#define FR_L      80                   // inside of the pot: walls and floor (px)
#define FR_R     240
#define FR_FLOOR 232
#define FR_LINE   64                   // the danger line
#define FR_RAIL   30                   // the dropper
#define FR_STEP   10                   // ms per physics step
#define FR_G      23                   // gravity: 1/256 px per step per step (about 900 px/s^2)
#define FR_VMAX 2048                   // at most 8 px a step: nothing passes through
#define FR_PUSH  768                   // at most 3 px apart per pair and pass
#define FR_IT      6                   // passes per step
#define FR_OVER  200                   // steps above the line that end the game

static const uint8_t FR_RAD[FR_KINDS] = { 7, 9, 12, 15, 18, 22, 26, 30, 35, 40 };
static const uint8_t FR_PTS[FR_KINDS] = { 1, 3, 6, 10, 15, 21, 28, 36, 45, 55 };
// body, shade, light
static const col FR_COL[FR_KINDS][3] = {
  { rgb(212, 0, 51),   rgb(127, 0, 25),   rgb(255, 127, 153) },   // cherry
  { rgb(255, 51, 85),  rgb(170, 0, 51),   rgb(255, 170, 170) },   // strawberry
  { rgb(153, 51, 204), rgb(85, 0, 127),   rgb(204, 153, 255) },   // grape
  { rgb(255, 230, 51), rgb(204, 153, 0),  rgb(255, 255, 170) },   // lemon
  { rgb(255, 140, 0),  rgb(204, 85, 0),   rgb(255, 204, 102) },   // orange
  { rgb(153, 212, 51), rgb(51, 127, 0),   rgb(204, 255, 153) },   // apple
  { rgb(255, 170, 153), rgb(212, 102, 85), rgb(255, 230, 204) },  // peach
  { rgb(255, 190, 0),  rgb(170, 102, 0),  rgb(255, 230, 102) },   // pineapple
  { rgb(153, 85, 51),  rgb(85, 42, 0),    rgb(204, 153, 102) },   // coconut
  { rgb(51, 170, 51),  rgb(0, 85, 0),     rgb(153, 230, 102) } }; // melon
static const char *const FR_NAME[FR_KINDS] = { "cherry", "strawberry", "grape", "lemon", "orange",
                                               "apple", "peach", "pineapple", "coconut", "melon" };

struct Fruit {
  int32_t  x, y, ox, oy;               // now and one step ago, 1/256 px (Verlet)
  int16_t  rad;                        // radius, 1/256 px (grows after a merge)
  uint8_t  kind, touch;                // touch: something pressed on it this step
  uint16_t age, over;                  // steps alive; steps above the line
};
struct FrState {
  Fruit    f[FR_MAX];
  uint8_t  n, top;                     // fruits; the biggest kind made
  bool     noMerge;                    // the tests: physics only
  uint16_t merges, danger;             // danger: the longest a fruit is above the line
  uint32_t score;
};
static FrState *fr = NULL;
static int32_t  frDropX;                // the dropper, 1/256 px

static uint32_t frSqrt(uint32_t v) {
  uint32_t r = 0, b = 1u << 30;
  while (b > v) b >>= 2;
  while (b) {
    if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1;
    b >>= 2;
  }
  return r;
}

static bool frAdd(uint8_t kind, int32_t x, int32_t y) {           // x, y in 1/256 px
  if (fr->n >= FR_MAX) return false;
  Fruit &a = fr->f[fr->n++];
  memset(&a, 0, sizeof(a));
  a.x = a.ox = x; a.y = a.oy = y;
  a.rad = (int16_t)(FR_RAD[kind] << 8);
  a.kind = kind;
  return true;
}
static void frRemove(uint8_t i) { fr->f[i] = fr->f[--fr->n]; }

// two circles apart, by their weight (area): the lighter one moves more
static void frPair(Fruit &a, Fruit &b, bool last) {
  int32_t dx = b.x - a.x, dy = b.y - a.y, rs = a.rad + b.rad;
  if (dx >= rs || dx <= -rs || dy >= rs || dy <= -rs) return;
  uint32_t d2 = (uint32_t)(dx * dx) + (uint32_t)(dy * dy);
  if (d2 >= (uint32_t)(rs * rs)) return;
  int32_t d = (int32_t)frSqrt(d2);
  if (d < 16) { dx = 256; dy = -64; d = 264; }                    // on top of each other: a fixed way out
  int32_t pen = rs - d;
  if (pen > FR_PUSH) pen = FR_PUSH;
  int32_t cx = dx * pen / d, cy = dy * pen / d;
  int32_t ma = (a.rad >> 8) * (a.rad >> 8), mb = (b.rad >> 8) * (b.rad >> 8), m = ma + mb;
  a.x -= cx * mb / m; a.y -= cy * mb / m;
  b.x += cx * ma / m; b.y += cy * ma / m;
  if (last) { a.touch = b.touch = 1; }
}

static void frWalls(Fruit &a) {
  int32_t lo = (FR_L << 8) + a.rad, hi = (FR_R << 8) - a.rad, fl = (FR_FLOOR << 8) - a.rad;
  if (a.x < lo) { a.x = lo; a.touch = 1; }
  if (a.x > hi) { a.x = hi; a.touch = 1; }
  if (a.y > fl) { a.y = fl; a.touch = 2; }
}

// two equal fruits touching become the next one (two melons vanish); false = none
static bool frMergeOne() {
  FrState &s = *fr;
  for (uint8_t i = 0; i < s.n; i++)
    for (uint8_t j = (uint8_t)(i + 1); j < s.n; j++) {
      Fruit &a = s.f[i], &b = s.f[j];
      if (a.kind != b.kind) continue;
      int32_t dx = b.x - a.x, dy = b.y - a.y, rs = a.rad + b.rad + 256;
      if (dx >= rs || dx <= -rs || dy >= rs || dy <= -rs) continue;
      if ((uint32_t)(dx * dx) + (uint32_t)(dy * dy) >= (uint32_t)(rs * rs)) continue;
      int16_t mx = (int16_t)(((a.x + b.x) / 2) >> 8), my = (int16_t)(((a.y + b.y) / 2) >> 8);
      s.merges++;
      if (a.kind == FR_KINDS - 1) {                              // two melons: gone with a bang
        s.score += 100;
        fxExplode(mx, my, 3);
        fxBurst(mx, my, 16, FR_COL[9][2], C::RED, 220, 700, 30, 3);
        fxShake(5, 400);
        fxFlash(C::WHITE, 220, 150);
        fxText(mx, my - 20, "+100", C::YELLOW, F_BOLD);
        sfx(1800, 250);
        frRemove(j); frRemove(i);
        return true;
      }
      uint8_t k = (uint8_t)(a.kind + 1);
      int32_t vx = ((a.x - a.ox) + (b.x - b.ox)) / 2, vy = ((a.y - a.oy) + (b.y - b.oy)) / 2;
      a.x = (a.x + b.x) / 2; a.y = (a.y + b.y) / 2;
      a.ox = a.x - vx; a.oy = a.y - vy;
      if (b.rad > a.rad) a.rad = b.rad;                          // grows from here to its size
      a.kind = k; a.over = 0;
      if (b.age > a.age) a.age = b.age;
      s.score += FR_PTS[k];
      if (k > s.top) s.top = k;
      fxBurst(mx, my, (uint8_t)(6 + k), FR_COL[k][2], FR_COL[k][1], (int16_t)(90 + k * 12), 450, 10, 2);
      fxRing(mx, my, (uint8_t)(FR_RAD[k] + 6), C::WHITE, FR_COL[k][0], 260);
      char t[8];
      snprintf(t, sizeof(t), "+%u", FR_PTS[k]);
      fxText(mx, my - FR_RAD[k], t, C::YELLOW, k >= 6 ? F_BOLD : F_SMALL);
      if (k >= 7) fxShake(2, 200);
      sfx((uint16_t)(500 + k * 110), 50);
      frRemove(j);
      return true;
    }
  return false;
}

static int32_t frClamp(int32_t v, int32_t m) { return v > m ? m : v < -m ? -m : v; }

static void frStep() {
  FrState &s = *fr;
  for (uint8_t i = 0; i < s.n; i++) {                           // move: Verlet with a little drag
    Fruit &a = s.f[i];
    int32_t vx = a.x - a.ox, vy = a.y - a.oy;
    vx -= vx / 128; vy -= vy / 128;
    vy += FR_G;
    vx = frClamp(vx, FR_VMAX); vy = frClamp(vy, FR_VMAX);
    a.ox = a.x; a.oy = a.y;
    a.x += vx; a.y += vy;
    int16_t full = (int16_t)(FR_RAD[a.kind] << 8);
    if (a.rad < full) a.rad = (int16_t)(a.rad + 40 < full ? a.rad + 40 : full);
    a.touch = 0;
    if (a.age < 60000) a.age++;
  }
  for (uint8_t it = 0; it < FR_IT; it++) {                      // overlaps apart, walls
    bool last = it == FR_IT - 1;
    for (uint8_t i = 0; i < s.n; i++)
      for (uint8_t j = (uint8_t)(i + 1); j < s.n; j++) frPair(s.f[i], s.f[j], last);
    for (uint8_t i = 0; i < s.n; i++) frWalls(s.f[i]);
  }
  s.danger = 0;
  for (uint8_t i = 0; i < s.n; i++) {                           // contact friction, rest, the line
    Fruit &a = s.f[i];
    if (a.touch) {
      int32_t vx = a.x - a.ox, vy = a.y - a.oy;
      vx -= vx / (a.touch == 2 ? 6 : 16); vy -= vy / 16;          // the floor holds more
      vx = frClamp(vx, 768); vy = frClamp(vy, 768);               // pushed out of an overlap: no fling
      if (vx >= -3 && vx <= 3 && vy >= -3 && vy <= 3) vx = vy = 0; // resting
      a.ox = a.x - vx; a.oy = a.y - vy;
    }
    if (a.age > 100 && a.y - a.rad < (FR_LINE << 8)) a.over++; else a.over = 0;
    if (a.over > s.danger) s.danger = a.over;
  }
  if (!s.noMerge) for (uint8_t k = 0; k < 4 && frMergeOne(); k++) {}
}

// ---------------- drawing ----------------
static void frFruit(uint8_t k, int16_t cx, int16_t cy, int16_t r) {
  const col *c = FR_COL[k];
  gfx.disc(cx, cy, r, c[1]);                                    // shade, then the lit body
  gfx.disc(cx - r / 8, cy - r / 8, r - r / 5, c[0]);
  const col leaf = rgb(51, 170, 51), stem = rgb(102, 51, 0);
  switch (k) {
  case 1:                                                       // strawberry: seeds
    for (int8_t q = 0; q < 6; q++) gfx.pixel(cx + (q % 3 - 1) * r / 2, cy + (q / 3) * r / 2 + (q % 2) - 1, rgb(255, 230, 102));
    break;
  case 2:                                                       // grape: a bunch
    gfx.circle(cx - r / 3, cy - r / 4, r / 3, c[2]); gfx.circle(cx + r / 3, cy - r / 4, r / 3, c[2]);
    gfx.circle(cx, cy + r / 3, r / 3, c[2]);
    break;
  case 3: case 4:                                               // lemon, orange: dimples
    for (int8_t q = 0; q < 5; q++) gfx.pixel(cx + (q * 7 % 5 - 2) * r / 4, cy + (q * 3 % 5 - 2) * r / 4 + r / 6, c[1]);
    break;
  case 6:                                                       // peach: the crease
    for (int16_t dy = -r + 3; dy < r / 2; dy++) gfx.pixel(cx + r / 5 + dy * dy / (3 * r), cy + dy, c[1]);
    break;
  case 7:                                                       // pineapple: diamonds
    for (int16_t dy = -r + 3; dy <= r - 3; dy++) {
      int16_t w = (int16_t)frSqrt((uint32_t)((r - 3) * (r - 3) - dy * dy));
      for (int16_t dx = -w; dx <= w; dx++) if (((dx + dy + 64) & 7) == 0 || ((dx - dy + 64) & 7) == 0) gfx.pixel(cx + dx, cy + dy, c[1]);
    }
    break;
  case 8:                                                       // coconut: fibres and three eyes
    for (int8_t q = 0; q < 14; q++) {
      int16_t a = (int16_t)(q * 37 % 32), d = (int16_t)(r * (3 + q % 5) / 9);
      gfx.pixel(cx + fxCos((uint8_t)a) * d / 64, cy + fxSin((uint8_t)a) * d / 64, c[2]);
    }
    gfx.disc(cx - r / 5, cy - r / 6, r / 9, c[1]); gfx.disc(cx + r / 5, cy - r / 6, r / 9, c[1]); gfx.disc(cx, cy + r / 6, r / 9, c[1]);
    break;
  case 9:                                                       // melon: stripes
    for (int16_t dy = -r + 2; dy <= r - 2; dy++) {
      int16_t w = (int16_t)frSqrt((uint32_t)(r * r - dy * dy)) - 1;
      for (int8_t q = -2; q <= 2; q++) {
        int16_t x = cx + q * w * 2 / 5 + ((dy / 4) & 1);
        gfx.hline(x, cy + dy, 2, c[1]);
      }
    }
    break;
  }
  gfx.disc(cx - r * 2 / 5, cy - r * 2 / 5, r / 4, c[2]);        // the shine
  if (r >= 9) gfx.disc(cx - r / 2, cy - r / 2, r / 10, C::WHITE); else gfx.pixel(cx - r / 2, cy - r / 2, C::WHITE);
  switch (k) {                                                  // on top: stems, leaves, crowns
  case 0:
    gfx.line(cx, cy - r + 1, cx + 3, cy - r - 5, stem);
    gfx.fill(cx + 3, cy - r - 6, 3, 2, leaf);
    break;
  case 1:
    gfx.hline(cx - r / 2, cy - r + 1, r, leaf);
    gfx.pixel(cx - r / 2 + 1, cy - r, leaf); gfx.pixel(cx, cy - r - 1, leaf); gfx.pixel(cx + r / 2 - 1, cy - r, leaf);
    break;
  case 2: case 5: case 6:
    gfx.line(cx, cy - r + 2, cx + 1, cy - r - 3, stem);
    gfx.disc(cx + r / 4 + 2, cy - r - 1, r / 6 + 1, leaf);
    break;
  case 3: case 4:
    gfx.fill(cx - 1, cy - r, 3, 2, rgb(0, 102, 0));
    gfx.disc(cx + 3, cy - r - 1, 2, leaf);
    break;
  case 7:
    for (int8_t q = -1; q <= 1; q++) gfx.triangle(cx + q * 6 - 4, cy - r + 3, cx + q * 8, cy - r - 9 + (q ? 3 : 0), cx + q * 6 + 4, cy - r + 3, q ? leaf : rgb(0, 127, 0));
    break;
  }
}

static void frDraw(int32_t dropX, int8_t hold, uint8_t nxt, uint32_t now) {
  FrState &s = *fr;
  gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(0, 42, 51), C::BLACK);
  // the pot: dark inside, clay walls with a rim
  const col clay = rgb(204, 102, 51), clayL = rgb(255, 170, 102), clayD = rgb(127, 42, 0);
  gfx.gradient(FR_L, FR_LINE - 8, FR_R - FR_L, FR_FLOOR - FR_LINE + 8, rgb(51, 42, 51), rgb(25, 0, 25));
  gfx.fill(FR_L - 8, FR_LINE - 4, 8, FR_FLOOR - FR_LINE + 12, clay);
  gfx.fill(FR_R, FR_LINE - 4, 8, FR_FLOOR - FR_LINE + 12, clay);
  gfx.fill(FR_L - 8, FR_FLOOR, FR_R - FR_L + 16, SCR_H - FR_FLOOR, clay);
  gfx.vline(FR_L - 7, FR_LINE - 4, FR_FLOOR - FR_LINE + 10, clayL); gfx.vline(FR_R + 1, FR_LINE - 4, FR_FLOOR - FR_LINE + 4, clayL);
  gfx.vline(FR_L - 1, FR_LINE - 4, FR_FLOOR - FR_LINE + 4, clayD); gfx.vline(FR_R + 7, FR_LINE - 4, FR_FLOOR - FR_LINE + 12, clayD);
  gfx.hline(FR_L, FR_FLOOR, FR_R - FR_L, clayD);
  gfx.rbox(FR_L - 12, FR_LINE - 9, 16, 7, 3, clayL); gfx.rbox(FR_R - 4, FR_LINE - 9, 16, 7, 3, clayL);
  // the danger line: red and dashed; blinking while a fruit is over it
  bool warn = s.danger > 20;
  col lc = warn && (now / 150) % 2 ? C::WHITE : (warn ? C::RED : C::DKRED);
  for (int16_t x = FR_L; x < FR_R; x += 8) gfx.hline(x, FR_LINE, 4, lc);
  // the dropper: rail, claw, the fruit it holds and where it falls
  int16_t dx = (int16_t)(dropX >> 8);
  gfx.hline(FR_L - 4, FR_RAIL - 2, FR_R - FR_L + 8, GREY2);
  if (hold >= 0) {
    int16_t r = FR_RAD[hold], cy = FR_RAIL + 2 + r;
    for (int16_t y = cy + r + 4; y < FR_FLOOR; y += 6) gfx.vline(dx, y, 2, GREY2);
  }
  for (uint8_t i = 0; i < s.n; i++) frFruit(s.f[i].kind, (int16_t)(s.f[i].x >> 8), (int16_t)(s.f[i].y >> 8), (int16_t)(s.f[i].rad >> 8));
  gfx.fill(dx - 6, FR_RAIL - 4, 12, 4, GREY3);
  gfx.hline(dx - 6, FR_RAIL - 4, 12, GREY4);
  if (hold >= 0) frFruit((uint8_t)hold, dx, FR_RAIL + 2 + FR_RAD[hold], FR_RAD[hold]);
  // left: the next fruit; right: the chain of all ten
  gfx.rbox(6, 34, 64, 74, 8, rgb(0, 42, 51));
  gfx.rframe(6, 34, 64, 74, 8, rgb(0, 127, 127));
  gfx.text(38 - Gfx::width("NEXT", F_SMALL) / 2, 40, "NEXT", C::LTGREY, F_SMALL);
  frFruit(nxt, 38, 78, FR_RAD[nxt]);
  gfx.text(8, 120, "BIGGEST", C::GREY, F_SMALL);
  gfx.text(8, 132, FR_NAME[s.top], FR_COL[s.top][2], F_SMALL);
  char b[16];
  snprintf(b, sizeof(b), "%u/%u", s.n, FR_MAX);
  gfx.text(8, 150, "FRUITS", C::GREY, F_SMALL);
  gfx.text(8, 162, b, s.n >= FR_MAX - 4 ? C::ORANGE : C::LTGREY, F_SMALL);
  gfx.text(4, SCR_H - 30, "L/R move", C::GREY, F_SMALL);
  gfx.text(4, SCR_H - 18, "OK/DN drop", C::GREY, F_SMALL);
  int16_t y = 32;
  for (uint8_t k = 0; k < FR_KINDS; k++) {
    int16_t r = (int16_t)(4 + k * 6 / 9);
    y += r;
    if (k <= s.top || k < 5) frFruit(k, 284, y, r);
    else { gfx.disc(284, y, r, GREY1); gfx.circle(284, y, r, GREY2); }
    y += r + 5;
  }
}

// one round; false = left with a long OK
static bool frPlay() {
  FrState &s = *fr;
  uint8_t cur = (uint8_t)random(3), nxt = (uint8_t)random(5);
  int32_t &dropX = frDropX;
  dropX = ((FR_L + FR_R) / 2) << 8;
  uint32_t last = gameMillis(), acc = 0, ready = 0, endAt = 0;
  fxReset();
  btnClear();
  while (poll()) {
    uint32_t now = gameMillis(), dt = now - last;
    last = now;
    if (dt > 60) dt = 60;
    fxUpdate();
    int16_t r = FR_RAD[cur];
    if (!endAt) {
      dropX += (int32_t)keyAxis(B_LEFT, B_RIGHT) * 130 * (int32_t)dt / 1000;   // 130 px/s
      if (dropX < (FR_L + r) << 8) dropX = (FR_L + r) << 8;
      if (dropX > (FR_R - r) << 8) dropX = (FR_R - r) << 8;
      bool drop = btn(B_OK) | btn(B_DOWN);
      if (drop && now >= ready && frAdd(cur, dropX, (FR_RAIL + 2 + r) << 8)) {
        cur = nxt; nxt = (uint8_t)random(5);
        ready = now + 450;
        sfx(520, 20);
      }
    }
    for (acc += dt; acc >= FR_STEP; acc -= FR_STEP) frStep();
    uint16_t score16 = s.score > 65535 ? 65535 : (uint16_t)s.score;
    if (!endAt && s.danger >= FR_OVER) { endAt = now; sfx(160, 400); fxShake(3, 300); }
    if (endAt && now - endAt > 1600) return true;
    frDraw(dropX, endAt || now < ready ? -1 : (int8_t)cur, nxt, now);
    fxDraw();
    if (endAt) {
      gfx.rbox(FR_L + 6, 110, FR_R - FR_L - 12, 40, 8, C::DKRED);
      gfx.rframe(FR_L + 6, 110, FR_R - FR_L - 12, 40, 8, C::WHITE);
      gfx.center(118, "POT FULL!", C::WHITE, F_BOLD);
    }
    fxShakeApply();
    fxFlashApply(FR_L, TOP_H, FR_R - FR_L, SCR_H - TOP_H);
    statusBar("FRUIT POT", score16, curHigh, rgb(255, 85, 85));
    gfx.send();
  }
  return false;
}

void fruitRun() {
  fr = (FrState *)calloc(1, sizeof(FrState));
  if (!fr) return;
  for (;;) {
    memset(fr, 0, sizeof(FrState));
    if (!frPlay() || !gameOver(fr->score > 65535 ? 65535 : (uint16_t)fr->score)) break;
  }
  free(fr);
  fr = NULL;
}

static const char *const IC_FRUIT[12] = {
  "....d.......", "...rr..d....", "..rwrr.yy...", "..rrr.ywyy..", ".gg...yyyy..", "gwgg.oooo...",
  "gggggowooo..", "ngggnooooon.", "n.gg..oooo.n", "nnnnnnnnnnnn", ".nnnnnnnnnn.", "..nnnnnnnn.." };
