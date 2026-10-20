// MiniArcade Color: the panel - ILI9341 (or ST7789) on SPI, 320 x 240 in
// landscape. The frame buffer holds colour indices (gfx.h); a row goes out
// as RGB565 through two DMA buffers, and rows that did not change since
// the last frame are skipped, so a mostly still picture is quick.
#include "gfx.h"
#include "sdkconfig.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define PIN_SCK   CONFIG_ARCADE_TFT_SCK
#define PIN_MOSI  CONFIG_ARCADE_TFT_MOSI
#define PIN_MISO  CONFIG_ARCADE_TFT_MISO
#define PIN_CS    CONFIG_ARCADE_TFT_CS
#define PIN_DC    CONFIG_ARCADE_TFT_DC
#define PIN_RST   CONFIG_ARCADE_TFT_RST
#define PIN_BL    CONFIG_ARCADE_TFT_BL

#define CHUNK_ROWS 12                          // rows per DMA transfer
static spi_device_handle_t dev;
static uint16_t lut[256];                      // colour index -> RGB565, bytes swapped for SPI
static uint32_t rowHash[SCR_H];                // what each row held last time
static bool     fullNext = true;               // send everything next time
static uint16_t *dma[2];
static uint8_t  madctl = 0x28;                 // landscape (MV), BGR

// ---------------- low level ----------------
static void IRAM_ATTR preCb(spi_transaction_t *t) { gpio_set_level((gpio_num_t)PIN_DC, (int)(intptr_t)t->user); }

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

// ---------------- panel ----------------
void Gfx::begin() {
  gpio_config_t o = {};
  o.pin_bit_mask = 1ULL << PIN_DC;
  if (PIN_RST >= 0) o.pin_bit_mask |= 1ULL << (PIN_RST >= 0 ? PIN_RST : 0);
  o.mode = GPIO_MODE_OUTPUT;
  gpio_config(&o);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = PIN_MOSI;
  bus.miso_io_num = PIN_MISO;                    // unused for now (SD card, touch)
  bus.sclk_io_num = PIN_SCK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = SCR_W * CHUNK_ROWS * 2;
  spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  spi_device_interface_config_t d = {};
  d.clock_speed_hz = CONFIG_ARCADE_TFT_MHZ * 1000 * 1000;
  d.mode = 0;
  d.spics_io_num = PIN_CS;
  d.queue_size = 3;
  d.pre_cb = preCb;
  spi_bus_add_device(SPI2_HOST, &d, &dev);
  for (int i = 0; i < 2; i++) dma[i] = (uint16_t *)heap_caps_malloc(SCR_W * CHUNK_ROWS * 2, MALLOC_CAP_DMA);

  if (PIN_RST >= 0) {
    gpio_set_level((gpio_num_t)PIN_RST, 0); vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)PIN_RST, 1); vTaskDelay(pdMS_TO_TICKS(120));
  }
  cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150));             // software reset
#if CONFIG_ARCADE_TFT_ST7789
  cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));             // sleep out
  { uint8_t v = 0x55; cmd(0x3A, &v, 1); }                // 16 bit colour
  cmd(0x21);                                             // the ST7789 panels are inverted
#else
  static const uint8_t pwr1[] = { 0x23 }, pwr2[] = { 0x10 }, vcm1[] = { 0x3E, 0x28 }, vcm2[] = { 0x86 };
  static const uint8_t frm[] = { 0x00, 0x18 }, dfc[] = { 0x08, 0x82, 0x27 }, gam[] = { 0x01 };
  cmd(0xC0, pwr1, 1); cmd(0xC1, pwr2, 1); cmd(0xC5, vcm1, 2); cmd(0xC7, vcm2, 1);
  { uint8_t v = 0x55; cmd(0x3A, &v, 1); }                // 16 bit colour
  cmd(0xB1, frm, 2); cmd(0xB6, dfc, 3); cmd(0x26, gam, 1);
  cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));             // sleep out
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
