// MiniArcade Color: Pong against the CPU. Part of console.cpp, which
// includes it. UP/DOWN move the left paddle; the ball gets faster with
// every hit and takes the angle from where it meets the paddle. First to
// 7 wins. Points: 10 per goal, 50 more for the win.

#define PG_Y0   28                     // court top / bottom
#define PG_Y1   238
#define PG_PH   40                     // paddle height
#define PG_PW   7
#define PG_BALL 7
#define PG_ME   rgb(102, 170, 255)     // the score colours
#define PG_CPU  rgb(255, 128, 153)

// ---------------- drawing (online Pong uses it too) ----------------
/* The arena between the walls y0 and y1: a dark blue floor, darker
   towards the sides and the walls, glowing court lines.              */
static void pgSparse(int16_t x, int16_t y, int16_t w, int16_t h, col c) {   // every fourth pixel
  for (int16_t j = 0; j < h; j += 2)
    for (int16_t i = (int16_t)((j / 2 + y / 2) & 1) * 2; i < w; i += 4) { gfx.pixel(x + i, y + j, c); gfx.pixel(x + i + 1, y + j + 1, c); }
}
static void pgCourt(int16_t y0, int16_t y1) {
  int16_t h = y1 - y0, cy = (y0 + y1) / 2, mx = SCR_W / 2;
  col dark = rgb(0, 0, 51);
  gfx.fill(0, y0, SCR_W, h, rgb(0, 42, 102));
  for (uint8_t k = 0; k < 2; k++) {                             // the sides fall off into the dark
    gfx.fill(k ? SCR_W - 8 : 0, y0, 8, h, dark);
    fxDitherFill(k ? SCR_W - 24 : 8, y0, 16, h, dark);
    pgSparse(k ? SCR_W - 48 : 24, y0, 24, h, dark);
  }
  fxDitherFill(0, y0, SCR_W, 4, dark);
  pgSparse(0, y0 + 4, SCR_W, 6, dark);
  fxDitherFill(0, y1 - 4, SCR_W, 4, dark);
  pgSparse(0, y1 - 10, SCR_W, 6, dark);
  col line = rgb(0, 128, 153), glow = rgb(0, 85, 153);
  gfx.frame(4, y0 + 5, SCR_W - 8, h - 10, line);               // the court
  gfx.frame(5, y0 + 6, SCR_W - 10, h - 12, glow);
  gfx.circle(mx, cy, 30, line);
  gfx.circle(mx, cy, 29, glow);
  gfx.circle(mx, cy, 31, glow);
  for (int16_t y = y0 + 6; y < y1 - 6; y += 14) {               // the centre line, glowing dashes
    gfx.fill(mx - 2, y, 4, 9, glow);
    gfx.fill(mx - 1, y + 1, 2, 7, rgb(102, 212, 255));
  }
  gfx.disc(mx, cy, 3, rgb(102, 212, 255));
  gfx.hline(0, y0, SCR_W, rgb(204, 255, 255));                 // the walls
  gfx.hline(0, y0 + 1, SCR_W, line);
  gfx.hline(0, y1 - 1, SCR_W, line);
  gfx.hline(0, y1, SCR_W, rgb(204, 255, 255));
}

// a score painted on the floor; pop 0..255 lifts it up and lights it (just scored)
static void pgScore(int16_t x, int16_t y, uint8_t v, col c, uint8_t pop) {
  char b[4];
  snprintf(b, sizeof(b), "%u", v);
  int16_t w = Gfx::width(b, F_BIG), lift = (int16_t)(pop > 128 ? (255 - pop) / 14 : pop / 14);
  if (pop) {
    gfx.rbox(x - 8, y - 6 - lift, w + 16, 46, 10, colDark(c, 2));
    gfx.rframe(x - 8, y - 6 - lift, w + 16, 46, 10, c);
    gfx.textShadow(x, y - lift, b, pop > 140 ? C::WHITE : colLight(c), C::BLACK, F_BIG);
  } else gfx.text(x, y, b, colDark(c), F_BIG);
}

// a paddle: a soft glow around it, bevelled edges and a bright core
static void pgPaddle(int16_t x, int16_t y, int16_t w, int16_t h, col c) {
  fxDitherFill(x - 3, y - 3, w + 6, h + 6, colDark(c, 2));
  gfx.rbox(x - 1, y - 1, w + 2, h + 2, 4, colDark(c, 2));
  gfx.rbox(x, y, w, h, 3, c);
  gfx.vline(x + 1, y + 2, h - 4, colLight(c, 2));
  gfx.hline(x + 2, y + 1, w - 4, colLight(c, 2));
  gfx.vline(x + w - 2, y + 2, h - 4, colDark(c));
  gfx.hline(x + 2, y + h - 2, w - 4, colDark(c));
  gfx.vline(x + w / 2, y + 4, h - 8, C::WHITE);
}

