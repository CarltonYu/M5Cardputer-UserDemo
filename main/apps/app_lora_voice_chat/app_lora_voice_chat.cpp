/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_lora_voice_chat.h"
#include "assets/lora_voice_big.h"
#include "assets/lora_voice_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <cstring>

using namespace mooncake;

AppLoraVoiceChat::AppLoraVoiceChat()
{
    setAppInfo().name     = "LoRaVoice";
    setAppInfo().userData = new AppIcon_t(image_data_lora_voice_big, image_data_lora_voice_small);
}

AppLoraVoiceChat::~AppLoraVoiceChat()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
    if (_pcm_buffer) {
        delete[] _pcm_buffer;
    }
    if (_rx_buffer) {
        delete[] _rx_buffer;
    }
}

void AppLoraVoiceChat::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    audio::set_keyboard_sfx_enable(false);

    _pcm_buffer = new int16_t[RECORD_BUF_SIZE]();
    _rx_buffer  = new int8_t[RECORD_BUF_SIZE]();

    _is_cap_available = GetHAL().capLora868.init();
    if (!_is_cap_available) {
        GetHAL().canvas.fillScreen(THEME_COLOR_BG);
        GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(10, 10);
        GetHAL().canvas.print("LoRa cap init failed");
        GetHAL().pushCanvas();
        return;
    }

    initDeviceId();

    _lora_slot_id = GetHAL().capLora868.onLoraMsg.connect([this](const std::string& data) { onLoraData(data); });

    _key_slot_id =
        GetHAL().keyboard.onKeyEvent.connect([this](const Keyboard::KeyEvent_t& keyEvent) { onKeyEvent(keyEvent); });

    _state                   = State::IDLE;
    _is_standby              = false;
    _is_remote_busy          = false;
    _is_local_busy           = false;
    _tx_packet_inflight      = false;
    _tx_sending_free_control = false;
    _tx_current_seq          = 0;
    _tx_total_seq            = 0;
    _rx_pending              = false;
    _rx_voice_len            = 0;
    memset(_busy_owner, 0, sizeof(_busy_owner));
    resetActivityTimer();
    renderIdle();
}

void AppLoraVoiceChat::onRunning()
{
    if (!_is_cap_available) {
        if (GetHAL().homeButton.wasClicked()) {
            close();
        }
        return;
    }

    updateRemoteBusyTimeout();

    switch (_state) {
        case State::RECORDING: {
            if (GetHAL().mic.isEnabled()) {
                auto data     = &_pcm_buffer[_record_sample_count];
                size_t remain = RECORD_BUF_SIZE - _record_sample_count;
                size_t chunk  = (remain < 200) ? remain : 200;
                if (chunk > 0 && GetHAL().mic.record(data, chunk, SAMPLE_RATE)) {
                    _record_sample_count += chunk;
                    renderRecording();
                }
                if (_record_sample_count >= RECORD_BUF_SIZE) {
                    stopRecordingAndSend();
                }
            }
            break;
        }
        case State::SENDING: {
            if (_tx_packet_inflight) {
                if (GetHAL().capLora868.isTxDone()) {
                    _tx_packet_inflight = false;
                    if (_tx_sending_free_control) {
                        finishSending();
                    } else {
                        _tx_current_seq++;
                    }
                }
            } else if (GetHAL().capLora868.isTxDone()) {
                if (_tx_current_seq < _tx_total_seq) {
                    _tx_packet_inflight = sendNextPacket();
                    if (_tx_packet_inflight) {
                        renderSending();
                    } else if (sendControlPacket(CONTROL_FREE)) {
                        _tx_packet_inflight      = true;
                        _tx_sending_free_control = true;
                        renderSending();
                    } else {
                        finishSending();
                    }
                } else if (sendControlPacket(CONTROL_FREE)) {
                    _tx_packet_inflight      = true;
                    _tx_sending_free_control = true;
                    renderSending();
                } else {
                    finishSending();
                }
            }
            break;
        }
        case State::PLAYING: {
            if (!GetHAL().speaker.isPlaying()) {
                _state = State::IDLE;
                resetActivityTimer();
                tryPlayReceived();
                if (_state == State::IDLE) {
                    renderIdle();
                }
            }
            break;
        }
        default:
            break;
    }

    if (GetHAL().homeButton.wasClicked()) {
        close();
        return;
    }

    maybeEnterStandby();
}

