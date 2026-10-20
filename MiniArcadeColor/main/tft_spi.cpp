// MiniArcade Color: the panel - ILI9341 (or ST7789) on SPI, 320 x 240 in
// landscape. The frame buffer holds colour indices (gfx.h); a row goes out
// as RGB565 through two DMA buffers, and rows that did not change since
// the last frame are skipped, so a mostly still picture is quick.
// The pins are found at the start by reading the panel back (SDO wired),
// see "finding the pins" below.
#include "gfx.h"
#include "pins.h"
#include "Preferences.h"
#include "sdkconfig.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "esp_rom_sys.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_reg.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define PIN_BL    CONFIG_ARCADE_TFT_BL

TftPins tftPins = { CONFIG_ARCADE_TFT_SCK, CONFIG_ARCADE_TFT_MOSI, CONFIG_ARCADE_TFT_CS,
                    CONFIG_ARCADE_TFT_DC, CONFIG_ARCADE_TFT_MISO, CONFIG_ARCADE_TFT_RST };
bool tftFound = false;
static int8_t dcPin = CONFIG_ARCADE_TFT_DC;    // for the DMA callback

bool tftPinUsed(uint8_t p) {
  const TftPins &t = tftPins;
  return p == t.sck || p == t.mosi || p == t.cs || p == t.dc || p == t.miso || p == t.rst;
}

#define CHUNK_ROWS 12                          // rows per DMA transfer
static spi_device_handle_t dev;
static uint16_t lut[256];                      // colour index -> RGB565, bytes swapped for SPI
static uint32_t rowHash[SCR_H];                // what each row held last time
static bool     fullNext = true;               // send everything next time
static uint16_t *dma[2];
static uint8_t  madctl = 0x28;                 // landscape (MV), BGR

// ---------------- low level ----------------
// runs in the interrupt, so only inline register access (safe while flash is busy)
static void IRAM_ATTR preCb(spi_transaction_t *t) { gpio_ll_set_level(&GPIO, dcPin, (uint32_t)(intptr_t)t->user); }

static void cmd(uint8_t c, const uint8_t *data = NULL, uint8_t n = 0) {
  spi_transaction_t t = {};
  t.length = 8;
  t.tx_buffer = &c;
  t.user = (void *)0;
  spi_device_polling_transmit(dev, &t);
  if (n) {
    spi_transaction_t d = {};
    d.length = 8 * n;
    d.tx_buffer = data;
    d.user = (void *)1;
    spi_device_polling_transmit(dev, &d);
  }
}

