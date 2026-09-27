#pragma once
#include <stdint.h>
struct TwoWire { void begin(int sda, int scl); };
extern TwoWire Wire;
