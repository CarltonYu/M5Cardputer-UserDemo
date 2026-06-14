#include "ui.h"

#include "hal/hal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "esp_err.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include "../../main/apps/app_chat/assets/chat_big.h"
#include "../../main/apps/app_chat/assets/chat_small.h"
#include "../../main/apps/app_clock/assets/timer_big.h"
#include "../../main/apps/app_clock/assets/timer_small.h"
#include "../../main/apps/app_gps/assets/gps_big.h"
#include "../../main/apps/app_gps/assets/gps_small.h"
#include "../../main/apps/app_imu/assets/imu_big.h"
#include "../../main/apps/app_imu/assets/imu_small.h"
#include "../../main/apps/app_keyboard/assets/keyboard_big.h"
#include "../../main/apps/app_keyboard/assets/keyboard_small.h"
#include "../../main/apps/app_launcher/view/keyboard_bar/assets/Aa0.h"
#include "../../main/apps/app_launcher/view/keyboard_bar/assets/alt0.h"
#include "../../main/apps/app_launcher/view/keyboard_bar/assets/ctrl0.h"
#include "../../main/apps/app_launcher/view/keyboard_bar/assets/fn0.h"
#include "../../main/apps/app_launcher/view/keyboard_bar/assets/opt1.h"
#include "../../main/apps/app_launcher/view/system_bar/assets/bat1.h"
#include "../../main/apps/app_launcher/view/system_bar/assets/wifi1.h"
#include "../../main/apps/app_launcher/view/system_bar/assets/wifi4.h"
#include "../../main/apps/app_lora_chat/assets/chat_lora_big.h"
#include "../../main/apps/app_lora_chat/assets/chat_lora_small.h"
#include "../../main/apps/app_lora_voice_chat/assets/lora_voice_big.h"
#include "../../main/apps/app_lora_voice_chat/assets/lora_voice_small.h"
#include "../../main/apps/app_record/assets/record_big.h"
#include "../../main/apps/app_record/assets/record_small.h"
#include "../../main/apps/app_remote/assets/ir_big.h"
#include "../../main/apps/app_remote/assets/ir_small.h"
#include "../../main/apps/app_repl/assets/repl_big.h"
#include "../../main/apps/app_repl/assets/repl_small.h"
#include "../../main/apps/app_sdcard/assets/tf_big.h"
#include "../../main/apps/app_sdcard/assets/tf_small.h"
#include "../../main/apps/app_set_wifi/assets/set_wifi_big.h"
#include "../../main/apps/app_set_wifi/assets/set_wifi_small.h"
#include "../../main/apps/app_stringir_toolkit/assets/stringir_toolkit_big.h"
#include "../../main/apps/app_stringir_toolkit/assets/stringir_toolkit_small.h"
#include "../../main/apps/app_wifi_scan/assets/scan_big.h"
#include "../../main/apps/app_wifi_scan/assets/scan_small.h"

