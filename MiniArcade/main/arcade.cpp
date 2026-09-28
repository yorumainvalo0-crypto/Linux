// Platform layer: I2C + SSD1306, GPIO, timing, NVS - replaces Arduino + U8g2
#include "Arduino.h"
#include "Wire.h"
#include "U8g2lib.h"
#include "Preferences.h"
#include "fonts.h"

#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_sleep.h"
#include "esp_idf_version.h"
#include "soc/rtc.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---------------- timing ----------------
uint32_t millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
void delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1)); }

// ---------------- gpio ----------------
void pinMode(uint8_t pin, uint8_t mode) {
  gpio_config_t c = {};
  c.pin_bit_mask = 1ULL << pin;
  c.mode = GPIO_MODE_INPUT;
  c.pull_up_en   = (mode == INPUT_PULLUP)   ? GPIO_PULLUP_ENABLE   : GPIO_PULLUP_DISABLE;  // INPUT = no pull
  c.pull_down_en = (mode == INPUT_PULLDOWN) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
  c.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&c);
}
int digitalRead(uint8_t pin) { return gpio_get_level((gpio_num_t)pin); }

// ---------------- buzzer (LEDC square wave) ----------------
static bool    ledcReady = false;
static uint8_t tonePin = 255;

void tone(uint8_t pin, unsigned int freq) {
  if (!freq) { noTone(pin); return; }
  ledc_timer_config_t t = {};
  t.speed_mode      = LEDC_LOW_SPEED_MODE;
  t.duty_resolution = LEDC_TIMER_10_BIT;
  t.timer_num       = LEDC_TIMER_0;
  t.freq_hz         = freq;
  t.clk_cfg         = LEDC_AUTO_CLK;
  ledc_timer_config(&t);
  if (!ledcReady || tonePin != pin) {
    ledc_channel_config_t c = {};
    c.gpio_num   = pin;
    c.speed_mode = LEDC_LOW_SPEED_MODE;
    c.channel    = LEDC_CHANNEL_0;
    c.timer_sel  = LEDC_TIMER_0;
    c.duty       = 512;                       // 50 % square wave
    c.hpoint     = 0;
    ledc_channel_config(&c);
    ledcReady = true;
    tonePin = pin;
  }
  ledc_set_freq(LEDC_LOW_SPEED_MODE, LEDC_TIMER_0, freq);
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 512);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

void noTone(uint8_t pin) {
  if (!ledcReady) return;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 0);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  gpio_set_level((gpio_num_t)pin, 0);
}

// ---------------- ADC (battery sense) ----------------
/* The raw value must go through the factory calibration stored in the chip.
   A fixed scale factor is off by a good 20 % and made a full cell look like
   an implausible voltage, so the battery was never recognised.           */
static adc_oneshot_unit_handle_t adcUnit = NULL;
static adc_cali_handle_t         adcCali = NULL;
static bool                      caliTried = false;

uint32_t analogReadMilliVolts(uint8_t pin) {
  if (pin > 4) return 0;                      // only GPIO0..4 reach ADC1 on the C3
  if (!adcUnit) {
    adc_oneshot_unit_init_cfg_t u = {};
    u.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&u, &adcUnit) != ESP_OK) { adcUnit = NULL; return 0; }
  }
  /* Only once per pin: configuring a channel also switches the pin's pull
     resistors off, which silently undid the pull-down of the battery test. */
  static uint8_t configured = 0;
  if (!(configured & (1 << pin))) {
    adc_oneshot_chan_cfg_t c = {};
    c.atten    = ADC_ATTEN_DB_12;             // input range roughly 0 .. 2.5 V
    c.bitwidth = ADC_BITWIDTH_12;
    adc_oneshot_config_channel(adcUnit, (adc_channel_t)pin, &c);
    configured |= (uint8_t)(1 << pin);
  }

  if (!caliTried) {                           // one-time calibration setup
    caliTried = true;
    adc_cali_curve_fitting_config_t cc = {};
    cc.unit_id  = ADC_UNIT_1;
    cc.atten    = ADC_ATTEN_DB_12;
    cc.bitwidth = ADC_BITWIDTH_12;
    if (adc_cali_create_scheme_curve_fitting(&cc, &adcCali) != ESP_OK) adcCali = NULL;
  }

  int raw = 0;
  if (adc_oneshot_read(adcUnit, (adc_channel_t)pin, &raw) != ESP_OK) return 0;
  int mv = 0;
  if (adcCali && adc_cali_raw_to_voltage(adcCali, raw, &mv) == ESP_OK) return (uint32_t)mv;
  return (uint32_t)((int64_t)raw * 2500 / 4095);   // fallback without calibration
}