static void window(uint16_t y0, uint16_t y1) {
  uint8_t ca[4] = { 0, 0, (SCR_W - 1) >> 8, (SCR_W - 1) & 0xFF };
  uint8_t ra[4] = { (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1 };
  cmd(0x2A, ca, 4);
  cmd(0x2B, ra, 4);
  cmd(0x2C);
}

static void makeLut() {
  for (int i = 0; i < 256; i++) {
    uint8_t r, g, b;
    colToRgb((col)i, r, g, b);
    uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    lut[i] = (uint16_t)((v >> 8) | (v << 8));
  }
}


// ---------------- finding the pins ----------------
/* The panel answers on SDO: write its MADCTL register, read it back, and
   the wiring is right when the value comes back. With the SDO line any
   pin can be tried: SCK, SDI, CS and DC are driven by hand (bit-banged)
   and every other pin is listened to at once, so the SDO pin falls out
   of the same test. Known layouts go first; on the C3 (13 pins) all
   combinations are tried if none fits. Every pin rests on its pull-up
   meanwhile, which keeps CS and RESET high. The result is saved.       */
struct Quad { int8_t sck, mosi, cs, dc; };

static uint64_t inAll() {
  uint64_t v = REG_READ(GPIO_IN_REG);
#if CONFIG_IDF_TARGET_ESP32S3
  v |= (uint64_t)REG_READ(GPIO_IN1_REG) << 32;
#endif
  return v;
}
static bool isBl(int p) { return p == PIN_BL; }     // int: PIN_BL is -1 without a backlight pin
static inline void lv(int8_t p, int l) { gpio_ll_set_level(&GPIO, p, (uint32_t)l); }
static inline void bbWait() { for (int i = 0; i < 24; i++) __asm__ __volatile__("nop"); }   // about 150 ns at 160 MHz: the panel reads at most 6 MHz

static void bbByte(const Quad &q, uint8_t b) {
  for (int i = 7; i >= 0; i--) { lv(q.mosi, b >> i & 1); bbWait(); lv(q.sck, 1); bbWait(); lv(q.sck, 0); }
}
static void bbWrite(const Quad &q, uint8_t c, uint8_t v) {
  lv(q.cs, 0); lv(q.dc, 0); bbByte(q, c); lv(q.dc, 1); bbByte(q, v); lv(q.cs, 1);
}
static void bbRead(const Quad &q, uint8_t c, uint64_t s[16]) {
  lv(q.cs, 0); lv(q.dc, 0); bbByte(q, c); lv(q.dc, 1); lv(q.mosi, 0);
  for (int i = 0; i < 16; i++) { lv(q.sck, 1); bbWait(); s[i] = inAll(); lv(q.sck, 0); bbWait(); }
  lv(q.cs, 1);
}
// the byte v on pin m, after 0..8 dummy bits; returns the shift or -1
static int findByte(const uint64_t s[16], int8_t m, uint8_t v) {
  for (int sh = 0; sh <= 8; sh++) {
    uint8_t b = 0;
    for (int i = 0; i < 8; i++) b = (uint8_t)(b << 1 | (s[sh + i] >> m & 1));
    if (b == v) return sh;
  }
  return -1;
}
static void quadOn(const Quad &q) {
  lv(q.cs, 1); lv(q.sck, 0); lv(q.dc, 1); lv(q.mosi, 0);
  const int8_t p[4] = { q.sck, q.mosi, q.cs, q.dc };
  for (int i = 0; i < 4; i++) gpio_set_direction((gpio_num_t)p[i], GPIO_MODE_INPUT_OUTPUT);
}
static void quadOff(const Quad &q) {
  const int8_t p[4] = { q.sck, q.mosi, q.cs, q.dc };
  for (int i = 0; i < 4; i++) gpio_set_direction((gpio_num_t)p[i], GPIO_MODE_INPUT);   // back to the pull-up
}
static bool inQuad(const Quad &q, int8_t p) { return p == q.sck || p == q.mosi || p == q.cs || p == q.dc; }

// the SDO pin when the panel answers on this wiring, else -1
static int8_t probe(const Quad &q, int8_t only = -1) {
  uint64_t a[16], b[16];
  quadOn(q);
  bbWrite(q, 0x36, 0xA8); bbRead(q, 0x0B, a);
  bbWrite(q, 0x36, 0x48); bbRead(q, 0x0B, b);
  quadOff(q);
  for (uint8_t i = 0; i < sizeof(PINS_ALL); i++) {
    int8_t m = (int8_t)PINS_ALL[i];
    if (inQuad(q, m) || (only >= 0 && m != only)) continue;
    int sa = findByte(a, m, 0xA8);
    if (sa >= 0 && sa == findByte(b, m, 0x48)) return m;
  }
  return -1;
}

// RESET: the pin whose low pulse brings MADCTL back to 0
static int8_t findReset(const Quad &q, int8_t miso) {
  uint64_t s[16];
  for (uint8_t i = 0; i < sizeof(PINS_ALL); i++) {
    int8_t p = (int8_t)PINS_ALL[i];
    if (inQuad(q, p) || p == miso || isBl(p)) continue;
    quadOn(q); bbWrite(q, 0x36, 0xA8); quadOff(q);
    // long pulse and long wait: some boards have a capacitor on RESET
    lv(p, 0); gpio_set_direction((gpio_num_t)p, GPIO_MODE_INPUT_OUTPUT);
    vTaskDelay(pdMS_TO_TICKS(3));
    gpio_set_direction((gpio_num_t)p, GPIO_MODE_INPUT);
    vTaskDelay(pdMS_TO_TICKS(60));
    quadOn(q); bbRead(q, 0x0B, s); quadOff(q);
    if (findByte(s, miso, 0x00) >= 0 && findByte(s, miso, 0xA8) < 0) return p;
  }
  return -1;
}

/* RESET not found (wired to 3V3, or the panel did not show it): the pin
   from the settings stays the display's anyway, so no key, buzzer or
   battery search ever pulls it low - that would blank the panel.      */
static void keepReset(TftPins &p) {
  int8_t r = CONFIG_ARCADE_TFT_RST;
  if (p.rst >= 0 || r < 0) return;
  if (r == p.sck || r == p.mosi || r == p.cs || r == p.dc || r == p.miso) return;
  p.rst = r;
}

static void allPullUp() {
  for (uint8_t i = 0; i < sizeof(PINS_ALL); i++)
    if (!isBl(PINS_ALL[i])) gpio_reset_pin((gpio_num_t)PINS_ALL[i]);   // input with pull-up
}

static bool search(TftPins &out, bool full) {
  static const Quad KNOWN[] = {
    { CONFIG_ARCADE_TFT_SCK, CONFIG_ARCADE_TFT_MOSI, CONFIG_ARCADE_TFT_CS, CONFIG_ARCADE_TFT_DC },
#if CONFIG_IDF_TARGET_ESP32S3
    { 12, 11, 10, 9 },
#else
    { 8, 10, 21, 0 },                                    // the school plan
    { 8, 9, 21, 4 },
    { 6, 7, 10, 5 },                                     // MiniArcade Color 0.1
#endif
  };
  Quad hit = {};
  int8_t miso = -1;
  for (uint8_t k = 0; k < sizeof(KNOWN) / sizeof(KNOWN[0]) && miso < 0; k++) { miso = probe(KNOWN[k]); hit = KNOWN[k]; }
  const uint8_t n = sizeof(PINS_ALL);
  // all combinations, twice: forward and backward. A test can pulse the
  // RESET pin just before the right one, which then finds the panel busy.
  for (int pass = 0; full && n <= 14 && pass < 2 && miso < 0; pass++)
    for (int k = 0; k < n * n * n * n && miso < 0; k++) {
      int c = pass ? n * n * n * n - 1 - k : k;
      if (k % 2000 == 0) vTaskDelay(1);                    // let the system breathe
      Quad q = { (int8_t)PINS_ALL[c % n], (int8_t)PINS_ALL[c / n % n], (int8_t)PINS_ALL[c / n / n % n], (int8_t)PINS_ALL[c / n / n / n] };
      if (q.sck == q.mosi || q.sck == q.cs || q.sck == q.dc || q.mosi == q.cs || q.mosi == q.dc || q.cs == q.dc) continue;
      if (isBl(q.sck) || isBl(q.mosi) || isBl(q.cs) || isBl(q.dc)) continue;
      miso = probe(q);
      hit = q;
    }
  if (miso < 0) return false;
  out = { hit.sck, hit.mosi, hit.cs, hit.dc, miso, findReset(hit, miso) };
  keepReset(out);
  return true;
}

// saved pins when they still answer, else a search; false = nothing found
static bool findPins(TftPins &p) {
  Preferences pr;
  pr.begin("arcade", true);
  TftPins saved;
  bool have = pr.getBytes("tftp", &saved, sizeof(saved)) == sizeof(saved);
  bool tried = pr.getUShort("tftn4", 0) != 0;             // full search failed before (no SDO?)
  pr.end();
  allPullUp();
  if (have && saved.rst >= 0) { lv(saved.rst, 1); gpio_set_direction((gpio_num_t)saved.rst, GPIO_MODE_INPUT_OUTPUT); }
  vTaskDelay(pdMS_TO_TICKS(120));                        // the panel's own power-on reset
  if (have && probe({ saved.sck, saved.mosi, saved.cs, saved.dc }, saved.miso) == saved.miso) { p = saved; keepReset(p); return true; }
  if (!search(p, !tried)) {
    if (!tried) { pr.begin("arcade", false); pr.putUShort("tftn4", 1); pr.end(); }
    if (have) p = saved;                                 // last known, cannot be checked
    return false;
  }
  pr.begin("arcade", false);
  pr.putBytes("tftp", &p, sizeof(p));
  pr.putUShort("tftn4", 0);
  pr.end();
  return true;
}

// ---------------- panel ----------------
void Gfx::begin() {
  tftFound = findPins(tftPins);
  const TftPins &P = tftPins;
  dcPin = P.dc;
  gpio_config_t o = {};
  o.pin_bit_mask = 1ULL << P.dc;
  if (P.rst >= 0) o.pin_bit_mask |= 1ULL << P.rst;
  o.mode = GPIO_MODE_OUTPUT;
  gpio_config(&o);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = P.mosi;
  bus.miso_io_num = P.miso;                      // unused for now (SD card, touch)
  bus.sclk_io_num = P.sck;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = SCR_W * CHUNK_ROWS * 2;
  spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  spi_device_interface_config_t d = {};
  d.clock_speed_hz = CONFIG_ARCADE_TFT_MHZ * 1000 * 1000;
  d.mode = 0;
  d.spics_io_num = P.cs;
  d.queue_size = 3;
  d.pre_cb = preCb;
  spi_bus_add_device(SPI2_HOST, &d, &dev);
  for (int i = 0; i < 2; i++) dma[i] = (uint16_t *)heap_caps_malloc(SCR_W * CHUNK_ROWS * 2, MALLOC_CAP_DMA);

  if (P.rst >= 0) {
    gpio_set_level((gpio_num_t)P.rst, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)P.rst, 1); vTaskDelay(pdMS_TO_TICKS(120));
  }
  cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150));             // software reset
