#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "esp_err.h"

namespace demo {

// Simple I2S audio stream for the ES8311 codec (16 kHz, 16-bit).
// The codec is wired for stereo 32-bit I2S slots; mono samples are duplicated
// into both channels by this class.
// The codec is half-duplex: recording and playback cannot run at the same
// time.  startRecording() and startPlayback() automatically switch the
// ES8311 into the correct mode.
class I2sAudio {
public:
    I2sAudio() = default;

    // Initialize the I2S peripheral and allocate DMA buffers.
    esp_err_t init();

    // Release the I2S peripheral and buffers.
    void deinit();

    bool initialized() const { return initialized_; }

    // ----- Recording --------------------------------------------------------
    // Switch ES8311 to microphone mode and start the I2S RX stream.
    esp_err_t startRecording();

    // Read up to `samples` mono 16-bit samples into `buffer`.
    // Returns the number of samples actually read, or a negative error code.
    int readRecordBuffer(std::int16_t* buffer, std::size_t samples);

    // Stop recording.
    void stopRecording();

    bool isRecording() const { return recording_; }

    // ----- Playback ---------------------------------------------------------
    // Switch ES8311 to speaker mode and start the I2S TX stream.
    esp_err_t startPlayback();

    // Write up to `samples` mono 16-bit samples from `buffer`.
    // Returns the number of samples actually written, or a negative error code.
    int writePlayBuffer(const std::int16_t* buffer, std::size_t samples);

    // Stop playback.
    void stopPlayback();

    bool isPlaying() const { return playing_; }

private:
    int port_ = CONFIG_AUDIO_I2S_PORT;
    bool initialized_ = false;
    bool recording_   = false;
    bool playing_     = false;
    bool tx_enabled_  = false;
    bool rx_enabled_  = false;

    void* tx_handle_ = nullptr;
    void* rx_handle_ = nullptr;

    // Scratch buffer used to expand mono to stereo and vice-versa.
    std::vector<std::int16_t> stereo_buffer_;
};

}  // namespace demo