// ---------------- cpu clock and sleep ----------------
void setCpuFrequencyMhz(uint8_t mhz) {
  rtc_cpu_freq_config_t cfg;
  if (!rtc_clk_cpu_freq_mhz_to_config(mhz, &cfg)) return;
  rtc_clk_cpu_freq_set_config_fast(&cfg);
}

void deepSleepUntilButton(uint8_t pin, uint8_t level) {
  /* the call was renamed in IDF 6, so pick whatever this version offers */
#if ESP_IDF_VERSION_MAJOR >= 6
  esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(1ULL << pin,
      level ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
#else
  esp_deep_sleep_enable_gpio_wakeup(1ULL << pin,
      level ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
#endif
  esp_deep_sleep_start();                    // never returns, wakes with a reset
}

// ---------------- random ----------------
static uint32_t rngState = 1;
void randomSeed(unsigned long s) { rngState = s ? (uint32_t)s : 1; }
static uint32_t rnd() { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return rngState; }
long random(long max)          { return max > 0 ? (long)(rnd() % (uint32_t)max) : 0; }
long random(long lo, long hi)  { return hi > lo ? lo + (long)(rnd() % (uint32_t)(hi - lo)) : lo; }

// ---------------- i2c + display ----------------
#define OLED_ADDR 0x3C
static i2c_master_bus_handle_t i2cBus;
static i2c_master_dev_handle_t oledDev;
TwoWire Wire;

void TwoWire::begin(int sda, int scl) {
  i2c_master_bus_config_t bc = {};
  bc.i2c_port = -1;
  bc.sda_io_num = (gpio_num_t)sda;
  bc.scl_io_num = (gpio_num_t)scl;
  bc.clk_source = I2C_CLK_SRC_DEFAULT;
  bc.glitch_ignore_cnt = 7;
  bc.flags.enable_internal_pullup = true;
  i2c_new_master_bus(&bc, &i2cBus);

  i2c_device_config_t dc = {};
  dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dc.device_address = OLED_ADDR;
  dc.scl_speed_hz = 400000;
  i2c_master_bus_add_device(i2cBus, &dc, &oledDev);
}

static void oledCmd(const uint8_t *c, size_t n) {
  uint8_t b[16];
  b[0] = 0x00;                         // Co = 0, D/C = 0 -> command stream
  memcpy(b + 1, c, n);
  i2c_master_transmit(oledDev, b, n + 1, 100);
}

void Gfx::begin() {
  static const uint8_t init[] = {
    0xAE,             // display off
    0xD5, 0x80,       // clock
    0xA8, 0x3F,       // multiplex = 64
    0xD3, 0x00,       // no offset
    0x40,             // start line 0
    0x8D, 0x14,       // charge pump on
    0x20, 0x00,       // horizontal addressing
    0xA1, 0xC8,       // segment remap + com scan direction
    0xDA, 0x12,       // com pins
    0x81, 0xCF,       // contrast
    0xD9, 0xF1,       // precharge
    0xDB, 0x40,       // vcom
    0xA4, 0xA6,       // resume from RAM, not inverted
    0xAF              // display on
  };
  for (size_t i = 0; i < sizeof(init); i += 8)
    oledCmd(init + i, (sizeof(init) - i) > 8 ? 8 : (sizeof(init) - i));
  clearBuffer();
  sendBuffer();
}

void Gfx::setContrast(uint8_t v) {
  uint8_t c[2] = { 0x81, v };
  oledCmd(c, 2);
}

void Gfx::setPowerSave(uint8_t off) {          // 1 = panel off
  uint8_t c[1] = { (uint8_t)(off ? 0xAE : 0xAF) };
  oledCmd(c, 1);
}

void Gfx::sendBuffer() {
  static const uint8_t win[] = { 0x21, 0, 127, 0x22, 0, 7 };   // full window
  oledCmd(win, sizeof(win));
  static uint8_t tx[1025];
  tx[0] = 0x40;                                                // data stream
  memcpy(tx + 1, fb, sizeof(fb));
  i2c_master_transmit(oledDev, tx, sizeof(tx), 200);
}

// ---------------- drawing ----------------
void Gfx::drawPixel(int16_t x, int16_t y) {
  if (x < 0 || x > 127 || y < 0 || y > 63) return;
  uint16_t i = (uint16_t)(y >> 3) * 128 + x;
  uint8_t  m = 1 << (y & 7);
  if (color == 2)      fb[i] ^= m;          // XOR, used for the night mode
  else if (color)      fb[i] |= m;
  else                 fb[i] &= ~m;
}
void Gfx::drawBox(int16_t x, int16_t y, int16_t w, int16_t h) {
  for (int16_t j = 0; j < h; j++) for (int16_t i = 0; i < w; i++) drawPixel(x + i, y + j);
}
void Gfx::drawFrame(int16_t x, int16_t y, int16_t w, int16_t h) {
  for (int16_t i = 0; i < w; i++) { drawPixel(x + i, y); drawPixel(x + i, y + h - 1); }
  for (int16_t j = 0; j < h; j++) { drawPixel(x, y + j); drawPixel(x + w - 1, y + j); }
}
void Gfx::drawHLine(int16_t x, int16_t y, int16_t w) { for (int16_t i = 0; i < w; i++) drawPixel(x + i, y); }
void Gfx::drawVLine(int16_t x, int16_t y, int16_t h) { for (int16_t j = 0; j < h; j++) drawPixel(x, y + j); }
void Gfx::drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1) {
  int16_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int16_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    drawPixel(x0, y0);
    if (x0 == x1 && y0 == y1) break;
    int16_t e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
#ifdef ARCADE_TRACE
extern void arcadeTraceStr(const char *s);      // host test harness only
#endif

void Gfx::drawStr(int16_t x, int16_t y, const char *s) {
#ifdef ARCADE_TRACE
  arcadeTraceStr(s);
#endif
  for (; *s; s++) {
    uint8_t c = (uint8_t)*s;
    if (c < 32 || c > 126) { x += font ? 7 : 5; continue; }
    if (font) {                                   // bold: 7 wide, 12 rows
      for (uint8_t col = 0; col < 7; col++) {
        uint16_t bits = FONT_L[c - 32][col];
        for (uint8_t r = 0; r < 12; r++) if (bits & (1 << r)) drawPixel(x + col, y - 9 + r);
      }
      x += 7;
    } else {                                      // regular: 5 wide, 9 rows
      for (uint8_t col = 0; col < 5; col++) {
        uint16_t bits = FONT_S[c - 32][col];
        for (uint8_t r = 0; r < 9; r++) if (bits & (1 << r)) drawPixel(x + col, y - 6 + r);
      }
      x += 5;
    }
  }
}

// ---------------- NVS ----------------
bool Preferences::begin(const char *ns, bool readOnly) {
  ro = readOnly;
  nvs_handle_t nh;
  if (nvs_open(ns, readOnly ? NVS_READONLY : NVS_READWRITE, &nh) != ESP_OK) { h = 0; return false; }
  h = (uint32_t)nh;
  return true;
}
void Preferences::end() {
  if (!h) return;
  if (!ro) nvs_commit((nvs_handle_t)h);
  nvs_close((nvs_handle_t)h);
  h = 0;
}
uint16_t Preferences::getUShort(const char *key, uint16_t def) {
  if (!h) return def;
  uint16_t v = def;
  return nvs_get_u16((nvs_handle_t)h, key, &v) == ESP_OK ? v : def;
}
void Preferences::putUShort(const char *key, uint16_t val) {
  if (h) nvs_set_u16((nvs_handle_t)h, key, val);
}
size_t Preferences::getBytes(const char *key, void *buf, size_t len) {
  if (!h) return 0;
  size_t n = len;
  return nvs_get_blob((nvs_handle_t)h, key, buf, &n) == ESP_OK ? n : 0;
}
size_t Preferences::putBytes(const char *key, const void *buf, size_t len) {
  if (h) nvs_set_blob((nvs_handle_t)h, key, buf, len);
  return len;
}
