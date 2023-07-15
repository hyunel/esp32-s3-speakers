/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "led.h"
#include "touchsensor.h"
#include "audio.h"
#include "usb.h"

static const char *TAG = "main";

ESP_EVENT_DEFINE_BASE(BUTTON_EVENT);
ESP_EVENT_DEFINE_BASE(USB_EVENT);

static void event_mode_change(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    if(base == USB_EVENT && id == USB_EVENT_MOUNT) {
        led_on();
    }

    if(base == USB_EVENT && id == USB_EVENT_UNMOUNT) {
        led_off();
    }

    if(base == USB_EVENT && id == USB_EVENT_STREAM_START) {
        led_fade(10, 100);
    }

    if(base == USB_EVENT && id == USB_EVENT_STREAM_STOP) {
        led_on();
    }
}

static void event_buttons(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    uint8_t buttonState = *(uint8_t*)event_data;
    uint16_t report = 0;
    
    // Left button
    if(buttonState & 1) {
        report |= 1 << 1;
    }

    // Right button
    if(buttonState & 2) {
        report |= 1 << 0;
    }

    usb_report_buttons(report);
}

void app_main(void)
{
    // 初始化事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 初始化 LED
    ESP_ERROR_CHECK(led_init());

    // 注册事件处理函数
    ESP_ERROR_CHECK(esp_event_handler_register(USB_EVENT, ESP_EVENT_ANY_ID, event_mode_change, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(BUTTON_EVENT, ESP_EVENT_ANY_ID, event_buttons, NULL));

    // 初始化触摸
    touchsensor_init();
    
    // 初始化音频模块
    ESP_ERROR_CHECK(audio_init());

    // 初始化 USB
    ESP_ERROR_CHECK(usb_init());
}
