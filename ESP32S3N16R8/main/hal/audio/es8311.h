#pragma once

#include <cstdint>

#include "esp_err.h"
#include "hal/i2c_bus.h"

namespace demo {

// Minimal ES8311 audio codec driver for Cardputer ADV.
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

private:
    I2cBus& i2c_;
    std::uint8_t addr_;
};

}  // namespace demo
