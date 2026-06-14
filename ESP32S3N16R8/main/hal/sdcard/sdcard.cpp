#include "sdcard.h"

#include <cstdio>
#include <cstring>

#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

namespace demo {

namespace {
constexpr const char* kTag        = "sdcard";
constexpr const char* kMountPoint = "/sdcard";
}  // namespace

SdCard::~SdCard()
{
    deinit();
}

esp_err_t SdCard::init()
{
    if (mounted_) {
        return ESP_OK;
    }

    esp_err_t err = initSpiBus();
    if (err != ESP_OK) {
        return err;
    }

    return mount();
}

SdCard::Info SdCard::probe()
{
    Info info;

    if (!mounted_) {
        if (init() != ESP_OK) {
            info.size = "Not Found";
            return info;
        }
    }

    if (!mounted_ || card_ == nullptr) {
        info.size = "Not Found";
        return info;
    }

    auto* sd_card = static_cast<sdmmc_card_t*>(card_);

    info.mounted = true;
    info.name    = std::string(sd_card->cid.name);

    const std::uint64_t capacity_bytes =
        static_cast<std::uint64_t>(sd_card->csd.capacity) * sd_card->csd.sector_size;
    const float capacity_gb = static_cast<float>(capacity_bytes) / (1024.0f * 1024.0f * 1024.0f);
    char size_buf[32]       = {};
    std::snprintf(size_buf, sizeof(size_buf), "%.1f GB", capacity_gb);
    info.size = size_buf;

    if (sd_card->is_sdio) {
        info.type = "SDIO";
    } else if (sd_card->is_mmc) {
        info.type = "MMC";
    } else {
        info.type = (sd_card->ocr & (1U << 30)) ? "SDHC/SDXC" : "SDSC";
    }

    return info;
}

void SdCard::deinit()
{
    if (mounted_ && card_ != nullptr) {
        esp_vfs_fat_sdcard_unmount(kMountPoint, static_cast<sdmmc_card_t*>(card_));
        mounted_ = false;
        card_    = nullptr;
    }
    // Keep the SPI bus initialized; it is shared with the LCD.
}

esp_err_t SdCard::initSpiBus()
{
    if (spi_initialized_) {
        return ESP_OK;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num     = CONFIG_LCD_PIN_MOSI;
    bus_cfg.miso_io_num     = CONFIG_SD_PIN_MISO;
    bus_cfg.sclk_io_num     = CONFIG_LCD_PIN_SCLK;
    bus_cfg.quadwp_io_num   = -1;
    bus_cfg.quadhd_io_num   = -1;
    bus_cfg.max_transfer_sz = 4096;

    esp_err_t err = spi_bus_initialize(static_cast<spi_host_device_t>(host.slot), &bus_cfg, SDSPI_DEFAULT_DMA);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    spi_initialized_ = true;
    ESP_LOGI(kTag, "SPI bus ready for SD card (shared with LCD)");
    return ESP_OK;
}

esp_err_t SdCard::mount()
{
    if (mounted_) {
        return ESP_OK;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = CONFIG_SD_SPI_CLOCK_HZ / 1000;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed           = false;
    mount_config.max_files                        = 5;
    mount_config.allocation_unit_size             = 16 * 1024;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs               = static_cast<gpio_num_t>(CONFIG_SD_PIN_CS);
    slot_config.host_id               = static_cast<spi_host_device_t>(host.slot);

    sdmmc_card_t* card = nullptr;
    esp_err_t err      = esp_vfs_fat_sdspi_mount(kMountPoint, &host, &slot_config, &mount_config, &card);
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(kTag, "failed to mount filesystem");
        } else {
            ESP_LOGE(kTag, "failed to initialize SD card: %s", esp_err_to_name(err));
        }
        return err;
    }

    card_    = card;
    mounted_ = true;
    ESP_LOGI(kTag, "SD card mounted at %s", kMountPoint);
    return ESP_OK;
}

}  // namespace demo
