idf_component_register(
    SRCS
        "main.cpp"
        "lcd_st7789.cpp"
        "input.cpp"
        "espnow_chat.cpp"
        "ui.cpp"
        "hal/hal.cpp"
        "hal/i2c_bus.cpp"
        "hal/keyboard/keyboard.cpp"
        "hal/keyboard/tca8418.cpp"
        "hal/audio/es8311.cpp"
        "hal/imu/mpu6050.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_lcd
        esp_driver_gpio
        esp_driver_spi
        esp_driver_i2c
        esp_timer
        esp_event
        esp_netif
        esp_wifi
        nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror=return-type)
