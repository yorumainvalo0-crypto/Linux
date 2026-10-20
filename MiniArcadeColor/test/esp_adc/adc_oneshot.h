// The OLED harness's ADC plus the pin lookup the colour build uses (C3: GPIO0..4).
#pragma once
#include "../../../MiniArcade/test/esp_adc/adc_oneshot.h"
typedef int adc_unit_t;
inline esp_err_t adc_oneshot_io_to_channel(int io, adc_unit_t *unit, adc_channel_t *ch) {
  if (io < 0 || io > 4) return ESP_FAIL;
  *unit = ADC_UNIT_1; *ch = io;
  return 0;
}