namespace demo {
namespace {

enum class AppKind {
    kPlaceholder,
    kChat,
    kWifiScan,
    kSdcard,
};

struct AppEntry {
    const char* name;
    const char* subtitle;
    const std::uint16_t* icon_big;
    const std::uint16_t* icon_small;
    AppKind kind;
};

constexpr int kKeyboardBarW     = 42;
constexpr int kSystemBarH       = 32;
constexpr int kMainX            = kKeyboardBarW;
constexpr int kMainY            = kSystemBarH;
constexpr int kMainW            = LcdSt7789::kWidth - kKeyboardBarW;
constexpr int kMainH            = LcdSt7789::kHeight - kSystemBarH;
constexpr int kSideIconW        = 72;
constexpr int kCenterIconW      = 100;
constexpr int kIconW            = kSideIconW;
constexpr int kSideImageW       = 56;
constexpr int kCenterImageW     = 80;
constexpr int kIconGap          = 16;
constexpr int kIconMarginTop    = 48;
constexpr int kIconTagMarginTop = 12;

constexpr LcdSt7789::Color kBg              = LcdSt7789::rgb(0x33, 0x33, 0x33);
constexpr LcdSt7789::Color kSystemBar       = LcdSt7789::rgb(0x99, 0xFF, 0x00);
constexpr LcdSt7789::Color kKeyboardBar     = LcdSt7789::rgb(0xD0, 0xD0, 0xD0);
constexpr LcdSt7789::Color kKeyboardIconBg  = LcdSt7789::rgb(0x55, 0x55, 0x55);
constexpr LcdSt7789::Color kIconBg          = LcdSt7789::rgb(0xE6, 0xE6, 0xE6);
constexpr LcdSt7789::Color kInputPanel      = LcdSt7789::rgb(0x60, 0x60, 0x60);
constexpr LcdSt7789::Color kBlack           = LcdSt7789::rgb(0x00, 0x00, 0x00);
constexpr LcdSt7789::Color kWhite           = LcdSt7789::rgb(0xFF, 0xFF, 0xFF);
constexpr LcdSt7789::Color kMuted           = LcdSt7789::rgb(0xA8, 0xA8, 0xA8);
constexpr LcdSt7789::Color kGreen           = LcdSt7789::rgb(0x00, 0xFF, 0x66);
constexpr LcdSt7789::Color kCyan            = LcdSt7789::rgb(0x00, 0xE8, 0xFF);
constexpr LcdSt7789::Color kOrange          = LcdSt7789::rgb(0xFF, 0xB0, 0x22);
constexpr LcdSt7789::Color kYellow          = LcdSt7789::rgb(0xFF, 0xFF, 0x00);
constexpr LcdSt7789::Color kRed             = LcdSt7789::rgb(0xFF, 0x60, 0x60);

const AppEntry kApps[] = {
    {"Scan", "WiFi", image_data_scan_big, image_data_scan_small, AppKind::kWifiScan},
    {"Record", "Audio", image_data_record_big, image_data_record_small, AppKind::kPlaceholder},
    {"Chat", "ESP NOW", image_data_chat_big, image_data_chat_small, AppKind::kChat},
    {"Remote", "IR", image_data_ir_big, image_data_ir_small, AppKind::kPlaceholder},
    {"REPL", "Pika", image_data_repl_big, image_data_repl_small, AppKind::kPlaceholder},
    {"SetWiFi", "Config", image_data_set_wifi_big, image_data_set_wifi_small, AppKind::kPlaceholder},
    {"Clock", "Time", image_data_timer_big, image_data_timer_small, AppKind::kPlaceholder},
    {"Keyboard", "HID", image_data_keyboard_big, image_data_keyboard_small, AppKind::kPlaceholder},
    {"IMU", "Motion", image_data_imu_big, image_data_imu_small, AppKind::kPlaceholder},
    {"Compass", "I2C", image_data_imu_big, image_data_imu_small, AppKind::kPlaceholder},
    {"SDCard", "TF", image_data_tf_big, image_data_tf_small, AppKind::kSdcard},
    {"StringIR", "Tool", image_data_stringir_toolkit_big, image_data_stringir_toolkit_small, AppKind::kPlaceholder},
    {"LoRaChat", "868", image_data_chat_lora_big, image_data_chat_lora_small, AppKind::kPlaceholder},
    {"LoRaVoice", "Voice", image_data_lora_voice_big, image_data_lora_voice_small, AppKind::kPlaceholder},
    {"GPS", "UART", image_data_gps_big, image_data_gps_small, AppKind::kPlaceholder},
};

const char* const kChatPresets[] = {
    "HELLO",
    "CARDPUTER ADV",
    "ESP NOW TEST",
    "PING",
    "ACK",
    "OK",
};

constexpr int kAppCount        = sizeof(kApps) / sizeof(kApps[0]);
constexpr int kChatPresetCount = sizeof(kChatPresets) / sizeof(kChatPresets[0]);

int wrapIndex(int index, int count)
{
    while (index < 0) {
        index += count;
    }
    while (index >= count) {
        index -= count;
    }
    return index;
}

std::string printableAscii(const std::string& text)
{
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        if (c >= 32 && c <= 126) {
            out.push_back(c);
        } else {
            out.push_back('?');
        }
    }
    return out;
}

std::string truncateToFit(LcdSt7789& lcd, const std::string& text, int max_width, int scale)
{
    std::string out = text;
    while (!out.empty() && lcd.measureText(out.c_str(), scale) > max_width) {
        out.pop_back();
    }
    if (out.size() + 2 < text.size() && out.size() >= 2) {
        out.pop_back();
        out.pop_back();
        out += "..";
    }
    return out;
}

}  // namespace

