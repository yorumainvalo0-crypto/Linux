// MiniArcade Color: the update mode's chip side. The request survives the
// restart in RTC memory, which a reset does not clear.
#include "updmem.h"
#include "esp_attr.h"
#include "esp_heap_caps_init.h"
#include "esp_system.h"

#define UPD_MAGIC 0x55504454u                  // "UPDT"
RTC_NOINIT_ATTR static uint32_t updMagic, updWhat;

void updRequest(uint8_t what) {
  updMagic = UPD_MAGIC;
  updWhat = what;
  esp_restart();
}

uint8_t updTake() {
  if (updMagic != UPD_MAGIC) return UPD_NONE;
  updMagic = 0;
  return (uint8_t)updWhat;
}

bool updDonate(void *p, size_t n) {
  intptr_t s = ((intptr_t)p + 15) & ~(intptr_t)15, e = ((intptr_t)p + (intptr_t)n) & ~(intptr_t)15;
  return e > s && heap_caps_add_region(s, e) == ESP_OK;
}

void updRestart() { esp_restart(); }
