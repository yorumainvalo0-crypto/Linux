// MiniArcade Color: Lights Out. Part of console.cpp, which includes it.
// Switch every light off. Pressing a field (OK) switches it and its four
// neighbours. A level is made by pressing "par" random fields on a dark
// board, so it can always be solved in par moves. More than twice par + 3
// moves ends the game. Points: 10 per level plus the moves you had left.
// 5 x 5 glowing lamps on the left, level and moves on the right.

#define LO_N    5
#define LO_C   40                    // cell pitch in px
#define LO_X   10                    // board, top left
#define LO_Y   31
#define LO_FX 260                    // ms a switched lamp flashes

static uint32_t loBoard;             // bit r * 5 + c = light on
static uint8_t  loR, loC;            // cursor
static int16_t  loCurX, loCurY;      // the cursor as drawn (glides)
static uint32_t loFlash, loFlashAt;  // the lamps the last press switched, and when

static uint32_t loPress(uint32_t b, uint8_t r, uint8_t c) {
  b ^= 1UL << (r * LO_N + c);
  if (r > 0)        b ^= 1UL << ((r - 1) * LO_N + c);
  if (r < LO_N - 1) b ^= 1UL << ((r + 1) * LO_N + c);
  if (c > 0)        b ^= 1UL << (r * LO_N + c - 1);
  if (c < LO_N - 1) b ^= 1UL << (r * LO_N + c + 1);
  return b;
}

// a new level with "par" different fields pressed; never an empty board
static uint32_t loMake(uint8_t par) {
  uint32_t b = 0, used = 0;
  while (!b) {
    b = 0; used = 0;
    for (uint8_t k = 0; k < par; k++) {
      uint8_t i;
      do i = (uint8_t)random(LO_N * LO_N); while (used & (1UL << i));
      used |= 1UL << i;
      b = loPress(b, i / LO_N, i % LO_N);
    }
  }
  return b;
}

static void loGlide(int16_t &v, int16_t target, uint32_t dt) {
  int16_t d = target - v;
  if (d > LO_C * 2 || d < -LO_C * 2) { v = target; return; }
  int16_t s = (int16_t)(d * (int32_t)dt / 50);
  if (!s && d) s = d > 0 ? 1 : -1;
  if ((d > 0 && s > d) || (d < 0 && s < d)) s = d;
  v += s;
}

static void loDraw(bool cursor, uint8_t level, uint8_t moves, uint8_t limit, uint16_t score, const char *msg, uint32_t dt) {
  statusBar("LIGHTS OUT", score, curHigh, C::YELLOW);
  gfx.fill(0, TOP_H, SCR_W, SCR_H - TOP_H, C::NAVY);
  gfx.rbox(LO_X - 4, LO_Y - 4, LO_N * LO_C + 8, LO_N * LO_C + 8, 8, rgb(30, 20, 50));
  uint32_t now = gameMillis(), ft = now - loFlashAt;
  for (uint8_t r = 0; r < LO_N; r++)
    for (uint8_t c = 0; c < LO_N; c++) {
      int16_t x = LO_X + c * LO_C, y = LO_Y + r * LO_C, s = LO_C - 4;
      uint32_t bit = 1UL << (r * LO_N + c);
      bool on = loBoard & bit;
      if (on) {
        gfx.rbox(x, y, s, s, 7, rgb(255, 170, 0));
        gfx.rbox(x + 4, y + 4, s - 8, s - 8, 5, C::YELLOW);
        gfx.rbox(x + 9, y + 9, s - 18, s - 18, 4, rgb(255, 255, 153));
        gfx.fill(x + 7, y + 6, 4, 3, C::WHITE);
      } else {
        gfx.rbox(x, y, s, s, 7, rgb(42, 42, 85));
        gfx.rframe(x, y, s, s, 7, rgb(70, 70, 120));
        gfx.disc(x + s / 2, y + s / 2, 3, rgb(60, 60, 110));
      }
      if ((loFlash & bit) && ft < LO_FX) {                  // a ring that grows out of a switched lamp
        int16_t g = (int16_t)(ft * 6 / LO_FX);
        gfx.rframe(x - g, y - g, s + 2 * g, s + 2 * g, 7 + g, on ? C::WHITE : rgb(102, 102, 204));
      }
    }
  if (cursor) {
    loGlide(loCurX, LO_X + loC * LO_C, dt);
    loGlide(loCurY, LO_Y + loR * LO_C, dt);
    col cc = (now / 300) % 2 ? C::CYAN : C::WHITE;
    gfx.rframe(loCurX - 3, loCurY - 3, LO_C + 2, LO_C + 2, 9, cc);
    gfx.rframe(loCurX - 2, loCurY - 2, LO_C, LO_C, 8, cc);
  }
  // the panel
  const int16_t PX = LO_X + LO_N * LO_C + 10;
  char b[20];
  gfx.text(PX, 34, "LEVEL", C::LTGREY, F_SMALL);
  snprintf(b, sizeof(b), "%u", level);
  gfx.text(PX, 46, b, C::YELLOW, F_BIG);
  gfx.text(PX, 88, "MOVES", C::LTGREY, F_SMALL);
  snprintf(b, sizeof(b), "%u / %u", moves, limit);
  gfx.text(PX, 100, b, C::WHITE, F_TEXT);
  int16_t bw = SCR_W - PX - 8;
  gfx.rbox(PX, 124, bw, 10, 4, rgb(42, 42, 85));
  if (limit) {
    int16_t fw = (int16_t)(bw * moves / limit);
    col fc = moves * 4 >= limit * 3 ? C::RED : (moves * 2 >= limit ? C::ORANGE : C::GREEN);
    if (fw > 0) gfx.rbox(PX, 124, fw, 10, 4, fc);
  }
  uint8_t lit = 0;
  for (uint8_t i = 0; i < 25; i++) if (loBoard & (1UL << i)) lit++;
  snprintf(b, sizeof(b), "%u lit", lit);
  gfx.text(PX, 144, b, C::ORANGE, F_TEXT);
  snprintf(b, sizeof(b), "%s", okKeys());
  gfx.text(PX, 196, b, C::GREY, F_SMALL);
  gfx.text(PX, 208, "= switch", C::GREY, F_SMALL);
  if (msg) {
    int16_t w = Gfx::width(msg, F_BOLD) + 28;
    int16_t x = LO_X + (LO_N * LO_C - w) / 2, y = LO_Y + LO_N * LO_C / 2 - 21;
    gfx.rbox(x + 3, y + 3, w, 42, 8, C::BLACK);
    gfx.rbox(x, y, w, 42, 8, rgb(102, 0, 153));
    gfx.rframe(x, y, w, 42, 8, C::WHITE);
    gfx.text(x + 14, y + 9, msg, C::WHITE, F_BOLD);
  }
}