void AppLoraVoiceChat::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    if (_is_local_busy) {
        sendControlPacket(CONTROL_FREE);
        _is_local_busy = false;
    }
    exitStandby(false);

    if (_key_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_slot_id);
        _key_slot_id = -1;
    }
    if (_lora_slot_id >= 0) {
        GetHAL().capLora868.onLoraMsg.disconnect(_lora_slot_id);
        _lora_slot_id = -1;
    }

    while (GetHAL().mic.isRecording()) {
        GetHAL().delay(1);
    }
    GetHAL().mic.end();

    while (GetHAL().speaker.isPlaying()) {
        GetHAL().delay(1);
    }
    GetHAL().speaker.begin();
    GetHAL().speaker.setVolume(255);

    if (_pcm_buffer) {
        delete[] _pcm_buffer;
        _pcm_buffer = nullptr;
    }
    if (_rx_buffer) {
        delete[] _rx_buffer;
        _rx_buffer = nullptr;
    }

    audio::set_keyboard_sfx_enable(true);
}

/* -------------------------------------------------------------------------- */
/*                              Session / Standby                             */
/* -------------------------------------------------------------------------- */

void AppLoraVoiceChat::initDeviceId()
{
    auto mac = GetHAL().getDeviceMac();
    if (mac.size() >= 6) {
        _device_id[0] = mac[3];
        _device_id[1] = mac[4];
        _device_id[2] = mac[5];
    }
}

void AppLoraVoiceChat::resetActivityTimer()
{
    _last_activity_time = GetHAL().millis();
}

void AppLoraVoiceChat::updateRemoteBusyTimeout()
{
    if (!_is_remote_busy) {
        return;
    }

    if (static_cast<int32_t>(GetHAL().millis() - _remote_busy_until) >= 0) {
        clearRemoteBusy();
    }
}

void AppLoraVoiceChat::enterStandby()
{
    if (_is_standby || _state != State::IDLE) {
        return;
    }

    mclog::tagInfo(getAppInfo().name, "enter standby");
    _is_standby = true;

    GetHAL().speaker.end();
    GetHAL().mic.end();
    GetHAL().display.powerSaveOn();
    GetHAL().display.sleep();
}

void AppLoraVoiceChat::exitStandby(bool render)
{
    if (!_is_standby) {
        return;
    }

    mclog::tagInfo(getAppInfo().name, "exit standby");
    _is_standby = false;

    GetHAL().display.powerSaveOff();
    GetHAL().display.wakeup();
    resetActivityTimer();

    if (render) {
        if (_state == State::PLAYING) {
            renderPlaying();
        } else if (_state == State::SENDING) {
            renderSending();
        } else if (_state == State::RECORDING) {
            renderRecording();
        } else {
            renderIdle();
        }
    }
}

void AppLoraVoiceChat::maybeEnterStandby()
{
    if (_is_standby || _state != State::IDLE) {
        return;
    }

    if (GetHAL().millis() - _last_activity_time >= STANDBY_TIMEOUT_MS) {
        enterStandby();
    }
}

bool AppLoraVoiceChat::isOwnDeviceId(const uint8_t* deviceId) const
{
    return memcmp(deviceId, _device_id, sizeof(_device_id)) == 0;
}

bool AppLoraVoiceChat::isBusyOwner(const uint8_t* deviceId) const
{
    return memcmp(deviceId, _busy_owner, sizeof(_busy_owner)) == 0;
}

