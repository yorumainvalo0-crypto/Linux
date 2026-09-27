// Arduino compatibility shim - just enough for the MiniArcade sketch
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#define INPUT         1
#define INPUT_PULLUP   2
#define INPUT_PULLDOWN 3
#define LOW  0
#define HIGH 1

uint32_t millis(void);
void     delay(uint32_t ms);
void     pinMode(uint8_t pin, uint8_t mode);
int      digitalRead(uint8_t pin);
void     tone(uint8_t pin, unsigned int freq);
void     noTone(uint8_t pin);
uint32_t analogReadMilliVolts(uint8_t pin);
void     setCpuFrequencyMhz(uint8_t mhz);
void     deepSleepUntilButton(uint8_t pin, uint8_t level);
void     randomSeed(unsigned long seed);
long     random(long max);
long     random(long lo, long hi);
#ifndef constrain
#define constrain(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#endif
#include "esp_random.h"   // IDF provides this one
