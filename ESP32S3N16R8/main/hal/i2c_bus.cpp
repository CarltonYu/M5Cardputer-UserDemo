#include "i2c_bus.h"

#include "esp_log.h"

namespace demo {

namespace {
constexpr const char* kTag = "i2c_bus";
}  // namespace

I2cBus::~I2cBus()
{
    for (auto& dev : devices_) {
        if (dev.handle != nullptr) {
            i2c_master_bus_rm_device(dev.handle);
            dev.handle = nullptr;
        }
    }
    if (bus_ != nullptr) {
        i2c_del_master_bus(bus_);
        bus_ = nullptr;
    }
}

esp_err_t I2cBus::init(int sda_gpio, int scl_gpio, std::uint32_t freq_hz)
{
    if (bus_ != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    sda_gpio_ = sda_gpio;
    scl_gpio_ = scl_gpio;
    freq_hz_  = freq_hz;

    i2c_master_bus_config_t bus_config{};
    bus_config.i2c_port   = I2C_NUM_1;
    bus_config.sda_io_num = static_cast<gpio_num_t>(sda_gpio);
    bus_config.scl_io_num = static_cast<gpio_num_t>(scl_gpio);
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
    bus_config.glitch_ignore_cnt = 7;
    bus_config.flags.enable_internal_pullup = true;

    esp_err_t err = i2c_new_master_bus(&bus_config, &bus_);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(kTag, "I2C bus ready: SDA=GPIO%d SCL=GPIO%d freq=%lu Hz", sda_gpio, scl_gpio, freq_hz);
    return ESP_OK;
}

std::vector<std::uint8_t> I2cBus::scan()
{
    std::vector<std::uint8_t> found;
    if (bus_ == nullptr) {
        return found;
    }

    ESP_LOGI(kTag, "I2C scan start");
    for (std::uint8_t addr = 0x08; addr < 0x78; ++addr) {
        i2c_device_config_t dev_config{};
        dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev_config.device_address  = addr;
        dev_config.scl_speed_hz    = freq_hz_;

        i2c_master_dev_handle_t dev = nullptr;
        if (i2c_master_bus_add_device(bus_, &dev_config, &dev) != ESP_OK) {
            continue;
        }

        // A zero-byte probe: start + address + stop.  An ACK means a device is present.
        esp_err_t err = i2c_master_probe(bus_, addr, 100);
        i2c_master_bus_rm_device(dev);

        if (err == ESP_OK) {
            ESP_LOGI(kTag, "  found device at 0x%02X", addr);
            found.push_back(addr);
        }
    }
    ESP_LOGI(kTag, "I2C scan done, %d device(s)", static_cast<int>(found.size()));
    return found;
}

esp_err_t I2cBus::ensureDevice(std::uint8_t addr, i2c_master_dev_handle_t* out_handle)
{
    if (bus_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (out_handle == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    // Look for an existing handle.
    for (auto& dev : devices_) {
        if (dev.addr == addr && dev.handle != nullptr) {
            *out_handle = dev.handle;
            return ESP_OK;
        }
    }

    // Find a free slot.
    DeviceCache* slot = nullptr;
    for (auto& dev : devices_) {
        if (dev.handle == nullptr) {
            slot = &dev;
            break;
        }
    }
    if (slot == nullptr) {
        ESP_LOGE(kTag, "too many I2C devices (max %d)", kMaxCachedDevices);
        return ESP_ERR_NO_MEM;
    }

    i2c_device_config_t dev_config{};
    dev_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_config.device_address  = addr;
    dev_config.scl_speed_hz    = freq_hz_;

    esp_err_t err = i2c_master_bus_add_device(bus_, &dev_config, &slot->handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "failed to add device 0x%02X: %s", addr, esp_err_to_name(err));
        return err;
    }
    slot->addr      = addr;
    *out_handle = slot->handle;
    return ESP_OK;
}

esp_err_t I2cBus::writeReg(std::uint8_t addr, std::uint8_t reg, std::uint8_t value)
{
    i2c_master_dev_handle_t dev = nullptr;
    esp_err_t err = ensureDevice(addr, &dev);
    if (err != ESP_OK) {
        return err;
    }
    std::uint8_t buf[2] = {reg, value};
    err = i2c_master_transmit(dev, buf, 2, 100);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "writeReg 0x%02X:0x%02X failed: %s", addr, reg, esp_err_to_name(err));
    }
    return err;
}

esp_err_t I2cBus::writeRegs(std::uint8_t addr, std::uint8_t reg, const std::uint8_t* data, std::size_t len)
{
    if (data == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_master_dev_handle_t dev = nullptr;
    esp_err_t err = ensureDevice(addr, &dev);
    if (err != ESP_OK) {
        return err;
    }
    std::vector<std::uint8_t> buf;
    buf.reserve(len + 1);
    buf.push_back(reg);
    buf.insert(buf.end(), data, data + len);
    err = i2c_master_transmit(dev, buf.data(), buf.size(), 100);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "writeRegs 0x%02X:0x%02X len=%u failed: %s", addr, reg,
                 static_cast<unsigned>(len), esp_err_to_name(err));
    }
    return err;
}

esp_err_t I2cBus::readReg(std::uint8_t addr, std::uint8_t reg, std::uint8_t* out)
{
    return readRegs(addr, reg, out, 1);
}

esp_err_t I2cBus::readRegs(std::uint8_t addr, std::uint8_t reg, std::uint8_t* out, std::size_t len)
{
    if (out == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_master_dev_handle_t dev = nullptr;
    esp_err_t err = ensureDevice(addr, &dev);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_transmit_receive(dev, &reg, 1, out, len, 100);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "readRegs 0x%02X:0x%02X len=%u failed: %s", addr, reg,
                 static_cast<unsigned>(len), esp_err_to_name(err));
    }
    return err;
}

esp_err_t I2cBus::writeRaw(std::uint8_t addr, const std::uint8_t* data, std::size_t len)
{
    if (data == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_master_dev_handle_t dev = nullptr;
    esp_err_t err = ensureDevice(addr, &dev);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_transmit(dev, data, len, 100);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "writeRaw 0x%02X len=%u failed: %s", addr,
                 static_cast<unsigned>(len), esp_err_to_name(err));
    }
    return err;
}

esp_err_t I2cBus::readRaw(std::uint8_t addr, std::uint8_t* out, std::size_t len)
{
    if (out == nullptr || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    i2c_master_dev_handle_t dev = nullptr;
    esp_err_t err = ensureDevice(addr, &dev);
    if (err != ESP_OK) {
        return err;
    }
    err = i2c_master_receive(dev, out, len, 100);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "readRaw 0x%02X len=%u failed: %s", addr,
                 static_cast<unsigned>(len), esp_err_to_name(err));
    }
    return err;
}

}  // namespace demo
