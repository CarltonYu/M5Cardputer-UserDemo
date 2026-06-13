#include "input.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

namespace demo {
namespace {

constexpr const char* kTag = "input";

#ifndef CONFIG_ENCODER_REVERSE
#define CONFIG_ENCODER_REVERSE 0
#endif

#if CONFIG_INPUT_ACTIVE_LOW
constexpr bool kInputActiveLow = true;
#else
constexpr bool kInputActiveLow = false;
#endif

bool isPinEnabled(int pin)
{
    return pin >= 0;
}

esp_err_t configureInputPin(int pin, bool active_low)
{
    if (!isPinEnabled(pin)) {
        return ESP_OK;
    }

    gpio_config_t config = {};
    config.mode          = GPIO_MODE_INPUT;
    config.pin_bit_mask  = 1ULL << static_cast<unsigned>(pin);
    config.pull_up_en    = active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    config.pull_down_en  = active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE;
    config.intr_type     = GPIO_INTR_DISABLE;
    return gpio_config(&config);
}

}  // namespace

esp_err_t DebouncedButton::init(int pin, bool active_low, std::uint32_t debounce_ms)
{
    pin_          = pin;
    active_low_   = active_low;
    debounce_us_  = debounce_ms * 1000U;
    enabled_      = isPinEnabled(pin_);

    if (!enabled_) {
        return ESP_OK;
    }

    const esp_err_t err = configureInputPin(pin_, active_low_);
    if (err != ESP_OK) {
        return err;
    }

    last_raw_pressed_ = readPressed();
    stable_pressed_   = last_raw_pressed_;
    last_change_us_   = esp_timer_get_time();
    return ESP_OK;
}

bool DebouncedButton::update(bool* pressed_edge)
{
    if (pressed_edge) {
        *pressed_edge = false;
    }
    if (!enabled_) {
        return false;
    }

    const bool raw_pressed = readPressed();
    const std::int64_t now = esp_timer_get_time();

    if (raw_pressed != last_raw_pressed_) {
        last_raw_pressed_ = raw_pressed;
        last_change_us_   = now;
    }

    if (raw_pressed != stable_pressed_ && now - last_change_us_ >= static_cast<std::int64_t>(debounce_us_)) {
        stable_pressed_ = raw_pressed;
        if (stable_pressed_ && pressed_edge) {
            *pressed_edge = true;
        }
        return true;
    }

    return false;
}

bool DebouncedButton::readPressed() const
{
    if (!enabled_) {
        return false;
    }

    const int level = gpio_get_level(static_cast<gpio_num_t>(pin_));
    return active_low_ ? level == 0 : level != 0;
}

esp_err_t RotaryInput::init()
{
    ESP_LOGI(kTag, "init encoder A=%d B=%d push=%d ok=%d", CONFIG_ENCODER_PIN_A, CONFIG_ENCODER_PIN_B,
             CONFIG_ENCODER_PIN_PUSH, CONFIG_BUTTON_OK_PIN);

    esp_err_t err = configureInputPin(CONFIG_ENCODER_PIN_A, kInputActiveLow);
    if (err != ESP_OK) {
        return err;
    }
    err = configureInputPin(CONFIG_ENCODER_PIN_B, kInputActiveLow);
    if (err != ESP_OK) {
        return err;
    }

    // Use GPIO interrupts to catch every A/B edge, even during fast rotation.
    err = gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    err = gpio_set_intr_type(static_cast<gpio_num_t>(CONFIG_ENCODER_PIN_A), GPIO_INTR_ANYEDGE);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_set_intr_type(static_cast<gpio_num_t>(CONFIG_ENCODER_PIN_B), GPIO_INTR_ANYEDGE);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_isr_handler_add(static_cast<gpio_num_t>(CONFIG_ENCODER_PIN_A), encoderIsr, this);
    if (err != ESP_OK) {
        return err;
    }
    err = gpio_isr_handler_add(static_cast<gpio_num_t>(CONFIG_ENCODER_PIN_B), encoderIsr, this);
    if (err != ESP_OK) {
        return err;
    }

    err = ok_.init(CONFIG_BUTTON_OK_PIN, kInputActiveLow, CONFIG_BUTTON_DEBOUNCE_MS);
    if (err != ESP_OK) {
        return err;
    }
    err = encoder_push_.init(CONFIG_ENCODER_PIN_PUSH, kInputActiveLow, CONFIG_BUTTON_DEBOUNCE_MS);
    if (err != ESP_OK) {
        return err;
    }

    const int a         = gpio_get_level(static_cast<gpio_num_t>(CONFIG_ENCODER_PIN_A));
    const int b         = gpio_get_level(static_cast<gpio_num_t>(CONFIG_ENCODER_PIN_B));
    isr_last_ab_state_  = (a << 1) | b;
    encoder_ready_      = true;

    return ESP_OK;
}

void IRAM_ATTR RotaryInput::encoderIsr(void* arg)
{
    auto* self = static_cast<RotaryInput*>(arg);

    const int a         = gpio_get_level(static_cast<gpio_num_t>(CONFIG_ENCODER_PIN_A));
    const int b         = gpio_get_level(static_cast<gpio_num_t>(CONFIG_ENCODER_PIN_B));
    const int state     = (a << 1) | b;
    const int old_state = self->isr_last_ab_state_;
    if (state == old_state) {
        return;
    }
    self->isr_last_ab_state_ = state;

    static constexpr int8_t transition_table[16] = {
        0, -1, 1, 0,
        1, 0, 0, -1,
        -1, 0, 0, 1,
        0, 1, -1, 0,
    };

    const int index     = (old_state << 2) | state;
    int transition      = transition_table[index & 0x0F];
#if CONFIG_ENCODER_REVERSE
    transition = -transition;
#endif

    if (transition == 0) {
        self->isr_accum_ = 0;
        return;
    }

    // If direction flipped mid-detent, start a new run in the new direction.
    if ((self->isr_accum_ > 0 && transition < 0) || (self->isr_accum_ < 0 && transition > 0)) {
        self->isr_accum_ = transition;
    } else {
        self->isr_accum_ += transition;
    }

    if (self->isr_accum_ >= 4) {
        self->isr_accum_ = 0;
        if (self->isr_detent_dir_ == 1) {
            ++self->isr_detent_count_;
        } else {
            self->isr_detent_dir_   = 1;
            self->isr_detent_count_ = 1;
        }
    } else if (self->isr_accum_ <= -4) {
        self->isr_accum_ = 0;
        if (self->isr_detent_dir_ == -1) {
            ++self->isr_detent_count_;
        } else {
            self->isr_detent_dir_   = -1;
            self->isr_detent_count_ = 1;
        }
    }
}

void RotaryInput::update()
{
    if (encoder_ready_) {
        int count = 0;
        int dir   = 0;
        portENTER_CRITICAL(&isr_mux_);
        count           = isr_detent_count_;
        dir             = isr_detent_dir_;
        isr_detent_count_ = 0;
        isr_detent_dir_   = 0;
        portEXIT_CRITICAL(&isr_mux_);

        for (int i = 0; i < count; ++i) {
            if (dir == last_detent_dir_) {
                ++detent_count_;
            } else {
                last_detent_dir_ = dir;
                detent_count_    = 1;
            }

            if (detent_count_ >= 2) {
                if (dir > 0) {
                    pushEvent(InputType::kRotateRight, 1);
                } else {
                    pushEvent(InputType::kRotateLeft, -1);
                }
                last_detent_dir_ = 0;
                detent_count_    = 0;
            }
        }
    }

    bool pressed_edge = false;
    ok_.update(&pressed_edge);
    if (pressed_edge) {
        pushEvent(InputType::kOkPressed);
    }

    pressed_edge = false;
    encoder_push_.update(&pressed_edge);
    if (pressed_edge) {
        pushEvent(InputType::kEncoderPressed);
    }
}

bool RotaryInput::popEvent(InputEvent* event)
{
    if (!event || head_ == tail_) {
        return false;
    }

    *event = queue_[tail_];
    tail_  = (tail_ + 1) % kQueueSize;
    return true;
}

void RotaryInput::pushEvent(InputType type, int ticks)
{
    const std::size_t next_head = (head_ + 1) % kQueueSize;
    if (next_head == tail_) {
        tail_ = (tail_ + 1) % kQueueSize;
    }

    queue_[head_].type         = type;
    queue_[head_].timestamp_us = esp_timer_get_time();
    queue_[head_].ticks        = ticks;
    head_ = next_head;
}

}  // namespace demo
