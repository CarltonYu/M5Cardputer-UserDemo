#include "espnow_chat.h"

#include <algorithm>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace demo {
namespace {

constexpr const char* kTag = "espnow_chat";
constexpr std::uint8_t kBroadcastPeer[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
constexpr std::uint8_t kEspnowComponentVersion          = 2;
constexpr std::uint8_t kEspnowComponentDataTypeData     = 10;

EspNowChat* g_active_chat = nullptr;

struct EspnowComponentFrameHead {
    std::uint16_t magic;
    std::uint8_t channel              : 4;
    bool filter_adjacent_channel      : 1;
    bool filter_weak_signal           : 1;
    bool security                     : 1;
    std::uint16_t                     : 4;
    bool broadcast                    : 1;
    bool group                        : 1;
    bool ack                          : 1;
    std::uint16_t retransmit_count    : 5;
    std::uint8_t forward_ttl          : 5;
    std::int8_t forward_rssi          : 8;
} __attribute__((packed));

struct EspnowComponentDataHeader {
    std::uint8_t type    : 4;
    std::uint8_t version : 2;
    std::uint8_t         : 2;
    std::uint8_t size;
    EspnowComponentFrameHead frame_head;
    std::uint8_t dest_addr[ESP_NOW_ETH_ALEN];
    std::uint8_t src_addr[ESP_NOW_ETH_ALEN];
    std::uint8_t payload[0];
} __attribute__((packed));

static_assert(sizeof(EspnowComponentFrameHead) == 6, "Unexpected ESP-NOW component frame-head layout");
static_assert(sizeof(EspnowComponentDataHeader) == 20, "Unexpected ESP-NOW component data layout");

std::size_t boundedLength(const char* text)
{
    std::size_t length = 0;
    while (text && text[length] != '\0' && length < EspNowChat::kMaxPayload) {
        ++length;
    }
    return length;
}

esp_err_t initNvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t okOrAlready(esp_err_t err)
{
    return err == ESP_ERR_INVALID_STATE ? ESP_OK : err;
}

void onReceive(const esp_now_recv_info_t* info, const std::uint8_t* data, int length)
{
    (void)info;
    if (!g_active_chat || !data || length <= 0) {
        return;
    }
    g_active_chat->enqueue(data, length);
}

}  // namespace

esp_err_t EspNowChat::init()
{
    if (initialized_) {
        return ESP_OK;
    }

    if (!rx_queue_) {
        rx_queue_ = xQueueCreate(8, sizeof(RxPacket));
        if (!rx_queue_) {
            last_error_ = ESP_ERR_NO_MEM;
            return last_error_;
        }
    }

    esp_err_t err = initNvs();
    if (err != ESP_OK) {
        last_error_ = err;
        return err;
    }

    err = okOrAlready(esp_netif_init());
    if (err != ESP_OK) {
        last_error_ = err;
        return err;
    }

    err = okOrAlready(esp_event_loop_create_default());
    if (err != ESP_OK) {
        last_error_ = err;
        return err;
    }

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();
    err = okOrAlready(esp_wifi_init(&wifi_config));
    if (err != ESP_OK) {
        last_error_ = err;
        return err;
    }

    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT) {
        last_error_ = err;
        return err;
    }

    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK) {
        last_error_ = err;
        return err;
    }
    if ((err = esp_wifi_start()) != ESP_OK && err != ESP_ERR_WIFI_CONN) {
        last_error_ = err;
        return err;
    }
    if ((err = esp_wifi_set_channel(CONFIG_ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE)) != ESP_OK) {
        last_error_ = err;
        return err;
    }

    err = esp_now_init();
    if (err != ESP_OK) {
        last_error_ = err;
        return err;
    }

    g_active_chat = this;
    if ((err = esp_now_register_recv_cb(onReceive)) != ESP_OK) {
        last_error_ = err;
        return err;
    }

    esp_now_peer_info_t peer = {};
    std::memcpy(peer.peer_addr, kBroadcastPeer, sizeof(kBroadcastPeer));
    peer.channel = CONFIG_ESPNOW_CHANNEL;
    peer.ifidx   = WIFI_IF_STA;
    peer.encrypt = false;

    err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST) {
        last_error_ = err;
        return err;
    }

    initialized_ = true;
    last_error_  = ESP_OK;
    ESP_LOGI(kTag, "ESP-NOW chat ready on channel %d", CONFIG_ESPNOW_CHANNEL);
    return ESP_OK;
}

