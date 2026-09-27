idf_component_register(SRCS "main.cpp" "arcade.cpp" "game.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_i2c esp_driver_gpio esp_driver_ledc esp_adc esp_timer nvs_flash)