void DemoUi::begin()
{
    selected_ = wrapIndex(1, kAppCount);
    selector_pos_    = static_cast<float>(selected_);
    selector_target_ = selector_pos_;
    selector_vel_    = 0.0f;
    chat_lines_.reserve(12);
    setStatus("READY");
    last_update_us_ = esp_timer_get_time();
}

bool DemoUi::update()
{
    bool redraw = false;

    if (page_ == Page::kChat) {
        std::string received;
        while (chat_.receive(&received)) {
            appendChatLine(printableAscii(received), LineKind::kReceived);
            redraw = true;
        }
    }

    if (page_ == Page::kWifiScan) {
        const std::int64_t now = esp_timer_get_time();
        if (!wifi_scanning_ && now - last_scan_us_ > 5 * 1000 * 1000LL) {
            performWifiScan();
            redraw       = true;
            last_scan_us_ = now;
        }
    }

    if (page_ == Page::kSdcard) {
        const std::int64_t now = esp_timer_get_time();
        if (now >= next_sd_probe_us_) {
            next_sd_probe_us_ = now + 2 * 1000 * 1000LL;
            probeSdcard();
            redraw = true;
        }
    }

    const std::int64_t now = esp_timer_get_time();
    if (now >= next_tick_us_) {
        next_tick_us_ = now + 500000;
        redraw        = true;
    }

    if (page_ == Page::kLauncher && fast_scroll_count_ > 0 &&
        now - last_rotate_event_us_ > kFastScrollThresholdUs) {
        fast_scroll_count_ = 0;
    }

    const float dt = (now - last_update_us_) / 1000000.0f;
    if (page_ == Page::kLauncher && dt >= 0.016f) {
        last_update_us_ = now;
        if (dt > 0.05f) {
            selector_vel_ = 0.0f;
        } else {
            const float acceleration =
                -200.0f * (selector_pos_ - selector_target_) - 18.0f * selector_vel_;
            selector_vel_ += acceleration * dt;
            selector_pos_ += selector_vel_ * dt;
        }
        if (std::abs(selector_pos_ - selector_target_) > 0.01f ||
            std::abs(selector_vel_) > 0.01f) {
            redraw = true;
        }
    }

    return redraw;
}

bool DemoUi::handleEvent(const InputEvent& event)
{
    switch (event.type) {
        case InputType::kRotateLeft:
        case InputType::kRotateRight:
            handleRotateEvent(event);
            return true;

        case InputType::kEncoderPressed:
            if (page_ == Page::kLauncher) {
                openSelected();
            } else if (page_ == Page::kChat) {
                sendChatPreset();
            } else {
                setStatus("ENTER");
            }
            return true;

        case InputType::kOkPressed:
            if (page_ == Page::kLauncher) {
                setStatus("OK");
            } else {
                closeApp();
            }
            return true;
    }

    return false;
}

void DemoUi::handleRotateEvent(const InputEvent& event)
{
    const std::int64_t now = esp_timer_get_time();
    const std::int64_t dt  = now - last_rotate_event_us_;
    last_rotate_event_us_   = now;

    if (dt > 0 && dt < kFastScrollThresholdUs) {
        ++fast_scroll_count_;
    } else {
        fast_scroll_count_ = 0;
    }

    int ticks = event.ticks;
    if (ticks == 0) {
        ticks = (event.type == InputType::kRotateRight) ? 1 : -1;
    }

    if (page_ == Page::kChat) {
        chat_preset_ = wrapIndex(chat_preset_ + ticks, kChatPresetCount);
        setStatus("PRESET");
        return;
    }

    selected_ = wrapIndex(selected_ + ticks, kAppCount);

    float raw_target        = static_cast<float>(selected_);
    const float ring_length = kAppCount + 1.0f;
    while (raw_target - selector_target_ > ring_length / 2.0f) {
        raw_target -= ring_length;
    }
    while (selector_target_ - raw_target > ring_length / 2.0f) {
        raw_target += ring_length;
    }
    selector_target_ = raw_target;

    // During fast continuous rotation skip the spring animation and jump
    // directly to the target, so every A/B edge is reflected immediately.
    // Slow single steps keep the smooth animation.
    if (fast_scroll_count_ >= kFastScrollMinCount) {
        selector_pos_ = selector_target_;
        selector_vel_ = 0.0f;
    }

    setStatus(ticks > 0 ? "RIGHT" : "LEFT");
}

