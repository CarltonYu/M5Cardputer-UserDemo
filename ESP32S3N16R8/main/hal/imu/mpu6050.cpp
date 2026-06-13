#include "mpu6050.h"

#include "esp_log.h"

namespace demo {

namespace {
constexpr const char* kTag = "mpu6050";

// MPU6050 register map.
constexpr std::uint8_t kRegWhoAmI     = 0x75;
constexpr std::uint8_t kRegPwrMgmt1   = 0x6B;
constexpr std::uint8_t kRegPwrMgmt2   = 0x6C;
constexpr std::uint8_t kRegSmplrtDiv  = 0x19;
constexpr std::uint8_t kRegConfig     = 0x1A;
constexpr std::uint8_t kRegGyroConfig = 0x1B;
constexpr std::uint8_t kRegAccelConfig = 0x1C;
constexpr std::uint8_t kRegFifoEn     = 0x23;
constexpr std::uint8_t kRegIntPinCfg  = 0x37;
constexpr std::uint8_t kRegIntEnable  = 0x38;
constexpr std::uint8_t kRegAccelXoutH = 0x3B;
constexpr std::uint8_t kRegTempOutH   = 0x41;

inline std::int16_t read_i16_be(const std::uint8_t* data)
{
    return static_cast<std::int16_t>((static_cast<std::uint16_t>(data[0]) << 8) | data[1]);
}
}  // namespace

Mpu6050::Mpu6050(I2cBus& i2c, std::uint8_t addr) : i2c_(i2c), addr_(addr) {}

esp_err_t Mpu6050::init()
{
    ready_ = false;

    std::uint8_t who = 0;
    esp_err_t err = i2c_.readReg(addr_, kRegWhoAmI, &who);
    if (err != ESP_OK || who != 0x68) {
        ESP_LOGE(kTag, "MPU6050 not found at 0x%02X (whoami=0x%02X)", addr_, who);
        return (err != ESP_OK) ? err : ESP_ERR_NOT_FOUND;
    }

    // Wake up and use PLL with X gyro reference.
    i2c_.writeReg(addr_, kRegPwrMgmt1, 0x01);
    // Disable standby modes.
    i2c_.writeReg(addr_, kRegPwrMgmt2, 0x00);
    // Sample rate = 1 kHz / (1 + 0) = 1 kHz.
    i2c_.writeReg(addr_, kRegSmplrtDiv, 0x00);
    // DLPF: 44 Hz accel / 42 Hz gyro.
    i2c_.writeReg(addr_, kRegConfig, 0x03);
    // Gyro full scale: ±250 dps.
    i2c_.writeReg(addr_, kRegGyroConfig, 0x00);
    // Accel full scale: ±2 g.
    i2c_.writeReg(addr_, kRegAccelConfig, 0x00);
    // Disable FIFO.
    i2c_.writeReg(addr_, kRegFifoEn, 0x00);
    // INT pin active high, push-pull, latched, clear on any read.
    i2c_.writeReg(addr_, kRegIntPinCfg, 0x22);
    // Disable interrupts.
    i2c_.writeReg(addr_, kRegIntEnable, 0x00);

    ready_ = true;
    ESP_LOGI(kTag, "MPU6050 ready at 0x%02X", addr_);
    return ESP_OK;
}

esp_err_t Mpu6050::read(ImuData* out)
{
    if (out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!ready_) {
        return ESP_ERR_INVALID_STATE;
    }

    std::uint8_t buffer[14] = {};
    esp_err_t err = i2c_.readRegs(addr_, kRegAccelXoutH, buffer, 14);
    if (err != ESP_OK) {
        return err;
    }

    out->accel_x = read_i16_be(&buffer[0]) * kAccelScale;
    out->accel_y = read_i16_be(&buffer[2]) * kAccelScale;
    out->accel_z = read_i16_be(&buffer[4]) * kAccelScale;
    out->temp    = read_i16_be(&buffer[6]) * kTempScale + kTempOffset;
    out->gyro_x  = read_i16_be(&buffer[8]) * kGyroScale;
    out->gyro_y  = read_i16_be(&buffer[10]) * kGyroScale;
    out->gyro_z  = read_i16_be(&buffer[12]) * kGyroScale;

    return ESP_OK;
}

}  // namespace demo
