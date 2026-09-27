// MiniArcade: Snake. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  SNAKE   (32 x 12 grid of 4 px cells in the blue area)
// =========================================================
#define S_W   32
#define S_H   12
#define S_MAX 250

void snakeRun() {
  static uint16_t body[S_MAX];        // packed (x<<8)|y, [0] = head
  bool again = true;
  while (again) {
    again = false;
    uint16_t len = 4, score = 0, stepMs = 180;
    int8_t   dx = 1, dy = 0, ndx = 1, ndy = 0;
    uint8_t  fx = random(S_W), fy = random(S_H);
    uint32_t nextStep = 0;
    for (uint16_t i = 0; i < len; i++) body[i] = ((8 - i) << 8) | 6;
    btnClear();

    while (poll()) {
      if (btn(B_LEFT)  && dx == 0) { ndx = -1; ndy =  0; }
      if (btn(B_RIGHT) && dx == 0) { ndx =  1; ndy =  0; }
      if (btn(B_UP)    && dy == 0) { ndx =  0; ndy = -1; }
      if (btn(B_DOWN)  && dy == 0) { ndx =  0; ndy =  1; }

      uint32_t now = gameMillis();
      if (now >= nextStep) {
        // boost: keep holding the key of the current direction (holding OK
        // would mean "back to the menu")
        bool boost = (dx > 0 && btnHeld(B_RIGHT)) || (dx < 0 && btnHeld(B_LEFT)) ||
                     (dy > 0 && btnHeld(B_DOWN))  || (dy < 0 && btnHeld(B_UP));
        nextStep = now + (boost ? stepMs / 3 : stepMs);
        dx = ndx; dy = ndy;
        int8_t hx = (int8_t)(body[0] >> 8) + dx;
        int8_t hy = (int8_t)(body[0] & 0xFF) + dy;

        bool dead = (hx < 0 || hx >= S_W || hy < 0 || hy >= S_H);
        uint16_t head = ((uint16_t)(uint8_t)hx << 8) | (uint8_t)hy;
        if (!dead)
          for (uint16_t i = 0; i + 1 < len; i++)
            if (body[i] == head) { dead = true; break; }
        if (dead) { again = gameOver(score); break; }

        if (hx == (int8_t)fx && hy == (int8_t)fy) {               // eat
          sfx(1100, 45);
          score += 10;
          if (len < S_MAX) len++;
          if (stepMs > 70) stepMs -= 4;
          while (true) {                                          // respawn food
            fx = random(S_W); fy = random(S_H);
            uint16_t f = ((uint16_t)fx << 8) | fy;
            bool bad = (f == head);                               // the head moves there now
            for (uint16_t i = 0; i + 1 < len && !bad; i++) if (body[i] == f) bad = true;
            if (!bad) break;
          }
        }
        memmove(&body[1], &body[0], (len - 1) * sizeof(uint16_t));
        body[0] = head;
      }

      statusBar("SNAKE", score, curHigh);
      for (uint16_t i = 0; i < len; i++)
        oled.drawBox((body[i] >> 8) * 4, TOP_H + (body[i] & 0xFF) * 4, 3, 3);
      oled.drawFrame(fx * 4, TOP_H + fy * 4, 3, 3);
      oled.sendBuffer();
    }
  }
}
