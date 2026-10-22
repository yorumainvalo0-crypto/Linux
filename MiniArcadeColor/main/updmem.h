// MiniArcade Color: what the update mode needs from the chip (updmem.cpp;
// the PC simulator has its own stand-ins).
#pragma once
#include <stdint.h>
#include <stddef.h>

enum UpdWhat : uint8_t { UPD_NONE = 0, UPD_GITHUB = 1, UPD_UPLOAD = 2 };

void    updRequest(uint8_t what);           // restart into the update mode (does not return)
uint8_t updTake();                          // at the start: the mode asked for (once), else UPD_NONE
bool    updDonate(void *p, size_t n);       // hand this memory to the heap for good
void    updRestart();                       // plain restart