#if CONFIG_ARCADE_TFT_ST7789
  cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));             // sleep out
  { uint8_t v = 0x55; cmd(0x3A, &v, 1); }                // 16 bit colour
  cmd(0x21);                                             // the ST7789 panels are inverted
#else
  // the full sequence the common libraries send; some panels stay white
  // with less (count, then the parameters)
  static const uint8_t INIT[] = {
    0xEF, 3, 0x03, 0x80, 0x02,
    0xCF, 3, 0x00, 0xC1, 0x30,                           // power control B
    0xED, 4, 0x64, 0x03, 0x12, 0x81,                     // power on sequence
    0xE8, 3, 0x85, 0x00, 0x78,                           // driver timing A
    0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,               // power control A
    0xF7, 1, 0x20,                                       // pump ratio
    0xEA, 2, 0x00, 0x00,                                 // driver timing B
    0xC0, 1, 0x23,                                       // power control 1
    0xC1, 1, 0x10,                                       // power control 2
    0xC5, 2, 0x3E, 0x28,                                 // VCOM 1
    0xC7, 1, 0x86,                                       // VCOM 2
    0x37, 1, 0x00,                                       // no vertical scroll
    0x3A, 1, 0x55,                                       // 16 bit colour
    0xB1, 2, 0x00, 0x18,                                 // frame rate
    0xB6, 3, 0x08, 0x82, 0x27,                           // display function
    0xF2, 1, 0x00,                                       // 3-gamma off
    0x26, 1, 0x01,                                       // gamma curve
    0xE0, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
    0xE1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
    0 };
  for (const uint8_t *p = INIT; *p; p += 2 + p[1]) cmd(p[0], p + 2, p[1]);
  cmd(0x11); vTaskDelay(pdMS_TO_TICKS(150));             // sleep out
