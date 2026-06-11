/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_imu.h"
#include "assets/imu_big.h"
#include "assets/imu_small.h"
#include "assets/imu_panel.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <assets.h>
#include <hal.h>
#include <algorithm>
#include <cmath>

using namespace mooncake;

namespace {
constexpr uint8_t QMC5883L_ADDR          = 0x0D;
constexpr uint8_t QMC5883L_REG_DATA      = 0x00;
constexpr uint8_t QMC5883L_REG_STATUS    = 0x06;
constexpr uint8_t QMC5883L_REG_CTRL1     = 0x09;
constexpr uint8_t QMC5883L_REG_CTRL2     = 0x0A;
constexpr uint8_t QMC5883L_REG_SET_RESET = 0x0B;
constexpr uint8_t HMC5883L_ADDR          = 0x1E;
constexpr uint8_t HMC5883L_REG_CONFIG_A  = 0x00;
constexpr uint8_t HMC5883L_REG_CONFIG_B  = 0x01;
constexpr uint8_t HMC5883L_REG_MODE      = 0x02;
constexpr uint8_t HMC5883L_REG_DATA      = 0x03;
constexpr uint8_t HMC5883L_REG_STATUS    = 0x09;
constexpr uint32_t COMPASS_I2C_FREQ      = 100000;
constexpr uint32_t COMPASS_PROBE_MS      = 2000;
constexpr float COMPASS_HEADING_OFFSET   = 0.0f;
constexpr bool COMPASS_SWAP_XY           = false;
constexpr int COMPASS_AXIS_X_SIGN        = 1;
constexpr int COMPASS_AXIS_Y_SIGN        = 1;
constexpr float PI_F                     = 3.14159265358979323846f;
constexpr uint8_t QMC5883L_ADDR_LIST[]   = {0x0D, 0x0C};
constexpr uint8_t HMC5883L_ADDR_LIST[]   = {0x1E};
constexpr uint32_t IMU_RENDER_INTERVAL_MS = 80;

int16_t read_i16_le(const uint8_t* data)
{
    return static_cast<int16_t>(static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8));
}

int16_t read_i16_be(const uint8_t* data)
{
    return static_cast<int16_t>((static_cast<uint16_t>(data[0]) << 8) | static_cast<uint16_t>(data[1]));
}
}  // namespace

AppImu::AppImu()
{
    setAppInfo().name     = "IMU";
    setAppInfo().userData = new AppIcon_t(image_data_imu_big, image_data_imu_small);
}

AppImu::~AppImu()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppImu::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    // Clear screen
    GetHAL().canvas.setBaseColor(THEME_COLOR_BG);
    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextSize(1);

    GetHAL().ensureCardputerAdvI2C();
    GetHAL().imu.begin();
    update_i2c_scan_status();
    _compass_last_probe_time = GetHAL().millis();
    _has_rendered            = false;
    render_page();
}

void AppImu::onRunning()
{
    auto imu_update = GetHAL().imu.update();
    if (imu_update) {
        // Obtain data on the current value of the IMU.
        _imu_data = GetHAL().imu.getImuData();

        // mclog::tagInfo(getAppInfo().name, "{:.1f} {:.1f} {:.1f}, {:.1f} {:.1f} {:.1f}", _imu_data.accel.x,
        //                _imu_data.accel.y, _imu_data.accel.z, _imu_data.gyro.x, _imu_data.gyro.y, _imu_data.gyro.z);

        update_panel_angle();
    }
    bool compass_update = update_compass();

    if (imu_update || compass_update || !_has_rendered || GetHAL().millis() - _last_render_time >= IMU_RENDER_INTERVAL_MS) {
        render_page();
    }

    // Close app when home button clicked
    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppImu::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
}

void AppImu::render_page()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    render_imu_data_label();
    render_imu_panel();
    GetHAL().pushCanvas();

    _last_render_time = GetHAL().millis();
    _has_rendered     = true;
}

