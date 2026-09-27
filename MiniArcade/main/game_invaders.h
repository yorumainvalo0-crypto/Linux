// MiniArcade: Invaders. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  INVADERS
// =========================================================
#define V_ROWS 3
#define V_COLS 7
#define V_CW   12              // cell width
#define V_CH   9               // cell height
#define V_TOP  (TOP_H + 2)

static uint8_t vAlive[V_ROWS];

void invadersRun() {
  bool again = true;
  while (again) {
    again = false;
    uint16_t score = 0;
    uint8_t  lives = 3, wave = 0, ship = 60, stepMs = 90, baseMs = 90;
    int16_t  ax = 2, ay = V_TOP;
    int8_t   adir = 1;
    int16_t  shotX = -1, shotY = 0;
    int16_t  bombX[3] = { -1, -1, -1 }, bombY[3] = { 0, 0, 0 };
    uint32_t next = 0, nextFrame = 0;
    for (uint8_t r = 0; r < V_ROWS; r++) vAlive[r] = (1 << V_COLS) - 1;
    btnClear();

    while (poll()) {
      uint32_t now = millis();
      if (btnTap(B_OK) && shotX < 0) { shotX = ship + 4; shotY = SCR_H - 8; sfx(280, 30); }

      if (now >= nextFrame) {                       // bullets run smoothly
        nextFrame = now + 25;
        if (btnHeld(B_LEFT)  && ship > 1)           ship -= 2;   // same speed at any frame rate
        if (btnHeld(B_RIGHT) && ship < SCR_W - 10)  ship += 2;
        if (shotX >= 0) { shotY -= 4; if (shotY < TOP_H) shotX = -1; }
        for (uint8_t b = 0; b < 3; b++)
          if (bombX[b] >= 0) {
            bombY[b] += 2;
            if (bombY[b] > SCR_H) bombX[b] = -1;
            else if (bombY[b] >= SCR_H - 6 && abs(bombX[b] - (ship + 4)) < 5) {
              bombX[b] = -1;
              if (--lives == 0) break;
            }
          }
        if (!lives) { again = gameOver(score); break; }

        if (shotX >= 0)                             // bullet vs aliens
          for (uint8_t r = 0; r < V_ROWS && shotX >= 0; r++)
            for (uint8_t c = 0; c < V_COLS; c++) {
              if (!(vAlive[r] & (1 << c))) continue;
              int16_t x = ax + c * V_CW, y = ay + r * V_CH;
              if (shotX >= x && shotX < x + 8 && shotY >= y && shotY < y + 6) {
                vAlive[r] &= ~(1 << c);
                shotX = -1;
                score += 10;
                sfx(880, 40);
                break;
              }
            }
      }

      if (now >= next) {                            // alien step
        next = now + stepMs;
        ax += adir * 2;
        if (ax < 1 || ax + V_COLS * V_CW > SCR_W - 1) { adir = -adir; ay += 3; }
        int8_t low = V_ROWS - 1;                    // lowest row that still has aliens
        while (low > 0 && !vAlive[low]) low--;
        if (ay + (low + 1) * V_CH >= SCR_H - 6) { again = gameOver(score); break; }

        for (uint8_t b = 0; b < 3; b++)             // drop a bomb
          if (bombX[b] < 0 && random(100) < 12) {
            uint8_t c = random(V_COLS);
            for (int8_t r = V_ROWS - 1; r >= 0; r--)
              if (vAlive[r] & (1 << c)) {
                bombX[b] = ax + c * V_CW + 4;
                bombY[b] = ay + r * V_CH + 6;
                break;
              }
            break;
          }

        uint8_t left = 0;
        for (uint8_t r = 0; r < V_ROWS; r++)
          for (uint8_t c = 0; c < V_COLS; c++) if (vAlive[r] & (1 << c)) left++;
        if (!left) {                                // next wave
          wave++;
          score += 50;
          ay = V_TOP; ax = 2; adir = 1;
          if (baseMs > 50) baseMs -= 10;           // each wave a little faster
          stepMs = baseMs;
          for (uint8_t r = 0; r < V_ROWS; r++) vAlive[r] = (1 << V_COLS) - 1;
        } else stepMs = (left < 6 && baseMs > 45) ? 45 : baseMs;   // last few rush
      }

      statusBar("INVADERS", score, curHigh);
      for (uint8_t r = 0; r < V_ROWS; r++)
        for (uint8_t c = 0; c < V_COLS; c++) {
          if (!(vAlive[r] & (1 << c))) continue;
          int16_t x = ax + c * V_CW, y = ay + r * V_CH;
          oled.drawBox(x + 1, y + 1, 6, 3);
          oled.drawPixel(x, y + 4); oled.drawPixel(x + 7, y + 4);
          oled.drawPixel(x + 2, y); oled.drawPixel(x + 5, y);
        }
      oled.drawBox(ship, SCR_H - 5, 9, 4);
      oled.drawBox(ship + 3, SCR_H - 8, 3, 3);
      if (shotX >= 0) oled.drawVLine(shotX, shotY, 3);
      for (uint8_t b = 0; b < 3; b++)
        if (bombX[b] >= 0) oled.drawVLine(bombX[b], bombY[b], 3);
      for (uint8_t i = 1; i < lives; i++) oled.drawBox(SCR_W - i * 5, SCR_H - 4, 3, 3);
      oled.sendBuffer();
    }
  }
}
