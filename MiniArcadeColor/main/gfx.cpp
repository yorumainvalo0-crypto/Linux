// MiniArcade Color: drawing into the frame buffer (no hardware here, the
// host tests use it as it is). See gfx.h.
#include "gfx.h"
#include "fonts.h"
#include <stdlib.h>

Gfx gfx;

#ifdef ARCADE_TRACE
extern void arcadeTraceText(int16_t x, int16_t y, const char *s, Font f);   // host test harness only
#endif

// ---------------- colours ----------------
static const uint8_t EXTRA_GREY[4] = { 32, 96, 160, 224 };

void colToRgb(col c, uint8_t &r, uint8_t &g, uint8_t &b) {
  if (c >= 252) { r = g = b = EXTRA_GREY[c - 252]; return; }
  r = (uint8_t)((c / 42) * 51);
  g = (uint8_t)(((c / 6) % 7) * 255 / 6);
  b = (uint8_t)((c % 6) * 51);
}

col colLerp(col a, col b, uint8_t t) {
  uint8_t r0, g0, b0, r1, g1, b1;
  colToRgb(a, r0, g0, b0);
  colToRgb(b, r1, g1, b1);
  return rgb((uint8_t)(r0 + (r1 - r0) * t / 255), (uint8_t)(g0 + (g1 - g0) * t / 255),
             (uint8_t)(b0 + (b1 - b0) * t / 255));
}

col colDark(col c, uint8_t steps) {
  uint8_t r, g, b;
  colToRgb(c, r, g, b);
  for (uint8_t i = 0; i < steps; i++) { r = r * 2 / 3; g = g * 2 / 3; b = b * 2 / 3; }
  return rgb(r, g, b);
}

col colLight(col c, uint8_t steps) {
  uint8_t r, g, b;
  colToRgb(c, r, g, b);
  for (uint8_t i = 0; i < steps; i++) { r += (255 - r) / 3; g += (255 - g) / 3; b += (255 - b) / 3; }
  return rgb(r, g, b);
}

// ---------------- shapes ----------------
void Gfx::hline(int16_t x, int16_t y, int16_t w, col c) {
  if ((uint16_t)y >= SCR_H || w <= 0) return;
  if (x < 0) { w += x; x = 0; }
  if (x + w > SCR_W) w = SCR_W - x;
  if (w > 0) memset(fb + y * SCR_W + x, c, w);
}

void Gfx::vline(int16_t x, int16_t y, int16_t h, col c) {
  if ((uint16_t)x >= SCR_W || h <= 0) return;
  if (y < 0) { h += y; y = 0; }
  if (y + h > SCR_H) h = SCR_H - y;
  for (int16_t j = 0; j < h; j++) fb[(y + j) * SCR_W + x] = c;
}

void Gfx::fill(int16_t x, int16_t y, int16_t w, int16_t h, col c) {
  if (y < 0) { h += y; y = 0; }
  if (y + h > SCR_H) h = SCR_H - y;
  for (int16_t j = 0; j < h; j++) hline(x, y + j, w, c);
}

void Gfx::frame(int16_t x, int16_t y, int16_t w, int16_t h, col c, uint8_t t) {
  for (uint8_t i = 0; i < t; i++) {
    hline(x + i, y + i, w - 2 * i, c);
    hline(x + i, y + h - 1 - i, w - 2 * i, c);
    vline(x + i, y + i, h - 2 * i, c);
    vline(x + w - 1 - i, y + i, h - 2 * i, c);
  }
}

void Gfx::line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, col c) {
  int16_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int16_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    pixel(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int16_t e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void Gfx::circle(int16_t cx, int16_t cy, int16_t r, col c) {
  int16_t x = r, y = 0, err = 1 - r;
  while (x >= y) {
    pixel(cx + x, cy + y, c); pixel(cx - x, cy + y, c); pixel(cx + x, cy - y, c); pixel(cx - x, cy - y, c);
    pixel(cx + y, cy + x, c); pixel(cx - y, cy + x, c); pixel(cx + y, cy - x, c); pixel(cx - y, cy - x, c);
    y++;
    if (err < 0) err += 2 * y + 1;
    else { x--; err += 2 * (y - x) + 1; }
  }
}

void Gfx::disc(int16_t cx, int16_t cy, int16_t r, col c) {
  for (int16_t dy = -r; dy <= r; dy++) {
    int16_t dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r) dx++;
    hline(cx - dx, cy + dy, 2 * dx + 1, c);
  }
}

void Gfx::rbox(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, col c) {
  if (2 * r > w) r = w / 2;
  if (2 * r > h) r = h / 2;
  fill(x, y + r, w, h - 2 * r, c);
  for (int16_t j = 0; j < r; j++) {
    int16_t dy = r - j, dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= r * r) dx++;
    int16_t inset = r - dx;
    hline(x + inset, y + j, w - 2 * inset, c);
    hline(x + inset, y + h - 1 - j, w - 2 * inset, c);
  }
}