void AppImu::render_imu_data_label()
{
    GetHAL().canvas.setTextColor((uint32_t)0x8FC8AA);
    _str_buffer = fmt::format("AX: {: .1f}", _imu_data.accel.x);
    GetHAL().canvas.drawString(_str_buffer.c_str(), 6, +2);
    _str_buffer = fmt::format("AY: {: .1f}", _imu_data.accel.y);
    GetHAL().canvas.drawString(_str_buffer.c_str(), 6, 16 + 2);
    _str_buffer = fmt::format("AZ: {: .1f}", _imu_data.accel.z);
    GetHAL().canvas.drawString(_str_buffer.c_str(), 6, 32 + 2);

    GetHAL().canvas.setTextColor((uint32_t)0x88AED9);
    _str_buffer = fmt::format("GX: {: .1f}", _imu_data.gyro.x);
    GetHAL().canvas.drawString(_str_buffer.c_str(), 6, 56 - 2);
    _str_buffer = fmt::format("GY: {: .1f}", _imu_data.gyro.y);
    GetHAL().canvas.drawString(_str_buffer.c_str(), 6, 72 - 2);
    _str_buffer = fmt::format("GZ: {: .1f}", _imu_data.gyro.z);
    GetHAL().canvas.drawString(_str_buffer.c_str(), 6, 88 - 2);

    _str_buffer.clear();
}

void AppImu::render_imu_panel()
{
    const int panel_center_x = 141;
    const int panel_center_y = 53;

    // Panel
    int w = 100;
    int h = 100;
    GetHAL().canvas.pushImageRotateZoomWithAA(panel_center_x, panel_center_y, w / 2, h / 2, _panel_angle, 1.0f, 1.0f, w,
                                              h, image_data_imu_panel);

    // Tilt ball
    int ball_offset_x = std::clamp((int)(_imu_data.accel.x * 15), -15, 15);
    int ball_offset_y = std::clamp((int)(_imu_data.accel.y * 15), -15, 15);
    ball_offset_x     = -ball_offset_x;
    // Ball
    GetHAL().canvas.fillSmoothCircle(panel_center_x + ball_offset_x, panel_center_y + ball_offset_y, 16,
                                     (uint32_t)0x778595);
    // Ball cross mark
    GetHAL().canvas.fillRect(panel_center_x - 7 / 2 + ball_offset_x, panel_center_y + ball_offset_y, 7, 1, TFT_WHITE);
    GetHAL().canvas.fillRect(panel_center_x + ball_offset_x, panel_center_y - 7 / 2 + ball_offset_y, 1, 7, TFT_WHITE);

    // Cross mark
    GetHAL().canvas.fillRect(panel_center_x, panel_center_y - 52 / 2, 1, 52, TFT_WHITE);
    GetHAL().canvas.fillRect(panel_center_x - 52 / 2, panel_center_y, 52, 1, TFT_WHITE);

    render_compass_overlay(panel_center_x, panel_center_y);
}

