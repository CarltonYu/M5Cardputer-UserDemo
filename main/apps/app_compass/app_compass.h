/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <M5Unified.hpp>
#include <mooncake.h>
#include <cstdint>
#include <string>

class AppCompass : public mooncake::AppAbility {
public:
    AppCompass();
    ~AppCompass();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class CompassChip {
        NONE,
        QMC5883L,
        HMC5883L,
    };

    struct BusStatus {
        const char* name      = "";
        m5::I2C_Class* i2c   = nullptr;
        bool enabled         = false;
        bool ack_qmc_0d      = false;
        bool ack_qmc_0c      = false;
        bool ack_hmc_1e      = false;
        int sda              = -1;
        int scl              = -1;
    };

    BusStatus _bus_g8_g9;
    BusStatus _bus_g2_g1;
    m5::I2C_Class* _active_i2c = nullptr;
    std::string _str_buffer;
    CompassChip _chip          = CompassChip::NONE;
    const char* _active_bus    = "";
    uint8_t _address           = 0;
    uint8_t _status            = 0;
    uint32_t _last_probe_time  = 0;
    uint32_t _last_read_time   = 0;
    uint32_t _last_render_time = 0;
    uint32_t _last_sample_time = 0;
    uint32_t _read_count       = 0;
    uint32_t _fail_count       = 0;
    int16_t _raw_x             = 0;
    int16_t _raw_y             = 0;
    int16_t _raw_z             = 0;
    float _heading             = 0.0f;
    float _min_x               = 0.0f;
    float _max_x               = 0.0f;
    float _min_y               = 0.0f;
    float _max_y               = 0.0f;
    bool _detected             = false;
    bool _has_sample           = false;
    bool _calibrating          = false;
    bool _needs_render         = true;

    void setup_i2c_buses();
    void update_bus_status();
    void update_one_bus_status(BusStatus& bus);
    bool probe_compass();
    bool probe_bus(BusStatus& bus);
    bool init_qmc5883l(BusStatus& bus, uint8_t address);
    bool init_hmc5883l(BusStatus& bus, uint8_t address);
    void set_detected(BusStatus& bus, CompassChip chip, uint8_t address);
    void clear_detected();
    bool read_compass();
    bool read_qmc5883l();
    bool read_hmc5883l();
    void update_heading();
    void render_page();
    void render_bus_line(const BusStatus& bus, int y);
    const char* chip_name() const;
    float normalize_degrees(float degrees) const;
};
