// MiniArcade Color: Breakout. Part of console.cpp, which includes it.
// LEFT/RIGHT move the paddle, UP (or OK) launches the ball. 10 x 6 bricks
// in rainbow rows, the top rows count more. 3 lives; a cleared wall comes
// back with a quicker ball.

#define BR_COLS 10
#define BR_ROWS 6
#define BR_BW   30                     // brick size
#define BR_BH   12
#define BR_X0   10                     // left of the wall
#define BR_Y0   48
#define BR_PY   222                    // paddle top
#define BR_PH   6
#define BR_BALL 3                      // ball radius

static uint8_t brWall[BR_ROWS][BR_COLS];
static const col BR_COL[BR_ROWS] = { C::RED, C::ORANGE, C::YELLOW, C::GREEN, C::CYAN, C::PURPLE };

static void brBrick(int16_t x, int16_t y, col c) {
  gfx.fill(x + 1, y + 1, BR_BW - 2, BR_BH - 2, c);
  gfx.hline(x + 1, y + 1, BR_BW - 2, colLight(c, 2));
  gfx.hline(x + 1, y + BR_BH - 2, BR_BW - 2, colDark(c, 2));
}

void breakoutRun() {
  bool again = true;
  while (again) {
    again = false;
    memset(brWall, 1, sizeof(brWall));
    uint8_t left = BR_ROWS * BR_COLS, lives = 3, level = 1;
    uint16_t score = 0;
    int16_t pw = 52;                                            // paddle width
    int32_t px = (SCR_W / 2) * 256;                             // paddle centre, 1/256 px
    int32_t bx = 0, by = 0, vx = 0, vy = 0;                     // ball, 1/256 px per ms
    bool stuck = true, over = false;
    uint32_t last = gameMillis();
    btnClear();
    while (poll()) {
      uint32_t now = gameMillis();
      uint32_t dt = now - last;
      if (dt > 40) dt = 40;
      last = now;
      int32_t pv = (int32_t)dt * 256 * 24 / 100;                // about 240 px/s
      px += pv * keyAxis(B_LEFT, B_RIGHT) / 256;
      int32_t lo = (pw / 2 + 2) * 256, hi = (SCR_W - pw / 2 - 2) * 256;
      if (px < lo) px = lo;
      if (px > hi) px = hi;

      if (stuck) {
        bx = px; by = (BR_PY - BR_BALL - 1) * 256;
        if (btn(B_UP) || btnTap(B_OK)) {
          stuck = false;
          int32_t sp = 30 + level * 4;                           // about 130 px/s at the start
          if (sp > 60) sp = 60;
          vx = (random(2) ? 1 : -1) * sp / 2;
          vy = -sp;
          sfx(900, 20);
        }
      } else {
        // move in small steps so the ball cannot tunnel through a brick
        int32_t steps = (abs(vx) > abs(vy) ? abs(vx) : abs(vy)) * (int32_t)dt / (3 * 256) + 1;
        for (int32_t s = 0; s < steps && !stuck; s++) {
          int32_t ox = bx, oy = by;
          bx += vx * (int32_t)dt / steps;
          by += vy * (int32_t)dt / steps;
          int16_t x = (int16_t)(bx / 256), y = (int16_t)(by / 256);
          // side walls and ceiling
          if (x < BR_BALL) { bx = BR_BALL * 256; vx = abs(vx); sfx(600, 8); }
          if (x > SCR_W - 1 - BR_BALL) { bx = (SCR_W - 1 - BR_BALL) * 256; vx = -abs(vx); sfx(600, 8); }
          if (y < TOP_H + BR_BALL) { by = (TOP_H + BR_BALL) * 256; vy = abs(vy); sfx(600, 8); }
          // paddle: the angle comes from where the ball hits it
          int16_t pc = (int16_t)(px / 256);
          if (vy > 0 && y + BR_BALL >= BR_PY && oy / 256 + BR_BALL <= BR_PY + 2 && abs(x - pc) <= pw / 2 + BR_BALL) {
            int32_t sp = 30 + level * 4 + (int32_t)(BR_ROWS * BR_COLS - left) / 10;
            if (sp > 75) sp = 75;
            int32_t off = (int32_t)(x - pc) * 100 / (pw / 2 + BR_BALL);   // -100..100
            vx = sp * off / 120;
            vy = -(sp - abs(vx) / 3);
            if (vy > -sp / 2) vy = -sp / 2;
            by = (BR_PY - BR_BALL - 1) * 256;
            sfx(800, 12);
          }
          // bricks: test the ball's centre against the grid
          int16_t cx = (x - BR_X0) / BR_BW, cy = (y - BR_Y0) / BR_BH;
          if (x >= BR_X0 && y >= BR_Y0 && cx < BR_COLS && cy < BR_ROWS && brWall[cy][cx]) {
            brWall[cy][cx] = 0;
            left--;
            score += (uint16_t)((BR_ROWS - cy) * level);
            sfx((uint16_t)(1600 - cy * 120), 25);
            // bounce off the side it came from
            int16_t ocx = (int16_t)((ox / 256 - BR_X0) / BR_BW), ocy = (int16_t)((oy / 256 - BR_Y0) / BR_BH);
            if (oy / 256 < BR_Y0) ocy = -1;
            if (ocx != cx) vx = -vx;
            if (ocy != cy || ocx == cx) vy = -vy;
            bx = ox; by = oy;
            if (!left) {                                          // wall cleared: next level
              level++;
              memset(brWall, 1, sizeof(brWall));
              left = BR_ROWS * BR_COLS;
              if (pw > 36) pw -= 4;
              stuck = true;
              sfx(1800, 200);
            }
          }
          // lost
          if (y > SCR_H + 4) {
            sfx(200, 300);
            if (--lives == 0) over = true;
            stuck = true;
          }
        }
      }

      // ---- draw ----
      statusBar("BREAKOUT", score, curHigh, C::RED);
      gfx.gradient(0, TOP_H, SCR_W, SCR_H - TOP_H, rgb(0, 0, 60), C::BLACK);
      for (uint8_t r = 0; r < BR_ROWS; r++)
        for (uint8_t c = 0; c < BR_COLS; c++)
          if (brWall[r][c]) brBrick(BR_X0 + c * BR_BW, BR_Y0 + r * BR_BH, BR_COL[r]);
      char b[16];
      snprintf(b, sizeof(b), "LEVEL %u", level);
      gfx.text(6, TOP_H + 4, b, C::LTGREY, F_SMALL);
      for (uint8_t i = 0; i < lives; i++) gfx.disc(SCR_W - 10 - i * 12, TOP_H + 8, 4, C::PINK);
      int16_t pc = (int16_t)(px / 256);
      gfx.rbox(pc - pw / 2, BR_PY, pw, BR_PH, 3, C::LTGREY);
      gfx.hline(pc - pw / 2 + 3, BR_PY + 1, pw - 6, C::WHITE);
      gfx.fill(pc - pw / 2, BR_PY + 2, 5, BR_PH - 3, C::RED);
      gfx.fill(pc + pw / 2 - 5, BR_PY + 2, 5, BR_PH - 3, C::RED);
      gfx.disc((int16_t)(bx / 256), (int16_t)(by / 256), BR_BALL, C::WHITE);
      if (stuck && !over) gfx.center(170, "UP to launch", C::WHITE);
      gfx.send();
      if (over) { delay(700); break; }
    }
    if (!over) return;
    again = gameOver(score);
  }
}
