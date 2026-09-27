#pragma once
#include <stdint.h>
struct rtc_cpu_freq_config_t { uint32_t freq_mhz; };
extern void simCpuMhz(int mhz);
inline bool rtc_clk_cpu_freq_mhz_to_config(uint32_t mhz, rtc_cpu_freq_config_t* c){ c->freq_mhz = mhz; return true; }
inline void rtc_clk_cpu_freq_set_config_fast(const rtc_cpu_freq_config_t* c){ simCpuMhz((int)c->freq_mhz); }
