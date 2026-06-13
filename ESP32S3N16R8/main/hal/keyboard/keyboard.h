#pragma once

#include <cstdint>

#include "esp_err.h"
#include "tca8418.h"

namespace demo {

class Keyboard {
public:
    struct KeyEvent {
        bool pressed = false;  // true = press, false = release.
        uint8_t row  = 0;
        uint8_t col  = 0;
        char ascii   = 0;      // 0 if not a printable key.
        bool isModifier = false;
    };

    esp_err_t init(I2cBus& i2c);
    void update();

    bool hasEvent() const { return has_event_; }
    const KeyEvent& getEvent() const { return event_; }
    void clearEvent() { has_event_ = false; }

private:
    Tca8418* tca_      = nullptr;
    bool has_event_    = false;
    KeyEvent event_;
    std::uint8_t modifier_mask_ = 0;

    KeyEvent decode(std::uint8_t raw);
    void updateModifierMask(const KeyEvent& key);
    char mapToAscii(std::uint8_t row, std::uint8_t col, bool shifted);
};

}  // namespace demo