// shows the board with a message for a moment; false = the player left
static bool loPause(const char *msg, uint8_t level, uint8_t moves, uint8_t limit, uint16_t score) {
  uint32_t t0 = gameMillis();
  while (gameMillis() - t0 < 1000) {
    if (!poll()) return false;
    loDraw(false, level, moves, limit, score, gameMillis() - t0 > 200 ? msg : NULL, 0);
    gfx.send();
  }
  return true;
}

void lightsOutRun() {
  bool again = true;
  while (again) {
    again = false;
    uint16_t score = 0;
    uint8_t  level = 1, limit = 0, moves = 0;
    loR = loC = 2;
    loCurX = LO_X + loC * LO_C; loCurY = LO_Y + loR * LO_C;
    loFlash = 0;
    bool over = false;
    while (!over) {
      uint8_t par = (uint8_t)(2 + level);         // 3, 4, 5, ... up to 15
      if (par > 15) par = 15;
      loBoard = loMake(par);
      limit = (uint8_t)(par * 2 + 3); moves = 0;
      uint32_t last = gameMillis();
      btnClear();
      while (true) {
        if (!poll()) return;
        uint32_t now = gameMillis(), dt = now - last;
        last = now;
        if (btn(B_UP))    { loR = (uint8_t)((loR + LO_N - 1) % LO_N); sfx(700, 10); }
        if (btn(B_DOWN))  { loR = (uint8_t)((loR + 1) % LO_N);        sfx(700, 10); }
        if (btn(B_LEFT))  { loC = (uint8_t)((loC + LO_N - 1) % LO_N); sfx(700, 10); }
        if (btn(B_RIGHT)) { loC = (uint8_t)((loC + 1) % LO_N);        sfx(700, 10); }
        if (btnTap(B_OK)) {
          uint32_t nb = loPress(loBoard, loR, loC);
          loFlash = nb ^ loBoard;
          loFlashAt = now;
          loBoard = nb;
          moves++;
          sfx(loBoard & (1UL << (loR * LO_N + loC)) ? 1100 : 800, 30);
        }
        if (!loBoard) {                            // all dark: next level
          score += 10 + (limit - moves);
          sfx(1500, 150);
          if (!loPause("DONE!", level, moves, limit, score)) return;
          level++;
          break;
        }
        if (moves >= limit) { over = true; break; }
        loDraw(true, level, moves, limit, score, NULL, dt);
        gfx.send();
      }
    }
    sfx(200, 300);
    if (!loPause("LIMIT", level, moves, limit, score)) return;
    again = gameOver(score);
  }
}

static const char *const IC_LIGHTS[12] = {
  "yyy.bbb.yyy.", "yyy.bbb.yyy.", "yyy.bbb.yyy.", "............", "bbb.yyy.bbb.", "bbb.ywy.bbb.",
  "bbb.yyy.bbb.", "............", "yyy.bbb.yyy.", "yyy.bbb.yyy.", "yyy.bbb.yyy.", "............" };
