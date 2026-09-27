#pragma once
#include <stdint.h>
#include "i2c_master.h"
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_ENABLE 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
struct gpio_config_t { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; };
extern void simGpioConfig(uint64_t mask, int pu, int pd);
extern int  simGpioLevel(int pin);
inline esp_err_t gpio_config(const gpio_config_t* c){ simGpioConfig(c->pin_bit_mask, c->pull_up_en, c->pull_down_en); return 0; }
inline int gpio_get_level(gpio_num_t p){ return simGpioLevel(p); }