void AppImu::render_compass_overlay(int center_x, int center_y)
{
    static constexpr int ring_radius = 49;

    if (!_compass_data.available) {
        GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        GetHAL().canvas.drawString("MAG --", 96, 88);
        if (!_i2c_scan_status.empty()) {
            GetHAL().canvas.drawString(_i2c_scan_status.c_str(), 96, 100);
        }
        return;
    }

    uint32_t compass_color = _compass_data.has_sample ? (uint32_t)0x60FFF5 : TFT_DARKGREY;
    GetHAL().canvas.drawCircle(center_x, center_y, ring_radius, compass_color);

    if (!_compass_data.has_sample) {
        GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        GetHAL().canvas.drawString("MAG WAIT", 96, 96);
        return;
    }

    float angle_rad = _compass_data.heading * PI_F / 180.0f;
    float sin_a     = std::sin(angle_rad);
    float cos_a     = std::cos(angle_rad);

    int tip_x  = center_x + static_cast<int>(sin_a * ring_radius);
    int tip_y  = center_y - static_cast<int>(cos_a * ring_radius);
    int tail_x = center_x + static_cast<int>(sin_a * 24);
    int tail_y = center_y - static_cast<int>(cos_a * 24);

    float side_angle = angle_rad + PI_F * 0.5f;
    int side_dx      = static_cast<int>(std::sin(side_angle) * 5);
    int side_dy      = -static_cast<int>(std::cos(side_angle) * 5);
    int base_x       = center_x + static_cast<int>(sin_a * (ring_radius - 12));
    int base_y       = center_y - static_cast<int>(cos_a * (ring_radius - 12));

    GetHAL().canvas.drawLine(tail_x, tail_y, tip_x, tip_y, compass_color);
    GetHAL().canvas.fillTriangle(tip_x, tip_y, base_x + side_dx, base_y + side_dy, base_x - side_dx, base_y - side_dy,
                                 compass_color);

    GetHAL().canvas.setTextColor(compass_color, THEME_COLOR_BG);
    GetHAL().canvas.drawString("N", tip_x - 3, tip_y - 3);

    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    _str_buffer = fmt::format("MAG {:03d}{}", static_cast<int>(_compass_data.heading + 0.5f) % 360,
                              _compass_data.calibrating ? " CAL" : "");
    GetHAL().canvas.drawString(_str_buffer.c_str(), 96, 88);
    _str_buffer = fmt::format("{}:{:02X}", _compass_data.bus_name, _compass_data.address);
    GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    GetHAL().canvas.drawString(_str_buffer.c_str(), 96, 100);
    _str_buffer.clear();
}

static void _calculate_attitude_yaw(float gyroZ, float deltaTime, float& yaw)
{
    yaw += gyroZ * deltaTime;
}

void AppImu::update_panel_angle()
{
    static uint32_t time_count = GetHAL().millis();
    static float yaw           = 0.0f;

    if (GetHAL().millis() - time_count > 200) {
        time_count = GetHAL().millis();
        yaw        = 0.0f;
        return;
    }

    _calculate_attitude_yaw(_imu_data.gyro.z, GetHAL().millis() - time_count, yaw);
    // mclog::tagInfo(getAppInfo().name, "get yaw: {}", yaw);

    _panel_angle = yaw / 1000;

    time_count = GetHAL().millis();
}

bool AppImu::update_compass()
{
    if (!_compass_data.available) {
        if (GetHAL().millis() - _compass_last_probe_time >= COMPASS_PROBE_MS) {
            return compass_init();
        }
        return false;
    }

    bool was_available = _compass_data.available;
    bool had_sample    = _compass_data.has_sample;
    bool read_ok       = compass_read();
    return read_ok || was_available != _compass_data.available || had_sample != _compass_data.has_sample;
}

bool AppImu::compass_init()
{
    _compass_last_probe_time = GetHAL().millis();
    GetHAL().ensureCardputerAdvI2C();
    update_i2c_scan_status();

    if (compass_init_on_bus(M5.In_I2C, "G8/9")) {
        return true;
    }

    if (compass_init_on_bus(M5.Ex_I2C, "G1/2")) {
        return true;
    }

    _compass_data = CompassData();
    _compass_i2c  = nullptr;
    return false;
}

