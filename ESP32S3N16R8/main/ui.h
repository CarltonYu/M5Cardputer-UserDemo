#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "espnow_chat.h"
#include "input.h"
#include "lcd_st7789.h"

namespace demo {

class DemoUi {
public:
    explicit DemoUi(LcdSt7789& lcd) : lcd_(lcd) {}

    void begin();
    bool update();
    bool handleEvent(const InputEvent& event);
    void render();

private:
    enum class Page {
        kLauncher,
        kChat,
        kPlaceholder,
    };

    enum class LineKind {
        kSystem,
        kSent,
        kReceived,
    };

    struct ChatLine {
        std::string text;
        LineKind kind = LineKind::kSystem;
    };

    LcdSt7789& lcd_;
    EspNowChat chat_;
    Page page_              = Page::kLauncher;
    int selected_           = 2;
    int chat_preset_        = 0;
    std::uint32_t frame_    = 0;
    std::int64_t next_tick_us_ = 0;
    std::int64_t last_update_us_ = 0;
    float selector_pos_     = 1.0f;
    float selector_vel_     = 0.0f;
    float selector_target_  = 1.0f;
    std::vector<ChatLine> chat_lines_;
    char status_[64] = "READY";

    void renderLauncher();
    void renderChat();
    void renderPlaceholder();
    void drawSystemBar();
    void drawKeyboardBar();
    void drawIconTile(int item_index, int x, int y, bool active);
    void drawIconImage(int item_index, int x, int y, bool active);
    void drawCenteredText(int x, int y, int w, const char* text, LcdSt7789::Color color, int scale);
    void drawKeyboardPill(int y, const char* text, LcdSt7789::Color fill, LcdSt7789::Color text_color);
    void openSelected();
    void closeApp();
    void sendChatPreset();
    void appendChatLine(const std::string& text, LineKind kind);
    void setStatus(const char* text);
};

}  // namespace demo
