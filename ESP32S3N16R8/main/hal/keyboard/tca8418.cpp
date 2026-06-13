#include "tca8418.h"

#include "esp_log.h"

namespace demo {

namespace {
constexpr const char* kTag = "tca8418";

// Register map (from the Texas Instruments TCA8418 datasheet).
constexpr std::uint8_t TCA8418_REG_CFG             = 0x01;
constexpr std::uint8_t TCA8418_REG_INT_STAT        = 0x02;
constexpr std::uint8_t TCA8418_REG_KEY_LCK_EC      = 0x03;
constexpr std::uint8_t TCA8418_REG_KEY_EVENT_A     = 0x04;
constexpr std::uint8_t TCA8418_REG_GPIO_DAT_STAT_1 = 0x14;
constexpr std::uint8_t TCA8418_REG_GPIO_DAT_STAT_2 = 0x15;
constexpr std::uint8_t TCA8418_REG_GPIO_DAT_STAT_3 = 0x16;
constexpr std::uint8_t TCA8418_REG_GPIO_DIR_1      = 0x23;
constexpr std::uint8_t TCA8418_REG_GPIO_DIR_2      = 0x24;
constexpr std::uint8_t TCA8418_REG_GPIO_DIR_3      = 0x25;
constexpr std::uint8_t TCA8418_REG_GPIO_INT_LVL_1  = 0x26;
constexpr std::uint8_t TCA8418_REG_GPIO_INT_LVL_2  = 0x27;
constexpr std::uint8_t TCA8418_REG_GPIO_INT_LVL_3  = 0x28;
constexpr std::uint8_t TCA8418_REG_GPIO_INT_EN_1   = 0x2A;
constexpr std::uint8_t TCA8418_REG_GPIO_INT_EN_2   = 0x2B;
constexpr std::uint8_t TCA8418_REG_GPIO_INT_EN_3   = 0x2C;
constexpr std::uint8_t TCA8418_REG_KP_GPIO_1       = 0x2D;
constexpr std::uint8_t TCA8418_REG_KP_GPIO_2       = 0x2E;
constexpr std::uint8_t TCA8418_REG_KP_GPIO_3       = 0x2F;
constexpr std::uint8_t TCA8418_REG_GPI_EM_1        = 0x30;
constexpr std::uint8_t TCA8418_REG_GPI_EM_2        = 0x31;
constexpr std::uint8_t TCA8418_REG_GPI_EM_3        = 0x32;
constexpr std::uint8_t TCA8418_REG_CFG_AI          = 0x80;
constexpr std::uint8_t TCA8418_REG_CFG_GPI_E_CGF   = 0x40;
constexpr std::uint8_t TCA8418_REG_CFG_OVR_FLOW_M  = 0x20;
constexpr std::uint8_t TCA8418_REG_CFG_INT_CFG     = 0x10;
constexpr std::uint8_t TCA8418_REG_CFG_OVR_FLOW_IEN = 0x08;
constexpr std::uint8_t TCA8418_REG_CFG_K_LCK_IEN   = 0x04;
constexpr std::uint8_t TCA8418_REG_CFG_GPI_IEN     = 0x02;
constexpr std::uint8_t TCA8418_REG_CFG_KE_IEN      = 0x01;

constexpr std::uint8_t kGpioStatClear[] = {
    TCA8418_REG_GPIO_DAT_STAT_1, TCA8418_REG_GPIO_DAT_STAT_2, TCA8418_REG_GPIO_DAT_STAT_3
};
}  // namespace

Tca8418::Tca8418(I2cBus& i2c, std::uint8_t addr) : i2c_(i2c), addr_(addr) {}

esp_err_t Tca8418::begin()
{
    // Verify device presence.
    std::uint8_t cfg = 0;
    esp_err_t err = i2c_.readReg(addr_, TCA8418_REG_CFG, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "device not responding at 0x%02X", addr_);
        return err;
    }

    // Reset the device.
    err = writeRegister8(TCA8418_REG_CFG, 0x01);
    if (err != ESP_OK) {
        return err;
    }

