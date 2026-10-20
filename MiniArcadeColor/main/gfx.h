// MiniArcade Color: 320 x 240 frame buffer with one byte per pixel and the
// drawing calls the games use. The byte is a colour index: a 6 x 7 x 6
// RGB cube (252 colours) plus four extra greys - more shades than 3-3-2
// RGB, and the whole picture still fits into 75 kB, so it works on the
// ESP32-C3 with WLAN running. tft_spi.cpp sends it to the panel.
#pragma once
#include <stdint.h>
#include <string.h>

#define SCR_W 320
#define SCR_H 240

typedef uint8_t col;

// the nearest colour of the cube, usable in constant expressions
constexpr col rgb(uint8_t r, uint8_t g, uint8_t b) {
  return (col)(((r * 5 + 127) / 255) * 42 + ((g * 6 + 127) / 255) * 6 + (b * 5 + 127) / 255);
}
// the extra greys between the cube's (index 252..255)
constexpr col GREY1 = 252, GREY2 = 253, GREY3 = 254, GREY4 = 255;

// a small set of named colours used all over the console
namespace C {
  constexpr col BLACK  = rgb(0, 0, 0),       WHITE  = rgb(255, 255, 255);
  constexpr col RED    = rgb(255, 51, 51),   DKRED  = rgb(153, 0, 0);
  constexpr col ORANGE = rgb(255, 153, 0),   YELLOW = rgb(255, 230, 0);
  constexpr col GREEN  = rgb(51, 204, 51),   DKGRN  = rgb(0, 102, 0),  LIME = rgb(153, 255, 51);
  constexpr col CYAN   = rgb(0, 230, 255),   BLUE   = rgb(51, 102, 255), DKBLUE = rgb(0, 0, 102);
  constexpr col NAVY   = rgb(0, 0, 51),      PURPLE = rgb(153, 51, 255), PINK = rgb(255, 102, 204);
  constexpr col BROWN  = rgb(153, 102, 51),  SAND   = rgb(230, 204, 153);
  constexpr col GREY   = rgb(128, 128, 128), DKGREY = rgb(64, 64, 64),   LTGREY = rgb(192, 192, 192);
}

enum Font : uint8_t {
  F_SMALL,     //  5 x  9 cells (the OLED font), for dense info
  F_TEXT,      // 10 x 18: the small font twice as big - menus and text
  F_BOLD,      // 14 x 24: the bold font twice as big - titles
  F_BIG        // 21 x 36: three times - scores, GAME OVER
};

class Gfx {
public:
  void     begin();                       // panel on (tft_spi.cpp)
  void     send();                        // frame to the panel, unchanged rows are skipped
  void     setBacklight(uint8_t pct);     // only with a backlight pin
  void     setOrientation(bool flip, bool bgr, bool invert);   // panel mounted the other way, colours swapped
  void     sleep(bool off);

  uint8_t *buffer() { return fb; }
  void     clear(col c = C::BLACK) { memset(fb, c, sizeof(fb)); }

  inline void pixel(int16_t x, int16_t y, col c) {
    if ((uint16_t)x < SCR_W && (uint16_t)y < SCR_H) fb[y * SCR_W + x] = c;
  }
  col      get(int16_t x, int16_t y) const { return ((uint16_t)x < SCR_W && (uint16_t)y < SCR_H) ? fb[y * SCR_W + x] : 0; }
  void     hline(int16_t x, int16_t y, int16_t w, col c);
  void     vline(int16_t x, int16_t y, int16_t h, col c);
  void     fill(int16_t x, int16_t y, int16_t w, int16_t h, col c);
  void     frame(int16_t x, int16_t y, int16_t w, int16_t h, col c, uint8_t thick = 1);
  void     line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, col c);
  void     circle(int16_t cx, int16_t cy, int16_t r, col c);
  void     disc(int16_t cx, int16_t cy, int16_t r, col c);
  void     rbox(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, col c);         // filled, round corners
  void     rframe(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, col c);
  void     triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, col c);
  void     gradient(int16_t x, int16_t y, int16_t w, int16_t h, col top, col bottom);  // vertical, stepped
  void     dim(int16_t x, int16_t y, int16_t w, int16_t h);                             // darken (dialog backdrop)

  /* Sprites written as text, one string per row: every character is a
     pixel, '.' or ' ' is transparent, the others look up `pal` ("Rr" ->
     red...). s = scale.                                               */
  void     sprite(int16_t x, int16_t y, const char *const *rows, uint8_t h, const char *keys, const col *pal, uint8_t s = 1, bool mirror = false);
  // a one-colour bitmap: rows of up to 32 pixels, bit 31 = left
  void     bits(int16_t x, int16_t y, const uint32_t *rows, uint8_t w, uint8_t h, col c, uint8_t s = 1);

  // text, (x, y) = top left; returns the x after the text
  int16_t  text(int16_t x, int16_t y, const char *s, col c, Font f = F_TEXT);
  int16_t  textShadow(int16_t x, int16_t y, const char *s, col c, col shadow, Font f = F_TEXT);
  void     center(int16_t y, const char *s, col c, Font f = F_TEXT);
  void     right(int16_t xr, int16_t y, const char *s, col c, Font f = F_TEXT);
  static int16_t width(const char *s, Font f = F_TEXT);
  static int16_t height(Font f);

private:
  uint8_t fb[SCR_W * SCR_H];
};

extern Gfx gfx;

// the display's pins, found at the start (-1 = not wired / not found)
struct TftPins { int8_t sck, mosi, cs, dc, miso, rst; };
extern TftPins tftPins;
extern bool    tftFound;                       // true: found by reading the panel back
bool tftPinUsed(uint8_t p);

// colour helpers on the cube
col  colLerp(col a, col b, uint8_t t);         // t = 0..255
col  colDark(col c, uint8_t steps = 1);
col  colLight(col c, uint8_t steps = 1);
void colToRgb(col c, uint8_t &r, uint8_t &g, uint8_t &b);
