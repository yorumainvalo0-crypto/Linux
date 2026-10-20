// MiniArcade Color: Mine. Part of console.cpp, which includes it.
// A 2D Minecraft: a world of 64 x 32 blocks (grass, dirt, stone, gold
// ore, trees) that is kept in flash between games. LEFT/RIGHT walk (one
// block steps up by itself), UP jumps. OK works on the block in front,
// DOWN on the block under your feet: a block is dug into the bag, an
// empty place gets a block from the bag. A point for every block dug.
// The rules are the OLED game's (its 4 px blocks are the units here),
// drawn 2.5 times as big.

#define MINE_W   64             // world size in blocks
#define MINE_H   32
#define MINE_TS  4              // block size in game units
#define MINE_PX  10             // block size on screen (2.5 px per unit)
#define MINE_VY  TOP_H
#define MINE_VH  (SCR_H - TOP_H)
#define MINE_TICK 30            // ms per game step

/* block 0 = air, 1 = dirt, 2 = stone, 3 = wood, 4 = ore, 5 = leaves;
   on screen also 6 = grass (dirt with air above), 0 = cave wall      */
struct MineState {
  uint8_t world[MINE_W * MINE_H / 2];        // 4 bit per block = 1024 byte, as saved
  col     tex[7][MINE_PX * MINE_PX];
  col     sky[MINE_VH];
  uint8_t surf[MINE_W];                      // first ground block of every column
};
static MineState *mineS = NULL;

static uint8_t mineGet(int16_t x, int16_t y) {
  if (x < 0 || x >= MINE_W || y < 0 || y >= MINE_H) return 2;   // world edge = stone
  uint16_t i = (uint16_t)y * MINE_W + x;
  return (i & 1) ? (mineS->world[i >> 1] >> 4) : (mineS->world[i >> 1] & 15);
}

static void mineSet(int16_t x, int16_t y, uint8_t v) {
  if (x < 0 || x >= MINE_W || y < 0 || y >= MINE_H) return;
  uint16_t i = (uint16_t)y * MINE_W + x;
  uint8_t &b = mineS->world[i >> 1];
  b = (i & 1) ? (uint8_t)((b & 0x0F) | (v << 4)) : (uint8_t)((b & 0xF0) | v);
}

static void mineGenerate() {
  memset(mineS->world, 0, sizeof(mineS->world));
  int8_t h = 14;
  for (int16_t x = 0; x < MINE_W; x++) {
    h += (int8_t)random(3) - 1;
    if (h < 9)  h = 9;
    if (h > 22) h = 22;
    for (int16_t y = h; y < MINE_H; y++) mineSet(x, y, (y < h + 3) ? 1 : (random(14) ? 2 : 4));
    if (x % 9 == 4 && h > 10) {                                 // a tree
      for (uint8_t t = 1; t <= 3; t++) mineSet(x, h - t, 3);
      for (int8_t dx = -1; dx <= 1; dx++)
        for (int8_t dy = -5; dy <= -3; dy++) mineSet(x + dx, h + dy, 5);
    }
  }
}

static bool mineLoad() {
  prefs.begin("arcade", true);
  size_t n = prefs.getBytes("world", mineS->world, sizeof(mineS->world));
  prefs.end();
  return n == sizeof(mineS->world);
}

static void mineSave() {
  prefs.begin("arcade", false);
  prefs.putBytes("world", mineS->world, sizeof(mineS->world));
  prefs.end();
}

static bool mineSolid(int16_t x, int16_t y, uint8_t w, uint8_t h) {   // a box in game units
  for (int16_t px = x; px < x + w; px++)
    for (int16_t py = y; py < y + h; py++)
      if (mineGet(px / MINE_TS, py / MINE_TS)) return true;
  return false;
}

static uint8_t mineHash(uint32_t a) { a *= 2654435761u; a ^= a >> 13; a *= 0x5bd1e995u; return (uint8_t)(a >> 24); }

