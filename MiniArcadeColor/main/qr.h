// MiniArcade Color: a small QR code maker, for a WLAN login or a link on
// the screen. Part of console.cpp, which includes it - not compiled on
// its own. Byte mode, error correction level L, versions 1 to 5 (21 to
// 37 modules, up to 106 characters): one block of data each, so no
// interleaving and no version field. All eight masks are tried and the
// one with the fewest bad patterns is kept (the four rules of the
// standard). The modules live in a buffer the caller hands in.

#define QR_MAXV   5
#define QR_MAXN   (17 + 4 * QR_MAXV)            // 37 modules a side
#define QR_BUF    (QR_MAXN * QR_MAXN)           // one byte per module: bit 0 dark, bit 1 fixed pattern

static const uint8_t QR_DATA[QR_MAXV + 1] = { 0, 19, 34, 55, 80, 108 };   // data codewords, level L
static const uint8_t QR_ECC[QR_MAXV + 1]  = { 0, 7, 10, 15, 20, 26 };    // error correction codewords

// multiplication in GF(256) with the QR polynomial x^8 + x^4 + x^3 + x^2 + 1
static uint8_t qrMul(uint8_t a, uint8_t b) {
  uint8_t r = 0;
  for (int8_t i = 7; i >= 0; i--) {
    r = (uint8_t)((r << 1) ^ ((r >> 7) * 0x1D));
    if ((b >> i) & 1) r ^= a;
  }
  return r;
}

struct Qr {
  uint8_t *m;                                   // n * n modules (QR_BUF bytes)
  uint8_t  n;
  void set(int x, int y, bool dark) { m[y * n + x] = (uint8_t)(2 | (dark ? 1 : 0)); }
  bool dark(int x, int y) const { return m[y * n + x] & 1; }
  bool fixed(int x, int y) const { return m[y * n + x] & 2; }
};

static void qrFinder(Qr &q, int cx, int cy) {
  for (int dy = -4; dy <= 4; dy++)
    for (int dx = -4; dx <= 4; dx++) {
      int x = cx + dx, y = cy + dy, d = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
      if (x >= 0 && y >= 0 && x < q.n && y < q.n) q.set(x, y, d != 2 && d != 4);
    }
}

// the 15 format bits (level L and the mask) in both places, and the dark module
static void qrFormat(Qr &q, uint8_t mask) {
  uint16_t data = (uint16_t)(1 << 3 | mask), rem = data;
  for (uint8_t i = 0; i < 10; i++) rem = (uint16_t)((rem << 1) ^ ((rem >> 9) * 0x537));
  uint16_t b = (uint16_t)((data << 10 | (rem & 0x3FF)) ^ 0x5412);
  int n = q.n;
  for (int i = 0; i <= 5; i++) q.set(8, i, b >> i & 1);
  q.set(8, 7, b >> 6 & 1); q.set(8, 8, b >> 7 & 1); q.set(7, 8, b >> 8 & 1);
  for (int i = 9; i < 15; i++) q.set(14 - i, 8, b >> i & 1);
  for (int i = 0; i < 8; i++) q.set(n - 1 - i, 8, b >> i & 1);
  for (int i = 8; i < 15; i++) q.set(8, n - 15 + i, b >> i & 1);
  q.set(8, n - 8, true);
}

static bool qrMaskBit(uint8_t mask, int x, int y) {
  switch (mask) {
  case 0: return (x + y) % 2 == 0;
  case 1: return y % 2 == 0;
  case 2: return x % 3 == 0;
  case 3: return (x + y) % 3 == 0;
  case 4: return (x / 3 + y / 2) % 2 == 0;
  case 5: return x * y % 2 + x * y % 3 == 0;
  case 6: return (x * y % 2 + x * y % 3) % 2 == 0;
  default: return ((x + y) % 2 + x * y % 3) % 2 == 0;
  }
}
static void qrMask(Qr &q, uint8_t mask) {
  for (int y = 0; y < q.n; y++)
    for (int x = 0; x < q.n; x++)
      if (!q.fixed(x, y) && qrMaskBit(mask, x, y)) q.m[y * q.n + x] ^= 1;
}

// how bad a masked code looks: long runs, 2 x 2 blocks, finder look-alikes, dark / light balance
static int32_t qrPenalty(const Qr &q) {
  int32_t p = 0, darkN = 0;
  int n = q.n;
  for (int pass = 0; pass < 2; pass++)                          // rows, then columns
    for (int a = 0; a < n; a++) {
      int run = 0;
      bool last = false;
      uint16_t win = 0;                                          // the last 11 modules
      for (int b = 0; b < n; b++) {
        bool d = pass ? q.dark(a, b) : q.dark(b, a);
        if (b && d == last) { if (++run == 5) p += 3; else if (run > 5) p++; }
        else run = 1;
        last = d;
        win = (uint16_t)(((win << 1) | d) & 0x7FF);
        if (b >= 10 && (win == 0x05D || win == 0x5D0)) p += 40;    // 1011101 with four light on a side
      }
    }
  for (int y = 0; y < n; y++)
    for (int x = 0; x < n; x++) {
      bool d = q.dark(x, y);
      darkN += d;
      if (x + 1 < n && y + 1 < n && d == q.dark(x + 1, y) && d == q.dark(x, y + 1) && d == q.dark(x + 1, y + 1)) p += 3;
    }
  int32_t total = n * n, k = (abs(darkN * 20 - total * 10) + total - 1) / total - 1;
  return p + (k > 0 ? k : 0) * 10;
}