#endif
  cmd(0x36, &madctl, 1);
  cmd(0x29);                                             // display on
  makeLut();
  if (PIN_BL >= 0) setBacklight(100);
  clear();
  fullNext = true;
  send();
}

void Gfx::setOrientation(bool flip, bool bgr, bool invert) {
#if CONFIG_ARCADE_TFT_ST7789
  madctl = (uint8_t)((flip ? 0xA0 : 0x60) | (bgr ? 0x08 : 0));
  cmd(invert ? 0x20 : 0x21);
#else
  madctl = (uint8_t)((flip ? 0xE8 : 0x28) & (bgr ? 0xFF : 0xF7));
  cmd(invert ? 0x21 : 0x20);
#endif
  cmd(0x36, &madctl, 1);
  fullNext = true;
}

void Gfx::setBacklight(uint8_t pct) {
  if (PIN_BL < 0) return;
  static bool ready = false;
  if (!ready) {
    ledc_timer_config_t t = {};
    t.speed_mode = LEDC_LOW_SPEED_MODE;
    t.duty_resolution = LEDC_TIMER_8_BIT;
    t.timer_num = LEDC_TIMER_1;                         // timer 0 is the buzzer
    t.freq_hz = 5000;
    t.clk_cfg = LEDC_AUTO_CLK;
    ledc_timer_config(&t);
    ledc_channel_config_t c = {};
    c.gpio_num = PIN_BL;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel = LEDC_CHANNEL_1;
    c.timer_sel = LEDC_TIMER_1;
    ledc_channel_config(&c);
    ready = true;
  }
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, pct >= 100 ? 255 : pct * 255 / 100);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1);
}

