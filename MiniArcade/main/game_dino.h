// MiniArcade: Dino. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  DINO   (the Chrome offline runner)
// =========================================================
#define DN_GY   61            // ground line
#define DN_X    12            // dino x position
#define DN_OBS   3

#define DN_CLOUDS 3

/* obstacle kinds: 0 = one small cactus, 1 = two small, 2 = big cactus,
   3 = pterodactyl (only once the score got past 30)                     */
static uint8_t dnWidth(uint8_t t)  { return (t == 1) ? 13 : (t == 2 ? 9 : (t == 3 ? 9 : 6)); }
static uint8_t dnHeight(uint8_t t) { return (t == 2) ? 13 : (t == 3 ? 6 : 9); }

/* Low birds must be jumped over, middle ones ducked under, high ones fly
   past. Before, the middle one flew over a standing dino - ducking was
   never needed.                                                          */
static int16_t dnBirdY(uint8_t h) { return (h == 0) ? DN_GY - 12 : (h == 1 ? DN_GY - 17 : DN_GY - 28); }

/* Smallest distance between two obstacles that can always be cleared at
   this speed - found by trying every jump / dive / duck timing on a PC.
   With a fixed 45 px some pairs could not be passed from speed 5 on.   */
static int16_t dnGap(uint16_t spd) {
  static const uint8_t g[9] = { 45, 45, 45, 45, 45, 51, 57, 69, 77 };
  uint8_t v = spd / 8;
  return g[v > 8 ? 8 : v];
}

