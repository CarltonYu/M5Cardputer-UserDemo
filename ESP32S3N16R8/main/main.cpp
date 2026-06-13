#include "input.h"
#include "lcd_st7789.h"
#include "ui.h"

#include "hal/hal.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
constexpr const char* kTag = "main";
}

extern "C" void app_main(void)
{
    static demo::LcdSt7789 lcd;
    static demo::RotaryInput input;
    static demo::DemoUi ui(lcd);

    ESP_LOGI(kTag, "ESP32-S3-N16R8 user demo port starting");

    esp_err_t err = lcd.init();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "LCD init failed: %s", esp_err_to_name(err));
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    err = input.init();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "input init failed: %s", esp_err_to_name(err));
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    err = demo::GetHal().init();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "HAL init failed: %s", esp_err_to_name(err));
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
    demo::GetHal().i2cScan();

    ui.begin();
    ui.render();

    bool redraw = false;
    while (true) {
        input.update();

        demo::InputEvent event{};
        while (input.popEvent(&event)) {
            redraw = ui.handleEvent(event) || redraw;
        }

        redraw = ui.update() || redraw;

        if (redraw) {
            ui.render();
            redraw = false;
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
