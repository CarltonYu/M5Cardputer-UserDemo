#include "hal.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cmath>
#include <vector>

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

    // Initialize I2S audio stream first so the codec has clocks when powered up.
    if (i2s_audio_.init() != ESP_OK) {
        ESP_LOGW(kTag, "I2S audio init failed, continuing without audio");
    }

    // Initialize ES8311 audio codec.  Non-fatal until I2S is wired.
    if (audio_.enableSpeaker() != ESP_OK) {
        ESP_LOGW(kTag, "ES8311 speaker init failed, continuing without audio");
    } else {
        // Mute the DAC until something is actually played; a powered-up DAC
        // without an active I2S stream can produce a buzzing noise.
        audio_.writeReg(0x32, 0x00);
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

esp_err_t Hal::playTone(int freq_hz, int duration_ms, float volume)
{
    if (!i2s_audio_.initialized()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (i2s_audio_.isRecording()) {
        return ESP_OK;
    }

    const std::size_t samples = static_cast<std::size_t>(CONFIG_AUDIO_SAMPLE_RATE) * duration_ms / 1000;
    if (samples == 0) {
        return ESP_OK;
    }

    std::vector<std::int16_t> buffer(samples);
    const float step = 2.0f * static_cast<float>(M_PI) * static_cast<float>(freq_hz) /
                       static_cast<float>(CONFIG_AUDIO_SAMPLE_RATE);
    for (std::size_t i = 0; i < samples; ++i) {
        float sample = std::sin(step * static_cast<float>(i)) * volume;
        buffer[i]    = static_cast<std::int16_t>(sample * 32767.0f);
    }

    i2s_audio_.stopRecording();
    if (!audio_.speakerReady()) {
        if (audio_.enableSpeaker() != ESP_OK) {
            return ESP_FAIL;
        }
    }
    // Restore normal beep volume in case it was left high by record playback.
    audio_.writeReg(0x32, 0xBF);
    if (i2s_audio_.startPlayback() != ESP_OK) {
        return ESP_FAIL;
    }

    std::size_t written = 0;
    while (written < samples) {
        int n = i2s_audio_.writePlayBuffer(&buffer[written], samples - written);
        if (n <= 0) {
            break;
        }
        written += static_cast<std::size_t>(n);
    }

    i2s_audio_.stopPlayback();
    return ESP_OK;
}

}  // namespace demo
