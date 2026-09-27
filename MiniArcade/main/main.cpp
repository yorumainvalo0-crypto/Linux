#include "Arduino.h"
#include "nvs_flash.h"
#include "esp_random.h"

void setup();
void loop();

extern "C" void app_main(void) {
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  setup();
  // a new firmware is confirmed with the first key press in the menu (see
  // fwConfirm) - a restart before that brings the previous one back
  for (;;) loop();
}
