/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_compass.h"
#include "app_imu/assets/imu_big.h"
#include "app_imu/assets/imu_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <hal.h>
#include <mooncake_log.h>
#include <algorithm>
#include <cmath>

using namespace mooncake;

namespace {
constexpr uint8_t QMC5883L_ADDR_0D        = 0x0D;
constexpr uint8_t QMC5883L_ADDR_0C        = 0x0C;
constexpr uint8_t QMC5883L_REG_DATA       = 0x00;
constexpr uint8_t QMC5883L_REG_STATUS     = 0x06;
constexpr uint8_t QMC5883L_REG_CTRL1      = 0x09;
constexpr uint8_t QMC5883L_REG_CTRL2      = 0x0A;
constexpr uint8_t QMC5883L_REG_SET_RESET  = 0x0B;
constexpr uint8_t HMC5883L_ADDR           = 0x1E;
constexpr uint8_t HMC5883L_REG_CONFIG_A   = 0x00;
constexpr uint8_t HMC5883L_REG_CONFIG_B   = 0x01;
constexpr uint8_t HMC5883L_REG_MODE       = 0x02;
constexpr uint8_t HMC5883L_REG_DATA       = 0x03;
constexpr uint8_t HMC5883L_REG_STATUS     = 0x09;
constexpr uint32_t COMPASS_I2C_FREQ       = 100000;
constexpr uint32_t COMPASS_PROBE_MS       = 1000;
constexpr uint32_t COMPASS_READ_MS        = 80;
constexpr uint32_t COMPASS_RENDER_MS      = 120;
constexpr float COMPASS_HEADING_OFFSET    = 0.0f;
constexpr bool COMPASS_SWAP_XY            = false;
constexpr int COMPASS_AXIS_X_SIGN         = 1;
constexpr int COMPASS_AXIS_Y_SIGN         = 1;
constexpr float PI_F                      = 3.14159265358979323846f;

int16_t read_i16_le(const uint8_t* data)
{
    return static_cast<int16_t>(static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8));
}

int16_t read_i16_be(const uint8_t* data)
{
    return static_cast<int16_t>((static_cast<uint16_t>(data[0]) << 8) | static_cast<uint16_t>(data[1]));
}

const char* yn(bool value)
{
    return value ? "Y" : "-";
}
}  // namespace

AppCompass::AppCompass()
{
    setAppInfo().name     = "Compass";
    setAppInfo().userData = new AppIcon_t(image_data_imu_big, image_data_imu_small);
}

AppCompass::~AppCompass()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppCompass::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    auto& canvas = GetHAL().canvas;
    canvas.setBaseColor(THEME_COLOR_BG);
    canvas.setTextScroll(false);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextDatum(textdatum_t::top_left);

    setup_i2c_buses();
    update_bus_status();
    probe_compass();
    _needs_render = true;
    render_page();
}

void AppCompass::onRunning()
{
    uint32_t now = GetHAL().millis();

    if (now - _last_probe_time >= COMPASS_PROBE_MS) {
        update_bus_status();
        if (!_detected) {
            probe_compass();
        }
        _needs_render = true;
    }

    if (_detected && now - _last_read_time >= COMPASS_READ_MS) {
        _last_read_time = now;
        _needs_render   = read_compass() || _needs_render;
    }

    if (_needs_render || now - _last_render_time >= COMPASS_RENDER_MS) {
        render_page();
    }

    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppCompass::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
}

void AppCompass::setup_i2c_buses()
{
    GetHAL().ensureCardputerAdvI2C();
    M5.In_I2C.begin();
    if (M5.Ex_I2C.isEnabled()) {
        M5.Ex_I2C.begin();
    }

    _bus_g8_g9.name = "G8/9";
    _bus_g8_g9.i2c  = &M5.In_I2C;
    _bus_g2_g1.name = "G2/1";
    _bus_g2_g1.i2c  = &M5.Ex_I2C;
}

void AppCompass::update_bus_status()
{
    _last_probe_time = GetHAL().millis();
    update_one_bus_status(_bus_g8_g9);
    update_one_bus_status(_bus_g2_g1);
}

