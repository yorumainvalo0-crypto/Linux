// MiniArcade: Lights Out. Part of MiniArcade.ino, which includes it -
// not compiled on its own.

// =========================================================
//  LIGHTS OUT   (switch every light off)
// =========================================================
/* Pressing a field switches it and its four neighbours. A level is made by
   pressing "par" random fields on a dark board, so it can always be
   solved in par moves. More than twice par + 3 moves ends the game.
   Points: 10 per level plus the moves you had left.                     */
#define LO_N    5
#define LO_C    9                    // cell size in pixels

static uint32_t loBoard;             // bit r * 5 + c = light on
static uint8_t  loR, loC;            // cursor

uint32_t loPress(uint32_t b, uint8_t r, uint8_t c) {
  b ^= 1UL << (r * LO_N + c);
  if (r > 0)        b ^= 1UL << ((r - 1) * LO_N + c);
  if (r < LO_N - 1) b ^= 1UL << ((r + 1) * LO_N + c);
  if (c > 0)        b ^= 1UL << (r * LO_N + c - 1);
  if (c < LO_N - 1) b ^= 1UL << (r * LO_N + c + 1);
  return b;
}

// a new level with "par" different fields pressed; never an empty board
uint32_t loMake(uint8_t par) {
  uint32_t b = 0, used = 0;
  while (!b) {
    b = 0; used = 0;
    for (uint8_t k = 0; k < par; k++) {
      uint8_t i;
      do i = random(LO_N * LO_N); while (used & (1UL << i));
      used |= 1UL << i;
      b = loPress(b, i / LO_N, i % LO_N);
    }
  }
  return b;
}

static void loDraw(uint8_t cr, uint8_t cc, uint8_t level, uint8_t moves, uint8_t limit, const char *msg) {
  int16_t ox = 4, oy = TOP_H + 2;
  for (uint8_t r = 0; r < LO_N; r++)
    for (uint8_t c = 0; c < LO_N; c++) {
      int16_t x = ox + c * LO_C, y = oy + r * LO_C;
      if (loBoard & (1UL << (r * LO_N + c))) oled.drawBox(x + 1, y + 1, LO_C - 2, LO_C - 2);
      else oled.drawPixel(x + LO_C / 2, y + LO_C / 2);
      if (r == cr && c == cc) {
        oled.drawFrame(x - 1, y - 1, LO_C + 2, LO_C + 2);
        if (loBoard & (1UL << (r * LO_N + c))) { oled.setDrawColor(0); oled.drawPixel(x + LO_C / 2, y + LO_C / 2); oled.setDrawColor(1); }
      }
    }
  char b[20];
  int16_t tx = ox + LO_N * LO_C + 8;
  snprintf(b, sizeof(b), "LEVEL %u", level);  oled.drawStr(tx, TOP_H + 10, b);
  snprintf(b, sizeof(b), "moves %u", moves);  oled.drawStr(tx, TOP_H + 22, b);
  snprintf(b, sizeof(b), "limit %u", limit);  oled.drawStr(tx, TOP_H + 32, b);
  if (msg) { oled.setFont(FONT_B); oled.drawStr(tx, TOP_H + 46, msg); oled.setFont(FONT); }
  else oled.drawStr(tx, TOP_H + 44, "OK=press");
}

void lightsOutRun() {
  bool again = true;
  while (again) {
    again = false;
    uint16_t score = 0;
    uint8_t  level = 1, &cr = loR, &cc = loC, limit = 0, moves = 0;
    cr = cc = 2;
    bool over = false;
    while (!over) {
      uint8_t par = 2 + level;                 // 3, 4, 5, ... up to 15
      if (par > 15) par = 15;
      loBoard = loMake(par);
      limit = par * 2 + 3; moves = 0;
      btnClear();
      while (true) {
        if (!poll()) return;
        if (btn(B_UP))    { cr = (cr + LO_N - 1) % LO_N; sfx(700, 10); }
        if (btn(B_DOWN))  { cr = (cr + 1) % LO_N;        sfx(700, 10); }
        if (btn(B_LEFT))  { cc = (cc + LO_N - 1) % LO_N; sfx(700, 10); }
        if (btn(B_RIGHT)) { cc = (cc + 1) % LO_N;        sfx(700, 10); }
        if (btnTap(B_OK)) {
          loBoard = loPress(loBoard, cr, cc);
          moves++;
          sfx(loBoard & (1UL << (cr * LO_N + cc)) ? 1100 : 800, 30);
        }
        statusBar("LIGHTS", score, curHigh);
        if (!loBoard) {                          // all dark: next level
          score += 10 + (limit - moves);
          sfx(1500, 150);
          for (uint8_t i = 0; i < 40 && poll(); i++) {
            statusBar("LIGHTS", score, curHigh);
            loDraw(255, 255, level, moves, limit, "DONE!");
            oled.sendBuffer();
          }
          level++;
          break;
        }
        if (moves >= limit) { over = true; break; }
        loDraw(cr, cc, level, moves, limit, NULL);
        oled.sendBuffer();
      }
    }
    for (uint8_t i = 0; i < 40 && poll(); i++) {
      statusBar("LIGHTS", score, curHigh);
      loDraw(255, 255, level, moves, limit, "LIMIT");
      oled.sendBuffer();
    }
    again = gameOver(score);
  }
}