    // Set all GPIO pins to input.
    writeRegister8(TCA8418_REG_GPIO_DIR_1, 0x00);
    writeRegister8(TCA8418_REG_GPIO_DIR_2, 0x00);
    writeRegister8(TCA8418_REG_GPIO_DIR_3, 0x00);

    // Add all pins to key events.
    writeRegister8(TCA8418_REG_GPI_EM_1, 0xFF);
    writeRegister8(TCA8418_REG_GPI_EM_2, 0xFF);
    writeRegister8(TCA8418_REG_GPI_EM_3, 0xFF);

    // Falling edge interrupts.
    writeRegister8(TCA8418_REG_GPIO_INT_LVL_1, 0x00);
    writeRegister8(TCA8418_REG_GPIO_INT_LVL_2, 0x00);
    writeRegister8(TCA8418_REG_GPIO_INT_LVL_3, 0x00);

    // Enable interrupts on all pins.
    writeRegister8(TCA8418_REG_GPIO_INT_EN_1, 0xFF);
    writeRegister8(TCA8418_REG_GPIO_INT_EN_2, 0xFF);
    writeRegister8(TCA8418_REG_GPIO_INT_EN_3, 0xFF);

    // Flush event FIFO.
    flush();

    ESP_LOGI(kTag, "TCA8418 ready at 0x%02X", addr_);
    return ESP_OK;
}

esp_err_t Tca8418::matrix(std::uint8_t rows, std::uint8_t cols)
{
    if (rows > 8 || cols > 10) {
        return ESP_ERR_INVALID_ARG;
    }

    std::uint8_t mask = 0;
    for (std::uint8_t r = 0; r < rows; ++r) {
        mask <<= 1;
        mask |= 1;
    }
    writeRegister8(TCA8418_REG_KP_GPIO_1, mask);

    mask = 0;
    for (std::uint8_t c = 0; c < cols && c < 8; ++c) {
        mask <<= 1;
        mask |= 1;
    }
    writeRegister8(TCA8418_REG_KP_GPIO_2, mask);

    mask = 0;
    for (std::uint8_t c = 8; c < cols; ++c) {
        mask <<= 1;
        mask |= 1;
    }
    writeRegister8(TCA8418_REG_KP_GPIO_3, mask);

    return ESP_OK;
}

esp_err_t Tca8418::flush()
{
    // Reading the KEY_EVENT_A register pops the FIFO.  Keep reading until empty.
    for (int i = 0; i < 16; ++i) {
        std::uint8_t event = getEvent();
        if (event == 0) {
            break;
        }
    }

    // Clear any stale GPIO interrupt status.
    for (std::uint8_t reg : kGpioStatClear) {
        std::uint8_t dummy = 0;
        i2c_.readReg(addr_, reg, &dummy);  // read clears status
    }
    return ESP_OK;
}

std::uint8_t Tca8418::getEvent()
{
    std::uint8_t event = 0;
    i2c_.readReg(addr_, TCA8418_REG_KEY_EVENT_A, &event);
    return event;
}

esp_err_t Tca8418::writeRegister8(std::uint8_t reg, std::uint8_t value)
{
    return i2c_.writeReg(addr_, reg, value);
}

std::uint8_t Tca8418::readRegister8(std::uint8_t reg)
{
    std::uint8_t value = 0;
    i2c_.readReg(addr_, reg, &value);
    return value;
}

esp_err_t Tca8418::enableInterrupts()
{
    std::uint8_t cfg = TCA8418_REG_CFG_AI | TCA8418_REG_CFG_GPI_E_CGF |
                       TCA8418_REG_CFG_OVR_FLOW_M | TCA8418_REG_CFG_INT_CFG |
                       TCA8418_REG_CFG_OVR_FLOW_IEN | TCA8418_REG_CFG_K_LCK_IEN |
                       TCA8418_REG_CFG_GPI_IEN | TCA8418_REG_CFG_KE_IEN;
    return writeRegister8(TCA8418_REG_CFG, cfg);
}

}  // namespace demo
