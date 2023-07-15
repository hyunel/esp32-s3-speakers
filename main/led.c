#include "led.h"
#include "esp_check.h"

static const char *TAG = "led";

static TaskHandle_t task_led_handle = NULL;

static void led_set_brightness(uint8_t brightness)
{
    if(brightness > 100)
        brightness = 100;
    ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, 8191 * (brightness / 100.0), 0);
}

static void task_led(void *pvParameter)
{
    uint8_t mode = 0, lastmode = 0;
    uint16_t interval = 0, duration = 0;
    while(true) {
        /**
         * bit 0-1      mode: 0 = off, 1 = on, 2 = blink, 3 = fade
         * bit 2-16     interval
         * bit 17-31    duration
        */
        uint32_t notify_value;
        if(xTaskNotifyWait(0, 0, &notify_value, 10 / portTICK_PERIOD_MS) == pdTRUE) {
            mode = notify_value & 0x03;
            interval = (notify_value >> 2) & 0x7FFF;
            duration = (notify_value >> 17) & 0x7FFF;
        }
        
        if(mode <= 1 && mode == lastmode) continue;

        switch (mode)
        {
        case 0:
            led_set_brightness(0);
            break;
        case 1:
            led_set_brightness(100);
            break;
        case 2:
            led_set_brightness(100);
            vTaskDelay(duration / portTICK_PERIOD_MS);
            led_set_brightness(0);
            vTaskDelay(interval / portTICK_PERIOD_MS);
            break;
        case 3:
            for(int i = 0; i < duration; i++) {
                led_set_brightness(i * 100 / duration);
                vTaskDelay(interval / portTICK_PERIOD_MS);
            }
            for(int i = duration; i > 0; i--) {
                led_set_brightness(i * 100 / duration);
                vTaskDelay(interval / portTICK_PERIOD_MS);
            }
            break;
        }

        lastmode = mode;
    }
}

esp_err_t led_init()
{
    esp_err_t ret = ESP_OK;

    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LOW_SPEED_MODE,
        .duty_resolution  = LEDC_TIMER_13_BIT,
        .timer_num        = LEDC_TIMER_0,
        .freq_hz          = 5000,  // Set output frequency at 5 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    if((ret = ledc_timer_config(&ledc_timer)) != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config: %d", ret);
        return ret;
    }

    ledc_channel_config_t ledc_channel = {
        .gpio_num       = PIN_LED,
        .speed_mode     = LEDC_LOW_SPEED_MODE,
        .channel        = LEDC_CHANNEL_0,
        .intr_type      = LEDC_INTR_DISABLE,
        .timer_sel      = LEDC_TIMER_0,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };

    ESP_RETURN_ON_ERROR(ledc_channel_config(&ledc_channel), TAG, "ledc_channel_config failed");
    ESP_RETURN_ON_ERROR(ledc_fade_func_install(0), TAG, "ledc_fade_func_install failed");

    // gpio_set_direction(PIN_LED, GPIO_MODE_OUTPUT);
    // gpio_set_level(PIN_LED, 1);
    xTaskCreate(task_led, "task_led", 1024, NULL, 5, &task_led_handle);
    return ret;
}

static void led_set_state(uint8_t mode, uint16_t interval, uint16_t duration)
{
    if(task_led_handle == NULL) return;
    uint32_t notify_value = (mode & 0x03) | (interval & 0x7FFF) << 2 | (duration & 0x7FFF) << 17;
    xTaskNotify(task_led_handle, notify_value, eSetValueWithOverwrite);
}

void led_on()
{
    led_set_state(1, 0, 0);
}

void led_off()
{
    led_set_state(0, 0, 0);
}

void led_blink(uint16_t interval, uint16_t duration)
{
    led_set_state(2, interval, duration);
}

void led_fade(uint16_t interval, uint16_t duration)
{
    led_set_state(3, interval, duration);
}