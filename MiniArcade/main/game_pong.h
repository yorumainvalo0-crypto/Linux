// MiniArcade: Pong. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  PONG   (endless rally against the CPU, one point per return)
// =========================================================
#define P_TOP  TOP_H
#define P_BOT  SCR_H
#define P_PH   13         // paddle height
#define P_STEP 20         // ms per simulation step

void pongRun() {
  bool again = true;
  while (again) {
    again = false;
    int16_t  bx = 64 << 4, by = 40 << 4;      // ball, 1/16 pixel units
    int16_t  vx = -20, vy = 9;
    uint8_t  py = 34, ay = 34;                // paddle tops (player / CPU)
    uint16_t score = 0;
    uint32_t nextStep = 0;
    btnClear();

    while (poll()) {
      uint32_t now = gameMillis();
      if (now >= nextStep) {
        nextStep = now + P_STEP;

        if (btnHeld(B_UP)   && py > P_TOP)        py -= 3;
        if (btnHeld(B_DOWN) && py < P_BOT - P_PH) py += 3;

        uint8_t at = ay + P_PH / 2, bt = by >> 4;      // CPU follows the ball
        uint8_t sp = 2 + score / 20; if (sp > 4) sp = 4;
        if (bt > at + 1 && ay < P_BOT - P_PH) ay += sp;
        if (bt < at - 1 && ay > P_TOP)        ay -= sp;
        if (ay < P_TOP) ay = P_TOP;                      // not into the status bar

        bx += vx; by += vy;
        if (by < (P_TOP << 4))       { by = P_TOP << 4;       vy = -vy; }
        if (by > ((P_BOT - 2) << 4)) { by = (P_BOT - 2) << 4; vy = -vy; }

        if (vx < 0 && (bx >> 4) <= 5 && (bx >> 4) >= 2) {        // player paddle
          uint8_t byp = by >> 4;
          if (byp + 2 >= py && byp <= py + P_PH) {
            bx = 5 << 4;
            vx = -vx + 1;                                        // faster each hit
            if (vx > 48) vx = 48;                                // keep it collidable
            vy += ((int16_t)(byp - (py + P_PH / 2))) * 2;        // angle from hit point
            if (vy >  24) vy =  24;
            if (vy < -24) vy = -24;
            if (vy > -3 && vy < 3) vy = random(2) ? 5 : -5;     // never perfectly flat
            sfx(750, 30);
            score++;
          }
        }
        if (vx > 0 && (bx >> 4) >= 121) {                        // CPU paddle
          uint8_t byp = by >> 4;
          if (byp + 2 >= ay && byp <= ay + P_PH) { bx = 121 << 4; vx = -vx; }
        }
        if ((bx >> 4) > 126) { score += 5; bx = 64 << 4; by = 40 << 4; vx = -20; vy = 9; }
        if ((bx >> 4) < 0)   { again = gameOver(score); break; }
      }

      statusBar("PONG", score, curHigh);
      oled.drawBox(2, py, 3, P_PH);
      oled.drawBox(123, ay, 3, P_PH);
      oled.drawBox(bx >> 4, by >> 4, 2, 2);
      oled.sendBuffer();
    }
  }
}
