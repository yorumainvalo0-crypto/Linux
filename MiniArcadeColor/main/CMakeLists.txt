# The colour console shares the hardware independent parts with the OLED
# console (../../MiniArcade/main): WLAN and updates, the phone pages, the
# multiplayer radio and protocol, the board game rules, the battery curve.
set(SHARED ../../MiniArcade/main)
idf_component_register(SRCS "main.cpp" "platform.cpp" "gfx.cpp" "tft_spi.cpp" "gyro.cpp" "sdcard.cpp" "console.cpp"
                            "${SHARED}/net.cpp" "${SHARED}/link.cpp" "${SHARED}/phone.cpp"
                       INCLUDE_DIRS "." "${SHARED}"
                       REQUIRES esp_driver_spi esp_driver_i2c esp_driver_gpio esp_driver_ledc esp_adc esp_timer nvs_flash
                                esp_wifi esp_netif esp_event esp_http_server esp_http_client
                                esp_https_ota app_update esp_app_format bootloader_support mbedtls lwip json
                                fatfs sdmmc esp_driver_sdspi)
