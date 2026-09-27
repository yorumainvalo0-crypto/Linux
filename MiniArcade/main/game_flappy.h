// MiniArcade: Flappy. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  FLAPPY   (one button, pipes scrolling right to left)
// =========================================================
#define F_TOP  (TOP_H + 1)
#define F_BOT  (SCR_H - 1)
#define F_GAP  22             // gap at the start ...
#define F_GAPMIN 17            // ... shrinks by one every 10 points down to this
#define F_SHIFT 20             // next gap at most this far from the last one
#define F_PIPES 3
#define F_BX   24              // bird x position

void flappyRun() {
  bool again = true;
  while (again) {
    again = false;
    int16_t by = ((F_TOP + F_BOT) / 2) << 4, vy = 0;   // bird y / speed, Q4
    int16_t fx[F_PIPES];
    uint8_t fg[F_PIPES], fh[F_PIPES];              // gap top and gap height per pipe
    uint8_t last = (F_TOP + F_BOT - F_GAP) / 2;
    for (uint8_t i = 0; i < F_PIPES; i++) {
      fx[i] = SCR_W + i * 44;
      fh[i] = F_GAP;
      fg[i] = F_TOP + 4 + random(F_BOT - F_TOP - F_GAP - 8);
      if (fg[i] > last + F_SHIFT) fg[i] = last + F_SHIFT;
      if (fg[i] + F_SHIFT < last) fg[i] = last - F_SHIFT;
      last = fg[i];
    }
    uint16_t score = 0;
    uint32_t next = 0;
    btnClear();

    while (poll()) {
      if (btnTap(B_OK) || btn(B_UP)) { vy = -26; sfx(620, 25); }   // flap

      uint32_t now = gameMillis();
      if (now >= next) {
        next = now + 30;
        vy += 2;
        if (vy > 36) vy = 36;
        by += vy;
        if (by < (F_TOP << 4)) { by = F_TOP << 4; vy = 0; }
        bool dead = (by >> 4) > F_BOT - 5;

        for (uint8_t i = 0; i < F_PIPES; i++) {
          int16_t old = fx[i];
          fx[i] -= 2;
          if (old + 8 > F_BX && fx[i] + 8 <= F_BX) { score++; sfx(1200, 40); }   // passed
          if (fx[i] < -8) {
            fx[i] += F_PIPES * 44;
            uint8_t h = F_GAP - score / 10;
            if (h < F_GAPMIN || h > F_GAP) h = F_GAPMIN;
            uint8_t prev = fg[(i + F_PIPES - 1) % F_PIPES];         // the pipe before it
            fh[i] = h;
            fg[i] = F_TOP + 4 + random(F_BOT - F_TOP - h - 8);
            if (fg[i] > prev + F_SHIFT) fg[i] = prev + F_SHIFT;
            if (fg[i] + F_SHIFT < prev) fg[i] = prev - F_SHIFT;
          }
          int16_t bt = by >> 4, bb = bt + 5;                       // bird box
          if (fx[i] < F_BX + 6 && fx[i] + 8 > F_BX &&
              (bt < fg[i] || bb > fg[i] + fh[i])) dead = true;
        }
        if (dead) { again = gameOver(score); break; }
      }

      statusBar("FLAPPY", score, curHigh);
      for (uint8_t i = 0; i < F_PIPES; i++) {
        if (fx[i] > SCR_W || fx[i] < -8) continue;
        int16_t x = fx[i] < 0 ? 0 : fx[i];
        uint8_t w = 8 - (x - fx[i]);
        if (x + w > SCR_W) w = SCR_W - x;
        oled.drawFrame(x, F_TOP, w, fg[i] - F_TOP);
        oled.drawFrame(x, fg[i] + fh[i], w, F_BOT - fg[i] - fh[i] + 1);
      }
      oled.drawBox(F_BX, by >> 4, 6, 5);                           // bird
      oled.setDrawColor(0);
      oled.drawPixel(F_BX + 4, (by >> 4) + 1);
      oled.setDrawColor(1);
      oled.sendBuffer();
    }
  }
}
