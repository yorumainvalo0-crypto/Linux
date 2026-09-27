#pragma once
#include "adc_oneshot.h"
typedef void* adc_cali_handle_t;
inline esp_err_t adc_cali_raw_to_voltage(adc_cali_handle_t, int raw, int* mv){ *mv = raw * 2500 / 4095; return 0; }
