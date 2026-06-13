#include "lcd_st7789.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "sdkconfig.h"

namespace demo {
namespace {

constexpr const char* kTag       = "lcd";
constexpr spi_host_device_t kSpi = SPI2_HOST;

bool transDoneCallback(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t* edata, void* user_ctx)
{
    auto* lcd = static_cast<LcdSt7789*>(user_ctx);
    BaseType_t high_task_wakeup = pdFALSE;
    lcd->notifyTransDoneFromIsr(&high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

#if CONFIG_LCD_BACKLIGHT_ACTIVE_HIGH
constexpr int kBacklightOn  = 1;
constexpr int kBacklightOff = 0;
#else
constexpr int kBacklightOn  = 0;
constexpr int kBacklightOff = 1;
#endif

#if CONFIG_LCD_BGR_COLOR
constexpr lcd_rgb_element_order_t kRgbElementOrder = LCD_RGB_ELEMENT_ORDER_BGR;
#else
constexpr lcd_rgb_element_order_t kRgbElementOrder = LCD_RGB_ELEMENT_ORDER_RGB;
#endif

#if CONFIG_LCD_INVERT_COLOR
constexpr bool kInvertColor = true;
#else
constexpr bool kInvertColor = false;
#endif

#if CONFIG_LCD_SWAP_XY
constexpr bool kSwapXy = true;
#else
constexpr bool kSwapXy = false;
#endif

#if CONFIG_LCD_MIRROR_X
constexpr bool kMirrorX = true;
#else
constexpr bool kMirrorX = false;
#endif

#if CONFIG_LCD_MIRROR_Y
constexpr bool kMirrorY = true;
#else
constexpr bool kMirrorY = false;
#endif

esp_err_t configureOutputPin(int pin)
{
    if (pin < 0) {
        return ESP_OK;
    }

    gpio_config_t config = {};
    config.mode          = GPIO_MODE_OUTPUT;
    config.pin_bit_mask  = 1ULL << static_cast<unsigned>(pin);
    return gpio_config(&config);
}

std::array<std::uint8_t, 7> glyphFor(char c)
{
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    switch (c) {
        case ' ':
            return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        case '0':
            return {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E};
        case '1':
            return {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E};
        case '2':
            return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F};
        case '3':
            return {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E};
        case '4':
            return {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02};
        case '5':
            return {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E};
        case '6':
            return {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E};
        case '7':
            return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08};
        case '8':
            return {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E};
        case '9':
            return {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C};
        case 'A':
            return {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
        case 'B':
            return {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E};
        case 'C':
            return {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
        case 'D':
            return {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E};
        case 'E':
            return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F};
        case 'F':
            return {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10};
        case 'G':
            return {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F};
        case 'H':
            return {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
        case 'I':
            return {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};
        case 'J':
            return {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C};
        case 'K':
            return {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11};
        case 'L':
            return {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F};
        case 'M':
            return {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11};
        case 'N':
            return {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11};
        case 'O':
            return {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
        case 'P':
            return {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};
        case 'Q':
            return {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D};
        case 'R':
            return {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11};
        case 'S':
            return {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E};
        case 'T':
            return {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04};
        case 'U':
            return {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E};
        case 'V':
            return {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04};
        case 'W':
            return {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A};
        case 'X':
            return {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11};
        case 'Y':
            return {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04};
        case 'Z':
            return {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F};
        case '-':
            return {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
        case '/':
            return {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10};
        case ':':
            return {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00};
        case '.':
            return {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
        case '!':
            return {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04};
        case '?':
            return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
        case '+':
            return {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
        case '<':
            return {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02};
        case '>':
            return {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08};
        case '_':
            return {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F};
        case '(':
            return {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02};
        case ')':
            return {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08};
        default:
            return {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04};
    }
}

}  // namespace

esp_err_t LcdSt7789::init()
{
    ESP_LOGI(kTag, "init ST7789 320x240");
    ESP_LOGI(kTag, "pins mosi=%d sclk=%d cs=%d dc=%d rst=%d bl=%d", CONFIG_LCD_PIN_MOSI, CONFIG_LCD_PIN_SCLK,
             CONFIG_LCD_PIN_CS, CONFIG_LCD_PIN_DC, CONFIG_LCD_PIN_RST, CONFIG_LCD_PIN_BL);

    frame_ = static_cast<Color*>(heap_caps_malloc(kWidth * kHeight * sizeof(Color), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (!frame_) {
        ESP_LOGE(kTag, "failed to allocate %u byte DMA framebuffer",
                 static_cast<unsigned>(kWidth * kHeight * sizeof(Color)));
        return ESP_ERR_NO_MEM;
    }
    clear(rgb(8, 10, 14));

    trans_done_sem_ = xSemaphoreCreateBinary();
    if (!trans_done_sem_) {
        ESP_LOGE(kTag, "failed to create transfer done semaphore");
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(trans_done_sem_, 0);

    esp_err_t err = configureOutputPin(CONFIG_LCD_PIN_BL);
    if (err != ESP_OK) {
        return err;
    }
    if (CONFIG_LCD_PIN_BL >= 0) {
        gpio_set_level(static_cast<gpio_num_t>(CONFIG_LCD_PIN_BL), kBacklightOff);
    }

    spi_bus_config_t bus_config = {};
    bus_config.sclk_io_num     = CONFIG_LCD_PIN_SCLK;
    bus_config.mosi_io_num     = CONFIG_LCD_PIN_MOSI;
    bus_config.miso_io_num     = -1;
    bus_config.quadwp_io_num   = -1;
    bus_config.quadhd_io_num   = -1;
    bus_config.max_transfer_sz = kWidth * kHeight * sizeof(Color);

    err = spi_bus_initialize(kSpi, &bus_config, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num                   = CONFIG_LCD_PIN_DC;
    io_config.cs_gpio_num                   = CONFIG_LCD_PIN_CS;
    io_config.pclk_hz                       = CONFIG_LCD_SPI_CLOCK_HZ;
    io_config.lcd_cmd_bits                  = 8;
    io_config.lcd_param_bits                = 8;
    io_config.spi_mode                      = 0;
    io_config.trans_queue_depth             = 1;

    err = esp_lcd_new_panel_io_spi(static_cast<esp_lcd_spi_bus_handle_t>(kSpi), &io_config, &io_);
    if (err != ESP_OK) {
        return err;
    }

    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = transDoneCallback,
    };
    esp_lcd_panel_io_register_event_callbacks(io_, &cbs, this);

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num             = CONFIG_LCD_PIN_RST;
    panel_config.rgb_ele_order = kRgbElementOrder;
    panel_config.bits_per_pixel = 16;

    err = esp_lcd_new_panel_st7789(io_, &panel_config, &panel_);
    if (err != ESP_OK) {
        return err;
    }

    if ((err = esp_lcd_panel_reset(panel_)) != ESP_OK) {
        return err;
    }
    if ((err = esp_lcd_panel_init(panel_)) != ESP_OK) {
        return err;
    }
    if ((err = esp_lcd_panel_invert_color(panel_, kInvertColor)) != ESP_OK) {
        return err;
    }
    if ((err = esp_lcd_panel_swap_xy(panel_, kSwapXy)) != ESP_OK) {
        return err;
    }
    if ((err = esp_lcd_panel_mirror(panel_, kMirrorX, kMirrorY)) != ESP_OK) {
        return err;
    }
    if ((err = esp_lcd_panel_set_gap(panel_, CONFIG_LCD_GAP_X, CONFIG_LCD_GAP_Y)) != ESP_OK) {
        return err;
    }
    if ((err = esp_lcd_panel_disp_on_off(panel_, true)) != ESP_OK) {
        return err;
    }

    initialized_ = true;
    err          = flush();
    if (err != ESP_OK) {
        return err;
    }

    if (CONFIG_LCD_PIN_BL >= 0) {
        gpio_set_level(static_cast<gpio_num_t>(CONFIG_LCD_PIN_BL), kBacklightOn);
    }

    return ESP_OK;
}

void LcdSt7789::notifyTransDoneFromIsr(BaseType_t* pxHigherPriorityTaskWoken)
{
    if (trans_done_sem_) {
        xSemaphoreGiveFromISR(trans_done_sem_, pxHigherPriorityTaskWoken);
    }
}

esp_err_t LcdSt7789::flush()
{
    if (!initialized_ || !panel_ || !frame_ || !trans_done_sem_) {
        return ESP_ERR_INVALID_STATE;
    }

    // Drain any stale signal before starting a new transfer.
    xSemaphoreTake(trans_done_sem_, 0);

    const esp_err_t err = esp_lcd_panel_draw_bitmap(panel_, 0, 0, kWidth, kHeight, frame_);
    if (err != ESP_OK) {
        return err;
    }

    // Wait for the DMA color transfer to finish before allowing the next frame
    // to overwrite the framebuffer. This avoids tearing.
    if (xSemaphoreTake(trans_done_sem_, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(kTag, "flush timed out waiting for DMA done");
    }

    return ESP_OK;
}

void LcdSt7789::clear(Color color)
{
    if (!frame_) {
        return;
    }
    std::fill(frame_, frame_ + kWidth * kHeight, color);
}

void LcdSt7789::drawPixel(int x, int y, Color color)
{
    if (!frame_ || x < 0 || y < 0 || x >= kWidth || y >= kHeight) {
        return;
    }
    frame_[y * kWidth + x] = color;
}

void LcdSt7789::drawLine(int x0, int y0, int x1, int y1, Color color)
{
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err      = dx + dy;

    while (true) {
        drawPixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void LcdSt7789::drawRect(int x, int y, int w, int h, Color color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    drawLine(x, y, x + w - 1, y, color);
    drawLine(x, y + h - 1, x + w - 1, y + h - 1, color);
    drawLine(x, y, x, y + h - 1, color);
    drawLine(x + w - 1, y, x + w - 1, y + h - 1, color);
}

void LcdSt7789::fillRect(int x, int y, int w, int h, Color color)
{
    if (!frame_ || w <= 0 || h <= 0) {
        return;
    }

    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(kWidth, x + w);
    const int y1 = std::min(kHeight, y + h);
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    for (int row = y0; row < y1; ++row) {
        std::fill(frame_ + row * kWidth + x0, frame_ + row * kWidth + x1, color);
    }
}

void LcdSt7789::drawRgb565Image(int x, int y, int w, int h, const std::uint16_t* data)
{
    if (!frame_ || !data || w <= 0 || h <= 0) {
        return;
    }

    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(kWidth, x + w);
    const int y1 = std::min(kHeight, y + h);
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    for (int row = y0; row < y1; ++row) {
        for (int col = x0; col < x1; ++col) {
            const std::uint16_t source = data[(row - y) * w + (col - x)];
            frame_[row * kWidth + col] = source;
        }
    }
}

void LcdSt7789::drawRgb565ImageScaled(int x, int y, int src_w, int src_h, int dst_w, int dst_h,
                                      const std::uint16_t* data)
{
    if (!frame_ || !data || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
        return;
    }

    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(kWidth, x + dst_w);
    const int y1 = std::min(kHeight, y + dst_h);
    if (x0 >= x1 || y0 >= y1) {
        return;
    }

    for (int row = y0; row < y1; ++row) {
        const int src_y = (row - y) * src_h / dst_h;
        for (int col = x0; col < x1; ++col) {
            const int src_x          = (col - x) * src_w / dst_w;
            frame_[row * kWidth + col] = data[src_y * src_w + src_x];
        }
    }
}

void LcdSt7789::drawCircle(int cx, int cy, int radius, Color color)
{
    if (radius <= 0) {
        return;
    }

    int x   = radius;
    int y   = 0;
    int err = 0;
    while (x >= y) {
        drawPixel(cx + x, cy + y, color);
        drawPixel(cx + y, cy + x, color);
        drawPixel(cx - y, cy + x, color);
        drawPixel(cx - x, cy + y, color);
        drawPixel(cx - x, cy - y, color);
        drawPixel(cx - y, cy - x, color);
        drawPixel(cx + y, cy - x, color);
        drawPixel(cx + x, cy - y, color);
        ++y;
        if (err <= 0) {
            err += 2 * y + 1;
        }
        if (err > 0) {
            --x;
            err -= 2 * x + 1;
        }
    }
}

void LcdSt7789::fillCircle(int cx, int cy, int radius, Color color)
{
    if (radius <= 0) {
        return;
    }
    const int r2 = radius * radius;
    for (int y = -radius; y <= radius; ++y) {
        for (int x = -radius; x <= radius; ++x) {
            if (x * x + y * y <= r2) {
                drawPixel(cx + x, cy + y, color);
            }
        }
    }
}

void LcdSt7789::fillRoundRect(int x, int y, int w, int h, int radius, Color color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    radius = std::max(0, std::min(radius, std::min(w, h) / 2));
    fillRect(x + radius, y, w - radius * 2, h, color);
    fillRect(x, y + radius, w, h - radius * 2, color);
    fillCircle(x + radius, y + radius, radius, color);
    fillCircle(x + w - radius - 1, y + radius, radius, color);
    fillCircle(x + radius, y + h - radius - 1, radius, color);
    fillCircle(x + w - radius - 1, y + h - radius - 1, radius, color);
}

void LcdSt7789::drawRoundRect(int x, int y, int w, int h, int radius, Color color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    radius = std::max(0, std::min(radius, std::min(w, h) / 2));
    drawLine(x + radius, y, x + w - radius - 1, y, color);
    drawLine(x + radius, y + h - 1, x + w - radius - 1, y + h - 1, color);
    drawLine(x, y + radius, x, y + h - radius - 1, color);
    drawLine(x + w - 1, y + radius, x + w - 1, y + h - radius - 1, color);
    drawCircle(x + radius, y + radius, radius, color);
    drawCircle(x + w - radius - 1, y + radius, radius, color);
    drawCircle(x + radius, y + h - radius - 1, radius, color);
    drawCircle(x + w - radius - 1, y + h - radius - 1, radius, color);
}

void LcdSt7789::drawChar(int x, int y, char c, Color color, int scale)
{
    if (scale < 1) {
        scale = 1;
    }

    const auto glyph = glyphFor(c);
    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            if ((glyph[row] & (1 << (4 - col))) != 0) {
                fillRect(x + col * scale, y + row * scale, scale, scale, color);
            }
        }
    }
}

void LcdSt7789::drawText(int x, int y, const char* text, Color color, int scale)
{
    if (!text) {
        return;
    }
    if (scale < 1) {
        scale = 1;
    }

    const int start_x = x;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p == '\n') {
            x = start_x;
            y += 8 * scale;
            continue;
        }
        drawChar(x, y, *p, color, scale);
        x += 6 * scale;
    }
}

int LcdSt7789::measureText(const char* text, int scale) const
{
    if (!text) {
        return 0;
    }
    if (scale < 1) {
        scale = 1;
    }

    int width     = 0;
    int max_width = 0;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p == '\n') {
            max_width = std::max(max_width, width);
            width     = 0;
            continue;
        }
        width += 6 * scale;
    }
    return std::max(max_width, width > 0 ? width - scale : 0);
}

}  // namespace demo
