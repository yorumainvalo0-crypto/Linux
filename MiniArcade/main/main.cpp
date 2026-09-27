#include "Arduino.h"
#include "nvs_flash.h"
#include "esp_random.h"
#include "esp_ota_ops.h"

void setup();
void loop();

extern "C" void app_main(void) {
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }
  setup();
  // the menu is up, so a freshly installed update works - keep it.
  // Crashing before this point makes the bootloader go back to the old one.
  esp_ota_mark_app_valid_cancel_rollback();
  for (;;) loop();
}
