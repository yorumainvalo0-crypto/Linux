#pragma once
#include <stdint.h>
#include "gpio.h"
#define LEDC_LOW_SPEED_MODE 0
#define LEDC_TIMER_10_BIT 10
#define LEDC_TIMER_0 0
#define LEDC_CHANNEL_0 0
#define LEDC_AUTO_CLK 0
struct ledc_timer_config_t { int speed_mode, duty_resolution, timer_num; uint32_t freq_hz; int clk_cfg; };
struct ledc_channel_config_t { int gpio_num, speed_mode, channel, timer_sel; uint32_t duty; int hpoint; };
extern void simTone(int freq);
static uint32_t simLedcFreq = 0, simLedcDuty = 0;
inline esp_err_t ledc_timer_config(const ledc_timer_config_t*){ return 0; }
inline esp_err_t ledc_channel_config(const ledc_channel_config_t*){ return 0; }
inline esp_err_t ledc_set_freq(int, int, uint32_t f){ simLedcFreq = f; return 0; }
inline esp_err_t ledc_set_duty(int, int, uint32_t d){ simLedcDuty = d; return 0; }
inline esp_err_t ledc_update_duty(int, int){ simTone(simLedcDuty ? (int)simLedcFreq : 0); return 0; }
