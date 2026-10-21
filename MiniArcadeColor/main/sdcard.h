// MiniArcade Color: the SD card in the display module. It shares SCK, MOSI
// and MISO with the display; only its CS needs a pin of its own (sdcard.cpp).
#pragma once
#include <stdint.h>

bool     sdMount(int8_t cs);      // FAT on the card at /sd; false = no card on this pin
void     sdUnmount();
bool     sdReady();               // mounted and still answering
uint32_t sdSizeMB();              // card size
uint32_t sdFreeMB();              // free space on it