/* text -> modules in buf (QR_BUF bytes); returns the size n (21..37) or 0
   when the text is too long. mask < 8 forces a mask (the tests).       */
static uint8_t qrMake(const char *text, uint8_t *buf, uint8_t mask = 255) {
  size_t len = strlen(text);
  uint8_t v = 1;
  while (v <= QR_MAXV && len + 2 > QR_DATA[v]) v++;
  if (v > QR_MAXV) return 0;
  uint8_t cw[108 + 26];
  uint8_t dc = QR_DATA[v], ec = QR_ECC[v];
  // the bit stream: mode 0100, 8 bit count, the bytes, end mark, padding
  memset(cw, 0, sizeof(cw));
  uint16_t bit = 0;
  auto put = [&](uint32_t val, uint8_t nb) {
    for (int8_t i = (int8_t)(nb - 1); i >= 0; i--, bit++) if (val >> i & 1) cw[bit >> 3] |= (uint8_t)(0x80 >> (bit & 7));
  };
  put(4, 4);
  put((uint32_t)len, 8);
  for (size_t i = 0; i < len; i++) put((uint8_t)text[i], 8);
  uint16_t cap = (uint16_t)(dc * 8);
  put(0, (uint8_t)(cap - bit < 4 ? cap - bit : 4));
  bit = (uint16_t)((bit + 7) & ~7);
  for (uint8_t pad = 0xEC; bit < cap; pad ^= 0xEC ^ 0x11) { cw[bit >> 3] = pad; bit += 8; }
  // Reed-Solomon: the divisor, then the remainder of the data
  uint8_t div[26], rem[26];
  memset(div, 0, sizeof(div));
  div[ec - 1] = 1;
  uint8_t root = 1;
  for (uint8_t i = 0; i < ec; i++) {
    for (uint8_t j = 0; j < ec; j++) {
      div[j] = qrMul(div[j], root);
      if (j + 1 < ec) div[j] ^= div[j + 1];
    }
    root = qrMul(root, 2);
  }
  memset(rem, 0, sizeof(rem));
  for (uint8_t i = 0; i < dc; i++) {
    uint8_t f = cw[i] ^ rem[0];
    memmove(rem, rem + 1, ec - 1);
    rem[ec - 1] = 0;
    for (uint8_t j = 0; j < ec; j++) rem[j] ^= qrMul(div[j], f);
  }
  memcpy(cw + dc, rem, ec);
  // the fixed patterns
  Qr q = { buf, (uint8_t)(17 + 4 * v) };
  int n = q.n;
  memset(buf, 0, (size_t)n * n);
  for (int i = 0; i < n; i++) { q.set(6, i, i % 2 == 0); q.set(i, 6, i % 2 == 0); }
  qrFinder(q, 3, 3); qrFinder(q, n - 4, 3); qrFinder(q, 3, n - 4);
  if (v >= 2)
    for (int dy = -2; dy <= 2; dy++)
      for (int dx = -2; dx <= 2; dx++) q.set(n - 7 + dx, n - 7 + dy, (abs(dx) > abs(dy) ? abs(dx) : abs(dy)) != 1);
  qrFormat(q, 0);                                               // reserves the format areas
  // the codewords in the zigzag from the bottom right
  uint16_t i = 0, total = (uint16_t)((dc + ec) * 8);
  for (int right = n - 1; right >= 1; right -= 2) {
    if (right == 6) right = 5;
    for (int vert = 0; vert < n; vert++)
      for (int j = 0; j < 2; j++) {
        int x = right - j;
        bool up = ((right + 1) & 2) == 0;
        int y = up ? n - 1 - vert : vert;
        if (q.fixed(x, y) || i >= total) continue;
        if (cw[i >> 3] >> (7 - (i & 7)) & 1) q.m[y * n + x] |= 1;
        i++;
      }
  }
  if (mask > 7) {                                               // the mask that looks best
    int32_t best = 0x7FFFFFFF;
    for (uint8_t k = 0; k < 8; k++) {
      qrMask(q, k); qrFormat(q, k);
      int32_t p = qrPenalty(q);
      if (p < best) { best = p; mask = k; }
      qrMask(q, k);
    }
  }
  qrMask(q, mask);
  qrFormat(q, mask);
  return (uint8_t)n;
}

// on the screen: s px a module, with a light border of three modules
static void qrDraw(const uint8_t *buf, uint8_t n, int16_t x, int16_t y, uint8_t s) {
  gfx.fill(x, y, (int16_t)((n + 6) * s), (int16_t)((n + 6) * s), C::WHITE);
  for (int j = 0; j < n; j++)
    for (int i = 0; i < n; i++)
      if (buf[j * n + i] & 1) gfx.fill((int16_t)(x + (i + 3) * s), (int16_t)(y + (j + 3) * s), s, s, C::BLACK);
}
