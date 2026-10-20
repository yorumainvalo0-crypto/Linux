#pragma once
#include <stdint.h>
extern void simDelayUs(uint32_t us);
inline void esp_rom_delay_us(uint32_t us) { simDelayUs(us); }
