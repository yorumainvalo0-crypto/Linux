// MiniArcade Color: Stack. Part of console.cpp, which includes it.
// A block slides left and right above the tower; OK (or DOWN) drops it.
// What hangs over the edge falls off, so the next block is only as wide
// as what was left. A block dropped right on top keeps its width, and a
// row of perfect drops even grows it back. One point per floor; the
// block gets a little faster, the sky darker the higher the tower.

#define STK_BH    16                   // block height
#define STK_W0    120                  // first block width
#define STK_ROWS  14                   // rows kept (more than fit on the screen)
#define STK_GND   (SCR_H - 14)         // top of the ground
#define STK_Q     16                   // the sliding block in 1/16 px

struct StkRow { int16_t x; uint8_t w; };

// the part of block (x, w) that lies on (bx, bw); w = 0 when nothing is left
static StkRow stkCut(int16_t x, uint8_t w, int16_t bx, uint8_t bw) {
  int16_t l = x > bx ? x : bx;
  int16_t r = (x + w < bx + bw) ? x + w : bx + bw;
  StkRow o = { l, (uint8_t)(r > l ? r - l : 0) };
  return o;
}

// floor k gets its colour from a walk round the colour wheel
static col stkColour(uint16_t k) {
  static const col WHEEL[6] = { C::RED, C::ORANGE, C::YELLOW, C::GREEN, C::CYAN, C::PURPLE };
  uint16_t s = k % 12;
  return colLerp(WHEEL[s / 2], WHEEL[(s / 2 + 1) % 6], (uint8_t)(s % 2 * 128));
}

static void stkBlock(int16_t x, int16_t y, int16_t w, col c) {
  if (w <= 0) return;
  gfx.fill(x, y, w, STK_BH, c);
  gfx.fill(x, y, w, 3, colLight(c));
  gfx.hline(x, y + STK_BH - 1, w, colDark(c, 2));
  gfx.fill(x, y + 3, 2, STK_BH - 4, colLight(c));
  gfx.fill(x + w - 2, y + 3, 2, STK_BH - 4, colDark(c));
}

