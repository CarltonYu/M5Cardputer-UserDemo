#pragma once

#include <cstdint>
#include <vector>

#include "driver/i2c_master.h"
#include "esp_err.h"

namespace demo {

// Thin C++ wrapper around ESP-IDF I2C master bus (new driver, IDF 5.x).
class I2cBus {
public:
    I2cBus() = default;
    ~I2cBus();

    // Disable copy/assign because of the underlying handle.
    I2cBus(const I2cBus&)            = delete;
    I2cBus& operator=(const I2cBus&) = delete;

    // Initialize the I2C master bus.  Call once before any device operation.
    esp_err_t init(int sda_gpio, int scl_gpio, std::uint32_t freq_hz = 400000);

    // Returns true after successful init.
    bool isReady() const { return bus_ != nullptr; }

    // Probe all 7-bit addresses and return those that ACK.
    std::vector<std::uint8_t> scan();

    // Convenience wrappers for a single device address.
    esp_err_t writeReg(std::uint8_t addr, std::uint8_t reg, std::uint8_t value);
    esp_err_t writeRegs(std::uint8_t addr, std::uint8_t reg, const std::uint8_t* data, std::size_t len);
    esp_err_t readReg(std::uint8_t addr, std::uint8_t reg, std::uint8_t* out);
    esp_err_t readRegs(std::uint8_t addr, std::uint8_t reg, std::uint8_t* out, std::size_t len);
    esp_err_t writeRaw(std::uint8_t addr, const std::uint8_t* data, std::size_t len);
    esp_err_t readRaw(std::uint8_t addr, std::uint8_t* out, std::size_t len);

private:
    // Acquire a temporary device handle for the requested address/frequency.
    // The handle is cached per address to avoid repeated allocations.
    esp_err_t ensureDevice(std::uint8_t addr, i2c_master_dev_handle_t* out_handle);

    i2c_master_bus_handle_t bus_ = nullptr;
    std::uint32_t freq_hz_       = 400000;
    int sda_gpio_                = -1;
    int scl_gpio_                = -1;

    static constexpr std::size_t kMaxCachedDevices = 8;
    struct DeviceCache {
        std::uint8_t addr              = 0xFF;
        i2c_master_dev_handle_t handle = nullptr;
    };
    DeviceCache devices_[kMaxCachedDevices]{};
};

}  // namespace demo
