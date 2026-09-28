idf_component_register(SRCS "main.cpp" "arcade.cpp" "game.cpp" "net.cpp" "link.cpp" "phone.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_i2c esp_driver_gpio esp_driver_ledc esp_adc esp_timer nvs_flash
                                esp_wifi esp_netif esp_event esp_http_server esp_http_client
                                esp_https_ota app_update esp_app_format bootloader_support mbedtls lwip)