void AppLoraVoiceChat::setRemoteBusy(const uint8_t* deviceId)
{
    if (isOwnDeviceId(deviceId) || _is_local_busy) {
        return;
    }

    bool changed = !_is_remote_busy || !isBusyOwner(deviceId);
    memcpy(_busy_owner, deviceId, sizeof(_busy_owner));
    _is_remote_busy    = true;
    _remote_busy_until = GetHAL().millis() + REMOTE_BUSY_TIMEOUT_MS;

    if (changed && !_is_standby && _state == State::IDLE) {
        renderIdle();
    }
}

void AppLoraVoiceChat::clearRemoteBusy(const uint8_t* deviceId)
{
    if (!_is_remote_busy) {
        return;
    }
    bool is_unknown_owner = _busy_owner[0] == 0xFF && _busy_owner[1] == 0xFF && _busy_owner[2] == 0xFF;
    if (deviceId && !isBusyOwner(deviceId) && !is_unknown_owner) {
        return;
    }

    _is_remote_busy = false;
    memset(_busy_owner, 0, sizeof(_busy_owner));

    if (!_is_standby && _state == State::IDLE) {
        renderIdle();
    }
}

bool AppLoraVoiceChat::sendControlPacket(uint8_t type)
{
    uint8_t packet[CONTROL_PACKET_SIZE] = {0xAB, 0xCD, 'L', 'V', _device_id[0], _device_id[1], _device_id[2], type};
    return GetHAL().capLora868.loraSendBytes(packet, sizeof(packet));
}

bool AppLoraVoiceChat::isControlPacket(const uint8_t* bytes, size_t len) const
{
    return len == CONTROL_PACKET_SIZE && bytes[0] == 0xAB && bytes[1] == 0xCD && bytes[2] == 'L' && bytes[3] == 'V';
}

void AppLoraVoiceChat::handleControlPacket(const uint8_t* bytes, size_t len)
{
    if (!isControlPacket(bytes, len)) {
        return;
    }

    const uint8_t* sender = &bytes[4];
    uint8_t type          = bytes[7];
    if (type == CONTROL_BUSY) {
        setRemoteBusy(sender);
    } else if (type == CONTROL_FREE) {
        clearRemoteBusy(sender);
    }
}

/* -------------------------------------------------------------------------- */
/*                               Keyboard (PTT)                               */
/* -------------------------------------------------------------------------- */

void AppLoraVoiceChat::onKeyEvent(const Keyboard::KeyEvent_t& keyEvent)
{
    if (keyEvent.state) {
        if (_is_standby) {
            exitStandby();
            return;
        }
        resetActivityTimer();
    }

    if (keyEvent.keyCode != KEY_ENTER || keyEvent.isModifier) {
        return;
    }

    if (keyEvent.state) {
        if (_state == State::IDLE && !_is_remote_busy) {
            startRecording();
        }
    } else {
        if (_state == State::RECORDING) {
            stopRecordingAndSend();
        }
    }
}

/* -------------------------------------------------------------------------- */
/*                                 Recording                                  */
/* -------------------------------------------------------------------------- */

void AppLoraVoiceChat::startRecording()
{
    if (_is_remote_busy) {
        renderIdle();
        return;
    }

    mclog::tagInfo(getAppInfo().name, "start recording");

    resetActivityTimer();
    _is_local_busy = true;
    sendControlPacket(CONTROL_BUSY);

    GetHAL().speaker.end();
    GetHAL().speaker.setVolume(255);

    auto cfg               = GetHAL().mic.config();
    cfg.magnification      = 128;
    cfg.noise_filter_level = 2;
    GetHAL().mic.config(cfg);
    GetHAL().mic.begin();

    _record_sample_count = 0;
    _record_start_time   = GetHAL().millis();
    _state               = State::RECORDING;
    renderRecording();
}