// the ball with a glow; the trail (tx, ty, n points, [0] = newest) fades behind it
static void pgBall(int16_t x, int16_t y, const int16_t *tx, const int16_t *ty, uint8_t n) {
  for (uint8_t i = n; i > 1; i--) {
    int16_t r = (int16_t)(4 - i / 2);
    col c = fxLerp(C::YELLOW, rgb(102, 0, 51), (uint8_t)(i * 255 / (n + 1)));
    if (r > 0) { if (i > 3) fxDitherDisc(tx[i - 1], ty[i - 1], r, c); else gfx.disc(tx[i - 1], ty[i - 1], r, c); }
  }
  fxDitherDisc(x, y, 6, rgb(153, 127, 0));
  gfx.disc(x, y, 4, rgb(255, 170, 0));
  gfx.disc(x, y, 3, C::YELLOW);
  gfx.fill(x - 2, y - 2, 2, 2, rgb(255, 255, 204));
}

void pongRun() {
  bool again = true;
  while (again) {
    again = false;
    int32_t py = (PG_Y0 + PG_Y1) / 2 * 256, cy = py;          // paddle centres, 1/256 px
    int32_t bx = 0, by = 0, vx = 0, vy = 0;
    uint8_t me = 0, cpu = 0, popWho = 0;
    uint32_t serveAt = gameMillis() + 900, last = gameMillis(), popAt = 0, doneAt = 0;
    int16_t trailX[6], trailY[6];
    uint8_t trailN = 0;
    bool serving = true, toCpu = true, done = false, over = false;
    fxReset();
    btnClear();
    while (poll()) {
      uint32_t now = gameMillis();
      uint32_t dt = now - last;
      if (dt > 40) dt = 40;
      last = now;
      // the player's paddle
      py += (int32_t)dt * 22 * keyAxis(B_UP, B_DOWN) / 100;           // up to 220 px/s
      int32_t lo = (PG_Y0 + PG_PH / 2) * 256, hi = (PG_Y1 - PG_PH / 2) * 256;
      if (py < lo) py = lo;
      if (py > hi) py = hi;
      if (serving) {
        bx = SCR_W / 2 * 256; by = (PG_Y0 + PG_Y1) / 2 * 256;
        if (now >= serveAt && !done) {
          serving = false;
          vx = (toCpu ? 1 : -1) * 40;                           // 1/256 px per ms: about 155 px/s
          vy = (int32_t)random(-20, 21);
        }
      } else {
        bx += vx * (int32_t)dt;
        by += vy * (int32_t)dt;
        // top and bottom walls
        if (by < (PG_Y0 + PG_BALL / 2) * 256) {
          by = (PG_Y0 + PG_BALL / 2) * 256; vy = -vy; sfx(600, 10);
          fxBurst((int16_t)(bx / 256), PG_Y0 + 2, 6, C::WHITE, C::CYAN, 120, 260, 0, 1, FX_STREAK);
        }
        if (by > (PG_Y1 - PG_BALL / 2) * 256) {
          by = (PG_Y1 - PG_BALL / 2) * 256; vy = -vy; sfx(600, 10);
          fxBurst((int16_t)(bx / 256), PG_Y1 - 2, 6, C::WHITE, C::CYAN, 120, 260, 0, 1, FX_STREAK);
        }
        // paddles
        int32_t bxp = bx / 256, byp = by / 256;
        if (vx < 0 && bxp - PG_BALL / 2 <= 14 + PG_PW && bxp > 10 && abs(byp - py / 256) <= PG_PH / 2 + 3) {
          vx = -vx * 104 / 100;
          vy = (byp - py / 256) * 2;
          sfx(900, 15);
          fxBurst(14 + PG_PW, (int16_t)byp, 9, C::WHITE, C::CYAN, 170, 300, 0, 1, FX_STREAK);
          fxRing(14 + PG_PW, (int16_t)byp, 10, C::WHITE, C::BLUE, 200);
        }
        if (vx > 0 && bxp + PG_BALL / 2 >= SCR_W - 14 - PG_PW && bxp < SCR_W - 10 && abs(byp - cy / 256) <= PG_PH / 2 + 3) {
          vx = -vx * 104 / 100;
          vy = (byp - cy / 256) * 2;
          sfx(700, 15);
          fxBurst(SCR_W - 14 - PG_PW, (int16_t)byp, 9, C::WHITE, C::PINK, 170, 300, 0, 1, FX_STREAK);
          fxRing(SCR_W - 14 - PG_PW, (int16_t)byp, 10, C::WHITE, C::PURPLE, 200);
        }
        if (vx > 95) vx = 95;                                   // at most about 370 px/s
        if (vx < -95) vx = -95;
        // goals
        if (bxp < -10 || bxp > SCR_W + 10) {
          if (bxp < 0) { cpu++; toCpu = false; sfx(200, 250); } else { me++; toCpu = true; sfx(1500, 200); }
          bool mine = bxp >= 0;
          int16_t gx = mine ? SCR_W - 12 : 12, gy = (int16_t)(byp < PG_Y0 + 8 ? PG_Y0 + 8 : (byp > PG_Y1 - 8 ? PG_Y1 - 8 : byp));
          col c = mine ? C::CYAN : C::PINK;
          fxBurst(gx, gy, 16, C::WHITE, c, 230, 650, 0, 2);
          fxBurst(gx, gy, 8, C::YELLOW, C::RED, 260, 400, 0, 1, FX_STREAK);
          fxRing(gx, gy, 34, C::WHITE, c, 350);
          fxShake(5, 380); fxFlash(c, 320, 110);
          popWho = mine ? 1 : 2; popAt = now;
          serving = true;
          serveAt = now + 900;
          trailN = 0;
          if (me >= 7 || cpu >= 7) {
            done = true; doneAt = now;
            if (me >= 7) for (uint8_t k = 0; k < 3; k++) fxBurst((int16_t)(70 + k * 90), 90 + (k & 1) * 40, 12, C::WHITE, k == 1 ? C::YELLOW : C::CYAN, 150, 900, 20, 2);
          }
        }
      }
      // the CPU follows the ball, a little slow and late
      int32_t target = (vx > 0 && !serving) ? by : (PG_Y0 + PG_Y1) / 2 * 256;
      int32_t step = (int32_t)dt * 256 * (12 + me * 2) / 100;
      if (cy < target - 512) cy += step;
      else if (cy > target + 512) cy -= step;
      if (cy < lo) cy = lo;
      if (cy > hi) cy = hi;
      if (!serving) {
        if (trailN < 6) trailN++;
        for (uint8_t i = trailN - 1; i > 0; i--) { trailX[i] = trailX[i - 1]; trailY[i] = trailY[i - 1]; }
        trailX[0] = (int16_t)(bx / 256); trailY[0] = (int16_t)(by / 256);
      }

      // ---- draw ----
      fxUpdate();
      uint16_t score = (uint16_t)(me * 10 + (me >= 7 ? 50 : 0));
      statusBar("PONG", score, curHigh, C::BLUE);
      pgCourt(PG_Y0, PG_Y1 + 1);
      uint8_t pop = popWho && now - popAt < 900 ? (uint8_t)(255 - (now - popAt) * 255 / 900) : 0;
      pgScore(SCR_W / 2 - 50, PG_Y0 + 16, me, PG_ME, popWho == 1 ? pop : 0);
      pgScore(SCR_W / 2 + 30, PG_Y0 + 16, cpu, PG_CPU, popWho == 2 ? pop : 0);
      pgPaddle(14, (int16_t)(py / 256 - PG_PH / 2), PG_PW, PG_PH, C::CYAN);
      pgPaddle(SCR_W - 14 - PG_PW, (int16_t)(cy / 256 - PG_PH / 2), PG_PW, PG_PH, C::PINK);
      if (!serving) pgBall((int16_t)(bx / 256), (int16_t)(by / 256), trailX, trailY, trailN);
      else if (!done && (now / 150) % 2) pgBall((int16_t)(bx / 256), (int16_t)(by / 256), trailX, trailY, 0);
      fxDraw();
      fxShakeApply();
      fxFlashApply(0, PG_Y0, SCR_W, PG_Y1 - PG_Y0 + 2);
      if (done) {
        const char *msg = me >= 7 ? "YOU WIN!" : "CPU WINS";
        int16_t w = Gfx::width(msg, F_BOLD) + 36, x = (SCR_W - w) / 2;
        gfx.rbox(x + 4, 116, w, 42, 10, C::BLACK);
        gfx.rbox(x, 112, w, 42, 10, me >= 7 ? rgb(0, 42, 102) : rgb(102, 0, 51));
        gfx.rframe(x, 112, w, 42, 10, me >= 7 ? C::CYAN : C::PINK);
        gfx.center(121, msg, C::WHITE, F_BOLD);
      }
      gfx.send();
      if (done && now - doneAt > 1500) { over = true; break; }
    }
    if (!over) return;
    again = gameOver((uint16_t)(me * 10 + (me >= 7 ? 50 : 0)));
  }
}
