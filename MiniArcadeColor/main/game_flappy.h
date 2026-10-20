// MiniArcade Color: Flappy. Part of console.cpp, which includes it.
// Any arrow flaps. The gaps get a little narrower and the pipes a little
// quicker with the score. One point per pipe.

#define FL_GROUND 214                  // top of the ground
#define FL_PIPE_W 34
#define FL_BX     80                   // the bird's x

static const char *const FL_BIRD[2][9] = {
  { "...yyyyy....", "..yyyyywwk..", ".yyyyyywwkk.", "yyyyyyyywkk.", "wwwyyyyyrrrr",
    "wwwwyyyrrrr.", ".wwyyyyyyy..", "..yyyyyyy...", "...ooooo...." },
  { "...yyyyy....", "..yyyyywwk..", "wwwyyyywwkk.", "wwwwyyyywkk.", ".wwyyyyyrrrr",
    "yyyyyyyrrrr.", ".yyyyyyyyy..", "..yyyyyyy...", "...ooooo...." } };
static const char FL_KEYS[] = "ywkro";
static const col  FL_PAL[]  = { C::YELLOW, C::WHITE, C::BLACK, C::RED, C::ORANGE };

struct FlPipe { int16_t x, gapY, gapH; bool passed; };

static void flCloud(int16_t x, int16_t y) {
  gfx.disc(x, y, 9, C::WHITE); gfx.disc(x + 11, y - 4, 11, C::WHITE); gfx.disc(x + 24, y, 9, C::WHITE);
  gfx.fill(x, y, 24, 9, C::WHITE);
}

static void flPipe(const FlPipe &p) {
  col body = C::GREEN, dark = C::DKGRN, light = C::LIME;
  int16_t top = p.gapY, bot = p.gapY + p.gapH;
  gfx.fill(p.x, TOP_H, FL_PIPE_W, top - TOP_H, body);
  gfx.fill(p.x, bot, FL_PIPE_W, FL_GROUND - bot, body);
  gfx.fill(p.x + 4, TOP_H, 4, top - TOP_H, light);
  gfx.fill(p.x + 4, bot, 4, FL_GROUND - bot, light);
  gfx.fill(p.x + FL_PIPE_W - 6, TOP_H, 6, top - TOP_H, dark);
  gfx.fill(p.x + FL_PIPE_W - 6, bot, 6, FL_GROUND - bot, dark);
  gfx.fill(p.x - 4, top - 14, FL_PIPE_W + 8, 14, body);           // caps
  gfx.fill(p.x - 4, bot, FL_PIPE_W + 8, 14, body);
  gfx.frame(p.x - 4, top - 14, FL_PIPE_W + 8, 14, dark);
  gfx.frame(p.x - 4, bot, FL_PIPE_W + 8, 14, dark);
}

void flappyRun() {
  bool again = true;
  while (again) {
    again = false;
    int32_t y = 110 * 256, vy = 0;                              // 1/256 px, per ms
    FlPipe pipes[3];
    for (uint8_t i = 0; i < 3; i++) pipes[i] = { (int16_t)(SCR_W + 40 + i * 130), (int16_t)random(70, 140), 84, false };
    uint16_t score = 0;
    bool started = false, dead = false;
    uint32_t last = gameMillis();
    int32_t scroll = 0;
    btnClear();
    while (poll()) {
      uint32_t now = gameMillis();
      uint32_t dt = now - last;
      if (dt > 40) dt = 40;
      last = now;
      bool flap = false;
      for (uint8_t k = 0; k < 4; k++) if (btn(k)) flap = true;
      if (btnTap(B_OK)) flap = true;
      if (flap && !dead) { vy = -66; started = true; sfx(1000, 20); }
      int16_t speed = (int16_t)(22 + score / 4);                 // 1/256 px per ms: 85 px/s at the start
      if (speed > 38) speed = 38;
      if (started && !dead) {
        vy += (int32_t)dt * 22 / 100;                           // gravity
        if (vy > 110) vy = 110;
        y += vy * (int32_t)dt;
        scroll += (int32_t)dt * speed;
        int16_t dx = (int16_t)(scroll / 256);
        scroll -= dx * 256;
        for (uint8_t i = 0; i < 3; i++) {
          FlPipe &p = pipes[i];
          p.x -= dx;
          if (p.x < -FL_PIPE_W - 8) {                           // back to the right
            int16_t far = 0;
            for (uint8_t k = 0; k < 3; k++) if (pipes[k].x > far) far = pipes[k].x;
            int16_t h = (int16_t)(84 - score / 2);
            if (h < 60) h = 60;
            p = { (int16_t)(far + 130), (int16_t)random(TOP_H + 30, FL_GROUND - h - 30), h, false };
          }
          if (!p.passed && p.x + FL_PIPE_W < FL_BX) { p.passed = true; score++; sfx(1500, 40); }
          // hit?
          int16_t by = (int16_t)(y / 256);
          if (FL_BX + 22 > p.x - 4 && FL_BX + 2 < p.x + FL_PIPE_W + 4 && (by + 2 < p.gapY || by + 16 > p.gapY + p.gapH))
            dead = true;
        }
        if (y / 256 + 18 > FL_GROUND || y < (TOP_H - 10) * 256) dead = true;
        if (dead) sfx(150, 400);
      } else if (!started) y = (110 + (int32_t)(sin(now / 250.0) * 6)) * 256;

      // ---- draw ----
      statusBar("FLAPPY", score, curHigh, C::ORANGE);
      gfx.gradient(0, TOP_H, SCR_W, FL_GROUND - TOP_H, rgb(60, 150, 255), rgb(190, 230, 255));
      int16_t cl = (int16_t)((now / 60) % 400);
      flCloud(300 - cl, 60); flCloud(500 - cl, 90); flCloud(150 - cl / 2, 45);
      for (int16_t x = 0; x < SCR_W; x += 40) {                  // city in the back
        int16_t hgt = (int16_t)(30 + (x * 7 % 25));
        gfx.fill(x - (int16_t)((now / 120) % 40), FL_GROUND - hgt, 30, hgt, rgb(150, 200, 230));
      }
      for (uint8_t i = 0; i < 3; i++) flPipe(pipes[i]);
      gfx.fill(0, FL_GROUND, SCR_W, SCR_H - FL_GROUND, C::SAND);
      gfx.fill(0, FL_GROUND, SCR_W, 6, C::LIME);
      for (int16_t x = -(int16_t)((now / 8) % 20); x < SCR_W; x += 20) gfx.triangle(x, FL_GROUND + 6, x + 10, FL_GROUND + 6, x + 5, FL_GROUND + 12, C::GREEN);
      uint8_t frame = (started && !dead && (now / 100) % 2) ? 1 : 0;
      gfx.sprite(FL_BX, (int16_t)(y / 256), FL_BIRD[frame], 9, FL_KEYS, FL_PAL, 2);
      if (!started) { gfx.rbox(60, 150, 200, 30, 8, C::BLACK); gfx.center(157, "press an arrow", C::WHITE); }
      gfx.send();
      if (dead) { delay(800); break; }
    }
    if (!dead) return;
    again = gameOver(score);
  }
}
