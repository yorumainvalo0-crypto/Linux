#pragma once
#include <stdint.h>
extern void simDelayMs(uint32_t);
inline void vTaskDelay(uint32_t t){ simDelayMs(t); }
