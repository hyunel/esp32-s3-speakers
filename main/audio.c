#include "audio.h"
#include "es8156.h"
#include "global.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "esp_check.h"

const static char* TAG = "audio";

static bool mI2sInitialized = false;
static i2s_chan_handle_t mHandleTx = NULL;

static esp_err_t init_i2s_driver(audio_stream_config_t *config)
{
    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = config->sample_rate_hz,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = AUDIO_MCLK_MULTIPLE,
        },
        .slot_cfg = {
            .data_bit_width = config->bits_per_sample,
            .slot_bit_width = config->bits_per_sample,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = config->bits_per_sample,
            .ws_pol = false,
            .bit_shift = true
        },
        .gpio_cfg = {
            .mclk = PIN_DAC_MCLK,
            .bclk = PIN_DAC_SCLK,
            .ws = PIN_DAC_LRCK,
            .dout = PIN_DAC_SDIN,
            .din = -1
        },
    };

    // Setup I2S peripheral
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // Auto clear the legacy data in the DMA buffer
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &mHandleTx, NULL), TAG, "i2s new channel failed");
    
    // Setup I2S channels
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(mHandleTx, &std_cfg), TAG, "i2s channel init failed");

    return ESP_OK;
}

esp_err_t audio_init() {
    // Initialize I2C bus
    const i2c_config_t es_i2c_cfg = {
        .sda_io_num = PIN_DAC_SDA,
        .scl_io_num = PIN_DAC_SCL,
        .mode = I2C_MODE_MASTER,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    const i2c_bus_handle_t i2c_bus = i2c_bus_create(I2C_NUM_0, &es_i2c_cfg);

    ESP_RETURN_ON_ERROR(es8156_codec_init(i2c_bus), TAG, "es8156 codec init failed");
    return audio_set_volume(AUDIO_VOLUME_DEFAULT);
}

esp_err_t audio_write(size_t size, void * data) {
    size_t bytes_written;
    ESP_RETURN_ON_ERROR(i2s_channel_write(mHandleTx, data, size, &bytes_written, portMAX_DELAY), TAG, "i2s channel write failed");
    if (bytes_written != size) {
        ESP_LOGW(TAG, "Failed to write all data to i2s channel");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t audio_stop() {
    return i2s_channel_disable(mHandleTx);
}

esp_err_t audio_start(audio_stream_config_t *config) {
    if(!mI2sInitialized) {
        ESP_RETURN_ON_ERROR(init_i2s_driver(config), TAG, "init i2s driver failed");
        mI2sInitialized = true;
    } else {
        i2s_std_clk_config_t clk_cfg = {
            .sample_rate_hz = config->sample_rate_hz,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = AUDIO_MCLK_MULTIPLE,
        };
        i2s_std_slot_config_t slot_cfg = {
            .data_bit_width = config->bits_per_sample,
            .slot_bit_width = config->bits_per_sample,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = config->bits_per_sample,
            .ws_pol = false,
            .bit_shift = true
        };

        ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_clock(mHandleTx, &clk_cfg), TAG, "i2s channel reconfig clock failed");
        ESP_RETURN_ON_ERROR(i2s_channel_reconfig_std_slot(mHandleTx, &slot_cfg), TAG, "i2s channel reconfig slot failed");
    }
    
    ESP_LOGD(TAG, "Starting audio stream with sample rate %lu Hz and %lu bits per sample", config->sample_rate_hz, config->bits_per_sample);
    return i2s_channel_enable(mHandleTx);
}


/**
 * @brief Set the volume of the audio output in dB (min: -95.5dB, max: 32dB)
*/
esp_err_t audio_set_volume(float gain_db) {
    return es8156_codec_set_voice_volume(gain_db * 2 + 191);
}

/**
 * @brief Set the mute state of the audio output
*/
esp_err_t audio_set_mute(int channel, bool enable) {
    return es8156_codec_set_voice_mute(channel, enable);
}