void Gfx::rframe(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r, col c) {
  if (2 * r > w) r = w / 2;
  if (2 * r > h) r = h / 2;
  hline(x + r, y, w - 2 * r, c);
  hline(x + r, y + h - 1, w - 2 * r, c);
  vline(x, y + r, h - 2 * r, c);
  vline(x + w - 1, y + r, h - 2 * r, c);
  int16_t px = r, py = 0, err = 1 - r;                  // quarter circles in the corners
  while (px >= py) {
    int16_t ax[2] = { px, py }, ay[2] = { py, px };
    for (uint8_t k = 0; k < 2; k++) {
      pixel(x + r - ax[k], y + r - ay[k], c);
      pixel(x + w - 1 - r + ax[k], y + r - ay[k], c);
      pixel(x + r - ax[k], y + h - 1 - r + ay[k], c);
      pixel(x + w - 1 - r + ax[k], y + h - 1 - r + ay[k], c);
    }
    py++;
    if (err < 0) err += 2 * py + 1;
    else { px--; err += 2 * (py - px) + 1; }
  }
}

void Gfx::triangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, col c) {
  // sort by y, then fill scan lines between the edges
  if (y0 > y1) { int16_t t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
  if (y1 > y2) { int16_t t = y1; y1 = y2; y2 = t; t = x1; x1 = x2; x2 = t; }
  if (y0 > y1) { int16_t t = y0; y0 = y1; y1 = t; t = x0; x0 = x1; x1 = t; }
  if (y2 == y0) { int16_t a = x0 < x1 ? x0 : x1, b = x0 > x1 ? x0 : x1; if (x2 < a) a = x2; if (x2 > b) b = x2; hline(a, y0, b - a + 1, c); return; }
  for (int16_t y = y0; y <= y2; y++) {
    int32_t xa = x0 + (int32_t)(x2 - x0) * (y - y0) / (y2 - y0);
    int32_t xb = (y < y1) ? (y1 == y0 ? x1 : x0 + (int32_t)(x1 - x0) * (y - y0) / (y1 - y0))
                          : (y2 == y1 ? x1 : x1 + (int32_t)(x2 - x1) * (y - y1) / (y2 - y1));
    if (xa > xb) { int32_t t = xa; xa = xb; xb = t; }
    hline((int16_t)xa, y, (int16_t)(xb - xa + 1), c);
  }
}

void Gfx::gradient(int16_t x, int16_t y, int16_t w, int16_t h, col top, col bottom) {
  for (int16_t j = 0; j < h; j++) hline(x, y + j, w, colLerp(top, bottom, (uint8_t)(h > 1 ? j * 255 / (h - 1) : 0)));
}

void Gfx::dim(int16_t x, int16_t y, int16_t w, int16_t h) {
  static uint8_t lut[256];
  static bool ready = false;
  if (!ready) { for (int i = 0; i < 256; i++) lut[i] = colDark((col)i, 2); ready = true; }
  for (int16_t j = y < 0 ? 0 : y; j < y + h && j < SCR_H; j++)
    for (int16_t i = x < 0 ? 0 : x; i < x + w && i < SCR_W; i++) fb[j * SCR_W + i] = lut[fb[j * SCR_W + i]];
}

// ---------------- sprites ----------------
void Gfx::sprite(int16_t x, int16_t y, const char *const *rows, uint8_t h, const char *keys, const col *pal, uint8_t s, bool mirror) {
  for (uint8_t r = 0; r < h; r++) {
    const char *row = rows[r];
    int16_t w = (int16_t)strlen(row);
    for (int16_t i = 0; i < w; i++) {
      char ch = row[mirror ? w - 1 - i : i];
      if (ch == '.' || ch == ' ') continue;
      const char *k = strchr(keys, ch);
      if (!k) continue;
      col c = pal[k - keys];
      if (s == 1) pixel(x + i, y + r, c);
      else fill(x + i * s, y + r * s, s, s, c);
    }
  }
}

void Gfx::bits(int16_t x, int16_t y, const uint32_t *rows, uint8_t w, uint8_t h, col c, uint8_t s) {
  for (uint8_t r = 0; r < h; r++)
    for (uint8_t i = 0; i < w; i++)
      if (rows[r] & (0x80000000UL >> i)) {
        if (s == 1) pixel(x + i, y + r, c);
        else fill(x + i * s, y + r * s, s, s, c);
      }
}

// ---------------- text ----------------
/* The two bitmap fonts of the OLED console, drawn 1x, 2x or 3x. FONT_S is
   5 wide (incl. spacing) and 9 rows, FONT_L 7 wide and 12 rows. Texts are
   UTF-8: the German letters below have glyphs of their own here (the OLED
   console's fonts stay as they are), any other non-ASCII letter is one
   empty cell.                                                          */
static uint8_t fScale(Font f) { return f == F_SMALL ? 1 : (f == F_BIG ? 3 : 2); }
static bool    fBold(Font f)  { return f == F_BOLD || f == F_BIG; }

// ä ö ü Ä Ö Ü ß: the second byte of their UTF-8 (C3 xx), then the columns
static const uint8_t  DE_CODE[7] = { 0xA4, 0xB6, 0xBC, 0x84, 0x96, 0x9C, 0x9F };
static const uint16_t DE_S[7][5] = {
  {0x0,0x75,0x54,0x74,0x79}, {0x0,0x7D,0x44,0x44,0x39}, {0x0,0x7D,0x40,0x60,0x7D}, {0x61,0x3C,0x22,0x3C,0x71},
  {0x1D,0x7E,0x42,0x62,0x3D}, {0x0,0x7F,0x40,0x40,0x3F}, {0x0,0x7E,0x1,0x25,0x1A} };
static const uint16_t DE_L[7][7] = {
  {0x0,0x3C8,0x3CB,0x24B,0x348,0x3F3,0x3C3}, {0x0,0x1F3,0x31B,0x208,0x31B,0x1F3,0xE0},
  {0x0,0x1FB,0x3FB,0x200,0x3FB,0x3FB,0x0}, {0x200,0x3E1,0x7D,0x4C,0x7D,0x3F1,0x300},
  {0x0,0x1F9,0x38D,0x204,0x305,0x1F9,0x70}, {0x0,0x1FD,0x301,0x200,0x301,0x3FD,0xFC},
  {0x0,0x7FC,0x7FE,0x2,0x27E,0x3DC,0x180} };

// letters, not bytes: the second byte of a UTF-8 letter takes no room
static int16_t letters(const char *s) {
  int16_t n = 0;
  for (; *s; s++) if (((uint8_t)*s & 0xC0) != 0x80) n++;
  return n;
}

int16_t Gfx::width(const char *s, Font f) { return letters(s) * (fBold(f) ? 7 : 5) * fScale(f); }
int16_t Gfx::height(Font f)               { return (fBold(f) ? 12 : 9) * fScale(f); }

int16_t Gfx::text(int16_t x, int16_t y, const char *s, col c, Font f) {
#ifdef ARCADE_TRACE
  arcadeTraceText(x, y, s, f);
#endif
  uint8_t k = fScale(f);
  bool bold = fBold(f);
  for (; *s; s++) {
    uint8_t ch = (uint8_t)*s;
    uint8_t cw = bold ? 7 : 5;
    if ((ch & 0xC0) == 0x80) continue;                      // the rest of a UTF-8 letter
    const uint16_t *g = NULL;
    if (ch >= 32 && ch <= 126) g = bold ? FONT_L[ch - 32] : FONT_S[ch - 32];
    else if (ch == 0xC3)
      for (uint8_t i = 0; i < 7; i++) if ((uint8_t)s[1] == DE_CODE[i]) g = bold ? DE_L[i] : DE_S[i];
    if (g)
      for (uint8_t cx = 0; cx < cw; cx++) {
        uint16_t bitsCol = g[cx];
        for (uint8_t r = 0; r < (bold ? 12 : 9); r++)
          if (bitsCol & (1 << r)) {
            if (k == 1) pixel(x + cx, y + r, c);
            else fill(x + cx * k, y + r * k, k, k, c);
          }
      }
    x += cw * k;
  }
  return x;
}

int16_t Gfx::textShadow(int16_t x, int16_t y, const char *s, col c, col shadow, Font f) {
  uint8_t d = fScale(f);
  text(x + d, y + d, s, shadow, f);
  return text(x, y, s, c, f);
}

void Gfx::center(int16_t y, const char *s, col c, Font f) { text((SCR_W - width(s, f)) / 2, y, s, c, f); }
void Gfx::right(int16_t xr, int16_t y, const char *s, col c, Font f) { text(xr - width(s, f), y, s, c, f); }
