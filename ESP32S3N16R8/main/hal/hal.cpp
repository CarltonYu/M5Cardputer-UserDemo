#include "hal.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace demo {

namespace {
constexpr const char* kTag = "hal";

// Created on first use.
Hal* g_hal_instance = nullptr;
}  // namespace

Hal::Hal() : keyboard_(), audio_(i2c_bus_), imu_(i2c_bus_, CONFIG_HAL_MPU6050_ADDR) {}

Hal& GetHal()
{
    if (g_hal_instance == nullptr) {
        g_hal_instance = new Hal();
    }
    return *g_hal_instance;
}

esp_err_t Hal::init()
{
    if (initialized_) {
        ESP_LOGW(kTag, "HAL already initialized");
        return ESP_OK;
    }

    ESP_LOGI(kTag, "HAL init start");

    // Internal I2C bus: Cardputer ADV default SDA=GPIO8, SCL=GPIO9.
    esp_err_t err = i2c_bus_.init(CONFIG_HAL_I2C_SDA_GPIO, CONFIG_HAL_I2C_SCL_GPIO,
                                  CONFIG_HAL_I2C_FREQ_HZ);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "I2C bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Initialize keyboard (TCA8418).  Failure is non-fatal for the demo skeleton.
    if (keyboard_.init(i2c_bus_) != ESP_OK) {
        ESP_LOGW(kTag, "keyboard init failed, continuing without keyboard");
    }

    // Initialize ES8311 audio codec.  Non-fatal until I2S is wired.
    if (audio_.enableSpeaker() != ESP_OK) {
        ESP_LOGW(kTag, "ES8311 speaker init failed, continuing without audio");
    }

    // Initialize MPU6050 IMU.  Non-fatal if not present.
    if (imu_.init() != ESP_OK) {
        ESP_LOGW(kTag, "MPU6050 init failed, continuing without IMU");
    }

    initialized_ = true;
    ESP_LOGI(kTag, "HAL init done");
    return ESP_OK;
}

void Hal::update()
{
    // Reserved for periodic tasks (audio pump, power monitor, etc.).
}

void Hal::delayMs(std::uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

std::uint32_t Hal::millis()
{
    return static_cast<std::uint32_t>(esp_timer_get_time() / 1000ULL);
}

void Hal::i2cScan()
{
    if (!i2c_bus_.isReady()) {
        ESP_LOGW(kTag, "I2C bus not ready");
        return;
    }

    auto found = i2c_bus_.scan();
    ESP_LOGI(kTag, "I2C scan result:");
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (int row = 0; row < 8; ++row) {
        printf("%02x: ", row * 16);
        for (int col = 0; col < 16; ++col) {
            uint8_t addr = static_cast<uint8_t>(row * 16 + col);
            bool present = false;
            for (auto a : found) {
                if (a == addr) {
                    present = true;
                    break;
                }
            }
            if (present) {
                printf("%02x ", addr);
            } else {
                printf("-- ");
            }
        }
        printf("\r\n");
    }
    fflush(stdout);
}

}  // namespace demo