void DemoUi::render()
{
    ++frame_;
    lcd_.clear(kBg);

    if (page_ == Page::kLauncher) {
        renderLauncher();
    } else if (page_ == Page::kChat) {
        renderChat();
    } else if (page_ == Page::kWifiScan) {
        renderWifiScan();
    } else if (page_ == Page::kSdcard) {
        renderSdcard();
    } else {
        renderPlaceholder();
    }

    drawKeyboardBar();
    drawSystemBar();

    (void)lcd_.flush();
}

void DemoUi::renderLauncher()
{
    lcd_.fillRect(kMainX, kMainY, kMainW, kMainH, kBg);

    const int center_x = kMainX + kMainW / 2 - kIconW / 2;
    const int center_y = kMainY + kIconMarginTop;
    const int side_y   = center_y + (kCenterIconW - kSideIconW) / 2;

    const float ring_length = kAppCount + 1.0f;
    for (int i = 0; i < kAppCount; ++i) {
        float index_offset = static_cast<float>(i) - selector_pos_;
        while (index_offset > ring_length / 2.0f) {
            index_offset -= ring_length;
        }
        while (index_offset < -ring_length / 2.0f) {
            index_offset += ring_length;
        }

        const int base_x  = center_x + static_cast<int>(index_offset * (kIconW + kIconGap));
        const bool active = (i == selected_);
        const int tile_w  = active ? kCenterIconW : kSideIconW;
        const int tile_x  = active ? base_x - (tile_w - kIconW) / 2 : base_x;

        if (tile_x < kMainX || tile_x + tile_w > kMainX + kMainW) {
            continue;
        }

        const int tile_y = active ? center_y : side_y;
        drawIconTile(i, tile_x, tile_y, active);
    }
}

void DemoUi::renderChat()
{
    constexpr int kInputH = 30;
    lcd_.fillRect(kMainX, kMainY, kMainW, kMainH, kBg);
    lcd_.fillRect(kMainX, kMainY, kMainW, kInputH, kInputPanel);

    char input_line[80] = {};
    std::snprintf(input_line, sizeof(input_line), ">>> %s%c", kChatPresets[chat_preset_], (frame_ & 1U) ? '_' : ' ');
    const std::string input = truncateToFit(lcd_, input_line, kMainW - 12, 2);
    lcd_.drawText(kMainX + 6, kMainY + 7, input.c_str(), kWhite, 2);

    const int first_y   = kMainY + kInputH + 7;
    const int line_h    = 13;
    const int max_lines = (LcdSt7789::kHeight - first_y - 18) / line_h;
    const int start     = std::max<int>(0, static_cast<int>(chat_lines_.size()) - max_lines);

    int y = first_y;
    for (int i = start; i < static_cast<int>(chat_lines_.size()); ++i) {
        const ChatLine& line = chat_lines_[i];
        const char* prefix   = "-- ";
        LcdSt7789::Color color = kMuted;

        if (line.kind == LineKind::kSent) {
            prefix = "<< ";
            color  = kGreen;
        } else if (line.kind == LineKind::kReceived) {
            prefix = ">> ";
            color  = kCyan;
        }

        std::string text = std::string(prefix) + line.text;
        text             = truncateToFit(lcd_, text, kMainW - 10, 1);
        lcd_.drawText(kMainX + 5, y, text.c_str(), color, 1);
        y += line_h;
    }

    lcd_.drawText(kMainX + 6, LcdSt7789::kHeight - 14, status_, chat_.ready() ? kMuted : kRed, 1);
}

