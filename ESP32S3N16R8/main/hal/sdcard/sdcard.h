#pragma once

#include <cstdint>
#include <string>

#include "esp_err.h"

namespace demo {

// Minimal SDSPI / FATFS wrapper for the ESP32-S3-N16R8 port.
class SdCard {
public:
    struct Info {
        bool mounted     = false;
        std::string name;
        std::string size;
        std::string type;
    };

    SdCard() = default;
    ~SdCard();

    // Disable copy because of the underlying sdmmc_card_t handle.
    SdCard(const SdCard&)            = delete;
    SdCard& operator=(const SdCard&) = delete;

    // Initialize the shared SPI bus (if needed) and mount the card.
    esp_err_t init();

    // Return true if the filesystem is currently mounted.
    bool isMounted() const { return mounted_; }

    // Probe the card: try to mount if not already mounted and return info.
    Info probe();

    // Unmount and release resources.
    void deinit();

private:
    bool spi_initialized_ = false;
    bool mounted_         = false;
    void* card_           = nullptr;  // sdmmc_card_t*

    esp_err_t initSpiBus();
    esp_err_t mount();
};

}  // namespace demo
