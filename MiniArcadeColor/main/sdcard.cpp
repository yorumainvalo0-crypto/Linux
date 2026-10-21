// MiniArcade Color: the SD card, see sdcard.h. Over SPI on the display's bus
// (SPI2, set up by tft_spi.cpp), at most 10 MHz: long wires to the slot.
#include "sdcard.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/gpio.h"

static sdmmc_card_t *card = NULL;

bool sdMount(int8_t cs) {
  sdUnmount();
  if (cs < 0) return false;
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  host.slot = SPI2_HOST;
  host.max_freq_khz = 10000;
  sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot.gpio_cs = (gpio_num_t)cs;
  slot.host_id = SPI2_HOST;
  esp_vfs_fat_sdmmc_mount_config_t mc = {};
  mc.format_if_mount_failed = false;          // never wipe a card
  mc.max_files = 3;
  mc.allocation_unit_size = 16 * 1024;
  if (esp_vfs_fat_sdspi_mount("/sd", &host, &slot, &mc, &card) != ESP_OK) {
    card = NULL;
    gpio_reset_pin((gpio_num_t)cs);           // the pin is free again
    return false;
  }
  return true;
}

void sdUnmount() {
  if (!card) return;
  esp_vfs_fat_sdcard_unmount("/sd", card);
  card = NULL;
}

bool sdReady() {
  if (card && sdmmc_get_status(card) != ESP_OK) sdUnmount();   // pulled out
  return card != NULL;
}

uint32_t sdSizeMB() { return card ? (uint32_t)((uint64_t)card->csd.capacity * card->csd.sector_size >> 20) : 0; }

uint32_t sdFreeMB() {
  uint64_t total = 0, freeB = 0;
  if (!card || esp_vfs_fat_info("/sd", &total, &freeB) != ESP_OK) return 0;
  return (uint32_t)(freeB >> 20);
}
