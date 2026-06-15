#include "es8311.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace demo {

namespace {
constexpr const char* kTag = "es8311";

// Each bulk entry is prefixed with the number of bytes that follow.
// A trailing 0 marks the end of the list.

// Common open + sample-format sequence for 16 kHz / 16-bit I2S slave mode.
// Clock config from M5Unified _microphone_enabled_cb_cardputer_adv (reg 0x01/0x02).
// Mic boost (0x16) left at ES8311 default 0 dB to avoid ADC clipping.
constexpr std::uint8_t kOpenSetFsBulk[] = {
    2, 0x0D, 0xFA,  // Power-up analog circuitry reference
    2, 0x44, 0x08,  // Enhance I2C noise immunity
    2, 0x44, 0x08,
    2, 0x01, 0xBA,  // Clock: MCLK from BCLK (Cardputer ADV per M5Unified)
    2, 0x02, 0x18,  // Clock multiplier PRE=3 (Cardputer ADV per M5Unified)
    2, 0x03, 0x10,  // ADC OSR
    2, 0x04, 0x20,  // DAC OSR
    2, 0x05, 0x00,  // ADC/DAC clock dividers
    2, 0x0B, 0x00,  // System
    2, 0x0C, 0x00,  // System
    2, 0x10, 0x1F,  // System
    2, 0x11, 0x7F,  // System
    2, 0x00, 0x80,  // Reset / slave mode, CSM power on
    // set_fs: 16-bit Philips I2S, 16 kHz
    2, 0x09, 0x0C,
    2, 0x0A, 0x0C,
    2, 0x02, 0x18,
    2, 0x05, 0x00,
    2, 0x03, 0x10,
    2, 0x04, 0x20,
    2, 0x07, 0x00,
    2, 0x08, 0xFF,
    2, 0x06, 0x03,
    2, 0x13, 0x10,
    2, 0x1B, 0x0A,
    2, 0x1C, 0x6A,
    2, 0x44, 0x58,  // Set internal reference signal (ADCL + DACR)
    0
};

constexpr std::uint8_t kEnableDacBulk[] = {
    2, 0x00, 0x80,  // Slave mode, CSM on
    2, 0x01, 0x3F,  // Use external MCLK
    2, 0x09, 0x0C,  // Power up DAC serial port
    2, 0x17, 0xBF,  // ADC volume max
    2, 0x0E, 0x02,  // Enable analog PGA
    2, 0x12, 0x00,  // Power-up DAC
    2, 0x14, 0x1A,  // Analog PGA / DMIC settings
    2, 0x0D, 0x01,  // Power up analog circuitry
    2, 0x15, 0x40,  // ADC ramp rate
    2, 0x37, 0x08,  // DAC ramprate / bypass equalizer
    2, 0x45, 0x00,  // GP control
    2, 0x32, 0xBF,  // DAC volume 0 dB
    0
};

// ADC-only path: match M5Unified _microphone_enabled_cb_cardputer_adv.
// Minimal register set — no unnecessary DAC/extra writes.
constexpr std::uint8_t kEnableAdcBulk[] = {
    2, 0x00, 0x80,  // Reset, CSM on
    2, 0x01, 0xBA,  // Clock: MCLK from BCLK
    2, 0x02, 0x18,  // MULT_PRE=3
    2, 0x0D, 0x01,  // Power up analog circuitry
    2, 0x0E, 0x02,  // Enable analog PGA + ADC modulator
    2, 0x14, 0x10,  // MIC1 input, PGA gain minimum (0 dB)
    2, 0x17, 0xBF,  // ADC volume ±0 dB
    2, 0x1C, 0x6A,  // ADC EQ bypass, cancel DC offset
    0
};

constexpr std::uint8_t kDisableBulk[] = {
    2, 0x32, 0x00,
    2, 0x17, 0x00,
    2, 0x0E, 0xFF,
    2, 0x12, 0x02,
    2, 0x14, 0x00,
    2, 0x0D, 0xFA,
    2, 0x15, 0x00,
    2, 0x02, 0x10,
    2, 0x00, 0x00,
    2, 0x00, 0x1F,
    2, 0x01, 0x30,
    2, 0x01, 0x00,
    2, 0x45, 0x00,
    2, 0x0D, 0xFC,
    2, 0x02, 0x00,
    0
};

esp_err_t writeBulk(I2cBus& i2c, std::uint8_t addr, const std::uint8_t* bulk)
{
    std::size_t i = 0;
    while (bulk[i] != 0) {
        std::uint8_t len = bulk[i];
        ++i;
        esp_err_t err = i2c.writeRaw(addr, &bulk[i], len);
        if (err != ESP_OK) {
            return err;
        }
        i += len;
    }
    return ESP_OK;
}
}  // namespace

Es8311::Es8311(I2cBus& i2c, std::uint8_t addr) : i2c_(i2c), addr_(addr) {}

esp_err_t Es8311::enableSpeaker()
{
    ESP_LOGI(kTag, "enable speaker");
    // Give the NS4150B 5 V rail and the ES8311 oscillator time to stabilise
    // before the first I2C transaction; cold boots occasionally NACK otherwise.
    // After a successful init the delay can be much shorter.
    vTaskDelay(pdMS_TO_TICKS(speaker_ok_ ? 50 : 500));
    for (int attempt = 0; attempt < (speaker_ok_ ? 3 : 5); ++attempt) {
        esp_err_t err = writeBulk(i2c_, addr_, kOpenSetFsBulk);
        if (err == ESP_OK) {
            err = writeBulk(i2c_, addr_, kEnableDacBulk);
            if (err == ESP_OK) {
                speaker_ok_ = true;
                mic_ok_     = false;  // no longer in mic mode
                return ESP_OK;
            }
        }
        ESP_LOGW(kTag, "speaker enable attempt %d failed, retrying", attempt + 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    speaker_ok_ = false;
    return ESP_FAIL;
}

esp_err_t Es8311::enableMicrophone()
{
    ESP_LOGI(kTag, "enable microphone");
    // Cold-boot needs a longer settling delay; after first success 10 ms is enough.
    vTaskDelay(pdMS_TO_TICKS(mic_ok_ ? 50 : 200));
    for (int attempt = 0; attempt < (mic_ok_ ? 3 : 3); ++attempt) {
        esp_err_t err = writeBulk(i2c_, addr_, kOpenSetFsBulk);
        if (err == ESP_OK) {
            err = writeBulk(i2c_, addr_, kEnableAdcBulk);
            if (err == ESP_OK) {
                mic_ok_      = true;
                speaker_ok_  = false;  // no longer in speaker mode
                return ESP_OK;
            }
        }
        ESP_LOGW(kTag, "microphone enable attempt %d failed, retrying", attempt + 1);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    mic_ok_ = false;
    return ESP_FAIL;
}

esp_err_t Es8311::disable()
{
    ESP_LOGI(kTag, "disable");
    return writeBulk(i2c_, addr_, kDisableBulk);
}

esp_err_t Es8311::writeReg(std::uint8_t reg, std::uint8_t value)
{
    return i2c_.writeReg(addr_, reg, value);
}

std::uint8_t Es8311::readReg(std::uint8_t reg)
{
    std::uint8_t value = 0;
    i2c_.readReg(addr_, reg, &value);
    return value;
}

}  // namespace demo