void Gfx::sleep(bool off) {
  cmd(off ? 0x28 : 0x29);
  cmd(off ? 0x10 : 0x11);
  if (PIN_BL >= 0) setBacklight(off ? 0 : 100);
  fullNext = true;
}

// ---------------- frame ----------------
static uint32_t hashRow(const uint8_t *p) {
  uint32_t h = 2166136261u;
  const uint32_t *w = (const uint32_t *)p;
  for (int i = 0; i < SCR_W / 4; i++) { h ^= w[i]; h *= 16777619u; }
  return h;
}

void Gfx::send() {
  int16_t y = 0;
  uint8_t which = 0;
  int pending = 0;
  while (y < SCR_H) {
    // find the next run of changed rows
    uint32_t h = hashRow(fb + y * SCR_W);
    if (!fullNext && h == rowHash[y]) { y++; continue; }
    int16_t y0 = y;
    while (y < SCR_H) {
      uint32_t hh = hashRow(fb + y * SCR_W);
      if (!fullNext && hh == rowHash[y]) break;
      rowHash[y] = hh;
      y++;
    }
    // wait for what is still in flight before changing the window
    spi_transaction_t *done;
    while (pending) { spi_device_get_trans_result(dev, &done, portMAX_DELAY); pending--; }
    window((uint16_t)y0, (uint16_t)(y - 1));
    static spi_transaction_t tr[2];
    for (int16_t r = y0; r < y; r += CHUNK_ROWS) {
      int16_t n = (y - r) < CHUNK_ROWS ? (y - r) : CHUNK_ROWS;
      if (pending == 2) { spi_device_get_trans_result(dev, &done, portMAX_DELAY); pending--; }   // this buffer is free again
      uint16_t *out = dma[which];
      const uint8_t *src = fb + r * SCR_W;
      for (int i = 0; i < n * SCR_W; i++) out[i] = lut[src[i]];
      memset(&tr[which], 0, sizeof(spi_transaction_t));
      tr[which].length = n * SCR_W * 16;
      tr[which].tx_buffer = out;
      tr[which].user = (void *)1;
      spi_device_queue_trans(dev, &tr[which], portMAX_DELAY);
      pending++;
      which ^= 1;
    }
  }
  spi_transaction_t *done;
  while (pending) { spi_device_get_trans_result(dev, &done, portMAX_DELAY); pending--; }
  fullNext = false;
}
