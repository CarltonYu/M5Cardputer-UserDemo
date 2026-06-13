#pragma once

#include <array>
#include <cstdint>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

namespace demo {

enum class InputType {
    kRotateLeft,
    kRotateRight,
    kOkPressed,
    kEncoderPressed,
};

struct InputEvent {
    InputType type;
    std::int64_t timestamp_us;
    int ticks = 0;  // Signed step count for rotary events (positive = right).
};

class DebouncedButton {
public:
    esp_err_t init(int pin, bool active_low, std::uint32_t debounce_ms);
    bool update(bool* pressed_edge);

private:
    int pin_                       = -1;
    bool active_low_               = true;
    bool enabled_                  = false;
    bool last_raw_pressed_         = false;
    bool stable_pressed_           = false;
    std::int64_t last_change_us_   = 0;
    std::uint32_t debounce_us_     = 25000;

    bool readPressed() const;
};

class RotaryInput {
public:
    esp_err_t init();
    void update();
    bool popEvent(InputEvent* event);

private:
    static constexpr std::size_t kQueueSize = 32;

    DebouncedButton ok_;
    DebouncedButton encoder_push_;
    std::array<InputEvent, kQueueSize> queue_{};
    std::size_t head_      = 0;
    std::size_t tail_      = 0;
    int last_detent_dir_   = 0;
    int detent_count_      = 0;
    bool encoder_ready_    = false;

    // Encoder state tracked inside the GPIO ISR.
    volatile int isr_last_ab_state_ = 0;
    volatile int isr_accum_         = 0;
    volatile int isr_detent_count_  = 0;
    volatile int isr_detent_dir_    = 0;
    portMUX_TYPE isr_mux_           = portMUX_INITIALIZER_UNLOCKED;

    static void IRAM_ATTR encoderIsr(void* arg);
    void pushEvent(InputType type, int ticks = 0);
};

}  // namespace demo
