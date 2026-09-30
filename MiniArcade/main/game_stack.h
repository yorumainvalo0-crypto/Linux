// MiniArcade: Stack. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  STACK   (drop the sliding block on the tower, OK = drop)
// =========================================================
/* A block slides left and right above the tower. OK drops it: what hangs
   over the edge falls off, so the next block is only as wide as what was
   left. A block dropped right on top keeps its width (and a row of
   perfect drops even grows it back). The tower scrolls down as it grows. */
#define TW_H      5           // block height
#define TW_W0     48          // first block width
#define TW_ROWS   8           // rows kept on screen

struct StkRow { int16_t x; uint8_t w; };
static StkRow  stkTop;          // the top of the tower
static int16_t stkX;            // the sliding block, 1/4 pixel
static uint8_t stkW;

// the part of block (x, w) that lies on (bx, bw); w = 0 when nothing is left
StkRow stkCut(int16_t x, uint8_t w, int16_t bx, uint8_t bw) {
  int16_t l = x > bx ? x : bx;
  int16_t r = (x + w < bx + bw) ? x + w : bx + bw;
  StkRow o = { l, (uint8_t)(r > l ? r - l : 0) };
  return o;
}

void stackRun() {
  bool again = true;
  while (again) {
    again = false;
    StkRow rows[TW_ROWS];                       // [0] = the top of the tower
    uint8_t n = 1;
    rows[0].x = (SCR_W - TW_W0) / 2; rows[0].w = TW_W0;
    int16_t  &x = stkX, dir = 1;                // the sliding block, 1/4 pixel
    uint8_t  &w = stkW, perfect = 0;
    x = 0; w = TW_W0;
    stkTop = rows[0];
    uint16_t score = 0;
    uint32_t next = 0;
    int16_t  fallX = 0, fallW = 0, fallY = 0;   // the piece that was cut off
    btnClear();

    while (poll()) {
      uint32_t now = gameMillis();
      if (now >= next) {
        next = now + 20;
        int16_t sp = 4 + score / 4;             // faster with every few floors
        if (sp > 14) sp = 14;
        x += dir * sp;
        if (x < 0)                      { x = 0; dir = 1; }
        if ((x >> 2) + w > SCR_W)       { x = (SCR_W - w) << 2; dir = -1; }
        if (fallW) fallY += 2;
        if (fallY > SCR_H) fallW = 0;
      }
      if (btnTap(B_OK) || btn(B_DOWN)) {
        int16_t px = x >> 2;
        StkRow on = stkCut(px, w, rows[0].x, rows[0].w);
        if (!on.w) {                            // missed the tower completely
          for (uint8_t i = 0; i < 30 && poll(); i++) {
            statusBar("STACK", score, curHigh);
            for (uint8_t r = 0; r < n; r++) oled.drawBox(rows[r].x, SCR_H - (n - r) * TW_H, rows[r].w, TW_H - 1);
            oled.drawFrame(px, SCR_H - (n + 1) * TW_H + i * 2, w, TW_H - 1);   // falls away
            oled.sendBuffer();
          }
          again = gameOver(score);
          break;
        }
        if (abs(on.x - px) <= 1 && on.w + 2 >= w) {   // right on top
          on.x = rows[0].x; on.w = rows[0].w;
          if (++perfect >= 3 && on.w < TW_W0) { on.w += 4; if (on.x > 2) on.x -= 2; }
          sfx(1400 + perfect * 100, 60);
        } else {
          perfect = 0;
          fallW = w - on.w; fallX = (px < on.x) ? px : on.x + on.w; fallY = SCR_H - (n + 1) * TW_H;
          sfx(700, 40);
        }
        memmove(&rows[1], &rows[0], sizeof(StkRow) * (TW_ROWS - 1));
        rows[0] = on;
        stkTop = on;
        if (n < TW_ROWS) n++;
        w = on.w;
        score++;
        x = (random(2) ? 0 : (SCR_W - w)) << 2;       // the next one comes from a side
        dir = x ? -1 : 1;
      }

      statusBar("STACK", score, curHigh);
      for (uint8_t r = 0; r < n; r++) {          // the tower, top row first
        int16_t y = SCR_H - (n - r) * TW_H;
        oled.drawBox(rows[r].x, y, rows[r].w, TW_H - 1);
      }
      oled.drawFrame(x >> 2, SCR_H - (n + 1) * TW_H, w, TW_H - 1);   // the sliding one
      if (fallW) oled.drawFrame(fallX, fallY, fallW, TW_H - 1);
      oled.sendBuffer();
    }
  }
}