// the block pictures, 10 x 10 each, with a little noise
static void mineTextures() {
  for (uint8_t t = 0; t < 7; t++)
    for (uint8_t y = 0; y < MINE_PX; y++)
      for (uint8_t x = 0; x < MINE_PX; x++) {
        uint8_t n = mineHash((uint32_t)(t * 977 + y * 31 + x * 7 + 1));
        col c;
        switch (t) {
        case 0: c = n < 40 ? rgb(40, 30, 25) : rgb(60, 45, 35); break;            // cave wall
        case 1: c = n < 50 ? rgb(110, 75, 45) : (n > 220 ? rgb(170, 120, 80) : rgb(140, 95, 60)); break;
        case 2: c = n < 50 ? rgb(95, 95, 100) : (n > 215 ? rgb(170, 170, 175) : rgb(130, 130, 135)); break;
        case 3: c = (x == 2 || x == 6 || (x == 8 && (n & 1))) ? rgb(85, 55, 25) : rgb(125, 85, 40); break;
        case 4: c = n > 190 ? (n > 235 ? C::WHITE : C::YELLOW) : (n < 50 ? rgb(95, 95, 100) : rgb(130, 130, 135)); break;
        case 5: c = n < 70 ? rgb(20, 100, 20) : (n > 210 ? rgb(90, 200, 70) : rgb(40, 150, 40)); break;
        default:                                                                  // grass on dirt
          if (y < 2 || (y < 4 && n > 120)) c = n > 200 ? C::LIME : C::GREEN;
          else c = n < 50 ? rgb(110, 75, 45) : rgb(140, 95, 60);
          break;
        }
        if (t != 0 && t != 5 && (x == MINE_PX - 1 || y == MINE_PX - 1)) c = colDark(c);   // a seam
        mineS->tex[t][y * MINE_PX + x] = c;
      }
  for (int16_t y = 0; y < MINE_VH; y++) mineS->sky[y] = colLerp(rgb(60, 140, 255), rgb(190, 230, 255), (uint8_t)(y * 255 / MINE_VH));
}

static void mineCloud(int16_t x, int16_t y) {
  gfx.rbox(x, y, 50, 14, 6, C::WHITE);
  gfx.rbox(x + 12, y - 8, 26, 14, 6, C::WHITE);
}

// Steve: hair, skin, eyes, shirt, trousers, shoes
static const char *const MINE_HERO[18] = {
  "..nnnn..", ".nnnnnn.", ".nhhhhh.", ".hhwbhwb", ".hhhhhhh", ".hhhnnh.", "..hhhh..", "cccccccc", "cccccccc",
  "hcccccch", "hcccccch", "hcccccch", ".dddddd.", ".dddddd.", ".dd..dd.", ".dd..dd.", ".dd..dd.", ".kk..kk." };
static const col MINE_HERO_PAL[] = { rgb(80, 50, 20), rgb(230, 170, 120), C::WHITE, C::BLUE, rgb(0, 170, 170), rgb(50, 50, 150), C::DKGREY };

