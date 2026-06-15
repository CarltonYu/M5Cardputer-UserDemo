#include "i2s_audio.h"

#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

namespace demo {

namespace {
constexpr const char* kTag = "i2s_audio";

i2s_port_t audioPort()
{
    return static_cast<i2s_port_t>(CONFIG_AUDIO_I2S_PORT);
}

i2s_std_config_t buildStdConfig()
{
    i2s_std_config_t cfg = {};
    cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(CONFIG_AUDIO_SAMPLE_RATE);
    cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
    cfg.gpio_cfg.mclk        = static_cast<gpio_num_t>(CONFIG_AUDIO_I2S_MCLK_GPIO);
    cfg.gpio_cfg.bclk        = static_cast<gpio_num_t>(CONFIG_AUDIO_I2S_BCLK_GPIO);
    cfg.gpio_cfg.ws          = static_cast<gpio_num_t>(CONFIG_AUDIO_I2S_WS_GPIO);
    cfg.gpio_cfg.dout        = static_cast<gpio_num_t>(CONFIG_AUDIO_I2S_DOUT_GPIO);
    cfg.gpio_cfg.din         = static_cast<gpio_num_t>(CONFIG_AUDIO_I2S_DIN_GPIO);
    cfg.gpio_cfg.invert_flags.mclk_inv = false;
    cfg.gpio_cfg.invert_flags.bclk_inv = false;
    cfg.gpio_cfg.invert_flags.ws_inv   = false;
    return cfg;
}

i2s_chan_config_t buildChanConfig()
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(audioPort(), I2S_ROLE_MASTER);
    chan_cfg.auto_clear        = true;
    chan_cfg.dma_desc_num      = 4;
    chan_cfg.dma_frame_num     = (CONFIG_AUDIO_SAMPLE_RATE / 1000) * 15;
    return chan_cfg;
}
}  // namespace

