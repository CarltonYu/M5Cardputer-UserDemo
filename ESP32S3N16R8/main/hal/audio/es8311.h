#pragma once

#include <cstdint>

#include "esp_err.h"
#include "hal/i2c_bus.h"

namespace demo {

// ES8311 audio codec driver.
// The register sequences below are ported from Espressif's esp_codec_dev
// component (device/es8311/es8311.c) so the codec clock and PGA configuration
// match the official ESP-IDF i2s_codec/i2s_es8311 example.
class Es8311 {
public:
    static constexpr std::uint8_t kDefaultAddress = 0x18;

    Es8311(I2cBus& i2c, std::uint8_t addr = kDefaultAddress);

    // Send power-up register sequence for speaker output.
    esp_err_t enableSpeaker();

    // Send power-up register sequence for microphone input.
    esp_err_t enableMicrophone();

    // Power down analog circuitry.
    esp_err_t disable();

    // Raw register access.
    esp_err_t writeReg(std::uint8_t reg, std::uint8_t value);
    std::uint8_t readReg(std::uint8_t reg);

    // True if the codec was last configured for speaker output.
    bool speakerReady() const { return speaker_ok_; }

    // True if the codec was last configured for microphone input.
    bool microphoneReady() const { return mic_ok_; }

private:
    I2cBus& i2c_;
    std::uint8_t addr_;
    bool speaker_ok_ = false;
    bool mic_ok_     = false;
};

}  // namespace demo
