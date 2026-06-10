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

    _lora_slot_id = GetHAL().capLora868.onLoraMsg.connect([this](const std::string& data) { onLoraData(data); });

    _key_slot_id =
        GetHAL().keyboard.onKeyEvent.connect([this](const Keyboard::KeyEvent_t& keyEvent) { onKeyEvent(keyEvent); });

    _state = State::IDLE;
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
            if (GetHAL().capLora868.isTxDone()) {
                _tx_current_seq++;
                if (_tx_current_seq >= _tx_total_seq) {
                    _state = State::IDLE;
                    tryPlayReceived();
                    renderIdle();
                } else {
                    if (sendNextPacket()) {
                        renderSending();
                    } else {
                        _state = State::IDLE;
                        renderIdle();
                    }
                }
            }
            break;
        }
        case State::PLAYING: {
            if (!GetHAL().speaker.isPlaying()) {
                _state = State::IDLE;
                tryPlayReceived();
                renderIdle();
            }
            break;
        }
        default:
            break;
    }

    if (GetHAL().homeButton.wasClicked()) {
        close();
    }
}

void AppLoraVoiceChat::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

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
/*                               Keyboard (PTT)                               */
/* -------------------------------------------------------------------------- */

void AppLoraVoiceChat::onKeyEvent(const Keyboard::KeyEvent_t& keyEvent)
{
    if (keyEvent.keyCode != KEY_ENTER || keyEvent.isModifier) {
        return;
    }

    if (keyEvent.state) {
        if (_state == State::IDLE) {
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
    mclog::tagInfo(getAppInfo().name, "start recording");

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
        _state = State::IDLE;
        renderIdle();
        return;
    }

    _tx_msg_id++;
    _tx_current_seq = 0;
    _tx_total_seq   = (_record_sample_count + VOICE_BYTES_PER_PACKET - 1) / VOICE_BYTES_PER_PACKET;
    if (_tx_total_seq == 0) {
        _tx_total_seq = 1;
    }

    _state = State::SENDING;
    if (sendNextPacket()) {
        renderSending();
    } else {
        _state = State::IDLE;
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
    if (data.size() < PACKET_HEADER_SIZE) {
        return;
    }

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data.data());
    if (bytes[0] != 0xAA || bytes[1] != 0x55) {
        return;
    }

    uint8_t msg_id = bytes[2];
    uint8_t seq    = bytes[3];
    uint8_t total  = bytes[4];

    if (total == 0 || total > MAX_PACKET_COUNT || seq >= total) {
        return;
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
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setCursor(10, 10);
    GetHAL().canvas.print("LoRa Voice Chat");
    GetHAL().canvas.setCursor(10, 30);
    GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    GetHAL().canvas.print("Hold ENTER to talk");
    GetHAL().pushCanvas();
}

void AppLoraVoiceChat::renderRecording()
{
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
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextColor(TFT_YELLOW, THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setCursor(10, 10);
    GetHAL().canvas.printf("SEND %d/%d", static_cast<int>(_tx_current_seq + 1), static_cast<int>(_tx_total_seq));
    GetHAL().pushCanvas();
}

void AppLoraVoiceChat::renderPlaying()
{
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