esp_err_t EspNowChat::send(const char* text)
{
    if (!text || text[0] == '\0') {
        last_error_ = ESP_ERR_INVALID_ARG;
        return last_error_;
    }

    esp_err_t err = init();
    if (err != ESP_OK) {
        return err;
    }

    const std::size_t length = boundedLength(text);
    std::uint8_t packet[sizeof(EspnowComponentDataHeader) + kMaxPayload] = {};
    auto* header = reinterpret_cast<EspnowComponentDataHeader*>(packet);

    header->type    = kEspnowComponentDataTypeData;
    header->version = kEspnowComponentVersion;
    header->size    = static_cast<std::uint8_t>(length);
    header->frame_head.magic            = static_cast<std::uint16_t>(esp_random());
    header->frame_head.broadcast        = true;
    header->frame_head.retransmit_count = 1;
    if (header->frame_head.magic == 0) {
        header->frame_head.magic = 1;
    }

    std::memcpy(header->dest_addr, kBroadcastPeer, sizeof(header->dest_addr));
    if (esp_wifi_get_mac(WIFI_IF_STA, header->src_addr) != ESP_OK) {
        std::memset(header->src_addr, 0, sizeof(header->src_addr));
    }
    std::memcpy(header->payload, text, length);

    err = esp_now_send(kBroadcastPeer, packet, sizeof(EspnowComponentDataHeader) + length);
    last_error_ = err;
    return err;
}

bool EspNowChat::receive(std::string* text)
{
    if (!rx_queue_) {
        return false;
    }

    RxPacket packet{};
    if (xQueueReceive(rx_queue_, &packet, 0) != pdTRUE) {
        return false;
    }

    if (text) {
        text->assign(packet.text, packet.length);
    }
    return true;
}

void EspNowChat::enqueue(const std::uint8_t* data, int length)
{
    if (!rx_queue_ || !data || length <= 0) {
        return;
    }

    const std::uint8_t* payload = data;
    int payload_length          = length;

    if (length >= static_cast<int>(sizeof(EspnowComponentDataHeader))) {
        const auto* header = reinterpret_cast<const EspnowComponentDataHeader*>(data);
        const int expected_length = static_cast<int>(sizeof(EspnowComponentDataHeader)) + header->size;
        if (header->version == kEspnowComponentVersion && expected_length == length) {
            if (header->type != kEspnowComponentDataTypeData) {
                return;
            }
            if (isDuplicate(header->type, header->frame_head.magic, header->src_addr)) {
                return;
            }
            payload        = header->payload;
            payload_length = header->size;
        }
    }

    if (payload_length <= 0) {
        return;
    }

    RxPacket packet{};
    packet.length = static_cast<std::uint8_t>(std::min<int>(payload_length, kMaxPayload));
    std::memcpy(packet.text, payload, packet.length);
    packet.text[packet.length] = '\0';

    if (xQueueSend(rx_queue_, &packet, 0) != pdTRUE) {
        RxPacket dropped{};
        (void)xQueueReceive(rx_queue_, &dropped, 0);
        (void)xQueueSend(rx_queue_, &packet, 0);
    }
}

bool EspNowChat::isDuplicate(std::uint8_t type, std::uint16_t magic, const std::uint8_t* src_addr)
{
    if (magic == 0 || !src_addr) {
        return false;
    }

    for (const MagicCacheEntry& entry : magic_cache_) {
        if (entry.valid && entry.type == type && entry.magic == magic &&
            std::memcmp(entry.src_addr, src_addr, ESP_NOW_ETH_ALEN) == 0) {
            return true;
        }
    }

    MagicCacheEntry& entry = magic_cache_[magic_cache_next_];
    entry.valid            = true;
    entry.type             = type;
    entry.magic            = magic;
    std::memcpy(entry.src_addr, src_addr, ESP_NOW_ETH_ALEN);
    magic_cache_next_ = (magic_cache_next_ + 1) % (sizeof(magic_cache_) / sizeof(magic_cache_[0]));
    return false;
}

}  // namespace demo