void AppCompass::update_one_bus_status(BusStatus& bus)
{
    bus.enabled    = bus.i2c != nullptr && bus.i2c->isEnabled();
    bus.ack_qmc_0d = false;
    bus.ack_qmc_0c = false;
    bus.ack_hmc_1e = false;
    bus.sda        = bus.enabled ? bus.i2c->getSDA() : -1;
    bus.scl        = bus.enabled ? bus.i2c->getSCL() : -1;

    if (!bus.enabled) {
        return;
    }

    bus.ack_qmc_0d = bus.i2c->scanID(QMC5883L_ADDR_0D, COMPASS_I2C_FREQ);
    bus.ack_qmc_0c = bus.i2c->scanID(QMC5883L_ADDR_0C, COMPASS_I2C_FREQ);
    bus.ack_hmc_1e = bus.i2c->scanID(HMC5883L_ADDR, COMPASS_I2C_FREQ);
}

bool AppCompass::probe_compass()
{
    if (probe_bus(_bus_g8_g9)) {
        return true;
    }
    if (probe_bus(_bus_g2_g1)) {
        return true;
    }

    clear_detected();
    return false;
}

bool AppCompass::probe_bus(BusStatus& bus)
{
    if (!bus.enabled) {
        return false;
    }

    if (init_qmc5883l(bus, QMC5883L_ADDR_0D)) {
        return true;
    }
    if (init_qmc5883l(bus, QMC5883L_ADDR_0C)) {
        return true;
    }
    if (init_hmc5883l(bus, HMC5883L_ADDR)) {
        return true;
    }

    return false;
}

bool AppCompass::init_qmc5883l(BusStatus& bus, uint8_t address)
{
    if (bus.i2c == nullptr || !bus.i2c->scanID(address, COMPASS_I2C_FREQ)) {
        return false;
    }

    bus.i2c->writeRegister8(address, QMC5883L_REG_CTRL2, 0x80, COMPASS_I2C_FREQ);
    GetHAL().delay(10);
    bool ok = bus.i2c->writeRegister8(address, QMC5883L_REG_SET_RESET, 0x01, COMPASS_I2C_FREQ);
    ok      = bus.i2c->writeRegister8(address, QMC5883L_REG_CTRL1, 0x1D, COMPASS_I2C_FREQ) && ok;

    if (!ok) {
        return false;
    }

    set_detected(bus, CompassChip::QMC5883L, address);
    return true;
}

bool AppCompass::init_hmc5883l(BusStatus& bus, uint8_t address)
{
    if (bus.i2c == nullptr || !bus.i2c->scanID(address, COMPASS_I2C_FREQ)) {
        return false;
    }

    bool ok = bus.i2c->writeRegister8(address, HMC5883L_REG_CONFIG_A, 0x70, COMPASS_I2C_FREQ);
    ok      = bus.i2c->writeRegister8(address, HMC5883L_REG_CONFIG_B, 0x20, COMPASS_I2C_FREQ) && ok;
    ok      = bus.i2c->writeRegister8(address, HMC5883L_REG_MODE, 0x00, COMPASS_I2C_FREQ) && ok;
    GetHAL().delay(10);

    if (!ok) {
        return false;
    }

    set_detected(bus, CompassChip::HMC5883L, address);
    return true;
}

void AppCompass::set_detected(BusStatus& bus, CompassChip chip, uint8_t address)
{
    _active_i2c       = bus.i2c;
    _active_bus       = bus.name;
    _chip             = chip;
    _address          = address;
    _status           = 0;
    _detected         = true;
    _has_sample       = false;
    _calibrating      = false;
    _raw_x            = 0;
    _raw_y            = 0;
    _raw_z            = 0;
    _heading          = 0.0f;
    _min_x            = 0.0f;
    _max_x            = 0.0f;
    _min_y            = 0.0f;
    _max_y            = 0.0f;
    _read_count       = 0;
    _fail_count       = 0;
    _last_sample_time = 0;
    mclog::tagInfo(getAppInfo().name, "{} detected on {} addr 0x{:02X}", chip_name(), _active_bus, _address);
}

