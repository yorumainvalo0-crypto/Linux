#pragma once
#include <stdint.h>
extern uint64_t simMicros();
inline int64_t esp_timer_get_time(){ return (int64_t)simMicros(); }
