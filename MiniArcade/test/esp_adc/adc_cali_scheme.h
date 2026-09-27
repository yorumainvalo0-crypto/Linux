#pragma once
#include "adc_cali.h"
struct adc_cali_curve_fitting_config_t { int unit_id, chan, atten, bitwidth; };
// no calibration on the PC: the platform layer falls back to a fixed scale
inline esp_err_t adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t*, adc_cali_handle_t*){ return ESP_FAIL; }
