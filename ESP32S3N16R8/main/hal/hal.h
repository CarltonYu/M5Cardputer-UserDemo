#pragma once

#include <cstdint>

#include "audio/es8311.h"
#include "audio/i2s_audio.h"
#include "imu/mpu6050.h"
#include "keyboard/keyboard.h"
#include "sdcard/sdcard.h"

namespace demo {

// Central hardware abstraction for the ESP32-S3-N16R8 port.
class Hal {
public:
    Hal();

    // Initialize all configured subsystems.  Must be called once at startup.
    esp_err_t init();

    // Periodic update: input, audio pumps, etc.
    void update();

    // Simple delay helpers.
    static void delayMs(std::uint32_t ms);
    static std::uint32_t millis();

    // Access the internal I2C bus (SDA=GPIO8/SCL=GPIO9 by default).
    I2cBus& i2c() { return i2c_bus_; }

    // Keyboard controller.
    Keyboard& keyboard() { return keyboard_; }

    // Audio codec (I2C register control).
    Es8311& audio() { return audio_; }

    // Audio I2S stream (record/playback).
    I2sAudio& i2sAudio() { return i2s_audio_; }

    // IMU.
    Mpu6050& imu() { return imu_; }

    // SD card.
    SdCard& sdcard() { return sdcard_; }

    // Convenience: scan the internal I2C bus and print results.
    void i2cScan();

    // Play a short tone through the speaker (volume 0.0 .. 1.0).
    esp_err_t playTone(int freq_hz, int duration_ms, float volume = 0.70f);

private:
    I2cBus i2c_bus_;
    Keyboard keyboard_;
    Es8311 audio_;
    I2sAudio i2s_audio_;
    Mpu6050 imu_;
    SdCard sdcard_;
    bool initialized_ = false;
};

// Global singleton.
Hal& GetHal();

}  // namespace demo
