/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "cap_lora868.h"
#include "../hal_config.h"
#include "uart/uart_helper.h"
#include "radio_lib/EspHal.h"
#include <driver/sdspi_host.h>
#include <mooncake_log.h>
#include <RadioLib.h>
#include <memory>
#include <mutex>

static const std::string _tag = "Cap-LoRa868";

bool CapLoRa868::init()
{
    mclog::tagInfo(_tag, "init");

    if (_is_inited) {
        return true;
    }

    // Start GPS first so the GNSS UART can settle while the SX1262 is being reset/configured.
    if (!_is_gps_inited && !gps_init()) {
        return false;
    }

    if (!_is_lora_inited && !lora_init()) {
        return false;
    }

    _is_inited = _is_gps_inited && _is_lora_inited;
    return _is_inited;
}

void CapLoRa868::update()
{
    if (!_is_lora_inited) {
        return;
    }

    lora_update();
}

/* -------------------------------------------------------------------------- */
/*                                    LoRa                                    */
/* -------------------------------------------------------------------------- */
// https://github.com/jgromes/RadioLib/blob/master/examples/NonArduino/ESP-IDF
// https://github.com/jgromes/RadioLib/blob/master/examples/SX126x

struct RadioLibData_t {
    std::unique_ptr<EspHal> hal;
    std::unique_ptr<Module> module;
    std::unique_ptr<SX1262> sx1262;

    void reset()
    {
        this->hal.reset();
        this->module.reset();
        this->sx1262.reset();
    }
};

static RadioLibData_t _radio_lib;
static volatile bool _lora_rx_flag = false;
static volatile bool _lora_tx_flag = false;

IRAM_ATTR void _lora_set_rx_flag(void)
{
    _lora_rx_flag = true;
}

IRAM_ATTR void _lora_set_tx_flag(void)
{
    _lora_tx_flag = true;
}

bool CapLoRa868::lora_init()
{
    mclog::tagInfo(_tag, "lora init");

    if (_is_lora_inited) {
        return true;
    }

    // Create hal
    spi_host_device_t spi_host = SDSPI_DEFAULT_HOST;
    _radio_lib.hal = std::make_unique<EspHal>(HAL_PIN_SPI_SCLK, HAL_PIN_SPI_MISO, HAL_PIN_SPI_MOSI, spi_host, true);

    // Create module
    _radio_lib.module = std::make_unique<Module>(_radio_lib.hal.get(), HAL_PIN_LORA_NSS_GPIO, HAL_PIN_LORA_DIO1_GPIO,
                                                 HAL_PIN_LORA_RST_GPIO, HAL_PIN_LORA_BUSY_GPIO);

    // Create sx1262
    _radio_lib.sx1262 = std::make_unique<SX1262>(_radio_lib.module.get());
    auto state =
        _radio_lib.sx1262->begin(lora_config::ferq, lora_config::bw, lora_config::sf, lora_config::cr,
                                 lora_config::syncWord, lora_config::power, lora_config::preambleLength, 3.0, false);
    if (state == RADIOLIB_ERR_NONE) {
        mclog::tagInfo(_tag, "sx1262 init success");
        _tx_power = lora_config::power;
    } else {
        mclog::tagError(_tag, "sx1262 init failed, code {}", state);
        goto HANDLE_ERROR;
    }

    // Setup
    _radio_lib.sx1262->setDio2AsRfSwitch(true);
    _radio_lib.sx1262->setCurrentLimit(140);

    // Start receiving
    _radio_lib.sx1262->setPacketReceivedAction(_lora_set_rx_flag);
    state = _radio_lib.sx1262->startReceive();
    if (state == RADIOLIB_ERR_NONE) {
        mclog::tagInfo(_tag, "sx1262 start receive success");
    } else {
        mclog::tagError(_tag, "sx1262 start receive failed, code {}", state);
        goto HANDLE_ERROR;
    }

    _is_lora_inited = true;
    return true;

HANDLE_ERROR:
    _is_lora_inited = false;
    _radio_lib.reset();
    return false;
}

