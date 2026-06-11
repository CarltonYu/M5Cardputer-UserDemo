/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <hal/hal.h>

/**
 * @brief
 *
 */
class AppImu : public mooncake::AppAbility {
public:
    AppImu();
    ~AppImu();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class CompassChip {
        NONE,
        QMC5883L,
        HMC5883L,
    };

    struct CompassData {
        bool available   = false;
        bool has_sample  = false;
        bool calibrating = false;
        CompassChip chip = CompassChip::NONE;
        int16_t raw_x    = 0;
        int16_t raw_y    = 0;
        int16_t raw_z    = 0;
        float x          = 0.0f;
        float y          = 0.0f;
        float z          = 0.0f;
        float min_x      = 0.0f;
        float max_x      = 0.0f;
        float min_y      = 0.0f;
        float max_y      = 0.0f;
        float heading    = 0.0f;
        uint8_t address  = 0;
        const char* bus_name = "";
    };

    m5::imu_data_t _imu_data;
    CompassData _compass_data;
    m5::I2C_Class* _compass_i2c = nullptr;
    std::string _str_buffer;
    std::string _i2c_scan_status;
    float _panel_angle = 0.0f;
    uint32_t _compass_last_probe_time = 0;
    uint32_t _last_render_time        = 0;
    bool _has_rendered                = false;

    void render_page();
    void render_imu_data_label();
    void render_imu_panel();
    void render_compass_overlay(int center_x, int center_y);
    void update_panel_angle();
    bool update_compass();
    bool compass_init();
    bool compass_init_on_bus(m5::I2C_Class& i2c, const char* bus_name);
    bool compass_init_qmc5883l(m5::I2C_Class& i2c, uint8_t address);
    bool compass_init_hmc5883l(m5::I2C_Class& i2c, uint8_t address);
    bool compass_read();
    bool compass_read_qmc5883l();
    bool compass_read_hmc5883l();
    void update_compass_heading();
    void update_i2c_scan_status();
    void append_i2c_scan_status(m5::I2C_Class& i2c, const char* bus_name);
    float normalize_degrees(float degrees) const;
};
