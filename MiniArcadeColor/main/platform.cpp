// MiniArcade Color platform layer: GPIO, timing, buzzer, ADC, sleep, NVS - the Arduino calls the console uses
#include "Arduino.h"
#include "Preferences.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_sleep.h"
#include "driver/rtc_io.h"
#include "soc/soc_caps.h"
#include "esp_idf_version.h"
#include "soc/rtc.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_rom_sys.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ---------------- timing ----------------
uint32_t millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
void delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1)); }
void delayMicroseconds(uint32_t us) { esp_rom_delay_us(us); }

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
  adc_unit_t unit;
  adc_channel_t ch;
  // ADC1 only: ADC2 is taken by the WLAN (C3: GPIO0..4, S3: GPIO1..10)
  if (adc_oneshot_io_to_channel(pin, &unit, &ch) != ESP_OK || unit != ADC_UNIT_1) return 0;
  if (!adcUnit) {
    adc_oneshot_unit_init_cfg_t u = {};
    u.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&u, &adcUnit) != ESP_OK) { adcUnit = NULL; return 0; }
  }
  /* Only once per channel: configuring a channel also switches the pin's
     pull resistors off, which silently undid the pull-down of the battery test. */
  static uint16_t configured = 0;
  if (!(configured & (1 << ch))) {
    adc_oneshot_chan_cfg_t c = {};
    c.atten    = ADC_ATTEN_DB_12;             // input range roughly 0 .. 2.5 V
    c.bitwidth = ADC_BITWIDTH_12;
    adc_oneshot_config_channel(adcUnit, ch, &c);
    configured |= (uint16_t)(1 << ch);
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
  if (adc_oneshot_read(adcUnit, ch, &raw) != ESP_OK) return 0;
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

/* Any of the keys in mask wakes the board (a reset). The C3 wakes from
   any GPIO; the S3 only from its RTC pins (GPIO0..21).               */
bool deepSleepUntilKeys(uint64_t mask, uint8_t level) {
  if (!mask) return false;
#if SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP
#if ESP_IDF_VERSION_MAJOR >= 6
  esp_sleep_enable_gpio_wakeup_on_hp_periph_powerdown(mask,
      level ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
#else
  esp_deep_sleep_enable_gpio_wakeup(mask, level ? ESP_GPIO_WAKEUP_GPIO_HIGH : ESP_GPIO_WAKEUP_GPIO_LOW);
#endif
#else
  for (int p = 0; p < 64; p++)
    if ((mask >> p & 1) && !rtc_gpio_is_valid_gpio((gpio_num_t)p)) return false;
  esp_sleep_enable_ext1_wakeup(mask, level ? ESP_EXT1_WAKEUP_ANY_HIGH : ESP_EXT1_WAKEUP_ANY_LOW);
#endif
  esp_deep_sleep_start();                    // never returns, wakes with a reset
  return true;
}

// ---------------- random ----------------
static uint32_t rngState = 1;
void randomSeed(unsigned long s) { rngState = s ? (uint32_t)s : 1; }
static uint32_t rnd() { rngState ^= rngState << 13; rngState ^= rngState >> 17; rngState ^= rngState << 5; return rngState; }
long random(long max)          { return max > 0 ? (long)(rnd() % (uint32_t)max) : 0; }
long random(long lo, long hi)  { return hi > lo ? lo + (long)(rnd() % (uint32_t)(hi - lo)) : lo; }

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
