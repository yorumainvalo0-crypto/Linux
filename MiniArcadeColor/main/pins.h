// MiniArcade Color: the general pins of the chip (no flash, PSRAM or USB).
// The display search and the key wizard pick from these.
#pragma once
#include <stdint.h>
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32S3
static const uint8_t PINS_ALL[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18,
                                    21, 38, 39, 40, 41, 42, 45, 46, 47, 48 };
#define ADC_FIRST 1        // ADC1: GPIO1..10
#define ADC_LAST  10
#else
static const uint8_t PINS_ALL[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 20, 21 };
#define ADC_FIRST 0        // ADC1: GPIO0..4
#define ADC_LAST  4
#endif