void AppCompass::clear_detected()
{
    _active_i2c  = nullptr;
    _active_bus  = "";
    _chip        = CompassChip::NONE;
    _address     = 0;
    _status      = 0;
    _detected    = false;
    _has_sample  = false;
    _calibrating = false;
}

bool AppCompass::read_compass()
{
    switch (_chip) {
        case CompassChip::QMC5883L:
            return read_qmc5883l();
        case CompassChip::HMC5883L:
            return read_hmc5883l();
        default:
            return false;
    }
}

bool AppCompass::read_qmc5883l()
{
    if (_active_i2c == nullptr) {
        return false;
    }

    uint8_t status = 0;
    if (!_active_i2c->readRegister(_address, QMC5883L_REG_STATUS, &status, 1, COMPASS_I2C_FREQ)) {
        _fail_count++;
        clear_detected();
        return false;
    }

    _status = status;
    if (!(status & 0x01)) {
        return false;
    }

    uint8_t data[6] = {};
    if (!_active_i2c->readRegister(_address, QMC5883L_REG_DATA, data, sizeof(data), COMPASS_I2C_FREQ)) {
        _fail_count++;
        clear_detected();
        return false;
    }

    _raw_x = read_i16_le(&data[0]);
    _raw_y = read_i16_le(&data[2]);
    _raw_z = read_i16_le(&data[4]);
    _read_count++;
    _last_sample_time = GetHAL().millis();
    update_heading();
    return true;
}

bool AppCompass::read_hmc5883l()
{
    if (_active_i2c == nullptr) {
        return false;
    }

    uint8_t status = 0;
    if (!_active_i2c->readRegister(_address, HMC5883L_REG_STATUS, &status, 1, COMPASS_I2C_FREQ)) {
        _fail_count++;
        clear_detected();
        return false;
    }

    _status = status;
    if (!(status & 0x01)) {
        return false;
    }

    uint8_t data[6] = {};
    if (!_active_i2c->readRegister(_address, HMC5883L_REG_DATA, data, sizeof(data), COMPASS_I2C_FREQ)) {
        _fail_count++;
        clear_detected();
        return false;
    }

    _raw_x = read_i16_be(&data[0]);
    _raw_z = read_i16_be(&data[2]);
    _raw_y = read_i16_be(&data[4]);
    _read_count++;
    _last_sample_time = GetHAL().millis();
    update_heading();
    return true;
}

void AppCompass::update_heading()
{
    float sensor_x = static_cast<float>(_raw_x);
    float sensor_y = static_cast<float>(_raw_y);
    float x        = (COMPASS_SWAP_XY ? sensor_y : sensor_x) * COMPASS_AXIS_X_SIGN;
    float y        = (COMPASS_SWAP_XY ? sensor_x : sensor_y) * COMPASS_AXIS_Y_SIGN;

    if (!_has_sample) {
        _min_x      = x;
        _max_x      = x;
        _min_y      = y;
        _max_y      = y;
        _has_sample = true;
    } else {
        _min_x = std::min(_min_x, x);
        _max_x = std::max(_max_x, x);
        _min_y = std::min(_min_y, y);
        _max_y = std::max(_max_y, y);
    }

    float span_x = _max_x - _min_x;
    float span_y = _max_y - _min_y;
    _calibrating = span_x < 200.0f || span_y < 200.0f;

    if (!_calibrating && span_x > 0.0f && span_y > 0.0f) {
        float offset_x = (_min_x + _max_x) * 0.5f;
        float offset_y = (_min_y + _max_y) * 0.5f;
        float avg_span = (span_x + span_y) * 0.5f;
        x              = (x - offset_x) * (avg_span / span_x);
        y              = (y - offset_y) * (avg_span / span_y);
    }

    _heading = normalize_degrees(std::atan2(x, y) * 180.0f / PI_F + COMPASS_HEADING_OFFSET);
}

