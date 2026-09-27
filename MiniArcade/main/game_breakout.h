// MiniArcade: Breakout. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  BREAKOUT
// =========================================================
#define BR_COLS 8
#define BR_ROWS 4
#define BR_BW   15            // brick width
#define BR_BH    4
#define BR_TOP  (TOP_H + 3)

static uint8_t brBricks[BR_ROWS];          // one bit per column

void breakoutRun() {
  bool again = true;
  while (again) {
    again = false;
    uint16_t score = 0;
    uint8_t  lives = 3, pad = 52, level = 0;
    int16_t  bx = 64 << 4, by = 50 << 4, vx = 14, vy = -18;
    uint32_t next = 0;
    for (uint8_t r = 0; r < BR_ROWS; r++) brBricks[r] = 0xFF;
    btnClear();

    while (poll()) {
      uint32_t now = millis();
      if (now >= next) {
        next = now + 20;
        if (btnHeld(B_LEFT)  && pad > 1)            pad -= 3;
        if (btnHeld(B_RIGHT) && pad < SCR_W - 25)   pad += 3;

        bx += vx; by += vy;
        if ((bx >> 4) < 0)        { bx = 0;              vx = -vx; }
        if ((bx >> 4) > SCR_W - 3){ bx = (SCR_W-3) << 4; vx = -vx; }
        if ((by >> 4) < TOP_H)    { by = TOP_H << 4;     vy = -vy; }

        int16_t px = bx >> 4, py = by >> 4;
        if (vy > 0 && py >= 58 && py <= 61 && px + 2 >= pad && px <= pad + 24) {
          vy = -vy;
          vx += ((px - (pad + 12)) * 2) / 3;          // steer with the paddle
          if (vx >  26) vx =  26;
          if (vx < -26) vx = -26;
        }
        if (py > SCR_H) {                              // lost the ball
          if (--lives == 0) { again = gameOver(score); break; }
          bx = 64 << 4; by = 50 << 4; vx = 14; vy = -18;
        }

        for (uint8_t r = 0; r < BR_ROWS; r++) {        // brick hits
          int16_t ry = BR_TOP + r * (BR_BH + 2);
          if (py + 2 < ry || py > ry + BR_BH) continue;
          uint8_t c = px / BR_BW;
          if (c < BR_COLS && (brBricks[r] & (1 << c))) {
            brBricks[r] &= ~(1 << c);
            vy = -vy;
            sfx(850, 25);
            score += 10 * (level + 1);
            break;                                     // one brick per step: no tunnelling
          }
        }
        uint8_t left = 0;
        for (uint8_t r = 0; r < BR_ROWS; r++) for (uint8_t c = 0; c < BR_COLS; c++)
          if (brBricks[r] & (1 << c)) left++;
        if (!left) {                                   // next level
          level++;
          score += 50;
          for (uint8_t r = 0; r < BR_ROWS; r++) brBricks[r] = 0xFF;
          bx = 64 << 4; by = 50 << 4;
          vx = (vx > 0 ? 16 : -16) + level; vy = -20 - level;
        }
      }

      statusBar("BREAKOUT", score, curHigh);
      for (uint8_t r = 0; r < BR_ROWS; r++)
        for (uint8_t c = 0; c < BR_COLS; c++)
          if (brBricks[r] & (1 << c))
            oled.drawBox(c * BR_BW + 1, BR_TOP + r * (BR_BH + 2), BR_BW - 2, BR_BH);
      oled.drawBox(pad, 61, 25, 3);
      oled.drawBox(bx >> 4, by >> 4, 3, 3);
      for (uint8_t i = 1; i < lives; i++) oled.drawBox(SCR_W - i * 5, 52, 3, 3);   // below the bricks
      oled.sendBuffer();
    }
  }
}
