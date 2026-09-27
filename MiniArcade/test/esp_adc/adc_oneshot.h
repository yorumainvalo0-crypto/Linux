#pragma once
#include "driver/i2c_master.h"
#ifndef ESP_FAIL
#define ESP_FAIL -1
#endif
#define ADC_UNIT_1 0
#define ADC_ATTEN_DB_12 3
#define ADC_BITWIDTH_12 12
typedef void* adc_oneshot_unit_handle_t;
typedef int adc_channel_t;
struct adc_oneshot_unit_init_cfg_t { int unit_id, clk_src, ulp_mode; };
struct adc_oneshot_chan_cfg_t { int atten, bitwidth; };
extern int simAdcRaw(int ch);
inline esp_err_t adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t*, adc_oneshot_unit_handle_t* h){ *h = (void*)1; return 0; }
inline esp_err_t adc_oneshot_config_channel(adc_oneshot_unit_handle_t, adc_channel_t, const adc_oneshot_chan_cfg_t*){ return 0; }
inline esp_err_t adc_oneshot_read(adc_oneshot_unit_handle_t, adc_channel_t ch, int* raw){ *raw = simAdcRaw(ch); return 0; }
