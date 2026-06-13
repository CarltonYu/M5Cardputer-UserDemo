#include "keyboard.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

namespace demo {

namespace {
constexpr const char* kTag = "keyboard";

// Cardputer ADV key matrix layout (4 rows x 14 columns).
// Each entry: {normal_char, shifted_char}.
constexpr char kKeyMap[4][14][2] = {
    {{'`', '~'}, {'1', '!'}, {'2', '@'}, {'3', '#'}, {'4', '$'}, {'5', '%'}, {'6', '^'},
     {'7', '&'}, {'8', '*'}, {'9', '('}, {'0', ')'}, {'-', '_'}, {'=', '+'}, {'\b', '\b'}},
    {{'\t', '\t'}, {'q', 'Q'}, {'w', 'W'}, {'e', 'E'}, {'r', 'R'}, {'t', 'T'}, {'y', 'Y'},
     {'u', 'U'}, {'i', 'I'}, {'o', 'O'}, {'p', 'P'}, {'[', '{'}, {']', '}'}, {'\\', '|'}},
    {{0, 0}, {0, 0}, {'a', 'A'}, {'s', 'S'}, {'d', 'D'}, {'f', 'F'}, {'g', 'G'},
     {'h', 'H'}, {'j', 'J'}, {'k', 'K'}, {'l', 'L'}, {';', ':'}, {'\'', '"'}, {'\r', '\r'}},
    {{0, 0}, {0, 0}, {0, 0}, {'z', 'Z'}, {'x', 'X'}, {'c', 'C'}, {'v', 'V'},
     {'b', 'B'}, {'n', 'N'}, {'m', 'M'}, {',', '<'}, {'.', '>'}, {'/', '?'}, {' ', ' '}},
};

// Modifier positions.
constexpr std::uint8_t kRowCtrl      = 3;
constexpr std::uint8_t kColCtrl      = 0;
constexpr std::uint8_t kRowOpt       = 3;
constexpr std::uint8_t kColOpt       = 1;
constexpr std::uint8_t kRowAlt       = 3;
constexpr std::uint8_t kColAlt       = 2;
constexpr std::uint8_t kRowShift     = 2;
constexpr std::uint8_t kColShift     = 0;
constexpr std::uint8_t kRowCapsLock  = 2;
constexpr std::uint8_t kColCapsLock  = 1;
}  // namespace

esp_err_t Keyboard::init(I2cBus& i2c)
{
    tca_ = new Tca8418(i2c, Tca8418::kDefaultAddress);
    esp_err_t err = tca_->begin();
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "TCA8418 init failed");
        delete tca_;
        tca_ = nullptr;
        return err;
    }

    tca_->matrix(4, 14);
    tca_->enableInterrupts();

    // Configure keyboard INT pin (GPIO 11 by default) as input.
    gpio_config_t io_conf{};
    io_conf.intr_type    = GPIO_INTR_NEGEDGE;
    io_conf.mode         = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << CONFIG_HAL_KEYBOARD_INT_GPIO);
    io_conf.pull_up_en   = GPIO_PULLUP_ENABLE;
    gpio_config(&io_conf);

    ESP_LOGI(kTag, "keyboard ready");
    return ESP_OK;
}

void Keyboard::update()
{
    has_event_ = false;
    if (tca_ == nullptr) {
        return;
    }

    // Poll for events.  For now we poll instead of using the INT pin interrupt.
    std::uint8_t raw = tca_->getEvent();
    if (raw == 0) {
        return;
    }

    event_      = decode(raw);
    has_event_  = true;
    updateModifierMask(event_);

    // Clear the key interrupt status bit.
    tca_->writeRegister8(0x02, 0x01);
}

Keyboard::KeyEvent Keyboard::decode(std::uint8_t raw)
{
    KeyEvent key;
    key.pressed = (raw & 0x80) != 0;
    std::uint8_t id = (raw & 0x7F) - 1;
    std::uint8_t tca_row = id / 10;
    std::uint8_t tca_col = id % 10;

    // Remap TCA8418 scanning order to Cardputer physical layout.
    key.col = tca_row * 2;
    if (tca_col > 3) {
        key.col++;
    }
    key.row = (tca_col + 4) % 4;

    bool shifted = (modifier_mask_ & (1 << 0)) != 0;
    key.ascii    = mapToAscii(key.row, key.col, shifted);

    key.isModifier = (key.row == kRowCtrl && key.col == kColCtrl) ||
                     (key.row == kRowShift && key.col == kColShift) ||
                     (key.row == kRowAlt && key.col == kColAlt) ||
                     (key.row == kRowOpt && key.col == kColOpt) ||
                     (key.row == kRowCapsLock && key.col == kColCapsLock);
    return key;
}

void Keyboard::updateModifierMask(const KeyEvent& key)
{
    if (!key.isModifier) {
        return;
    }

    if (key.row == kRowShift && key.col == kColShift) {
        if (key.pressed) {
            modifier_mask_ |= (1 << 0);
        } else {
            modifier_mask_ &= ~(1 << 0);
        }
    }
    // Ctrl/Alt/Opt/Caps can be added here when needed.
}

char Keyboard::mapToAscii(std::uint8_t row, std::uint8_t col, bool shifted)
{
    if (row >= 4 || col >= 14) {
        return 0;
    }
    return kKeyMap[row][col][shifted ? 1 : 0];
}

}  // namespace demo
