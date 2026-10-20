// MiniArcade Color: Invaders. Part of console.cpp, which includes it.
// LEFT/RIGHT move the cannon, OK (or UP) fires - one shot at a time.
// 3 rows of 7 aliens march sideways and step down at the edges, dropping
// bombs. 10 points per alien, 50 for a cleared wave; every wave marches a
// little faster and the last few aliens rush. 3 lives; the game also ends
// when the aliens reach the cannon.

#define IV_ROWS   3
#define IV_COLS   7
#define IV_CW     30                   // cell size
#define IV_CH     26
#define IV_AW     24                   // alien size inside its cell
#define IV_AH     16
#define IV_TOP    (TOP_H + 14)         // where a wave starts
#define IV_STEP   5                    // px per alien step ...
#define IV_DROP   22                   // ... and down at the edges
#define IV_SHIPW  26
#define IV_SHIPY  212                  // cannon top
#define IV_GROUND 230
#define IV_BOMBS  3
#define IV_BOOMS  6
#define IV_TICK   25                   // ms per game step: cannon 160 px/s, shot 280, bombs 160

// three alien kinds (one per row), two frames each, drawn twice as big
static const char *const IV_ALIEN[3][2][8] = {
  { { "....kk......", "...kkkk.....", "..kkkkkk....", ".kk.kk.kk...", ".kkkkkkkk...", "...k..k.....", "..k.kk.k....", ".k.k..k.k..." },
    { "....kk......", "...kkkk.....", "..kkkkkk....", ".kk.kk.kk...", ".kkkkkkkk...", "..k.kk.k....", ".k......k...", "..k....k...." } },
  { { "..k.....k...", "...k...k....", "..kkkkkkk...", ".kk.kkk.kk..", "kkkkkkkkkkk.", "k.kkkkkkk.k.", "k.k.....k.k.", "...kk.kk...." },
    { "..k.....k...", "k..k...k..k.", "k.kkkkkkk.k.", "kkk.kkk.kkk.", "kkkkkkkkkkk.", ".kkkkkkkkk..", "..k.....k...", ".k.......k.." } },
  { { "....kkkk....", ".kkkkkkkkkk.", "kkkkkkkkkkkk", "kkk..kk..kkk", "kkkkkkkkkkkk", "...kk..kk...", "..kk.kk.kk..", "kk........kk" },
    { "....kkkk....", ".kkkkkkkkkk.", "kkkkkkkkkkkk", "kkk..kk..kkk", "kkkkkkkkkkkk", "..kkk..kkk..", ".kk..kk..kk.", "..kk....kk.." } } };
static const col IV_ROWCOL[IV_ROWS] = { C::PINK, C::CYAN, C::ORANGE };

static const char *const IV_SHIP[8] = {
  "......w......", ".....wcw.....", ".....ccc.....", "..ccccccccc..",
  ".ccccccccccc.", "ccccccccccccc", "cddcccccccddc", "dd.........dd" };
static const char IV_SHIP_KEYS[] = "wcd";
static const col  IV_SHIP_PAL[]  = { C::WHITE, C::LIME, C::GREEN };

struct IvBoom { int16_t x, y; uint32_t t; col c; };

static void ivBoom(IvBoom *b, int16_t x, int16_t y, col c, uint32_t now) {
  uint8_t k = 0;
  for (uint8_t i = 1; i < IV_BOOMS; i++) if (b[i].t < b[k].t) k = i;   // the oldest makes room
  b[k] = { x, y, now ? now : 1, c };
}

static void ivDrawBoom(const IvBoom &b, uint32_t now) {
  uint32_t age = now - b.t;
  if (!b.t || age >= 360) return;
  static const int8_t DX[8] = { 10, 7, 0, -7, -10, -7, 0, 7 }, DY[8] = { 0, 7, 10, 7, 0, -7, -10, -7 };
  int16_t r = (int16_t)(4 + age / 25);
  col c = age < 100 ? C::WHITE : (age < 230 ? b.c : colDark(b.c, 2));
  for (uint8_t k = 0; k < 8; k++) gfx.fill(b.x + DX[k] * r / 10 - 1, b.y + DY[k] * r / 10 - 1, 3, 3, c);
  if (age < 150) gfx.disc(b.x, b.y, (int16_t)(7 - age / 25), C::YELLOW);
}

