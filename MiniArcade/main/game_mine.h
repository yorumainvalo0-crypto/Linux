// MiniArcade: Mine. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  MINE  (2D Minecraft: dig, build, world is kept in flash)
// =========================================================
#define M_W    64             // world size in tiles
#define M_H    32
#define M_TS    4             // tile size in pixels
#define M_VIEW (SCR_H - TOP_H)

static uint8_t mWorld[M_W * M_H / 2];      // 4 bit per tile = 1024 byte

/* tile 0 = air, 1 = dirt, 2 = stone, 3 = wood, 4 = ore, 5 = leaves */
static const uint16_t M_TEX[6] = { 0x0000, 0xA5A5, 0xFFFF, 0x6666, 0xF99F, 0x9669 };

static uint8_t mGet(int16_t x, int16_t y) {
  if (x < 0 || x >= M_W || y < 0) return 2;          // world edge = stone
  if (y >= M_H) return 2;
  uint16_t i = (uint16_t)y * M_W + x;
  return (i & 1) ? (mWorld[i >> 1] >> 4) : (mWorld[i >> 1] & 15);
}

static void mSet(int16_t x, int16_t y, uint8_t v) {
  if (x < 0 || x >= M_W || y < 0 || y >= M_H) return;
  uint16_t i = (uint16_t)y * M_W + x;
  if (i & 1) mWorld[i >> 1] = (mWorld[i >> 1] & 0x0F) | (v << 4);
  else       mWorld[i >> 1] = (mWorld[i >> 1] & 0xF0) | v;
}

static void mGenerate() {
  memset(mWorld, 0, sizeof(mWorld));
  int8_t h = 14;
  for (int16_t x = 0; x < M_W; x++) {
    h += (int8_t)random(3) - 1;
    if (h < 9)  h = 9;
    if (h > 22) h = 22;
    for (int16_t y = h; y < M_H; y++)
      mSet(x, y, (y < h + 3) ? 1 : (random(14) ? 2 : 4));
    if (x % 9 == 4 && h > 10) {                       // a tree
      for (uint8_t t = 1; t <= 3; t++) mSet(x, h - t, 3);
      for (int8_t dx = -1; dx <= 1; dx++)
        for (int8_t dy = -5; dy <= -3; dy++) mSet(x + dx, h + dy, 5);
    }
  }
}

static bool mLoad() {
  prefs.begin("arcade", true);
  size_t n = prefs.getBytes("world", mWorld, sizeof(mWorld));
  prefs.end();
  return n == sizeof(mWorld);
}

static void mSave() {
  prefs.begin("arcade", false);
  prefs.putBytes("world", mWorld, sizeof(mWorld));
  prefs.end();
}

static bool mSolid(int16_t x, int16_t y, uint8_t w, uint8_t h) {   // pixel box
  for (int16_t px = x; px < x + w; px += 1)
    for (int16_t py = y; py < y + h; py += 1)
      if (mGet(px / M_TS, py / M_TS)) return true;
  return false;
}

void mineRun() {
  if (!mLoad()) mGenerate();

  int16_t  px = 32 * M_TS, py = 0, vy = 0;      // player, py/vy in Q4 pixels
  int8_t   face = 1;
  uint16_t score = 0, inv = 0;
  uint32_t next = 0;
  while (mGet(px / M_TS, py / M_TS + 1) == 0 && py < (M_H - 3) * M_TS * 16) py += 16;
  btnClear();

  while (poll()) {
    uint32_t now = millis();
    if (now >= next) {
      next = now + 30;

      if (btnHeld(B_LEFT) || btnHeld(B_RIGHT)) {
        face = btnHeld(B_LEFT) ? -1 : 1;
        int16_t nx = px + face;
        if (!mSolid(nx, py >> 4, 3, 7)) px = nx;
        else if (!mSolid(nx, (py >> 4) - M_TS, 3, 7)) { px = nx; py -= M_TS << 4; }
      }
      bool ground = mSolid(px, (py >> 4) + 7, 3, 1);
      if (btn(B_UP) && ground) vy = -42;                     // jump

      vy += 4;
      if (vy > 60) vy = 60;
      int16_t ny = py + vy;
      if (vy > 0) {
        if (mSolid(px, (ny >> 4) + 7, 3, 1)) { ny = ((((ny >> 4) + 7) / M_TS) * M_TS - 7) << 4; vy = 0; }
      } else if (mSolid(px, ny >> 4, 3, 1)) {
        ny = ((((ny >> 4) / M_TS) + 1) * M_TS) << 4;
        vy = 0;
      }
      py = ny;
      if ((py >> 4) > M_H * M_TS) { py = 0; px = 32 * M_TS; }   // fell out

      /* OK works on the tile in front, DOWN on the tile under your feet.
         Solid tile -> dig it, empty tile -> place a block from the bag. */
      bool actF = btn(B_OK), actD = btn(B_DOWN);
      if (actF || actD) {
        int16_t tx, ty;
        if (actD) { tx = px / M_TS;                        ty = ((py >> 4) + 8) / M_TS; }
        else      { tx = (px + (face > 0 ? 4 : -1)) / M_TS; ty = ((py >> 4) + 3) / M_TS; }
        uint8_t t = mGet(tx, ty);
        bool inside = tx >= 0 && tx < M_W && ty >= 0 && ty < M_H;   // the edge is not diggable
        if (t && inside)        { mSet(tx, ty, 0); inv++; score++; sfx(320, 25); }   // dig
        else if (!t && inv)     { mSet(tx, ty, 1); inv--; }            // build
      }
    }

    // ---- draw ----
    int16_t camX = px - SCR_W / 2, camY = (py >> 4) - M_VIEW / 2;
    if (camX < 0) camX = 0;
    if (camX > M_W * M_TS - SCR_W) camX = M_W * M_TS - SCR_W;
    if (camY < 0) camY = 0;
    if (camY > M_H * M_TS - M_VIEW) camY = M_H * M_TS - M_VIEW;

    statusBar("MINE", score, curHigh);
    for (int16_t ty = camY / M_TS; ty <= (camY + M_VIEW) / M_TS; ty++)
      for (int16_t tx = camX / M_TS; tx <= (camX + SCR_W) / M_TS; tx++) {
        uint8_t t = mGet(tx, ty);
        if (!t || t > 5) continue;
        uint16_t tex = M_TEX[t];
        for (uint8_t r = 0; r < 4; r++)
          for (uint8_t c = 0; c < 4; c++)
            if ((tex & (0x8000 >> (r * 4 + c))) && ty * M_TS + r - camY >= 0)   // not into the bar
              oled.drawPixel(tx * M_TS + c - camX, ty * M_TS + r - camY + TOP_H);
      }
    int16_t sx = px - camX, sy = (py >> 4) - camY + TOP_H;
    oled.setDrawColor(0);                                          // outline so the
    oled.drawBox(sx - 1, sy - 1, 5, 9);                            // player stays
    oled.setDrawColor(1);                                          // visible in dirt
    oled.drawBox(sx, sy, 3, 7);

    char b[16];
    snprintf(b, sizeof(b), "BAG %u", inv);
    oled.setDrawColor(0);
    oled.drawBox(0, SCR_H - 8, oled.getStrWidth(b) + 4, 8);
    oled.setDrawColor(1);
    oled.drawStr(2, SCR_H - 1, b);
    oled.sendBuffer();
  }

  mSave();                                    // keep the world for next time
  if (score > curHigh) { curHigh = score; saveHigh(curGame, curHigh); }
}
