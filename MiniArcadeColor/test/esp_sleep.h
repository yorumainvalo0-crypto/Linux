#pragma once
#include <stdint.h>
#define ESP_GPIO_WAKEUP_GPIO_LOW 0
#define ESP_GPIO_WAKEUP_GPIO_HIGH 1
#define ESP_EXT1_WAKEUP_ANY_LOW 0
#define ESP_EXT1_WAKEUP_ANY_HIGH 1
extern void simDeepSleep();
inline int esp_deep_sleep_enable_gpio_wakeup(uint64_t, int) { return 0; }
inline int esp_sleep_enable_ext1_wakeup(uint64_t, int) { return 0; }
inline void esp_deep_sleep_start() { simDeepSleep(); }
