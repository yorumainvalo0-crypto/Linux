#pragma once
#include "driver/gpio.h"
inline bool rtc_gpio_is_valid_gpio(gpio_num_t p) { return p <= 21; }
