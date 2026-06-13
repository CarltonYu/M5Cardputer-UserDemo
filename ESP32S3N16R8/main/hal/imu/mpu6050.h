#pragma once

#include <cstdint>

#include "esp_err.h"
#include "hal/i2c_bus.h"

namespace demo {

struct ImuData {
    float accel_x;  // g
    float accel_y;
    float accel_z;
    float gyro_x;   // degrees per second
    float gyro_y;
    float gyro_z;
    float temp;     // Celsius
};

class Mpu6050 {
public:
    static constexpr std::uint8_t kDefaultAddress = 0x68;
    static constexpr std::uint8_t kAltAddress     = 0x69;

    Mpu6050(I2cBus& i2c, std::uint8_t addr = kDefaultAddress);

    esp_err_t init();
    bool isReady() const { return ready_; }

    // Read all sensors into the provided structure.
    esp_err_t read(ImuData* out);

private:
    I2cBus& i2c_;
    std::uint8_t addr_;
    bool ready_ = false;

    static constexpr float kAccelScale = 2.0f / 32768.0f;  // ±2 g
    static constexpr float kGyroScale  = 250.0f / 32768.0f;  // ±250 dps
    static constexpr float kTempScale  = 1.0f / 340.0f;
    static constexpr float kTempOffset = 36.53f;
};

}  // namespace demo
