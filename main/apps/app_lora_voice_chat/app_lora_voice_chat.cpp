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
#include <cstdio>
#include <cstring>

using namespace mooncake;

namespace {
constexpr uint8_t ES8311_I2C_ADDR            = 0x18;
constexpr uint32_t ES8311_I2C_FREQ           = 400000;
constexpr uint8_t ES8311_DAC_VOLUME_REG      = 0x32;
constexpr uint8_t ES8311_HP_OUTPUT_REG       = 0x13;
constexpr uint8_t ES8311_HP_OUTPUT_ENABLE    = 0x10;
constexpr uint8_t ES8311_HP_OUTPUT_DISABLE   = 0x00;
constexpr uint8_t ES8311_DAC_VOLUME_NORMAL   = 0xBF;
constexpr uint8_t ES8311_DAC_VOLUME_SILENT   = 0x00;
constexpr uint32_t SPEAKER_STOP_TIMEOUT_MS   = 80;
constexpr int32_t WAVEFORM_LEFT              = 10;
constexpr int32_t WAVEFORM_TOP               = 15;
constexpr int32_t PROGRESS_LEFT              = 10;
constexpr int32_t PROGRESS_TOP               = 62;
constexpr int32_t PROGRESS_HEIGHT            = 10;
constexpr int32_t STATUS_LINE_TOP            = 100;
constexpr int32_t IDLE_STATUS_LINE_TOP       = 42;
constexpr int32_t IDLE_LAST_RECORD_TOP       = 56;

void setCardputerAdvCodecOutputEnabled(bool enabled)
{
    M5.In_I2C.writeRegister8(ES8311_I2C_ADDR, ES8311_DAC_VOLUME_REG,
                             enabled ? ES8311_DAC_VOLUME_NORMAL : ES8311_DAC_VOLUME_SILENT, ES8311_I2C_FREQ);
    M5.In_I2C.writeRegister8(ES8311_I2C_ADDR, ES8311_HP_OUTPUT_REG,
                             enabled ? ES8311_HP_OUTPUT_ENABLE : ES8311_HP_OUTPUT_DISABLE, ES8311_I2C_FREQ);
}
}  // namespace

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
    if (_last_voice_buffer) {
        delete[] _last_voice_buffer;
    }
}

void AppLoraVoiceChat::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    audio::set_keyboard_sfx_enable(false);
    silenceSpeaker();

    _pcm_buffer        = new int16_t[RECORD_BUF_SIZE]();
    _rx_buffer         = new int8_t[RECORD_BUF_SIZE]();
    _last_voice_buffer = new int8_t[RECORD_BUF_SIZE]();

    _is_cap_available = GetHAL().capLora868.init();
    if (!_is_cap_available) {
        GetHAL().canvas.fillScreen(THEME_COLOR_BG);
        GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(10, 10);
        GetHAL().canvas.print("LoRa cap init failed");
        GetHAL().pushCanvas();
        return;
    }

    _tx_power = GetHAL().capLora868.getTxPower();
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
    _last_voice_len          = 0;
    _is_replaying_last       = false;
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
        case State::IDLE: {
            if (!_is_standby && _last_record_direction != LastRecordDirection::NONE &&
                GetHAL().millis() - _last_ui_frame_time >= IDLE_REFRESH_MS) {
                renderIdle();
            }
            break;
        }
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
        case State::RECEIVING: {
            break;
        }
        case State::PLAYING: {
            if (!GetHAL().speaker.isPlaying()) {
                silenceSpeaker();
                _is_replaying_last = false;
                _state = State::IDLE;
                resetActivityTimer();
                tryPlayReceived();
                if (_state == State::IDLE) {
                    renderIdle();
                }
            } else if (GetHAL().millis() - _last_ui_frame_time >= UI_FRAME_INTERVAL_MS) {
                renderPlaying();
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
    if (_last_voice_buffer) {
        delete[] _last_voice_buffer;
        _last_voice_buffer = nullptr;
    }
    _last_voice_len = 0;

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
        size_t received = getReceivedPacketCount();
        clearRemoteBusy();
        if (_state == State::RECEIVING) {
            if (received > 0) {
                saveLastVoiceFromBytes(_rx_buffer, _rx_voice_len);
                setLastRecord(LastRecordDirection::RECEIVED, received, false);
            }
            _rx_pending = false;
            _state      = State::IDLE;
            renderIdle();
        }
    }
}