void dinoRun() {
  bool again = true;
  while (again) {
    again = false;
    int16_t  y = 0, vy = 0;                    // height above ground, Q4
    int16_t  ox[DN_OBS], cx[DN_CLOUDS];
    uint8_t  ot[DN_OBS], oh[DN_OBS], cy[DN_CLOUDS];
    uint16_t score = 0, spd = 26, blink = 0, nextBlink = 100;
    uint32_t dist = 0;                         // 16 bit wrapped: score stopped at 818
    bool     night = false;
    uint32_t next = 0;
    for (uint8_t i = 0; i < DN_OBS; i++) {
      ox[i] = SCR_W + 30 + i * 60;
      ot[i] = random(3);
      oh[i] = 0;
    }
    for (uint8_t i = 0; i < DN_CLOUDS; i++) { cx[i] = random(SCR_W); cy[i] = 18 + random(9); }
    btnClear();

    while (poll()) {
      bool duck = btnHeld(B_DOWN) && y == 0;
      if ((btnTap(B_UP) || btnTap(B_OK)) && y == 0) { vy = -52; sfx(700, 35); }
      if (btnHeld(B_DOWN) && y < 0) vy += 6;             // dive back down

      uint32_t now = gameMillis();
      if (now >= next) {
        next = now + 30;
        vy += 5;
        y += vy;
        if (y > 0) { y = 0; vy = 0; }
        dist += spd;
        score = (dist / 80 > 65535) ? 65535 : (uint16_t)(dist / 80);
        if (spd < 64) spd = 26 + score / 5;
        if (score >= nextBlink) { nextBlink += 100; blink = 30; sfx(1150, 120); }
        if (blink) blink--;
        if (score && score % 350 == 0 && (dist / 80) != (dist - spd) / 80) night = !night;

        uint8_t dh = duck ? 7 : 14;
        int16_t dt = DN_GY - dh + (y >> 4);
        for (uint8_t i = 0; i < DN_OBS; i++) {
          ox[i] -= spd / 8;
          if (ox[i] < -16) {                              // recycle behind the screen
            int16_t far = ox[0];
            for (uint8_t k = 1; k < DN_OBS; k++) if (ox[k] > far) far = ox[k];
            ox[i] = far + dnGap(spd) + random(50);
            ot[i] = (score > 30 && random(4) == 0) ? 3 : random(3);
            oh[i] = (ot[i] == 3) ? random(3) : 0;         // three flight heights
          }
          int16_t ow = dnWidth(ot[i]), ohh = dnHeight(ot[i]);
          int16_t oy = (ot[i] == 3) ? dnBirdY(oh[i]) : (DN_GY - ohh);
          if (ox[i] < DN_X + (duck ? 12 : 8) && ox[i] + ow > DN_X + 1 &&
              oy < dt + dh && oy + ohh > dt) {
            again = gameOver(score);
            goto dinoDone;
          }
        }
        for (uint8_t i = 0; i < DN_CLOUDS; i++) {         // clouds drift slowly
          cx[i] -= 1;
          if (cx[i] < -14) { cx[i] = SCR_W + random(30); cy[i] = 18 + random(9); }
        }
      }

      // ---------------- draw ----------------
      statusBar("DINO", score, curHigh);
      if (!blink || (blink / 5) & 1) {                    // score blinks every 100
        for (uint8_t i = 0; i < DN_CLOUDS; i++) {         // clouds
          oled.drawHLine(cx[i] + 2, cy[i], 8);
          oled.drawHLine(cx[i], cy[i] + 1, 12);
          oled.drawPixel(cx[i] + 3, cy[i] - 1);
          oled.drawPixel(cx[i] + 7, cy[i] - 1);
        }
      }
      oled.drawHLine(0, DN_GY + 1, SCR_W);                // ground
      for (uint8_t i = 0; i < 10; i++) {
        int16_t gx = (i * 13 - (dist / 8) % 13 + SCR_W) % SCR_W;
        oled.drawPixel(gx, DN_GY + 3);
        if (i & 1) oled.drawHLine(gx, DN_GY + 2, 2);
      }

      {                                                   // the dino itself
        uint8_t dh = duck ? 7 : 14;
        int16_t dt = DN_GY - dh + (y >> 4);
        if (duck) {
          oled.drawBox(DN_X, dt + 1, 12, 6);              // stretched body
          oled.drawBox(DN_X + 10, dt, 6, 5);              // head
          oled.setDrawColor(0);
          oled.drawPixel(DN_X + 14, dt + 1);
          oled.setDrawColor(1);
          oled.drawBox(DN_X + 2, dt + 7, 3, 2);
        } else {
          oled.drawBox(DN_X + 1, dt + 4, 6, 8);           // body
          oled.drawBox(DN_X + 5, dt, 7, 6);               // head
          oled.setDrawColor(0);
          oled.drawPixel(DN_X + 10, dt + 2);              // eye
          oled.drawBox(DN_X + 6, dt + 5, 3, 1);           // mouth line
          oled.setDrawColor(1);
          oled.drawBox(DN_X, dt + 6, 2, 3);               // arm
          if (y < 0) {                                    // jumping: legs tucked
            oled.drawBox(DN_X + 2, dt + 12, 2, 2);
            oled.drawBox(DN_X + 5, dt + 12, 2, 2);
          } else if (((dist / 6) & 1)) {                  // running animation
            oled.drawBox(DN_X + 2, dt + 12, 2, 2);
          } else {
            oled.drawBox(DN_X + 5, dt + 12, 2, 2);
          }
        }
      }

      for (uint8_t i = 0; i < DN_OBS; i++) {              // obstacles
        if (ox[i] > SCR_W || ox[i] < -16) continue;
        if (ot[i] == 3) {                                 // pterodactyl
          int16_t by = dnBirdY(oh[i]);
          oled.drawBox(ox[i] + 2, by + 2, 5, 3);
          oled.drawPixel(ox[i] + 7, by + 2);
          bool up = (dist / 50) & 1;
          if (up) {
            oled.drawLine(ox[i] + 2, by + 2, ox[i] - 1, by - 2);
            oled.drawLine(ox[i] + 6, by + 2, ox[i] + 9, by - 2);
          } else {
            oled.drawLine(ox[i] + 2, by + 3, ox[i] - 1, by + 7);
            oled.drawLine(ox[i] + 6, by + 3, ox[i] + 9, by + 7);
          }
        } else {
          uint8_t n = (ot[i] == 1) ? 2 : 1;
          uint8_t h = dnHeight(ot[i]), w = (ot[i] == 2) ? 5 : 4;
          for (uint8_t k = 0; k < n; k++) {
            int16_t x = ox[i] + k * 7;
            oled.drawBox(x + w / 2 - 1, DN_GY - h, 3, h);          // trunk
            oled.drawBox(x, DN_GY - h + 3, 2, 3);                  // left arm
            oled.drawBox(x + 1, DN_GY - h + 3, 1, 1);
            oled.drawBox(x + w - 1, DN_GY - h + 4, 2, 3);          // right arm
          }
        }
      }

      if (night) {                                        // night: invert the scene
        oled.setDrawColor(2);
        oled.drawBox(0, TOP_H, SCR_W, SCR_H - TOP_H);
        oled.setDrawColor(1);
      }
      oled.sendBuffer();
    }
    dinoDone: ;
  }
}
