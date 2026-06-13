#include "es8311.h"

#include "esp_log.h"

namespace demo {

namespace {
constexpr const char* kTag = "es8311";

// Each bulk entry is prefixed with the number of bytes that follow.
// A trailing 0 marks the end of the list.
constexpr std::uint8_t kSpeakerEnableBulk[] = {
    2, 0x00, 0x80,  // RESET / CSM power on
    2, 0x01, 0xB5,  // CLOCK MANAGER / MCLK = BCLK
    2, 0x02, 0x18,  // CLOCK MANAGER / MULT_PRE = 3
    2, 0x0D, 0x01,  // SYSTEM / Power up analog circuitry
    2, 0x12, 0x00,  // SYSTEM / Power-up DAC
    2, 0x13, 0x10,  // SYSTEM / Enable output to HP drive
    2, 0x32, 0xBF,  // DAC / DAC volume (0xBF == ±0 dB)
    2, 0x37, 0x08,  // DAC / Bypass DAC equalizer
    0
};

constexpr std::uint8_t kMicEnableBulk[] = {
    2, 0x00, 0x80,  // RESET / CSM power on
    2, 0x01, 0xBA,  // CLOCK MANAGER / MCLK = BCLK (mic path)
    2, 0x02, 0x18,  // CLOCK MANAGER / MULT_PRE = 3
    2, 0x0D, 0x01,  // SYSTEM / Power up analog circuitry
    2, 0x0E, 0x02,  // SYSTEM / Enable analog PGA, enable ADC modulator
    2, 0x14, 0x10,  // ADC / select Mic1p-Mic1n, minimum PGA gain
    2, 0x17, 0xFF,  // ADC / ADC volume max gain
    2, 0x1C, 0x6A,  // ADC / Equalizer bypass, cancel DC offset
    0
};

constexpr std::uint8_t kDisableBulk[] = {
    2, 0x0D, 0xFC,  // SYSTEM / Power down analog circuitry
    2, 0x0E, 0x6A,  // SYSTEM
    2, 0x00, 0x00,  // RESET / CSM power down
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
    return writeBulk(i2c_, addr_, kSpeakerEnableBulk);
}

esp_err_t Es8311::enableMicrophone()
{
    ESP_LOGI(kTag, "enable microphone");
    return writeBulk(i2c_, addr_, kMicEnableBulk);
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