void AppLoraVoiceChat::enterStandby()
{
    if (_is_standby || _state != State::IDLE) {
        return;
    }

    mclog::tagInfo(getAppInfo().name, "enter standby");
    _is_standby = true;

    silenceSpeaker();
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
        } else if (_state == State::RECEIVING) {
            renderReceiving();
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

size_t AppLoraVoiceChat::getReceivedPacketCount() const
{
    size_t received = 0;
    for (size_t i = 0; i < _rx_expected_total; ++i) {
        if (_rx_received_mask[i / 8] & (1 << (i % 8))) {
            received++;
        }
    }
    return received;
}

float AppLoraVoiceChat::getSendProgress() const
{
    if (_tx_total_seq == 0 || _tx_sending_free_control || _tx_current_seq >= _tx_total_seq) {
        return 1.0f;
    }

    size_t sent_or_inflight = _tx_current_seq;
    if (_tx_packet_inflight && sent_or_inflight < _tx_total_seq) {
        sent_or_inflight++;
    }
    return static_cast<float>(sent_or_inflight) / static_cast<float>(_tx_total_seq);
}

float AppLoraVoiceChat::getReceiveProgress() const
{
    if (_rx_expected_total == 0) {
        return 0.0f;
    }
    return static_cast<float>(getReceivedPacketCount()) / static_cast<float>(_rx_expected_total);
}

void AppLoraVoiceChat::setLastRecord(LastRecordDirection direction, size_t packet_count, bool complete)
{
    _last_record_direction    = direction;
    _last_record_packet_count = packet_count;
    _last_record_complete     = complete;
    _last_record_time         = GetHAL().millis();
}

void AppLoraVoiceChat::formatDuration(char* buffer, size_t buffer_size, uint32_t duration_ms) const
{
    uint32_t total_seconds = duration_ms / 1000;
    uint32_t hours         = total_seconds / 3600;
    uint32_t minutes       = (total_seconds / 60) % 60;
    uint32_t seconds       = total_seconds % 60;

    if (hours > 0) {
        snprintf(buffer, buffer_size, "%lu:%02lu:%02lu", static_cast<unsigned long>(hours),
                 static_cast<unsigned long>(minutes), static_cast<unsigned long>(seconds));
    } else {
        snprintf(buffer, buffer_size, "%02lu:%02lu", static_cast<unsigned long>(minutes),
                 static_cast<unsigned long>(seconds));
    }
}

/* -------------------------------------------------------------------------- */
/*                               Keyboard (PTT)                               */
/* -------------------------------------------------------------------------- */

void AppLoraVoiceChat::onKeyEvent(const Keyboard::KeyEvent_t& keyEvent)
{
    bool is_settings_key =
        !keyEvent.isModifier && (keyEvent.keyCode == KEY_LEFTBRACE || keyEvent.keyCode == KEY_RIGHTBRACE ||
                                 keyEvent.keyCode == KEY_Z || keyEvent.keyCode == KEY_X);

    if (keyEvent.state) {
        if (_is_standby) {
            exitStandby();
            if (keyEvent.keyCode != KEY_R && !is_settings_key) {
                return;
            }
        }
        resetActivityTimer();
    }

    if (handleSettingsKey(keyEvent)) {
        return;
    }

    if (keyEvent.keyCode == KEY_R && !keyEvent.isModifier) {
        if (keyEvent.state) {
            playLastVoice();
        }
        return;
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

bool AppLoraVoiceChat::handleSettingsKey(const Keyboard::KeyEvent_t& keyEvent)
{
    if (!keyEvent.state || keyEvent.isModifier) {
        return false;
    }

    switch (keyEvent.keyCode) {
        case KEY_LEFTBRACE:
            adjustPlaybackVolume(-static_cast<int>(PLAYBACK_VOLUME_STEP));
            return true;
        case KEY_RIGHTBRACE:
            adjustPlaybackVolume(PLAYBACK_VOLUME_STEP);
            return true;
        case KEY_Z:
            adjustTxPower(-1);
            return true;
        case KEY_X:
            adjustTxPower(1);
            return true;
        default:
            return false;
    }
}

void AppLoraVoiceChat::adjustPlaybackVolume(int delta)
{
    int value = static_cast<int>(_playback_volume) + delta;
    if (value < 0) {
        value = 0;
    } else if (value > 255) {
        value = 255;
    }

    _playback_volume = static_cast<uint8_t>(value);
    if (_state == State::PLAYING) {
        GetHAL().speaker.setVolume(_playback_volume);
    }

    renderCurrentState();
}

void AppLoraVoiceChat::adjustTxPower(int delta)
{
    int value = static_cast<int>(_tx_power) + delta;
    if (value < CapLoRa868::lora_config::minPower) {
        value = CapLoRa868::lora_config::minPower;
    } else if (value > CapLoRa868::lora_config::maxPower) {
        value = CapLoRa868::lora_config::maxPower;
    }

    int8_t requested_power = static_cast<int8_t>(value);
    if (requested_power != _tx_power && GetHAL().capLora868.setTxPower(requested_power)) {
        _tx_power = requested_power;
    }

    renderCurrentState();
}

uint8_t AppLoraVoiceChat::getPlaybackVolumePercent() const
{
    return static_cast<uint8_t>((static_cast<uint16_t>(_playback_volume) * 100 + 127) / 255);
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

    silenceSpeaker();

    auto cfg               = GetHAL().mic.config();
    cfg.magnification      = 128;
    cfg.noise_filter_level = 2;
    GetHAL().mic.config(cfg);
    GetHAL().mic.begin();

    _record_sample_count = 0;
    _record_start_time   = GetHAL().millis();
    _last_ui_frame_time  = 0;
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
    silenceSpeaker();

    if (_record_sample_count == 0) {
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
    silenceSpeaker();

    if (_record_sample_count > 0) {
        bool complete        = _tx_total_seq == 0 || _tx_current_seq >= _tx_total_seq;
        size_t packet_count = complete ? _tx_total_seq : _tx_current_seq;
        saveLastVoiceFromPcm(_pcm_buffer, _record_sample_count);
        setLastRecord(LastRecordDirection::SENT, packet_count, complete);
    }

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
        setLastRecord(LastRecordDirection::RECEIVED, getReceivedPacketCount(), true);
        _rx_pending = false;
        if (_state == State::IDLE || _state == State::RECEIVING) {
            tryPlayReceived();
        }
    } else if (_state == State::IDLE || _state == State::RECEIVING) {
        _state = State::RECEIVING;
        renderReceiving();
    }
}

void AppLoraVoiceChat::silenceSpeaker()
{
    GetHAL().speaker.setVolume(0);
    GetHAL().speaker.stop();

    uint32_t start_time = GetHAL().millis();
    while (GetHAL().speaker.isPlaying() && GetHAL().millis() - start_time < SPEAKER_STOP_TIMEOUT_MS) {
        GetHAL().delay(1);
    }

    setCardputerAdvCodecOutputEnabled(false);
    GetHAL().speaker.end();
}

void AppLoraVoiceChat::prepareSpeakerForPlayback()
{
    while (GetHAL().mic.isRecording()) {
        GetHAL().delay(1);
    }
    GetHAL().mic.end();

    setCardputerAdvCodecOutputEnabled(true);
    GetHAL().speaker.begin();
    GetHAL().speaker.setVolume(_playback_volume);
}

void AppLoraVoiceChat::saveLastVoiceFromPcm(const int16_t* samples, size_t sample_count)
{
    if (!_last_voice_buffer || !samples || sample_count == 0) {
        return;
    }
    if (sample_count > RECORD_BUF_SIZE) {
        sample_count = RECORD_BUF_SIZE;
    }

    for (size_t i = 0; i < sample_count; ++i) {
        _last_voice_buffer[i] = static_cast<int8_t>(samples[i] >> 8);
    }
    _last_voice_len = sample_count;
}

void AppLoraVoiceChat::saveLastVoiceFromBytes(const int8_t* samples, size_t sample_count)
{
    if (!_last_voice_buffer || !samples || sample_count == 0) {
        return;
    }
    if (sample_count > RECORD_BUF_SIZE) {
        sample_count = RECORD_BUF_SIZE;
    }

    memcpy(_last_voice_buffer, samples, sample_count);
    _last_voice_len = sample_count;
}

bool AppLoraVoiceChat::playLastVoice()
{
    if (!_pcm_buffer || !_last_voice_buffer || _last_voice_len == 0) {
        renderIdle();
        return false;
    }
    if (_state == State::RECORDING || _state == State::SENDING || _state == State::RECEIVING) {
        return false;
    }

    exitStandby(false);
    if (_state == State::PLAYING) {
        silenceSpeaker();
    }

    size_t play_len = _last_voice_len;
    if (play_len > RECORD_BUF_SIZE) {
        play_len = RECORD_BUF_SIZE;
    }
    for (size_t i = 0; i < play_len; ++i) {
        _pcm_buffer[i] = static_cast<int16_t>(_last_voice_buffer[i]) << 8;
    }

    prepareSpeakerForPlayback();
    GetHAL().speaker.playRaw(_pcm_buffer, play_len, SAMPLE_RATE, false);

    _play_sample_count  = play_len;
    _play_start_time    = GetHAL().millis();
    _last_ui_frame_time = 0;
    _is_replaying_last  = true;
    _state              = State::PLAYING;
    renderPlaying();
    return true;
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
    saveLastVoiceFromBytes(_rx_buffer, play_len);

    for (size_t i = 0; i < play_len; ++i) {
        _pcm_buffer[i] = static_cast<int16_t>(_rx_buffer[i]) << 8;
    }

    prepareSpeakerForPlayback();
    GetHAL().speaker.playRaw(_pcm_buffer, play_len, SAMPLE_RATE, false);

    _play_sample_count   = play_len;
    _play_start_time     = GetHAL().millis();
    _last_ui_frame_time  = 0;
    _is_replaying_last   = false;
    _state               = State::PLAYING;
    _rx_voice_len        = 0;
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
    GetHAL().canvas.setFont(&fonts::Font0);
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
        if (_last_voice_len > 0) {
            GetHAL().canvas.print("ENTER talk  R replay");
        } else {
            GetHAL().canvas.print("Hold ENTER to talk");
        }
    }
    renderSettingsStatus(IDLE_STATUS_LINE_TOP);
    renderLastRecord();
    GetHAL().pushCanvas();

    _last_ui_frame_time = GetHAL().millis();
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
    GetHAL().canvas.setFont(&fonts::Font0);
    GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setCursor(10, 0);
    GetHAL().canvas.printf("REC %lu.%lus", sec, frac);
    renderWaveform();
    renderSettingsStatus(STATUS_LINE_TOP);
    GetHAL().pushCanvas();
}

void AppLoraVoiceChat::renderSending()
{
    if (_is_standby) {
        return;
    }

    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setFont(&fonts::Font0);
    GetHAL().canvas.setTextColor(TFT_YELLOW, THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setCursor(10, 10);
    if (_tx_sending_free_control || _tx_current_seq >= _tx_total_seq) {
        GetHAL().canvas.print("SEND DONE");
    } else {
        GetHAL().canvas.printf("SEND %d/%d", static_cast<int>(_tx_current_seq + 1), static_cast<int>(_tx_total_seq));
    }
    drawProgressBar(getSendProgress(), TFT_YELLOW);
    renderSettingsStatus(STATUS_LINE_TOP);
    GetHAL().pushCanvas();
}

void AppLoraVoiceChat::renderReceiving()
{
    if (_is_standby) {
        return;
    }

    size_t received = getReceivedPacketCount();

    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setFont(&fonts::Font0);
    GetHAL().canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setCursor(10, 10);
    if (_rx_expected_total > 0) {
        GetHAL().canvas.printf("RECV %d/%d", static_cast<int>(received), static_cast<int>(_rx_expected_total));
    } else {
        GetHAL().canvas.print("RECV...");
    }
    drawProgressBar(getReceiveProgress(), TFT_CYAN);
    renderSettingsStatus(STATUS_LINE_TOP);
    GetHAL().pushCanvas();
}

void AppLoraVoiceChat::renderPlaying()
{
    if (_is_standby) {
        return;
    }

    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setFont(&fonts::Font0);
    GetHAL().canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setCursor(10, 0);
    GetHAL().canvas.print(_is_replaying_last ? "REPLAY..." : "PLAYING...");
    renderWaveform();
    renderSettingsStatus(STATUS_LINE_TOP);
    GetHAL().pushCanvas();

    _last_ui_frame_time = GetHAL().millis();
}

void AppLoraVoiceChat::renderCurrentState()
{
    if (_is_standby) {
        return;
    }

    switch (_state) {
        case State::RECORDING:
            renderRecording();
            break;
        case State::SENDING:
            renderSending();
            break;
        case State::RECEIVING:
            renderReceiving();
            break;
        case State::PLAYING:
            renderPlaying();
            break;
        case State::IDLE:
        default:
            renderIdle();
            break;
    }
}

void AppLoraVoiceChat::renderSettingsStatus(int y)
{
    GetHAL().canvas.setFont(&fonts::Font0);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(10, y);
    GetHAL().canvas.printf("VOL %3d%%  TX %ddBm", static_cast<int>(getPlaybackVolumePercent()),
                           static_cast<int>(_tx_power));
}

void AppLoraVoiceChat::renderLastRecord()
{
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setCursor(10, IDLE_LAST_RECORD_TOP);

    if (_last_record_direction == LastRecordDirection::NONE) {
        GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        GetHAL().canvas.print("Last: --");
        return;
    }

    const char* direction = _last_record_direction == LastRecordDirection::SENT ? "TX" : "RX";
    uint32_t direction_color = _last_record_direction == LastRecordDirection::SENT ? TFT_YELLOW : TFT_CYAN;

    char ago_buffer[16] = {};
    char at_buffer[16]  = {};
    formatDuration(ago_buffer, sizeof(ago_buffer), GetHAL().millis() - _last_record_time);
    formatDuration(at_buffer, sizeof(at_buffer), _last_record_time);

    GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    GetHAL().canvas.print("Last record");

    GetHAL().canvas.setCursor(10, IDLE_LAST_RECORD_TOP + 14);
    GetHAL().canvas.setTextColor(direction_color, THEME_COLOR_BG);
    GetHAL().canvas.printf("%s", direction);
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.printf(" %d pkts", static_cast<int>(_last_record_packet_count));
    if (!_last_record_complete) {
        GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
        GetHAL().canvas.print(" partial");
    }

    GetHAL().canvas.setCursor(10, IDLE_LAST_RECORD_TOP + 28);
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.printf("%s ago", ago_buffer);

    GetHAL().canvas.setCursor(10, IDLE_LAST_RECORD_TOP + 42);
    GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    GetHAL().canvas.printf("T+%s", at_buffer);
}

void AppLoraVoiceChat::renderWaveform()
{
    if (!_pcm_buffer) {
        return;
    }

    const int16_t* samples = nullptr;
    size_t sample_count    = 0;

    if (_state == State::RECORDING) {
        sample_count = _record_sample_count < WAVEFORM_SAMPLE_COUNT ? _record_sample_count : WAVEFORM_SAMPLE_COUNT;
        if (sample_count > 0) {
            samples = &_pcm_buffer[_record_sample_count - sample_count];
        }
    } else if (_state == State::PLAYING) {
        size_t play_sample_pos =
            static_cast<size_t>((static_cast<uint64_t>(GetHAL().millis() - _play_start_time) * SAMPLE_RATE) / 1000);
        if (play_sample_pos > _play_sample_count) {
            play_sample_pos = _play_sample_count;
        }

        size_t sample_start = 0;
        if (play_sample_pos > WAVEFORM_SAMPLE_COUNT) {
            sample_start = play_sample_pos - WAVEFORM_SAMPLE_COUNT;
        }

        if (sample_start < _play_sample_count) {
            sample_count = _play_sample_count - sample_start;
            if (sample_count > WAVEFORM_SAMPLE_COUNT) {
                sample_count = WAVEFORM_SAMPLE_COUNT;
            }
            samples = &_pcm_buffer[sample_start];
        }
    }

    drawWaveformSamples(samples, sample_count, TFT_WHITE);
}

void AppLoraVoiceChat::drawWaveformSamples(const int16_t* samples, size_t sample_count, uint32_t color)
{
    int32_t waveform_width = GetHAL().canvas.width() - WAVEFORM_LEFT * 2;
    if (waveform_width <= 0) {
        return;
    }

    int32_t waveform_bottom = STATUS_LINE_TOP - 2;
    if (waveform_bottom > GetHAL().canvas.height()) {
        waveform_bottom = GetHAL().canvas.height();
    }

    int32_t waveform_height = waveform_bottom - WAVEFORM_TOP;
    if (waveform_height <= 0) {
        return;
    }

    GetHAL().canvas.fillRect(WAVEFORM_LEFT, WAVEFORM_TOP, waveform_width, waveform_height, THEME_COLOR_BG);

    int32_t center_y = WAVEFORM_TOP + waveform_height / 2;
    if (!samples || sample_count == 0) {
        GetHAL().canvas.fillRect(WAVEFORM_LEFT, center_y, waveform_width, 1, TFT_DARKGREY);
        return;
    }

    static constexpr int shift = 8;
    for (int32_t x = 0; x < waveform_width - 1; ++x) {
        size_t sample_index = static_cast<size_t>(x) * sample_count / waveform_width;
        if (sample_index >= sample_count) {
            sample_index = sample_count - 1;
        }

        int32_t y = center_y + (samples[sample_index] >> shift);
        if (y < WAVEFORM_TOP) {
            y = WAVEFORM_TOP;
        }
        if (y >= WAVEFORM_TOP + waveform_height - 1) {
            y = WAVEFORM_TOP + waveform_height - 2;
        }

        int32_t px = WAVEFORM_LEFT + x;
        GetHAL().canvas.drawPixel(px, y, color);
        GetHAL().canvas.drawPixel(px + 1, y, color);
        GetHAL().canvas.drawPixel(px, y + 1, color);
        GetHAL().canvas.drawPixel(px + 1, y + 1, color);
    }
}

void AppLoraVoiceChat::drawProgressBar(float progress, uint32_t color)
{
    if (progress < 0.0f) {
        progress = 0.0f;
    }
    if (progress > 1.0f) {
        progress = 1.0f;
    }

    int32_t progress_width = GetHAL().canvas.width() - PROGRESS_LEFT * 2;
    if (progress_width <= 2) {
        return;
    }

    GetHAL().canvas.drawRect(PROGRESS_LEFT, PROGRESS_TOP, progress_width, PROGRESS_HEIGHT, TFT_DARKGREY);
    GetHAL().canvas.fillRect(PROGRESS_LEFT + 1, PROGRESS_TOP + 1, progress_width - 2, PROGRESS_HEIGHT - 2,
                             THEME_COLOR_BG);

    int32_t fill_width = static_cast<int32_t>((progress_width - 2) * progress);
    if (fill_width > 0) {
        GetHAL().canvas.fillRect(PROGRESS_LEFT + 1, PROGRESS_TOP + 1, fill_width, PROGRESS_HEIGHT - 2, color);
    }
}