bool AppImu::compass_init_on_bus(m5::I2C_Class& i2c, const char* bus_name)
{
    if (!i2c.isEnabled()) {
        return false;
    }

    for (uint8_t address : QMC5883L_ADDR_LIST) {
        if (compass_init_qmc5883l(i2c, address)) {
            _compass_i2c               = &i2c;
            _compass_data              = CompassData();
            _compass_data.available    = true;
            _compass_data.chip         = CompassChip::QMC5883L;
            _compass_data.address      = address;
            _compass_data.bus_name     = bus_name;
            mclog::tagInfo(getAppInfo().name, "QMC5883L compass detected on {} I2C addr 0x{:02X}", bus_name, address);
            return true;
        }
    }

    for (uint8_t address : HMC5883L_ADDR_LIST) {
        if (compass_init_hmc5883l(i2c, address)) {
            _compass_i2c               = &i2c;
            _compass_data              = CompassData();
            _compass_data.available    = true;
            _compass_data.chip         = CompassChip::HMC5883L;
            _compass_data.address      = address;
            _compass_data.bus_name     = bus_name;
            mclog::tagInfo(getAppInfo().name, "HMC5883L compass detected on {} I2C addr 0x{:02X}", bus_name, address);
            return true;
        }
    }

    return false;
}

bool AppImu::compass_init_qmc5883l(m5::I2C_Class& i2c, uint8_t address)
{
    if (!i2c.scanID(address, COMPASS_I2C_FREQ)) {
        return false;
    }

    i2c.writeRegister8(address, QMC5883L_REG_CTRL2, 0x80, COMPASS_I2C_FREQ);
    GetHAL().delay(10);
    if (!i2c.writeRegister8(address, QMC5883L_REG_SET_RESET, 0x01, COMPASS_I2C_FREQ)) {
        return false;
    }
    return i2c.writeRegister8(address, QMC5883L_REG_CTRL1, 0x1D, COMPASS_I2C_FREQ);
}

bool AppImu::compass_init_hmc5883l(m5::I2C_Class& i2c, uint8_t address)
{
    if (!i2c.scanID(address, COMPASS_I2C_FREQ)) {
        return false;
    }

    bool ok = i2c.writeRegister8(address, HMC5883L_REG_CONFIG_A, 0x70, COMPASS_I2C_FREQ);
    ok      = i2c.writeRegister8(address, HMC5883L_REG_CONFIG_B, 0x20, COMPASS_I2C_FREQ) && ok;
    ok      = i2c.writeRegister8(address, HMC5883L_REG_MODE, 0x00, COMPASS_I2C_FREQ) && ok;
    GetHAL().delay(10);
    return ok;
}

bool AppImu::compass_read()
{
    switch (_compass_data.chip) {
        case CompassChip::QMC5883L:
            return compass_read_qmc5883l();
        case CompassChip::HMC5883L:
            return compass_read_hmc5883l();
        default:
            return false;
    }
}

bool AppImu::compass_read_qmc5883l()
{
    if (!_compass_i2c) {
        return false;
    }

    uint8_t status = 0;
    if (!_compass_i2c->readRegister(_compass_data.address, QMC5883L_REG_STATUS, &status, 1, COMPASS_I2C_FREQ)) {
        _compass_data.available = false;
        return false;
    }
    if ((status & 0x04) || !(status & 0x01)) {
        return false;
    }

    uint8_t data[6] = {};
    if (!_compass_i2c->readRegister(_compass_data.address, QMC5883L_REG_DATA, data, sizeof(data), COMPASS_I2C_FREQ)) {
        _compass_data.available = false;
        return false;
    }

    _compass_data.raw_x = read_i16_le(&data[0]);
    _compass_data.raw_y = read_i16_le(&data[2]);
    _compass_data.raw_z = read_i16_le(&data[4]);
    update_compass_heading();
    return true;
}

bool AppImu::compass_read_hmc5883l()
{
    if (!_compass_i2c) {
        return false;
    }

    uint8_t status = 0;
    if (!_compass_i2c->readRegister(_compass_data.address, HMC5883L_REG_STATUS, &status, 1, COMPASS_I2C_FREQ)) {
        _compass_data.available = false;
        return false;
    }
    if (!(status & 0x01)) {
        return false;
    }

    uint8_t data[6] = {};
    if (!_compass_i2c->readRegister(_compass_data.address, HMC5883L_REG_DATA, data, sizeof(data), COMPASS_I2C_FREQ)) {
        _compass_data.available = false;
        return false;
    }

    _compass_data.raw_x = read_i16_be(&data[0]);
    _compass_data.raw_z = read_i16_be(&data[2]);
    _compass_data.raw_y = read_i16_be(&data[4]);
    update_compass_heading();
    return true;
}

