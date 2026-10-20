// MiniArcade Color: Snake. Part of console.cpp, which includes it.
// 32 x 21 fields of 10 px on a chequered lawn. Arrows steer; an apple is a
// point, the golden star (it comes now and then and goes again) five.
// The snake gets a little faster with every apple.

#define SN_C   10                      // field size
#define SN_W   32
#define SN_H   21
#define SN_Y0  28                      // top of the field
#define SN_MAX (SN_W * SN_H)

static uint8_t snX[SN_MAX], snY[SN_MAX];
static uint16_t snLen;

static bool snOnSnake(uint8_t x, uint8_t y, uint16_t from = 0) {
  for (uint16_t i = from; i < snLen; i++) if (snX[i] == x && snY[i] == y) return true;
  return false;
}

static void snPlace(uint8_t &fx, uint8_t &fy) {
  do { fx = (uint8_t)random(SN_W); fy = (uint8_t)random(SN_H); } while (snOnSnake(fx, fy));
}

static void snLawn() {
  for (uint8_t y = 0; y < SN_H; y++)
    for (uint8_t x = 0; x < SN_W; x++)
      gfx.fill(x * SN_C, SN_Y0 + y * SN_C, SN_C, SN_C, (x + y) & 1 ? rgb(34, 102, 34) : rgb(51, 128, 51));
}

static void snApple(int16_t px, int16_t py) {
  gfx.disc(px + 5, py + 6, 4, C::RED);
  gfx.pixel(px + 3, py + 4, C::WHITE);
  gfx.vline(px + 5, py, 2, C::BROWN);
  gfx.fill(px + 6, py, 3, 2, C::LIME);
}

static void snStar(int16_t px, int16_t py, uint32_t t) {
  col c = (t / 120) % 2 ? C::YELLOW : C::ORANGE;
  gfx.triangle(px + 5, py, px + 1, py + 9, px + 9, py + 9, c);
  gfx.triangle(px + 5, py + 10, px + 0, py + 3, px + 10, py + 3, c);
}

void snakeRun() {
  static const int8_t DX[4] = { 0, 0, -1, 1 }, DY[4] = { -1, 1, 0, 0 };   // as B_UP..B_RIGHT
  bool again = true;
  while (again) {
    again = false;
    snLen = 4;
    for (uint16_t i = 0; i < snLen; i++) { snX[i] = (uint8_t)(10 - i); snY[i] = SN_H / 2; }
    uint8_t dir = B_RIGHT, queue[2], nq = 0;
    uint8_t fx, fy, sx = 0, sy = 0;
    snPlace(fx, fy);
    bool star = false;
    uint32_t starUntil = 0, nextStar = gameMillis() + 8000;
    uint16_t score = 0, step = 140;
    bool started = false, dead = false;
    uint32_t next = gameMillis();
    btnClear();
    while (poll()) {
      for (uint8_t k = 0; k < 4; k++)
        if (btn(k)) {
          uint8_t last = nq ? queue[nq - 1] : dir;
          bool opposite = (k ^ 1) == last;                     // UP/DOWN and LEFT/RIGHT pairs
          if (k != last && !opposite && nq < 2) queue[nq++] = k;
          started = true;
        }
      uint32_t now = gameMillis();
      if (!started) next = now;
      while (started && !dead && (int32_t)(now - next) >= 0) {
        next += step;
        if (nq) { dir = queue[0]; queue[0] = queue[1]; nq--; }
        int16_t nx = snX[0] + DX[dir], ny = snY[0] + DY[dir];
        bool grow = nx == fx && ny == fy;
        bool gold = star && nx == sx && ny == sy;
        bool tailLeaves = snX[snLen - 1] == nx && snY[snLen - 1] == ny && !grow;   // that field gets free
        if (nx < 0 || ny < 0 || nx >= SN_W || ny >= SN_H || (snOnSnake((uint8_t)nx, (uint8_t)ny) && !tailLeaves)) {
          dead = true;
          sfx(150, 400);
          break;
        }
        if (grow && snLen < SN_MAX) snLen++;
        for (uint16_t i = snLen - 1; i > 0; i--) { snX[i] = snX[i - 1]; snY[i] = snY[i - 1]; }
        snX[0] = (uint8_t)nx; snY[0] = (uint8_t)ny;
        if (grow) { score++; sfx(1200, 30); snPlace(fx, fy); if (step > 60) step -= 3; }
        if (gold) { score += 5; star = false; sfx(1800, 80); nextStar = now + 10000; }
      }
      if (!star && now >= nextStar && started) { do snPlace(sx, sy); while (sx == fx && sy == fy); star = true; starUntil = now + 6000; }
      if (star && now >= starUntil) { star = false; nextStar = now + 9000; }

      statusBar("SNAKE", score, curHigh, C::GREEN);
      snLawn();
      snApple(fx * SN_C, SN_Y0 + fy * SN_C);
      if (star && (starUntil - now > 1500 || (now / 100) % 2)) snStar(sx * SN_C, SN_Y0 + sy * SN_C, now);
      for (int16_t i = snLen - 1; i >= 0; i--) {
        int16_t px = snX[i] * SN_C, py = SN_Y0 + snY[i] * SN_C;
        col c = dead ? ((now / 150) % 2 ? C::RED : C::DKRED) : (i % 4 < 2 ? rgb(153, 255, 51) : rgb(102, 204, 0));
        if (i == 0) {
          gfx.rbox(px, py, SN_C, SN_C, 3, dead ? C::RED : C::LIME);
          int16_t ex = DX[dir], ey = DY[dir];                  // eyes look ahead
          int16_t a1x = px + 5 + ey * 2 + ex * 2, a1y = py + 5 + ex * 2 + ey * 2;
          int16_t a2x = px + 5 - ey * 2 + ex * 2, a2y = py + 5 - ex * 2 + ey * 2;
          gfx.fill(a1x - 1, a1y - 1, 2, 2, C::BLACK);
          gfx.fill(a2x - 1, a2y - 1, 2, 2, C::BLACK);
        } else if (i == snLen - 1) gfx.rbox(px + 2, py + 2, SN_C - 4, SN_C - 4, 2, c);
        else gfx.rbox(px + 1, py + 1, SN_C - 2, SN_C - 2, 2, c);
      }
      if (!started) {
        gfx.rbox(60, 110, 200, 34, 8, C::BLACK);
        gfx.center(119, "press an arrow", C::WHITE);
      }
      gfx.send();
      if (dead) { delay(700); break; }
    }
    if (!dead) return;                                         // left from the pause menu
    again = gameOver(score);
  }
}
