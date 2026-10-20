// MiniArcade Color: Pong against the CPU. Part of console.cpp, which
// includes it. UP/DOWN move the left paddle; the ball gets faster with
// every hit and takes the angle from where it meets the paddle. First to
// 7 wins. Points: 10 per goal, 50 more for the win.

#define PG_Y0   28                     // court top / bottom
#define PG_Y1   238
#define PG_PH   40                     // paddle height
#define PG_PW   7
#define PG_BALL 7

void pongRun() {
  bool again = true;
  while (again) {
    again = false;
    int32_t py = (PG_Y0 + PG_Y1) / 2 * 256, cy = py;          // paddle centres, 1/256 px
    int32_t bx = 0, by = 0, vx = 0, vy = 0;
    uint8_t me = 0, cpu = 0;
    uint32_t serveAt = gameMillis() + 900, last = gameMillis();
    int16_t trailX[6], trailY[6];
    uint8_t trailN = 0;
    bool serving = true, toCpu = true, done = false;
    btnClear();
    while (poll()) {
      uint32_t now = gameMillis();
      uint32_t dt = now - last;
      if (dt > 40) dt = 40;
      last = now;
      // the player's paddle
      if (btnHeld(B_UP))   py -= (int32_t)dt * 256 * 26 / 100;
      if (btnHeld(B_DOWN)) py += (int32_t)dt * 256 * 26 / 100;
      int32_t lo = (PG_Y0 + PG_PH / 2) * 256, hi = (PG_Y1 - PG_PH / 2) * 256;
      if (py < lo) py = lo;
      if (py > hi) py = hi;
      if (serving) {
        bx = SCR_W / 2 * 256; by = (PG_Y0 + PG_Y1) / 2 * 256;
        if (now >= serveAt) {
          serving = false;
          vx = (toCpu ? 1 : -1) * 75;                           // 1/256 px per ms: about 290 px/s
          vy = (int32_t)random(-40, 40);
        }
      } else {
        bx += vx * (int32_t)dt;
        by += vy * (int32_t)dt;
        // top and bottom walls
        if (by < (PG_Y0 + PG_BALL / 2) * 256) { by = (PG_Y0 + PG_BALL / 2) * 256; vy = -vy; sfx(600, 10); }
        if (by > (PG_Y1 - PG_BALL / 2) * 256) { by = (PG_Y1 - PG_BALL / 2) * 256; vy = -vy; sfx(600, 10); }
        // paddles
        int32_t bxp = bx / 256, byp = by / 256;
        if (vx < 0 && bxp - PG_BALL / 2 <= 14 + PG_PW && bxp > 10 && abs(byp - py / 256) <= PG_PH / 2 + 3) {
          vx = -vx * 106 / 100;
          vy = (byp - py / 256) * 4;
          sfx(900, 15);
        }
        if (vx > 0 && bxp + PG_BALL / 2 >= SCR_W - 14 - PG_PW && bxp < SCR_W - 10 && abs(byp - cy / 256) <= PG_PH / 2 + 3) {
          vx = -vx * 106 / 100;
          vy = (byp - cy / 256) * 4;
          sfx(700, 15);
        }
        if (vx > 190) vx = 190;
        if (vx < -190) vx = -190;
        // goals
        if (bxp < -10 || bxp > SCR_W + 10) {
          if (bxp < 0) { cpu++; toCpu = false; sfx(200, 250); } else { me++; toCpu = true; sfx(1500, 200); }
          serving = true;
          serveAt = now + 900;
          trailN = 0;
          if (me >= 7 || cpu >= 7) done = true;
        }
      }
      // the CPU follows the ball, a little slow and late
      int32_t target = (vx > 0 && !serving) ? by : (PG_Y0 + PG_Y1) / 2 * 256;
      int32_t step = (int32_t)dt * 256 * (16 + me * 2) / 100;
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
      uint16_t score = (uint16_t)(me * 10 + (me >= 7 ? 50 : 0));
      statusBar("PONG", score, curHigh, C::BLUE);
      gfx.gradient(0, PG_Y0, SCR_W, PG_Y1 - PG_Y0 + 2, rgb(0, 40, 90), rgb(0, 15, 40));
      gfx.hline(0, PG_Y0, SCR_W, C::LTGREY);
      gfx.hline(0, PG_Y1 + 1, SCR_W, C::LTGREY);
      for (int16_t y = PG_Y0 + 4; y < PG_Y1; y += 14) gfx.fill(SCR_W / 2 - 1, y, 3, 8, rgb(100, 140, 200));
      char b[4];
      snprintf(b, sizeof(b), "%u", me);
      gfx.text(SCR_W / 2 - 50, PG_Y0 + 10, b, rgb(100, 180, 255), F_BIG);
      snprintf(b, sizeof(b), "%u", cpu);
      gfx.text(SCR_W / 2 + 30, PG_Y0 + 10, b, rgb(255, 130, 130), F_BIG);
      gfx.rbox(14, (int16_t)(py / 256 - PG_PH / 2), PG_PW, PG_PH, 3, C::CYAN);
      gfx.rbox(SCR_W - 14 - PG_PW, (int16_t)(cy / 256 - PG_PH / 2), PG_PW, PG_PH, 3, C::PINK);
      for (uint8_t i = trailN; i > 1; i--)
        gfx.disc(trailX[i - 1], trailY[i - 1], 3 - (i > 3), colDark(C::YELLOW, (uint8_t)(i / 2)));
      if (!serving || (now / 150) % 2) gfx.disc((int16_t)(bx / 256), (int16_t)(by / 256), PG_BALL / 2, C::YELLOW);
      if (done) gfx.center(120, me >= 7 ? "YOU WIN!" : "CPU WINS", C::WHITE, F_BOLD);
      gfx.send();
      if (done) { delay(1200); break; }
    }
    if (!done) return;
    again = gameOver((uint16_t)(me * 10 + (me >= 7 ? 50 : 0)));
  }
}