// a few fixed stars that twinkle, and a far planet
static void ivSky(uint32_t now) {
  gfx.fill(0, TOP_H, SCR_W, IV_GROUND - TOP_H, C::BLACK);
  gfx.gradient(0, IV_GROUND - 36, SCR_W, 36, C::BLACK, rgb(70, 0, 130));   // glow over the horizon
  gfx.disc(268, 78, 26, rgb(60, 30, 90));
  gfx.disc(262, 72, 20, rgb(90, 50, 130));
  gfx.hline(234, 84, 70, rgb(120, 90, 160));
  for (uint8_t i = 0; i < 48; i++) {
    uint32_t h = (uint32_t)(i + 7) * 2654435761u;
    int16_t x = (int16_t)((h >> 8) % SCR_W), y = (int16_t)(TOP_H + (h >> 18) % (IV_GROUND - TOP_H - 4));
    bool tw = ((now / 250 + i) % 9) == 0;
    gfx.pixel(x, y, tw ? C::WHITE : (i % 3 ? C::GREY : C::LTGREY));
  }
}

void invadersRun() {
  bool again = true;
  while (again) {
    again = false;
    uint8_t alive[IV_ROWS];
    for (uint8_t r = 0; r < IV_ROWS; r++) alive[r] = (1 << IV_COLS) - 1;
    uint16_t score = 0;
    uint8_t lives = 3, wave = 1, stepMs = 90, baseMs = 90, frame = 0;
    int16_t ship = (SCR_W - IV_SHIPW) / 2, ax = 4, ay = IV_TOP;
    int8_t adir = 1;
    int16_t shotX = -1, shotY = 0;
    int16_t bombX[IV_BOMBS] = { -1, -1, -1 }, bombY[IV_BOMBS] = { 0, 0, 0 };
    IvBoom booms[IV_BOOMS];
    memset(booms, 0, sizeof(booms));
    uint32_t now = gameMillis(), tick = now, next = now + stepMs, hitAt = 0, waveAt = now;
    bool over = false;
    btnClear();
    while (poll()) {
      now = gameMillis();
      bool fire = btnTap(B_OK) | btn(B_UP);
      if (fire && shotX < 0) { shotX = ship + IV_SHIPW / 2; shotY = IV_SHIPY - 8; sfx(280, 30); }

      if ((int32_t)(now - tick) > 40) tick = now - 40;         // a slow frame: catch up at most 40 ms
      while ((int32_t)(now - tick) >= 0 && !over) {
        tick += IV_TICK;
        if (btnHeld(B_LEFT)  && ship > 4)                      ship -= 4;
        if (btnHeld(B_RIGHT) && ship < SCR_W - 4 - IV_SHIPW)   ship += 4;
        if (shotX >= 0) { shotY -= 7; if (shotY < TOP_H) shotX = -1; }
        for (uint8_t b = 0; b < IV_BOMBS; b++) {
          if (bombX[b] < 0) continue;
          bombY[b] += 4;
          if (bombY[b] + 10 >= IV_GROUND) { ivBoom(booms, bombX[b], IV_GROUND - 2, C::RED, now); bombX[b] = -1; }
          else if (bombY[b] + 10 >= IV_SHIPY + 4 && bombY[b] < IV_SHIPY + 16 && abs(bombX[b] - (ship + IV_SHIPW / 2)) < 13) {
            bombX[b] = -1;
            ivBoom(booms, ship + IV_SHIPW / 2, IV_SHIPY + 8, C::LIME, now);
            hitAt = now;
            sfx(150, 300);
            if (--lives == 0) over = true;
          }
        }
        if (shotX >= 0)                                        // shot vs aliens
          for (uint8_t r = 0; r < IV_ROWS && shotX >= 0; r++)
            for (uint8_t c = 0; c < IV_COLS; c++) {
              if (!(alive[r] & (1 << c))) continue;
              int16_t x = ax + c * IV_CW + (IV_CW - IV_AW) / 2, y = ay + r * IV_CH;
              if (shotX >= x && shotX < x + IV_AW && shotY < y + IV_AH && shotY + 10 > y) {
                alive[r] &= (uint8_t)~(1 << c);
                ivBoom(booms, x + IV_AW / 2, y + IV_AH / 2, IV_ROWCOL[r], now);
                shotX = -1;
                score += 10;
                sfx(880, 40);
                break;
              }
            }

        if ((int32_t)(tick - next) >= 0 && !over) {           // the aliens step
          next = tick + stepMs;
          frame ^= 1;
          ax += adir * IV_STEP;
          if (ax < 4 || ax + IV_COLS * IV_CW > SCR_W - 4) { adir = (int8_t)-adir; ay += IV_DROP; sfx(90, 25); }
          int8_t low = IV_ROWS - 1;                            // lowest row that still has aliens
          while (low > 0 && !alive[low]) low--;
          if (ay + low * IV_CH + IV_AH >= IV_SHIPY) { over = true; sfx(150, 400); }

          for (uint8_t b = 0; b < IV_BOMBS; b++)               // drop a bomb
            if (bombX[b] < 0 && random(100) < 12) {
              uint8_t c = (uint8_t)random(IV_COLS);
              for (int8_t r = IV_ROWS - 1; r >= 0; r--)
                if (alive[r] & (1 << c)) {
                  bombX[b] = ax + c * IV_CW + IV_CW / 2;
                  bombY[b] = ay + r * IV_CH + IV_AH;
                  break;
                }
              break;
            }

          uint8_t left = 0;
          for (uint8_t r = 0; r < IV_ROWS; r++)
            for (uint8_t c = 0; c < IV_COLS; c++) if (alive[r] & (1 << c)) left++;
          if (!left) {                                         // next wave
            wave++;
            score += 50;
            ay = IV_TOP; ax = 4; adir = 1;
            if (baseMs > 50) baseMs -= 10;                     // each wave a little faster
            stepMs = baseMs;
            for (uint8_t r = 0; r < IV_ROWS; r++) alive[r] = (1 << IV_COLS) - 1;
            waveAt = now;
            sfx(1500, 120);
          } else stepMs = (left < 6 && baseMs > 45) ? 45 : baseMs;   // the last few rush
        }
      }

      // ---- draw ----
      statusBar("INVADERS", score, curHigh, C::GREEN);
      ivSky(now);
      gfx.fill(0, IV_GROUND, SCR_W, SCR_H - IV_GROUND, rgb(40, 25, 70));
      gfx.hline(0, IV_GROUND, SCR_W, rgb(150, 110, 220));
      gfx.hline(0, IV_GROUND + 1, SCR_W, rgb(90, 60, 140));
      for (uint8_t r = 0; r < IV_ROWS; r++) {
        col pal[1] = { IV_ROWCOL[r] }, sh[1] = { colDark(IV_ROWCOL[r], 3) };
        for (uint8_t c = 0; c < IV_COLS; c++) {
          if (!(alive[r] & (1 << c))) continue;
          int16_t x = ax + c * IV_CW + (IV_CW - IV_AW) / 2, y = ay + r * IV_CH;
          gfx.sprite(x + 2, y + 2, IV_ALIEN[r][frame], 8, "k", sh, 2);
          gfx.sprite(x, y, IV_ALIEN[r][frame], 8, "k", pal, 2);
        }
      }
      for (uint8_t b = 0; b < IV_BOMBS; b++)                    // bombs: a little zigzag
        if (bombX[b] >= 0) {
          int16_t x = bombX[b], y = bombY[b];
          int8_t w = ((now / 80) % 2) ? 2 : -2;
          gfx.line(x, y, x + w, y + 3, C::RED);
          gfx.line(x + w, y + 3, x - w, y + 6, C::ORANGE);
          gfx.line(x - w, y + 6, x, y + 9, C::YELLOW);
        }
      if (shotX >= 0) {
        gfx.fill(shotX - 1, shotY, 3, 10, C::YELLOW);
        gfx.vline(shotX, shotY, 10, C::WHITE);
      }
      bool blink = hitAt && now - hitAt < 600 && (now / 80) % 2;
      if (!blink && !(over && lives == 0)) gfx.sprite(ship, IV_SHIPY, IV_SHIP, 8, IV_SHIP_KEYS, IV_SHIP_PAL, 2);
      for (uint8_t i = 0; i < IV_BOOMS; i++) ivDrawBoom(booms[i], now);
      // the ground band: wave and spare cannons
      char b[16];
      snprintf(b, sizeof(b), "WAVE %u", wave);
      gfx.text(6, IV_GROUND + 2, b, C::LTGREY, F_SMALL);
      for (uint8_t i = 1; i < lives; i++) gfx.sprite(SCR_W - 4 - i * 16, IV_GROUND + 1, IV_SHIP, 8, IV_SHIP_KEYS, IV_SHIP_PAL, 1);
      if (now - waveAt < 1200 && wave > 1) {
        snprintf(b, sizeof(b), "WAVE %u", wave);
        gfx.textShadow((SCR_W - Gfx::width(b, F_BOLD)) / 2, 150, b, C::YELLOW, C::DKRED, F_BOLD);
      }
      gfx.send();
      if (over) { delay(700); break; }
    }
    if (!over) return;
    again = gameOver(score);
  }
}

static const char *const IC_INVADERS[12] = {
  "..g......g..", "...g....g...", "..gggggggg..", ".gg.gggg.gg.", "gggggggggggg", "g.gggggggg.g",
  "g.g......g.g", "...gg..gg...", ".....y......", ".....c......", "...ccccc....", "..ccccccc..." };
