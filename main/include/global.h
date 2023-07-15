#pragma once

#include "esp_event.h"

#define PIN_LED         38
#define PIN_TOUCH_L     11
#define PIN_TOUCH_R     10

#define PIN_DAC_SDA     8
#define PIN_DAC_SCL     9
#define PIN_DAC_MCLK    7
#define PIN_DAC_SCLK    6
#define PIN_DAC_SDIN    5
#define PIN_DAC_LRCK    2

ESP_EVENT_DECLARE_BASE(BUTTON_EVENT);
typedef enum {
    BUTTON_EVENT_LBUTTONDOWN,
    BUTTON_EVENT_LBUTTONUP,
    BUTTON_EVENT_RBUTTONDOWN,
    BUTTON_EVENT_RBUTTONUP,
} button_event_t;

ESP_EVENT_DECLARE_BASE(USB_EVENT);
typedef enum {
    USB_EVENT_MOUNT,
    USB_EVENT_UNMOUNT,
    USB_EVENT_SUSPEND,
    USB_EVENT_RESUME,
    USB_EVENT_STREAM_START,
    USB_EVENT_STREAM_STOP
} usb_event_t;