void DemoUi::renderPlaceholder()
{
    const AppEntry& app = kApps[selected_];
    lcd_.fillRect(kMainX, kMainY, kMainW, kMainH, kBg);

    const int tile_x = kMainX + kMainW / 2 - kCenterIconW / 2;
    const int tile_y = kMainY + 48;
    lcd_.fillRoundRect(tile_x, tile_y, kCenterIconW, kCenterIconW, 8, kIconBg);
    lcd_.drawRgb565ImageScaled(tile_x + (kCenterIconW - kCenterImageW) / 2,
                               tile_y + (kCenterIconW - kCenterImageW) / 2,
                               56, 56, kCenterImageW, kCenterImageW, app.icon_big);

    drawCenteredText(kMainX, tile_y + 78, kMainW, app.name, kIconBg, 2);
    drawCenteredText(kMainX, tile_y + 102, kMainW, "PORT PENDING", kMuted, 1);
    lcd_.drawText(kMainX + 6, LcdSt7789::kHeight - 14, status_, kOrange, 1);
}

void DemoUi::drawSystemBar()
{
    lcd_.fillRect(kMainX, 0, kMainW, kSystemBarH, kBg);
    lcd_.fillRoundRect(kMainX + 5, 4, kMainW - 10, kSystemBarH - 8, (kSystemBarH - 8) / 2, kSystemBar);

    const std::uint64_t seconds = static_cast<std::uint64_t>(esp_timer_get_time() / 1000000);
    char time_text[16]          = {};
    std::snprintf(time_text, sizeof(time_text), "%02u:%02u:%02u", static_cast<unsigned>((seconds / 3600) % 24),
                  static_cast<unsigned>((seconds / 60) % 60), static_cast<unsigned>(seconds % 60));

    drawCenteredText(kMainX, 9, kMainW, time_text, kBlack, 2);
    lcd_.drawRgb565Image(kMainX + 16, 8, 16, 16, chat_.ready() ? image_data_wifi1 : image_data_wifi4);
    lcd_.drawRgb565Image(kMainX + kMainW - 48, 8, 32, 16, image_data_bat1);
    lcd_.drawText(kMainX + kMainW - 41, 13, "100", kBlack, 1);
}

void DemoUi::drawKeyboardBar()
{
    lcd_.fillRect(0, 0, kKeyboardBarW, LcdSt7789::kHeight, kBg);
    lcd_.fillRoundRect(4, 6, kKeyboardBarW - 8, LcdSt7789::kHeight - 12, (kKeyboardBarW - 8) / 2, kKeyboardBar);

    constexpr int x      = 10;
    constexpr int width  = 22;
    constexpr int height = 17;
    constexpr int gap_y  = 13;
    int y                = 39;

    lcd_.drawRgb565Image(x, y, width, height, image_data_Aa0);
    y += height + gap_y;
    lcd_.drawRgb565Image(x, y, width, height, image_data_fn0);
    y += height + gap_y;
    lcd_.drawRgb565Image(x, y, width, height, image_data_ctrl0);
    y += height + gap_y;
    lcd_.drawRgb565Image(x, y, width, height, image_data_opt1);
    y += height + gap_y;
    lcd_.drawRgb565Image(x, y, width, height, image_data_alt0);
}

void DemoUi::drawIconTile(int item_index, int x, int y, bool active)
{
    const AppEntry& app = kApps[wrapIndex(item_index, kAppCount)];
    if (active) {
        lcd_.fillRoundRect(x, y, kCenterIconW, kCenterIconW, 12, kIconBg);
        drawIconImage(item_index, x + (kCenterIconW - kCenterImageW) / 2,
                      y + (kCenterIconW - kCenterImageW) / 2, true);
        drawCenteredText(kMainX, y + kCenterIconW + kIconTagMarginTop, kMainW, app.name, kIconBg, 2);
    } else {
        lcd_.fillRoundRect(x, y, kSideIconW, kSideIconW, 10, kIconBg);
        drawIconImage(item_index, x + (kSideIconW - kSideImageW) / 2,
                      y + (kSideIconW - kSideImageW) / 2, false);
        drawCenteredText(x, y + kSideIconW + kIconTagMarginTop, kSideIconW, app.name, kIconBg, 1);
    }
}

