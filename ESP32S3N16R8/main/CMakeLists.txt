idf_component_register(
    SRCS
        "main.cpp"
        "lcd_st7789.cpp"
        "input.cpp"
        "espnow_chat.cpp"
        "ui.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp_lcd
        esp_driver_gpio
        esp_driver_spi
        esp_timer
        esp_event
        esp_netif
        esp_wifi
        nvs_flash
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror=return-type)
