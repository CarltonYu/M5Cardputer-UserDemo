#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

namespace demo {

class EspNowChat {
public:
    static constexpr std::size_t kMaxPayload = 230;

    esp_err_t init();
    esp_err_t send(const char* text);
    bool receive(std::string* text);

    bool ready() const { return initialized_; }
    esp_err_t lastError() const { return last_error_; }
    void enqueue(const std::uint8_t* data, int length);

private:
    struct RxPacket {
        std::uint8_t length = 0;
        char text[kMaxPayload + 1] = {};
    };

    struct MagicCacheEntry {
        bool valid = false;
        std::uint8_t type = 0;
        std::uint16_t magic = 0;
        std::uint8_t src_addr[6] = {};
    };

    QueueHandle_t rx_queue_    = nullptr;
    bool initialized_          = false;
    esp_err_t last_error_      = ESP_OK;
    MagicCacheEntry magic_cache_[8] = {};
    std::uint8_t magic_cache_next_ = 0;

    bool isDuplicate(std::uint8_t type, std::uint16_t magic, const std::uint8_t* src_addr);
};

}  // namespace demo
