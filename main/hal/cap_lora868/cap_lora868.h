/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "TinyGPSPlus/TinyGPS++.h"
#include <mooncake_log_signal.h>
#include <M5Unified.h>
#include <string>

class CapLoRa868 {
public:
    bool init();
    void update();

    /* ---------------------------------- LoRa ---------------------------------- */
    struct lora_config {
        static constexpr float ferq              = 868.0f;
        static constexpr float bw                = 500.0f;
        static constexpr uint8_t sf              = 7;
        static constexpr uint8_t cr              = 5;
        static constexpr uint8_t syncWord        = 0x34;
        static constexpr int8_t power            = 10;
        static constexpr int8_t minPower         = -9;
        static constexpr int8_t maxPower         = 22;
        static constexpr uint16_t preambleLength = 10;
    };

    bool loraSendMsg(const std::string& msg);
    bool loraSendBytes(const uint8_t* data, size_t len);
    bool setTxPower(int8_t power);
    int8_t getTxPower() const
    {
        return _tx_power;
    }
    bool isTxDone() const;
    mclog::Signal<const std::string&> onLoraMsg;

    /* ----------------------------------- GPS ---------------------------------- */
    TinyGPSPlus* borrowGPS();
    void returnGPS();

private:
    bool _is_inited              = false;
    bool _is_lora_inited         = false;
    bool _is_gps_inited          = false;
    volatile bool _is_tx_pending = false;
    int8_t _tx_power             = lora_config::power;

    bool lora_init();
    void lora_update();
    bool gps_init();
};
