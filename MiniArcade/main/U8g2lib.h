// Minimal 1 bit framebuffer + SSD1306 driver with the U8g2 call names,
// so the game code compiles unchanged.
#pragma once
#include <stdint.h>
#include <string.h>

#define U8G2_R0        0
#define U8X8_PIN_NONE  255
#define u8g2_font_5x7_tr    0
#define u8g2_font_7x13B_tr  1

class Gfx {
public:
  Gfx(int, int) {}
  void begin();
  void setFontMode(int) {}
  void setContrast(uint8_t v);
  void setPowerSave(uint8_t off);
  void setFont(uint8_t f)      { font = f; }
  void setDrawColor(uint8_t c) { color = c; }
  void clearBuffer()           { memset(fb, 0, sizeof(fb)); }
  void sendBuffer();
  void drawPixel(int16_t x, int16_t y);
  void drawBox(int16_t x, int16_t y, int16_t w, int16_t h);
  void drawFrame(int16_t x, int16_t y, int16_t w, int16_t h);
  void drawHLine(int16_t x, int16_t y, int16_t w);
  void drawVLine(int16_t x, int16_t y, int16_t h);
  void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1);
  void drawStr(int16_t x, int16_t y, const char *s);
  int16_t getStrWidth(const char *s) { return (int16_t)strlen(s) * (font ? 7 : 5); }
  uint8_t *getBufferPtr()      { return fb; }   // same name as in U8g2
private:
  uint8_t fb[1024];            // 128 x 64, one bit per pixel, SSD1306 page order
  uint8_t font = 0, color = 1;
};
typedef Gfx U8G2_SSD1306_128X64_NONAME_F_HW_I2C;