esp_err_t I2sAudio::init()
{
    if (initialized_) {
        return ESP_OK;
    }

    i2s_chan_config_t chan_cfg = buildChanConfig();
    i2s_chan_handle_t tx = nullptr;
    i2s_chan_handle_t rx = nullptr;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx, &rx), kTag, "i2s_new_channel failed");

    i2s_std_config_t std_cfg = buildStdConfig();
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &std_cfg), kTag, "TX init failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx, &std_cfg), kTag, "RX init failed");

    tx_handle_ = tx;
    rx_handle_ = rx;
    tx_enabled_ = false;
    rx_enabled_ = false;
    initialized_ = true;
    ESP_LOGI(kTag, "I2S audio ready: MCLK=%d BCLK=%d WS=%d DIN=%d DOUT=%d @%d Hz stereo/32-bit-slot",
             CONFIG_AUDIO_I2S_MCLK_GPIO, CONFIG_AUDIO_I2S_BCLK_GPIO, CONFIG_AUDIO_I2S_WS_GPIO,
             CONFIG_AUDIO_I2S_DIN_GPIO, CONFIG_AUDIO_I2S_DOUT_GPIO, CONFIG_AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

void I2sAudio::deinit()
{
    stopRecording();
    stopPlayback();

    if (tx_handle_ != nullptr) {
        i2s_del_channel(static_cast<i2s_chan_handle_t>(tx_handle_));
        tx_handle_ = nullptr;
    }
    if (rx_handle_ != nullptr) {
        i2s_del_channel(static_cast<i2s_chan_handle_t>(rx_handle_));
        rx_handle_ = nullptr;
    }
    tx_enabled_ = false;
    rx_enabled_ = false;
    initialized_ = false;
}

esp_err_t I2sAudio::startRecording()
{
    if (!initialized_ || rx_handle_ == nullptr || tx_handle_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (recording_) {
        return ESP_OK;
    }

    // Stop active playback audio, but keep the TX channel enabled so the
    // shared BCLK/MCLK keep running for the RX channel.
    playing_ = false;

    if (!tx_enabled_) {
        ESP_RETURN_ON_ERROR(i2s_channel_enable(static_cast<i2s_chan_handle_t>(tx_handle_)), kTag,
                            "TX enable failed");
        tx_enabled_ = true;
    }
    if (!rx_enabled_) {
        ESP_RETURN_ON_ERROR(i2s_channel_enable(static_cast<i2s_chan_handle_t>(rx_handle_)), kTag,
                            "RX enable failed");
        rx_enabled_ = true;
    }
    recording_ = true;
    ESP_LOGI(kTag, "recording started");
    return ESP_OK;
}

int I2sAudio::readRecordBuffer(std::int16_t* buffer, std::size_t samples)
{
    if (!recording_ || rx_handle_ == nullptr || buffer == nullptr || samples == 0) {
        return 0;
    }

    if (stereo_buffer_.size() < samples * 2) {
        stereo_buffer_.resize(samples * 2);
    }

    std::size_t bytes_read = 0;
    esp_err_t err =
        i2s_channel_read(static_cast<i2s_chan_handle_t>(rx_handle_), stereo_buffer_.data(),
                         samples * 2 * sizeof(std::int16_t), &bytes_read, pdMS_TO_TICKS(100));
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        ESP_LOGE(kTag, "RX read failed: %s", esp_err_to_name(err));
        return -1;
    }

    std::size_t stereo_samples = bytes_read / (2 * sizeof(std::int16_t));
    if (stereo_samples == 0) {
        static int s_empty_count = 0;
        if (++s_empty_count <= 10) {
            ESP_LOGW(kTag, "RX empty (bytes_read=%u)", static_cast<unsigned>(bytes_read));
        }
    }
    for (std::size_t i = 0; i < stereo_samples; ++i) {
        // ES8311 is mono and outputs on the left channel of the stereo slot.
        buffer[i] = stereo_buffer_[2 * i];
    }
    return static_cast<int>(stereo_samples);
}

void I2sAudio::stopRecording()
{
    if (rx_handle_ != nullptr && rx_enabled_) {
        i2s_channel_disable(static_cast<i2s_chan_handle_t>(rx_handle_));
        rx_enabled_ = false;
    }
    recording_ = false;
}

esp_err_t I2sAudio::startPlayback()
{
    if (!initialized_ || tx_handle_ == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (playing_) {
        return ESP_OK;
    }

    // Disable RX when switching to playback; leave TX enabled.
    if (recording_ && rx_handle_ != nullptr && rx_enabled_) {
        i2s_channel_disable(static_cast<i2s_chan_handle_t>(rx_handle_));
        rx_enabled_ = false;
        recording_ = false;
    }

    if (!tx_enabled_) {
        ESP_RETURN_ON_ERROR(i2s_channel_enable(static_cast<i2s_chan_handle_t>(tx_handle_)), kTag,
                            "TX enable failed");
        tx_enabled_ = true;
    }
    playing_ = true;
    ESP_LOGI(kTag, "playback started");
    return ESP_OK;
}

int I2sAudio::writePlayBuffer(const std::int16_t* buffer, std::size_t samples)
{
    if (!playing_ || tx_handle_ == nullptr || buffer == nullptr || samples == 0) {
        return 0;
    }

    if (stereo_buffer_.size() < samples * 2) {
        stereo_buffer_.resize(samples * 2);
    }
    for (std::size_t i = 0; i < samples; ++i) {
        stereo_buffer_[2 * i]     = buffer[i];
        stereo_buffer_[2 * i + 1] = buffer[i];
    }

    std::size_t bytes_written = 0;
    esp_err_t err =
        i2s_channel_write(static_cast<i2s_chan_handle_t>(tx_handle_), stereo_buffer_.data(),
                          samples * 2 * sizeof(std::int16_t), &bytes_written, pdMS_TO_TICKS(100));
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        ESP_LOGE(kTag, "TX write failed: %s", esp_err_to_name(err));
        return -1;
    }
    return static_cast<int>(bytes_written / (2 * sizeof(std::int16_t)));
}

void I2sAudio::stopPlayback()
{
    // Keep the TX channel enabled.  The driver's auto_clear feature will send
    // zeros when no data is written, which keeps the line quiet and avoids the
    // pops/clicks caused by repeatedly enabling/disabling the channel.
    playing_ = false;
}

}  // namespace demo