void CapLoRa868::lora_update()
{
    // mclog::tagDebug(_tag, "{} {}", _lora_rx_flag, _lora_tx_flag);

    if (_lora_rx_flag) {
        _lora_rx_flag = false;

        size_t len = _radio_lib.sx1262->getPacketLength();
        std::string msg;
        msg.resize(len);
        int state = _radio_lib.sx1262->readData(reinterpret_cast<uint8_t*>(&msg[0]), len);
        if (state == RADIOLIB_ERR_NONE) {
            mclog::tagDebug(_tag, "lora receive msg: {} | len: {} | RSSI: {}dBm | SNR: {}dB", msg, len,
                            _radio_lib.sx1262->getRSSI(), _radio_lib.sx1262->getSNR());
        } else {
            mclog::tagError(_tag, "lora read data failed, code {}", state);
        }

        onLoraMsg.emit(msg);
    }

    if (_lora_tx_flag) {
        _lora_tx_flag  = false;
        _is_tx_pending = false;

        mclog::tagDebug(_tag, "lora send msg success");
        _radio_lib.sx1262->setPacketReceivedAction(_lora_set_rx_flag);
        _radio_lib.sx1262->startReceive();
    }
}

bool CapLoRa868::loraSendMsg(const std::string& msg)
{
    if (!_is_lora_inited || _is_tx_pending) {
        return false;
    }

    mclog::tagDebug(_tag, "lora send msg: {}", msg);

    _radio_lib.sx1262->setPacketSentAction(_lora_set_tx_flag);
    int state = _radio_lib.sx1262->startTransmit(msg.c_str());
    if (state != RADIOLIB_ERR_NONE) {
        mclog::tagError(_tag, "lora send msg failed, code {}", state);
        return false;
    }
    _is_tx_pending = true;
    return true;
}

bool CapLoRa868::setTxPower(int8_t power)
{
    if (!_is_lora_inited || !_radio_lib.sx1262 || _is_tx_pending) {
        return false;
    }

    if (power < lora_config::minPower) {
        power = lora_config::minPower;
    } else if (power > lora_config::maxPower) {
        power = lora_config::maxPower;
    }

    int state = _radio_lib.sx1262->setOutputPower(power);
    if (state != RADIOLIB_ERR_NONE) {
        mclog::tagError(_tag, "set tx power failed, power {}dBm, code {}", power, state);
        return false;
    }

    _tx_power = power;
    mclog::tagInfo(_tag, "tx power set to {}dBm", _tx_power);
    return true;
}

bool CapLoRa868::isTxDone() const
{
    return !_is_tx_pending;
}

bool CapLoRa868::loraSendBytes(const uint8_t* data, size_t len)
{
    if (!_is_lora_inited || _is_tx_pending) {
        return false;
    }

    mclog::tagDebug(_tag, "lora send bytes: len {}", len);

    _radio_lib.sx1262->setPacketSentAction(_lora_set_tx_flag);
    int state = _radio_lib.sx1262->startTransmit(data, len);
    if (state != RADIOLIB_ERR_NONE) {
        mclog::tagError(_tag, "lora send bytes failed, code {}", state);
        return false;
    }
    _is_tx_pending = true;
    return true;
}

/* -------------------------------------------------------------------------- */
/*                                     GPS                                    */
/* -------------------------------------------------------------------------- */
// https://github.com/mikalhart/TinyGPSPlus/blob/master/examples/BasicExample

static std::unique_ptr<TinyGPSPlus> _gps;
static std::mutex _gps_mutex;

void _handle_gps_msg(const char* msg)
{
    // mclog::tagDebug(_tag, "on gps msg:\n{}", msg);

    std::lock_guard<std::mutex> lock(_gps_mutex);
    if (!_gps) {
        return;
    }

    const char* p = msg;
    while (*p) {
        if (_gps->encode(*p++)) {
            // mclog::tagDebug(_tag, "gps parse ready");
        }
    }
}

bool CapLoRa868::gps_init()
{
    mclog::tagInfo(_tag, "gps init");

    if (_is_gps_inited) {
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(_gps_mutex);
        if (!_gps) {
            _gps = std::make_unique<TinyGPSPlus>();
        }
    }
    gps_uart_helper_set_on_msg_callback(_handle_gps_msg);
    gps_uart_helper_init();

    _is_gps_inited = true;
    return true;
}

TinyGPSPlus* CapLoRa868::borrowGPS()
{
    if (!_gps) {
        mclog::tagError(_tag, "gps not initialized");
        return nullptr;
    }
    _gps_mutex.lock();
    return _gps.get();
}

void CapLoRa868::returnGPS()
{
    _gps_mutex.unlock();
}
