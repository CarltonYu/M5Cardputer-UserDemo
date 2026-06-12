#pragma once

#include <cstddef>
#include <cstdint>

#include "esp_err.h"
#include "esp_lcd_types.h"

namespace demo {

class LcdSt7789 {
public:
    using Color = std::uint16_t;

    static constexpr int kWidth  = 320;
    static constexpr int kHeight = 240;

    static constexpr Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b)
    {
        const std::uint16_t color = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        return static_cast<Color>((color >> 8) | (color << 8));
    }

    esp_err_t init();
    esp_err_t flush();

    void clear(Color color);
    void drawPixel(int x, int y, Color color);
    void drawLine(int x0, int y0, int x1, int y1, Color color);
    void drawRect(int x, int y, int w, int h, Color color);
    void fillRect(int x, int y, int w, int h, Color color);
    void drawRgb565Image(int x, int y, int w, int h, const std::uint16_t* data);
    void drawCircle(int cx, int cy, int radius, Color color);
    void fillCircle(int cx, int cy, int radius, Color color);
    void fillRoundRect(int x, int y, int w, int h, int radius, Color color);
    void drawRoundRect(int x, int y, int w, int h, int radius, Color color);

    void drawChar(int x, int y, char c, Color color, int scale = 1);
    void drawText(int x, int y, const char* text, Color color, int scale = 1);
    int measureText(const char* text, int scale = 1) const;

private:
    esp_lcd_panel_io_handle_t io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    Color* frame_                 = nullptr;
    bool initialized_             = false;
};

}  // namespace demo