void AppLoraVoiceChat::stopRecordingAndSend()
{
    mclog::tagInfo(getAppInfo().name, "stop recording, samples: {}", _record_sample_count);

    while (GetHAL().mic.isRecording()) {
        GetHAL().delay(1);
    }
    GetHAL().mic.end();

    if (_record_sample_count == 0) {
        GetHAL().speaker.begin();
        _tx_current_seq          = 0;
        _tx_total_seq            = 0;
        _tx_packet_inflight      = false;
        _tx_sending_free_control = false;
        _state                   = State::SENDING;
        renderSending();
        return;
    }

    _tx_msg_id++;
    _tx_current_seq = 0;
    _tx_total_seq   = (_record_sample_count + VOICE_BYTES_PER_PACKET - 1) / VOICE_BYTES_PER_PACKET;
    if (_tx_total_seq == 0) {
        _tx_total_seq = 1;
    }

    _state                   = State::SENDING;
    _tx_packet_inflight      = false;
    _tx_sending_free_control = false;
    renderSending();
}

void AppLoraVoiceChat::finishSending()
{
    _tx_packet_inflight      = false;
    _tx_sending_free_control = false;
    _is_local_busy           = false;
    _state                   = State::IDLE;
    resetActivityTimer();
    tryPlayReceived();
    if (_state == State::IDLE) {
        renderIdle();
    }
}

/* -------------------------------------------------------------------------- */
/*                                   Sending                                  */
/* -------------------------------------------------------------------------- */

bool AppLoraVoiceChat::sendNextPacket()
{
    if (!GetHAL().capLora868.isTxDone()) {
        return false;
    }

    size_t offset = _tx_current_seq * VOICE_BYTES_PER_PACKET;
    size_t remain = _record_sample_count - offset;
    if (remain > VOICE_BYTES_PER_PACKET) {
        remain = VOICE_BYTES_PER_PACKET;
    }

    uint8_t packet[PACKET_MAX_LEN];
    packet[0] = 0xAA;
    packet[1] = 0x55;
    packet[2] = _tx_msg_id;
    packet[3] = static_cast<uint8_t>(_tx_current_seq);
    packet[4] = static_cast<uint8_t>(_tx_total_seq);

    for (size_t i = 0; i < remain; ++i) {
        packet[PACKET_HEADER_SIZE + i] = static_cast<int8_t>(_pcm_buffer[offset + i] >> 8);
    }

    size_t packet_len = PACKET_HEADER_SIZE + remain;
    return GetHAL().capLora868.loraSendBytes(packet, packet_len);
}

/* -------------------------------------------------------------------------- */
/*                                  Receiving                                 */
/* -------------------------------------------------------------------------- */

void AppLoraVoiceChat::onLoraData(const std::string& data)
{
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
    if (isControlPacket(bytes, data.size())) {
        handleControlPacket(bytes, data.size());
        return;
    }

    if (data.size() < PACKET_HEADER_SIZE) {
        return;
    }

    if (bytes[0] != 0xAA || bytes[1] != 0x55) {
        return;
    }

    uint8_t msg_id = bytes[2];
    uint8_t seq    = bytes[3];
    uint8_t total  = bytes[4];

    if (total == 0 || total > MAX_PACKET_COUNT || seq >= total) {
        return;
    }

    if (!_is_local_busy && !_is_remote_busy) {
        uint8_t unknown_owner[3] = {0xFF, 0xFF, 0xFF};
        setRemoteBusy(unknown_owner);
    } else if (_is_remote_busy) {
        _remote_busy_until = GetHAL().millis() + REMOTE_BUSY_TIMEOUT_MS;
    }

    if (!_rx_pending || msg_id != _rx_msg_id || total != _rx_expected_total) {
        _rx_msg_id         = msg_id;
        _rx_expected_total = total;
        memset(_rx_received_mask, 0, sizeof(_rx_received_mask));
        memset(_rx_buffer, 0, RECORD_BUF_SIZE);
        _rx_voice_len = 0;
        _rx_pending   = true;
    }

    if (!_rx_pending) {
        return;
    }

    _rx_received_mask[seq / 8] |= (1 << (seq % 8));

    size_t payload_len = data.size() - PACKET_HEADER_SIZE;
    size_t offset      = seq * VOICE_BYTES_PER_PACKET;
    if (offset >= RECORD_BUF_SIZE) {
        return;
    }
    if (offset + payload_len > RECORD_BUF_SIZE) {
        payload_len = RECORD_BUF_SIZE - offset;
    }
    memcpy(&_rx_buffer[offset], &bytes[PACKET_HEADER_SIZE], payload_len);

    size_t received_end = offset + payload_len;
    if (received_end > _rx_voice_len) {
        _rx_voice_len = received_end;
    }

    bool complete = true;
    for (size_t i = 0; i < total; ++i) {
        if (!(_rx_received_mask[i / 8] & (1 << (i % 8)))) {
            complete = false;
            break;
        }
    }

    if (complete) {
        _rx_pending = false;
        if (_state == State::IDLE) {
            tryPlayReceived();
        }
    }
}