void DemoUi::drawIconImage(int item_index, int x, int y, bool active)
{
    const AppEntry& app = kApps[wrapIndex(item_index, kAppCount)];
    if (active) {
        lcd_.drawRgb565ImageScaled(x, y, 56, 56, kCenterImageW, kCenterImageW, app.icon_big);
    } else {
        lcd_.drawRgb565ImageScaled(x, y, 56, 56, kSideImageW, kSideImageW, app.icon_big);
    }
}

void DemoUi::drawCenteredText(int x, int y, int w, const char* text, LcdSt7789::Color color, int scale)
{
    const std::string clipped = truncateToFit(lcd_, text ? text : "", w, scale);
    const int width           = lcd_.measureText(clipped.c_str(), scale);
    lcd_.drawText(x + (w - width) / 2, y, clipped.c_str(), color, scale);
}

void DemoUi::drawKeyboardPill(int y, const char* text, LcdSt7789::Color fill, LcdSt7789::Color text_color)
{
    lcd_.fillRoundRect(8, y, 26, 18, 4, fill);
    drawCenteredText(8, y + 5, 26, text, text_color, 1);
}

void DemoUi::openSelected()
{
    const AppEntry& app = kApps[selected_];
    if (app.kind == AppKind::kChat) {
        page_ = Page::kChat;
        setStatus("CHAT");
        const esp_err_t err = chat_.init();
        if (chat_lines_.empty()) {
            appendChatLine(err == ESP_OK ? "Chat ready..." : "ESP NOW init failed", LineKind::kSystem);
        }
        if (err != ESP_OK) {
            char text[48] = {};
            std::snprintf(text, sizeof(text), "ERR %s", esp_err_to_name(err));
            appendChatLine(text, LineKind::kSystem);
            setStatus("CHAT FAIL");
        }
    } else if (app.kind == AppKind::kWifiScan) {
        page_ = Page::kWifiScan;
        setStatus("SCAN");
        startWifiScan();
    } else if (app.kind == AppKind::kSdcard) {
        page_ = Page::kSdcard;
        setStatus("SDCARD");
        next_sd_probe_us_ = 0;
        probeSdcard();
    } else {
        page_ = Page::kPlaceholder;
        setStatus(app.subtitle);
    }
}

void DemoUi::closeApp()
{
    if (page_ == Page::kWifiScan) {
        stopWifiScan();
    }
    page_ = Page::kLauncher;
    setStatus("BACK");
}

void DemoUi::sendChatPreset()
{
    const char* message = kChatPresets[chat_preset_];
    const esp_err_t err = chat_.send(message);
    if (err == ESP_OK) {
        appendChatLine(message, LineKind::kSent);
        setStatus("SENT");
    } else {
        char text[48] = {};
        std::snprintf(text, sizeof(text), "SEND %s", esp_err_to_name(err));
        appendChatLine(text, LineKind::kSystem);
        setStatus("SEND FAIL");
    }
}

void DemoUi::appendChatLine(const std::string& text, LineKind kind)
{
    chat_lines_.push_back({text, kind});
    while (chat_lines_.size() > 12) {
        chat_lines_.erase(chat_lines_.begin());
    }
}

void DemoUi::setStatus(const char* text)
{
    std::snprintf(status_, sizeof(status_), "%s", text ? text : "");
}

void DemoUi::renderWifiScan()
{
    lcd_.fillRect(kMainX, kMainY, kMainW, kMainH, kBg);

    // Title.
    lcd_.drawText(kMainX + 6, kMainY + 6, "WiFi Scan", kOrange, 2);

    constexpr int kLineH    = 14;
    constexpr int kMaxLines = 8;
    int y                   = kMainY + 32;

    if (wifi_scanning_) {
        lcd_.drawText(kMainX + 6, y, "Scanning...", kWhite, 1);
    } else if (wifi_results_.empty()) {
        lcd_.drawText(kMainX + 6, y, "No networks found", kRed, 1);
    } else {
        const int start = std::max<int>(0, static_cast<int>(wifi_results_.size()) - kMaxLines);
        for (int i = start; i < static_cast<int>(wifi_results_.size()); ++i) {
            const auto& result = wifi_results_[i];
            LcdSt7789::Color color;
            if (result.first > -60) {
                color = kGreen;
            } else if (result.first > -70) {
                color = kYellow;
            } else {
                color = kRed;
            }

            char line[64] = {};
            std::snprintf(line, sizeof(line), "%d %s", result.first, result.second.c_str());
            const std::string clipped = truncateToFit(lcd_, line, kMainW - 12, 1);
            lcd_.drawText(kMainX + 6, y, clipped.c_str(), color, 1);
            y += kLineH;
        }
    }

    lcd_.drawText(kMainX + 6, LcdSt7789::kHeight - 14, status_, kMuted, 1);
}