void AppCompass::render_page()
{
    auto& canvas = GetHAL().canvas;
    canvas.fillScreen(THEME_COLOR_BG);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextDatum(textdatum_t::top_left);

    canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    canvas.drawString("Compass GY-271 debug", 0, 0);

    render_bus_line(_bus_g8_g9, 10);
    render_bus_line(_bus_g2_g1, 20);

    canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas.drawString("G40/14: SPI SCK/MOSI", 0, 30);

    canvas.setTextColor(_detected ? (uint32_t)0x60FFF5 : TFT_ORANGE, THEME_COLOR_BG);
    _str_buffer = fmt::format("CHIP:{} BUS:{} ADDR:{:02X}", chip_name(), _detected ? _active_bus : "--", _address);
    canvas.drawString(_str_buffer.c_str(), 0, 40);

    canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    if (_chip == CompassChip::QMC5883L) {
        _str_buffer = fmt::format("ST:{:02X} RDY:{} OVL:{} DOR:{}", _status, (_status & 0x01) ? 1 : 0,
                                  (_status & 0x02) ? 1 : 0, (_status & 0x04) ? 1 : 0);
    } else if (_chip == CompassChip::HMC5883L) {
        _str_buffer = fmt::format("ST:{:02X} RDY:{} LOCK:{}", _status, (_status & 0x01) ? 1 : 0,
                                  (_status & 0x02) ? 1 : 0);
    } else {
        _str_buffer = "ST:-- RDY:-";
    }
    canvas.drawString(_str_buffer.c_str(), 0, 50);

    _str_buffer = fmt::format("RAW X:{:6d} Y:{:6d}", _raw_x, _raw_y);
    canvas.drawString(_str_buffer.c_str(), 0, 60);
    _str_buffer = fmt::format("RAW Z:{:6d}", _raw_z);
    if (_last_sample_time > 0) {
        _str_buffer += fmt::format(" AGE:{}ms", GetHAL().millis() - _last_sample_time);
    } else {
        _str_buffer += " AGE:--";
    }
    canvas.drawString(_str_buffer.c_str(), 0, 70);

    _str_buffer = fmt::format("HDG:{:6.1f} CAL:{}", _heading, _calibrating ? "Y" : "-");
    canvas.drawString(_str_buffer.c_str(), 0, 80);
    _str_buffer = fmt::format("SPAN X:{:5.0f} Y:{:5.0f}", _max_x - _min_x, _max_y - _min_y);
    canvas.drawString(_str_buffer.c_str(), 0, 90);
    _str_buffer = fmt::format("CNT:{} ERR:{}", _read_count, _fail_count);
    canvas.drawString(_str_buffer.c_str(), 0, 100);

    GetHAL().pushCanvas();
    _last_render_time = GetHAL().millis();
    _needs_render     = false;
}

void AppCompass::render_bus_line(const BusStatus& bus, int y)
{
    auto& canvas = GetHAL().canvas;
    canvas.setTextColor(bus.ack_qmc_0d || bus.ack_qmc_0c || bus.ack_hmc_1e ? (uint32_t)0x8FC8AA : TFT_LIGHTGREY,
                        THEME_COLOR_BG);
    if (!bus.enabled) {
        _str_buffer = fmt::format("{} off", bus.name);
    } else {
        _str_buffer = fmt::format("{} SDA{} SCL{} D:{} C:{} H:{}", bus.name, bus.sda, bus.scl, yn(bus.ack_qmc_0d),
                                  yn(bus.ack_qmc_0c), yn(bus.ack_hmc_1e));
    }
    canvas.drawString(_str_buffer.c_str(), 0, y);
}

const char* AppCompass::chip_name() const
{
    switch (_chip) {
        case CompassChip::QMC5883L:
            return "QMC";
        case CompassChip::HMC5883L:
            return "HMC";
        default:
            return "--";
    }
}

float AppCompass::normalize_degrees(float degrees) const
{
    while (degrees < 0.0f) {
        degrees += 360.0f;
    }
    while (degrees >= 360.0f) {
        degrees -= 360.0f;
    }
    return degrees;
}
