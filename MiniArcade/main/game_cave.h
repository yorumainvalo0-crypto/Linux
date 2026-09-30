// MiniArcade: Cave. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  CAVE   (hold UP to climb, let go to sink)
// =========================================================
/* The cave scrolls from right to left, one column every frame. Its middle
   wanders up and down and it gets narrower the further you fly; now and
   then a rock hangs in the way. The score is the distance.           */
#define CV_TOP   (TOP_H + 1)
#define CV_BOT   (SCR_H - 1)
#define CV_X     20            // the copter's column
#define CV_GAP0  36            // opening at the start ...
#define CV_GAPMIN 18           // ... narrows down to this
#define CV_ROCK   5            // height of a rock: one side stays passable

static uint8_t cvTop[SCR_W], cvBot[SCR_W];     // walls per screen column
static uint8_t cvRock[SCR_W];                  // 0 = none, else the rock's top row
static int16_t cvMid, cvDrift;                 // middle of the opening, Q4
static uint8_t cvGap;
static int16_t cvY;                            // the copter, Q4

static void cvColumn(uint8_t x, uint16_t dist) {
  cvGap = CV_GAP0 - dist / 60;
  if (cvGap < CV_GAPMIN || cvGap > CV_GAP0) cvGap = CV_GAPMIN;
  if (random(12) == 0) cvDrift = (int16_t)random(9) - 4;       // new direction now and then
  cvMid += cvDrift * 4;
  int16_t lo = (CV_TOP + cvGap / 2 + 1) << 4, hi = (CV_BOT - cvGap / 2 - 1) << 4;
  if (cvMid < lo) { cvMid = lo; cvDrift = 2; }
  if (cvMid > hi) { cvMid = hi; cvDrift = -2; }
  int16_t m = cvMid >> 4;
  cvTop[x] = m - cvGap / 2;
  cvBot[x] = m + cvGap / 2;
  cvRock[x] = 0;
}

void caveRun() {
  bool again = true;
  while (again) {
    again = false;
    cvMid = ((CV_TOP + CV_BOT) / 2) << 4; cvDrift = 0;
    uint16_t dist = 0;
    for (uint8_t x = 0; x < SCR_W; x++) cvColumn(x, 0);
    int16_t &y = cvY, vy = 0;                                   // copter, Q4
    y = ((CV_TOP + CV_BOT) / 2) << 4;
    uint8_t rockIn = 60;                                        // columns until the next rock
    uint32_t next = 0;
    bool started = false;                                       // waits for the first UP
    btnClear();

    while (poll()) {
      uint32_t now = gameMillis();
      if (!started && (btnHeld(B_UP) || btnHeld(B_OK))) started = true;
      if (started && now >= next) {
        next = now + 25;
        bool up = btnHeld(B_UP) || btnHeld(B_OK);
        vy += up ? -3 : 3;
        if (vy > 28) vy = 28;
        if (vy < -28) vy = -28;
        y += vy;
        memmove(cvTop, cvTop + 1, SCR_W - 1);                   // scroll one column
        memmove(cvBot, cvBot + 1, SCR_W - 1);
        memmove(cvRock, cvRock + 1, SCR_W - 1);
        dist++;
        cvColumn(SCR_W - 1, dist);
        if (--rockIn == 0) {                                    // a rock in the opening
          rockIn = 40 + random(40) - (dist > 2000 ? 20 : dist / 100);
          uint8_t t = cvTop[SCR_W - 1], b = cvBot[SCR_W - 1];
          uint8_t r = t + 3 + random(b - t - CV_ROCK - 6 > 0 ? b - t - CV_ROCK - 6 : 1);
          for (uint8_t k = 0; k < 4; k++) cvRock[SCR_W - 1 - k] = r;
        }
        int16_t cy = y >> 4;                                    // the copter is 7 x 4
        bool hit = false;
        for (uint8_t c = CV_X; c < CV_X + 7; c++) {
          if (cy <= cvTop[c] || cy + 4 >= cvBot[c]) hit = true;
          if (cvRock[c] && cy + 4 > cvRock[c] && cy < cvRock[c] + CV_ROCK) hit = true;
        }
        if (hit) {
          sfx(150, 400);
          for (uint8_t i = 0; i < 25 && poll(); i++) {          // a moment to see it
            statusBar("CAVE", dist / 10, curHigh);
            for (uint8_t x = 0; x < SCR_W; x++) {
              oled.drawVLine(x, CV_TOP, cvTop[x] - CV_TOP + 1);
              oled.drawVLine(x, cvBot[x], CV_BOT - cvBot[x] + 1);
            }
            oled.drawFrame(CV_X - i / 2, cy - i / 2, 7 + i, 4 + i);
            oled.sendBuffer();
          }
          again = gameOver(dist / 10);
          break;
        }
        if (dist % 100 == 0) sfx(1300, 20);
      }

      statusBar("CAVE", dist / 10, curHigh);
      for (uint8_t x = 0; x < SCR_W; x++) {
        oled.drawVLine(x, CV_TOP, cvTop[x] - CV_TOP + 1);
        oled.drawVLine(x, cvBot[x], CV_BOT - cvBot[x] + 1);
        if (cvRock[x]) oled.drawVLine(x, cvRock[x], CV_ROCK);
      }
      int16_t cy = y >> 4;                                      // the copter
      oled.drawBox(CV_X + 1, cy + 1, 5, 3);
      oled.drawHLine(CV_X, cy, 7);                              // rotor
      oled.drawPixel(CV_X + ((gameMillis() / 60) & 1 ? 0 : 6), cy - 1);
      oled.drawHLine(CV_X - 2, cy + 2, 2);                      // tail
      if (!started) centerStr(SCR_H - 12, "hold UP to fly");
      oled.sendBuffer();
    }
  }
}