void DemoUi::startWifiScan()
{
    if (wifi_inited_) {
        return;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE("wifi_scan", "NVS init failed: %s", esp_err_to_name(err));
        setStatus("NVS FAIL");
        return;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_inited_ = true;
    last_scan_us_ = esp_timer_get_time();
    wifi_results_.clear();
}

void DemoUi::stopWifiScan()
{
    if (!wifi_inited_) {
        return;
    }
    esp_wifi_stop();
    esp_wifi_deinit();
    wifi_inited_ = false;
    wifi_scanning_ = false;
    wifi_results_.clear();
}

void DemoUi::performWifiScan()
{
    if (!wifi_inited_) {
        return;
    }

    wifi_scanning_ = true;
    wifi_results_.clear();

    constexpr int kMaxAp = 16;
    wifi_ap_record_t ap_info[kMaxAp] = {};
    uint16_t ap_count                = 0;
    uint16_t number                  = kMaxAp;

    esp_err_t err = esp_wifi_scan_start(nullptr, true);
    if (err != ESP_OK) {
        ESP_LOGE("wifi_scan", "scan start failed: %s", esp_err_to_name(err));
        wifi_scanning_ = false;
        setStatus("SCAN FAIL");
        return;
    }

    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK) {
        ESP_LOGE("wifi_scan", "get ap num failed: %s", esp_err_to_name(err));
        wifi_scanning_ = false;
        setStatus("SCAN FAIL");
        return;
    }

    err = esp_wifi_scan_get_ap_records(&number, ap_info);
    if (err != ESP_OK) {
        ESP_LOGE("wifi_scan", "get ap records failed: %s", esp_err_to_name(err));
        wifi_scanning_ = false;
        setStatus("SCAN FAIL");
        return;
    }

    for (int i = 0; i < number; ++i) {
        const char* ssid = reinterpret_cast<const char*>(ap_info[i].ssid);
        if (ssid[0] == '\0') {
            continue;
        }
        wifi_results_.push_back({ap_info[i].rssi, ssid});
    }

    std::sort(wifi_results_.begin(), wifi_results_.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    wifi_scanning_ = false;
    setStatus("SCAN OK");
    ESP_LOGI("wifi_scan", "found %d networks", static_cast<int>(wifi_results_.size()));
}

void DemoUi::renderSdcard()
{
    lcd_.fillRect(kMainX, kMainY, kMainW, kMainH, kBg);

    lcd_.drawText(kMainX + 6, kMainY + 6, "SD Card", kOrange, 2);

    constexpr int kLineY = kMainY + 36;
    constexpr int kLineH = 16;

    if (sd_mounted_) {
        lcd_.drawText(kMainX + 6, kLineY, "Mounted", kGreen, 1);
        lcd_.drawText(kMainX + 6, kLineY + kLineH, sd_name_.c_str(), kWhite, 1);
        lcd_.drawText(kMainX + 6, kLineY + kLineH * 2, sd_size_.c_str(), kCyan, 1);
        lcd_.drawText(kMainX + 6, kLineY + kLineH * 3, sd_type_.c_str(), kCyan, 1);
    } else {
        lcd_.drawText(kMainX + 6, kLineY, "Not Found", kRed, 1);
    }

    lcd_.drawText(kMainX + 6, LcdSt7789::kHeight - 14, status_, kMuted, 1);
}

void DemoUi::probeSdcard()
{
    auto info       = demo::GetHal().sdcard().probe();
    sd_mounted_     = info.mounted;
    sd_name_        = std::move(info.name);
    sd_size_        = std::move(info.size);
    sd_type_        = std::move(info.type);
    setStatus(info.mounted ? "SD OK" : "SD FAIL");
}

}  // namespace demo
