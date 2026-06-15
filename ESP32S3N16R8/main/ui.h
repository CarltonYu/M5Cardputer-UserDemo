#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "espnow_chat.h"
#include "hal/imu/mpu6050.h"
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
        kWifiScan,
        kSdcard,
        kRecord,
        kImu,
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
    std::int64_t last_rotate_event_us_ = 0;
    int fast_scroll_count_ = 0;
    float selector_pos_     = 1.0f;
    float selector_vel_     = 0.0f;
    float selector_target_  = 1.0f;

    static constexpr std::int64_t kFastScrollThresholdUs = 80 * 1000LL;
    static constexpr int kFastScrollMinCount = 2;
    std::vector<ChatLine> chat_lines_;
    char status_[64] = "READY";

    // Wi-Fi scan state.
    bool wifi_inited_           = false;
    bool wifi_scanning_         = false;
    std::int64_t last_scan_us_  = 0;
    std::vector<std::pair<int, std::string>> wifi_results_;  // {rssi, ssid}

    // SD card state.
    std::int64_t next_sd_probe_us_ = 0;
    std::string sd_name_;
    std::string sd_size_;
    std::string sd_type_;
    bool sd_mounted_ = false;

    // IMU state.
    ImuData imu_data_ = {};
    std::int64_t imu_next_read_us_ = 0;

    // Record state.
    enum class RecState { kIdle, kRecording, kStopped, kPlaying };
    RecState rec_state_ = RecState::kIdle;

    static constexpr std::size_t kRecordBlocks      = 80;
    static constexpr std::size_t kRecordBlockSize   = 200;
    static constexpr std::size_t kRecordTotalSize   = kRecordBlocks * kRecordBlockSize;
    std::int16_t* record_buffer_   = nullptr;
    std::size_t record_write_idx_  = 0;
    std::size_t record_draw_idx_   = 0;
    std::size_t record_play_sample_idx_ = 0;

    void renderLauncher();
    void renderChat();
    void renderWifiScan();
    void renderSdcard();
    void renderRecord();
    void renderImu();
    void renderPlaceholder();
    void updateRecord();
    void startRecordPage();
    void stopRecordPage();
    void toggleRecording();
    void startRecordPlayback();
    void stopRecordPlayback();
    void drawSystemBar();
    void drawKeyboardBar();
    void drawIconTile(int item_index, int x, int y, bool active);
    void drawIconImage(int item_index, int x, int y, bool active);
    void dumpRecordBuffer();
    void drawCenteredText(int x, int y, int w, const char* text, LcdSt7789::Color color, int scale);
    void drawKeyboardPill(int y, const char* text, LcdSt7789::Color fill, LcdSt7789::Color text_color);
    void handleRotateEvent(const InputEvent& event);
    void openSelected();
    void closeApp();
    void sendChatPreset();
    void startWifiScan();
    void stopWifiScan();
    void performWifiScan();
    void probeSdcard();
    void appendChatLine(const std::string& text, LineKind kind);
    void setStatus(const char* text);
};

}  // namespace demo
