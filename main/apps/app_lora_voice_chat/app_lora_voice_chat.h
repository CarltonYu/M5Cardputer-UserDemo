/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <hal/hal.h>
#include <string>

class AppLoraVoiceChat : public mooncake::AppAbility {
public:
    AppLoraVoiceChat();
    ~AppLoraVoiceChat();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class State {
        IDLE,
        RECORDING,
        SENDING,
        PLAYING,
    };

    static constexpr size_t SAMPLE_RATE            = 8000;
    static constexpr size_t MAX_RECORD_SEC         = 3;
    static constexpr size_t RECORD_BUF_SIZE        = SAMPLE_RATE * MAX_RECORD_SEC;
    static constexpr size_t PACKET_MAX_LEN         = 220;
    static constexpr size_t PACKET_HEADER_SIZE     = 5;
    static constexpr size_t VOICE_BYTES_PER_PACKET = PACKET_MAX_LEN - PACKET_HEADER_SIZE;
    static constexpr size_t MAX_PACKET_COUNT = (RECORD_BUF_SIZE + VOICE_BYTES_PER_PACKET - 1) / VOICE_BYTES_PER_PACKET;
    static constexpr size_t RX_MASK_SIZE     = (MAX_PACKET_COUNT + 7) / 8;

    State _state = State::IDLE;

    int16_t* _pcm_buffer        = nullptr;
    size_t _record_sample_count = 0;

    int8_t* _rx_buffer   = nullptr;
    size_t _rx_voice_len = 0;
    bool _rx_pending     = false;

    uint8_t _tx_msg_id     = 0;
    size_t _tx_current_seq = 0;
    size_t _tx_total_seq   = 0;

    uint8_t _rx_msg_id                      = 0;
    uint8_t _rx_expected_total              = 0;
    uint8_t _rx_received_mask[RX_MASK_SIZE] = {};

    bool _is_cap_available      = false;
    int _lora_slot_id           = -1;
    int _key_slot_id            = -1;
    uint32_t _record_start_time = 0;

    void onKeyEvent(const Keyboard::KeyEvent_t& keyEvent);
    void onLoraData(const std::string& data);

    void startRecording();
    void stopRecordingAndSend();
    bool sendNextPacket();
    void tryPlayReceived();

    void renderIdle();
    void renderRecording();
    void renderSending();
    void renderPlaying();
    void renderWaveform();
};
