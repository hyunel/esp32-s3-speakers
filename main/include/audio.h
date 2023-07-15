#include "esp_types.h"
#include "esp_err.h"

#define AUDIO_MCLK_MULTIPLE     (384) // If not using 24-bit data width, 256 should be enough

#define AUDIO_VPA               (5.0)
#define AUDIO_VDAC              (3.3)
#define AUDIO_PA_GAIN           10
#define AUDIO_MAX_GAIN          (20.0 * log10(VPA / VDAC))

#define AUDIO_I2S_NUM           I2S_NUM_0
#define AUDIO_VOLUME_MAX        32
#define AUDIO_VOLUME_MIN        (-95.5)
#define AUDIO_VOLUME_RES        (0.5)
#define AUDIO_VOLUME_DEFAULT    (-10.0)

typedef struct audio_stream_config {
    uint32_t sample_rate_hz;
    uint32_t bits_per_sample;
} audio_stream_config_t;

esp_err_t audio_init();
esp_err_t audio_write(size_t size, void * data);
esp_err_t audio_start(audio_stream_config_t *config);
esp_err_t audio_stop();

esp_err_t audio_set_volume(float gain_db);
esp_err_t audio_set_mute(int channel, bool enable);