void stackRun() {
  StkRow *rows = (StkRow *)malloc(sizeof(StkRow) * STK_ROWS);   // [0] = the top of the tower
  if (!rows) return;
  bool again = true;
  while (again) {
    again = false;
    uint8_t  n = 1, perfect = 0;
    rows[0].x = (SCR_W - STK_W0) / 2; rows[0].w = STK_W0;
    int32_t  x = 0;                                // the sliding block, 1/16 px
    int8_t   dir = 1;
    uint8_t  w = STK_W0;
    uint16_t score = 0;
    int32_t  cam = 0;                              // how far the view went up, 1/16 px
    int16_t  fallX = 0, fallW = 0;                 // the piece that was cut off
    int32_t  fallY = 0, fallV = 0;                 // its y and speed, 1/16 px
    uint32_t last = gameMillis(), shine = 0;
    bool     over = false;
    btnClear();

    while (poll()) {
      uint32_t now = gameMillis();
      uint32_t dt = now - last;
      if (dt > 40) dt = 40;
      last = now;
      int32_t sp = 110 + score * 6;                // px/s: 110 at the start, at most 300
      if (sp > 300) sp = 300;
      if (!over) {
        x += dir * sp * STK_Q * (int32_t)dt / 1000;
        if (x < 0)                               { x = 0; dir = 1; }
        if (x / STK_Q + w > SCR_W)               { x = (SCR_W - w) * STK_Q; dir = -1; }
      }
      if (fallW) {
        fallV += (int32_t)dt * 14;                 // 900 px/s^2, in 1/16 px per s
        fallY += fallV * (int32_t)dt / 1000;
        if (fallY / STK_Q + cam / STK_Q > SCR_H) fallW = 0;
      }
      // the view follows the tower so the sliding block stays well below the top
      int32_t want = ((int32_t)(score + 2) * STK_BH - (STK_GND - TOP_H - 70)) * STK_Q;
      if (want < 0) want = 0;
      if (cam < want) { cam += (want - cam) * (int32_t)dt / 120 + 1; if (cam > want) cam = want; }
      int16_t camPx = (int16_t)(cam / STK_Q);
      int16_t slideY = STK_GND - (score + 2) * STK_BH + camPx;    // where the sliding block is

      if (!over && (btnTap(B_OK) || btn(B_DOWN) || btn(B_UP))) {
        int16_t px = (int16_t)(x / STK_Q);
        StkRow on = stkCut(px, w, rows[0].x, rows[0].w);
        if (!on.w) {                               // missed the tower completely
          over = true;
          fallX = px; fallW = w; fallY = (int32_t)(slideY - camPx) * STK_Q; fallV = 0;
          sfx(200, 300);
        } else {
          if (abs(px - rows[0].x) <= 4) {          // right on top
            on = rows[0];
            if (++perfect >= 3 && on.w < STK_W0) { on.w = (uint8_t)(on.w + 10 > STK_W0 ? STK_W0 : on.w + 10); if (on.x > 5) on.x -= 5; }
            sfx((uint16_t)(1400 + perfect * 100), 60);
            shine = now + 400;
          } else {
            perfect = 0;
            fallW = w - on.w; fallX = px < on.x ? px : on.x + on.w;
            fallY = (int32_t)(slideY - camPx) * STK_Q; fallV = 0;
            sfx(700, 40);
          }
          memmove(&rows[1], &rows[0], sizeof(StkRow) * (STK_ROWS - 1));
          rows[0] = on;
          if (n < STK_ROWS) n++;
          w = on.w;
          score++;
          x = (random(2) ? 0 : (SCR_W - w)) * STK_Q;   // the next one comes from a side
          dir = x ? -1 : 1;
        }
      }

      // ---- draw ----
      statusBar("STACK", score, curHigh, C::CYAN);
      uint8_t hgt = (uint8_t)(camPx / 6 > 255 ? 255 : camPx / 6);           // day .. night with height
      gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, colLerp(rgb(80, 160, 255), rgb(0, 0, 30), hgt),
                   colLerp(rgb(200, 230, 255), rgb(60, 20, 90), hgt));
      if (hgt > 100)
        for (uint8_t i = 0; i < 50; i++) {                                    // stars up high
          uint32_t h = (uint32_t)(i + 3) * 2654435761u;
          int16_t sy = (int16_t)(TOP_H + ((h >> 20) + camPx / 4) % (SCR_H - TOP_H));
          gfx.pixel((int16_t)((h >> 8) % SCR_W), sy, i % 4 ? C::GREY : C::WHITE);
        }
      for (uint8_t i = 0; i < 4; i++) {                                       // clouds pass by below
        int16_t cy = (int16_t)(150 - i * 260 + camPx / 2);
        if (cy < TOP_H - 20 || cy > SCR_H + 20) continue;
        int16_t cx = (int16_t)((i * 97 + now / 80) % (SCR_W + 80)) - 40;
        gfx.disc(cx, cy, 10, C::WHITE); gfx.disc(cx + 14, cy - 6, 13, C::WHITE); gfx.disc(cx + 30, cy, 10, C::WHITE);
        gfx.fill(cx, cy, 30, 10, C::WHITE);
      }
      int16_t city = STK_GND + camPx / 2;                                      // the town, sinking slower
      if (city - 100 < SCR_H) {
        col house = colLerp(rgb(120, 150, 200), rgb(30, 30, 70), hgt);
        for (int16_t k = 0; k < 14; k++) {
          uint32_t h = (uint32_t)(k + 11) * 2654435761u;
          int16_t bw = (int16_t)(18 + (h >> 27)), bh = (int16_t)(30 + (h >> 20) % 70), bx = (int16_t)(k * 24 - 6);
          gfx.fill(bx, city - bh, bw, bh, house);
          for (int16_t wy = city - bh + 5; wy < city - 6; wy += 9)            // windows
            for (int16_t wx = bx + 4; wx < bx + bw - 4; wx += 7)
              gfx.fill(wx, wy, 3, 4, ((wx * 3 + wy) & 7) < 3 ? C::YELLOW : colLight(house));
        }
      }
      if (fallW) {                                                            // a cut piece falls behind the ground
        int16_t fy = (int16_t)(fallY / STK_Q) + camPx;
        stkBlock(fallX, fy, fallW, colDark(stkColour((uint16_t)(over ? score + 1 : score)), 1));
      }
      int16_t gy = STK_GND + camPx;                                           // the ground, until it scrolls away
      if (gy < SCR_H) {
        gfx.fill(0, gy, SCR_W, SCR_H - gy, rgb(70, 150, 60));
        gfx.hline(0, gy, SCR_W, rgb(140, 220, 100));
        gfx.fill((SCR_W - STK_W0) / 2 - 8, gy, STK_W0 + 16, 6, C::GREY);
      }
      for (uint8_t r = 0; r < n; r++) {                                       // the tower, top row first
        int16_t y = STK_GND - (score + 1 - r) * STK_BH + camPx;
        if (y > SCR_H) break;
        stkBlock(rows[r].x, y, rows[r].w, stkColour((uint16_t)(score - r)));
      }
      if (shine > now) gfx.rframe(rows[0].x - 2, STK_GND - (score + 1) * STK_BH + camPx - 2, rows[0].w + 4, STK_BH + 4, 3, C::WHITE);
      if (!over) stkBlock((int16_t)(x / STK_Q), slideY, w, stkColour((uint16_t)(score + 1)));
      if (perfect >= 2 && shine > now) {
        char b[16];
        snprintf(b, sizeof(b), "PERFECT x%u", perfect);
        gfx.textShadow((SCR_W - Gfx::width(b, F_BOLD)) / 2, TOP_H + 14, b, C::YELLOW, C::BLACK, F_BOLD);
      }
      gfx.send();
      if (over && !fallW) break;                                               // the miss has fallen away
    }
    if (!over) break;                                                          // left from the pause menu
    again = gameOver(score);
  }
  free(rows);
}

static const char *const IC_STACK[12] = {
  "............", "..cccccc....", "..cccccc....", "...gggggg...", "...gggggg...", "..yyyyyyy...",
  "..yyyyyyy...", "..ooooooo...", "..ooooooo...", ".rrrrrrrrr..", ".rrrrrrrrr..", "dddddddddddd" };