void mineRun() {
  mineS = (MineState *)malloc(sizeof(MineState));
  if (!mineS) return;
  if (!mineLoad()) mineGenerate();
  mineTextures();

  int16_t  px = 32 * MINE_TS, py = 0, vy = 0;      // player, py / vy in 1/16 units
  int8_t   face = 1;
  uint16_t score = 0, inv = 0;
  while (mineGet(px / MINE_TS, py / MINE_TS + 1) == 0 && py < (MINE_H - 3) * MINE_TS * 16) py += 16;
  bool jump = false, actF = false, actD = false;
  uint32_t next = gameMillis(), hintUntil = gameMillis() + 5000;
  btnClear();

  while (poll()) {
    if (btn(B_UP))   jump = true;                   // kept for the next step
    if (btn(B_OK))   actF = true;
    if (btn(B_DOWN)) actD = true;
    uint32_t now = gameMillis();
    if ((int32_t)(now - next) > 120) next = now - 120;
    while ((int32_t)(now - next) >= 0) {
      next += MINE_TICK;
      if (btnHeld(B_LEFT) || btnHeld(B_RIGHT)) {
        face = btnHeld(B_LEFT) ? -1 : 1;
        int16_t nx = px + face;
        if (!mineSolid(nx, py >> 4, 3, 7)) px = nx;
        else if (!mineSolid(nx, (py >> 4) - MINE_TS, 3, 7)) { px = nx; py -= MINE_TS << 4; }
      }
      bool ground = mineSolid(px, (py >> 4) + 7, 3, 1);
      if (jump && ground) vy = -42;
      vy += 4;
      if (vy > 60) vy = 60;
      int16_t ny = py + vy;
      if (vy > 0) {
        if (mineSolid(px, (ny >> 4) + 7, 3, 1)) { ny = ((((ny >> 4) + 7) / MINE_TS) * MINE_TS - 7) << 4; vy = 0; }
      } else if (mineSolid(px, ny >> 4, 3, 1)) {
        ny = ((((ny >> 4) / MINE_TS) + 1) * MINE_TS) << 4;
        vy = 0;
      }
      py = ny;
      if ((py >> 4) > MINE_H * MINE_TS) { py = 0; px = 32 * MINE_TS; }   // fell out

      /* OK works on the block in front, DOWN on the block under your feet.
         A block -> dig it, an empty place -> put a block from the bag. */
      if (actF || actD) {
        int16_t tx, ty;
        if (actD) { tx = px / MINE_TS;                         ty = ((py >> 4) + 8) / MINE_TS; }
        else      { tx = (px + (face > 0 ? 4 : -1)) / MINE_TS; ty = ((py >> 4) + 3) / MINE_TS; }
        uint8_t t = mineGet(tx, ty);
        bool inside = tx >= 0 && tx < MINE_W && ty >= 0 && ty < MINE_H;   // the edge is not diggable
        if (t && inside)    { mineSet(tx, ty, 0); inv++; score++; sfx(320, 25); }   // dig
        else if (!t && inv) { mineSet(tx, ty, 1); inv--; sfx(600, 15); }            // build
      }
      jump = actF = actD = false;
    }

    // ---- draw ----
    int16_t camX = px * 5 / 2 + 4 - SCR_W / 2, camY = (py >> 4) * 5 / 2 + 9 - MINE_VH / 2;
    camX = constrain(camX, 0, MINE_W * MINE_PX - SCR_W);
    camY = constrain(camY, 0, MINE_H * MINE_PX - MINE_VH);
    uint8_t *fb = gfx.buffer();
    for (int16_t y = 0; y < MINE_VH; y++) memset(fb + (MINE_VY + y) * SCR_W, mineS->sky[y], SCR_W);
    gfx.disc(270, MINE_VY + 30 - camY / 4, 16, C::YELLOW);
    for (uint8_t i = 0; i < 4; i++) mineCloud((int16_t)(((i * 190 + 40 - camX / 3) % 760 + 760) % 760 - 100), (int16_t)(MINE_VY + 22 + (i % 2) * 26 - camY / 3));
    for (int16_t x = 0; x < MINE_W; x++) {
      uint8_t y = 0;
      while (y < MINE_H) { uint8_t t = mineGet(x, y); if (t == 1 || t == 2 || t == 4) break; y++; }
      mineS->surf[x] = y;
    }
    for (int16_t ty = camY / MINE_PX; ty <= (camY + MINE_VH - 1) / MINE_PX; ty++)
      for (int16_t tx = camX / MINE_PX; tx <= (camX + SCR_W - 1) / MINE_PX; tx++) {
        uint8_t t = mineGet(tx, ty);
        if (!t) { if (ty < mineS->surf[tx]) continue; }       // sky stays, a dug hole shows the cave wall
        else if (t == 1 && !mineGet(tx, ty - 1)) t = 6;       // grass
        const col *tex = mineS->tex[t];
        int16_t sx = tx * MINE_PX - camX, sy = ty * MINE_PX - camY + MINE_VY;
        int16_t x0 = sx < 0 ? -sx : 0, x1 = sx + MINE_PX > SCR_W ? SCR_W - sx : MINE_PX;
        for (int16_t r = 0; r < MINE_PX; r++) {
          int16_t y = sy + r;
          if (y < MINE_VY || y >= SCR_H) continue;
          memcpy(fb + y * SCR_W + sx + x0, tex + r * MINE_PX + x0, (size_t)(x1 - x0));
        }
      }
    // the block OK works on, then the player
    int16_t fx = (px + (face > 0 ? 4 : -1)) / MINE_TS, fy = ((py >> 4) + 3) / MINE_TS;
    int16_t hx = fx * MINE_PX - camX, hy = fy * MINE_PX - camY + MINE_VY;
    if (hy >= MINE_VY) gfx.frame(hx, hy, MINE_PX, MINE_PX, mineGet(fx, fy) ? C::WHITE : C::YELLOW);
    int16_t sx = px * 5 / 2 - camX, sy = (py >> 4) * 5 / 2 - camY + MINE_VY;
    gfx.sprite(sx, sy, MINE_HERO, 18, "nhwbcdk", MINE_HERO_PAL, 1, face < 0);

    char b[16];
    snprintf(b, sizeof(b), "BAG %u", inv);
    gfx.rbox(4, SCR_H - 22, Gfx::width(b) + 30, 19, 5, C::BLACK);
    for (uint8_t r = 0; r < MINE_PX; r++) memcpy(fb + (SCR_H - 18 + r) * SCR_W + 9, mineS->tex[6] + r * MINE_PX, MINE_PX);
    gfx.text(24, SCR_H - 21, b, C::WHITE);
    if ((int32_t)(hintUntil - now) > 0) {
      gfx.rbox(110, SCR_H - 22, 206, 19, 5, C::BLACK);
      gfx.text(116, SCR_H - 17, "OK dig/build ahead, DOWN below", C::LTGREY, F_SMALL);
    }
    statusBar("MINE", score, curHigh, C::GREEN);
    gfx.send();
  }

  mineSave();                                    // keep the world for next time
  if (score > curHigh) { curHigh = score; saveHigh(curGame, curHigh); }
  free(mineS);
  mineS = NULL;
}

// the icon in the library (12 x 12, letters as ICON_KEYS in console.cpp)
static const char *const IC_MINE[12] = {
  "cccccccccyyc", "cccccccccyyc", "cccwwccccccc", "ccwwwwcccccc", "cccccccccccc", "gggggggggggg",
  "gdgggdgggdgg", "nnnnnnnnnnnn", "nnsnnnnnnsnn", "ssssssssssss", "ssysssssysss", "sssssyssssss" };
