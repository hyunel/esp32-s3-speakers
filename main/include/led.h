#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "global.h"
#include "esp_log.h"

esp_err_t led_init();
void led_on();
void led_off();
void led_blink(uint16_t interval, uint16_t duration);
void led_fade(uint16_t interval, uint16_t duration);