#include "tusb.h"
#include "usb_descriptors.h"

// #define USB_VOLUME_MAX      AUDIO_VOLUME_MAX
#define USB_VOLUME_MAX      16
#define USB_VOLUME_OFFSET   0

esp_err_t usb_init();
bool usb_report_buttons(uint16_t report);