void AppImu::update_compass_heading()
{
    float sensor_x = static_cast<float>(_compass_data.raw_x);
    float sensor_y = static_cast<float>(_compass_data.raw_y);
    float x        = (COMPASS_SWAP_XY ? sensor_y : sensor_x) * COMPASS_AXIS_X_SIGN;
    float y        = (COMPASS_SWAP_XY ? sensor_x : sensor_y) * COMPASS_AXIS_Y_SIGN;
    float z = static_cast<float>(_compass_data.raw_z);

    if (!_compass_data.has_sample) {
        _compass_data.min_x     = x;
        _compass_data.max_x     = x;
        _compass_data.min_y     = y;
        _compass_data.max_y     = y;
        _compass_data.has_sample = true;
    } else {
        _compass_data.min_x = std::min(_compass_data.min_x, x);
        _compass_data.max_x = std::max(_compass_data.max_x, x);
        _compass_data.min_y = std::min(_compass_data.min_y, y);
        _compass_data.max_y = std::max(_compass_data.max_y, y);
    }

    float span_x = _compass_data.max_x - _compass_data.min_x;
    float span_y = _compass_data.max_y - _compass_data.min_y;
    _compass_data.calibrating = span_x < 200.0f || span_y < 200.0f;

    if (!_compass_data.calibrating) {
        float offset_x = (_compass_data.min_x + _compass_data.max_x) * 0.5f;
        float offset_y = (_compass_data.min_y + _compass_data.max_y) * 0.5f;
        float avg_span = (span_x + span_y) * 0.5f;
        x              = (x - offset_x) * (avg_span / span_x);
        y              = (y - offset_y) * (avg_span / span_y);
    }

    _compass_data.x       = x;
    _compass_data.y       = y;
    _compass_data.z       = z;
    _compass_data.heading = normalize_degrees(std::atan2(x, y) * 180.0f / PI_F + COMPASS_HEADING_OFFSET);
}

void AppImu::update_i2c_scan_status()
{
    _i2c_scan_status.clear();
    append_i2c_scan_status(M5.In_I2C, "G8/9");
    _i2c_scan_status += " ";
    append_i2c_scan_status(M5.Ex_I2C, "G1/2");
}

void AppImu::append_i2c_scan_status(m5::I2C_Class& i2c, const char* bus_name)
{
    _i2c_scan_status += bus_name;
    _i2c_scan_status += ":";

    if (!i2c.isEnabled()) {
        _i2c_scan_status += "off";
        return;
    }

    bool found_any = false;
    int shown      = 0;
    auto append_address = [&](uint8_t address) {
        if (!i2c.scanID(address, COMPASS_I2C_FREQ)) {
            return;
        }

        if (shown > 0) {
            _i2c_scan_status += ",";
        }
        _i2c_scan_status += fmt::format("{:02X}", address);
        shown++;
        found_any = true;
    };

    for (uint8_t address : QMC5883L_ADDR_LIST) {
        append_address(address);
    }
    for (uint8_t address : HMC5883L_ADDR_LIST) {
        bool already_checked = false;
        for (uint8_t qmc_address : QMC5883L_ADDR_LIST) {
            if (qmc_address == address) {
                already_checked = true;
                break;
            }
        }
        if (already_checked) {
            continue;
        }
        append_address(address);
    }

    if (!found_any) {
        _i2c_scan_status += "--";
    }
}

float AppImu::normalize_degrees(float degrees) const
{
    while (degrees < 0.0f) {
        degrees += 360.0f;
    }
    while (degrees >= 360.0f) {
        degrees -= 360.0f;
    }
    return degrees;
}