void AppLoraVoiceChat::tryPlayReceived()
{
    if (_rx_voice_len == 0) {
        return;
    }

    exitStandby(false);
    clearRemoteBusy();

    size_t play_len = _rx_voice_len;
    if (play_len > RECORD_BUF_SIZE) {
        play_len = RECORD_BUF_SIZE;
    }

    for (size_t i = 0; i < play_len; ++i) {
        _pcm_buffer[i] = static_cast<int16_t>(_rx_buffer[i]) << 8;
    }

    GetHAL().speaker.begin();
    GetHAL().speaker.setVolume(255);
    GetHAL().speaker.playRaw(_pcm_buffer, play_len, SAMPLE_RATE, false);

    _state        = State::PLAYING;
    _rx_voice_len = 0;
    renderPlaying();
}

/* -------------------------------------------------------------------------- */
/*                                    UI                                      */
/* -------------------------------------------------------------------------- */

void AppLoraVoiceChat::renderIdle()
{
    if (_is_standby) {
        return;
    }

    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setCursor(10, 10);
    GetHAL().canvas.print("LoRa Voice Chat");
    GetHAL().canvas.setCursor(10, 30);
    if (_is_remote_busy) {
        GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
        GetHAL().canvas.print("Channel busy");
    } else {
        GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
        GetHAL().canvas.print("Hold ENTER to talk");
    }
    GetHAL().pushCanvas();
}

void AppLoraVoiceChat::renderRecording()
{
    if (_is_standby) {
        return;
    }

    uint32_t ms   = GetHAL().millis() - _record_start_time;
    uint32_t sec  = ms / 1000;
    uint32_t frac = (ms % 1000) / 100;
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setCursor(10, 0);
    GetHAL().canvas.printf("REC %lu.%lus", sec, frac);
    GetHAL().pushCanvas();
}

void AppLoraVoiceChat::renderSending()
{
    if (_is_standby) {
        return;
    }

    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextColor(TFT_YELLOW, THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setCursor(10, 10);
    if (_tx_sending_free_control || _tx_current_seq >= _tx_total_seq) {
        GetHAL().canvas.print("SEND DONE");
    } else {
        GetHAL().canvas.printf("SEND %d/%d", static_cast<int>(_tx_current_seq + 1), static_cast<int>(_tx_total_seq));
    }
    GetHAL().pushCanvas();
}

void AppLoraVoiceChat::renderPlaying()
{
    if (_is_standby) {
        return;
    }

    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setCursor(10, 10);
    GetHAL().canvas.print("PLAYING...");
    GetHAL().pushCanvas();
}

void AppLoraVoiceChat::renderWaveform()
{
    // Placeholder for future waveform rendering during recording
}
