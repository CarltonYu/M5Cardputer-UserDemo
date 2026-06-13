#pragma once

#include <cstdint>

#include "esp_err.h"
#include "hal/i2c_bus.h"

namespace demo {

// Minimal ESP-IDF port of the Adafruit TCA8418 driver.
// Only the features needed for the Cardputer ADV keyboard are kept.
class Tca8418 {
public:
    static constexpr std::uint8_t kDefaultAddress = 0x34;

    Tca8418(I2cBus& i2c, std::uint8_t addr = kDefaultAddress);

    esp_err_t begin();

    // Configure keypad matrix size.
    esp_err_t matrix(std::uint8_t rows, std::uint8_t cols);

    // Clear the key event FIFO.
    esp_err_t flush();

    // Read the next key event from the FIFO (0 means empty).
    std::uint8_t getEvent();

    // Read/write raw registers.
    esp_err_t writeRegister8(std::uint8_t reg, std::uint8_t value);
    std::uint8_t readRegister8(std::uint8_t reg);

    // Enable key/GPI interrupts.
    esp_err_t enableInterrupts();

private:
    I2cBus& i2c_;
    std::uint8_t addr_;
};

}  // namespace demo
