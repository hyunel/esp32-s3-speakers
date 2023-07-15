#include "usb.h"
#include "esp_log.h"
#include "esp_private/usb_phy.h"
#include "soc/usb_pins.h"
#include "esp_check.h"
#include "audio.h"
#include "global.h"

static const char *TAG = "USB";

/**
 * @brief This top level thread processes all usb events and invokes callbacks
 */
static void tusb_device_task(void *arg)
{
  ESP_LOGI(TAG, "USB task started");
  while (1) tud_task();
}

/**
 * @brief 发送 HID 报告
 * 
 * bit 0: volume increment\n
 * bit 1: volume decrement\n
 * bit 2: play/pause
*/
bool usb_report_buttons(uint16_t report)
{
  if(!tud_hid_ready()) {
    ESP_LOGW(TAG, "HID not ready");
    return false;
  }
  
  return tud_hid_report(1, &report, sizeof(report));
}

esp_err_t usb_init()
{
  // 下面是个空函数，但是不加会报 undefined reference，不太明白具体什么原因
  usb_descriptors_dummy();

  // Configure USB PHY
  usb_phy_config_t phy_conf = {
      .controller = USB_PHY_CTRL_OTG,
      .otg_mode = USB_OTG_MODE_DEVICE,
      .target = USB_PHY_TARGET_INT
  };

  usb_phy_handle_t phy_hdl;
  ESP_RETURN_ON_ERROR(usb_new_phy(&phy_conf, &phy_hdl), TAG, "Install USB PHY failed");
  ESP_RETURN_ON_FALSE(tusb_init(), ESP_FAIL, TAG, "Init TinyUSB stack failed");

  xTaskCreatePinnedToCore(tusb_device_task, "TinyUSB", CONFIG_TINYUSB_TASK_STACK_SIZE, NULL, CONFIG_TINYUSB_TASK_PRIORITY, NULL, 1);

  ESP_LOGI(TAG, "USB initialized");
  return ESP_OK;
}


//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTOTYPES
//--------------------------------------------------------------------+

// List of supported sample rates
const uint32_t sample_rates[] = {44100, 48000, 88200, 96000};

uint32_t current_sample_rate = 44100;

#define N_SAMPLE_RATES TU_ARRAY_SIZE(sample_rates)

// Audio controls
// Current states
int8_t mute = 0;

// TODO save volume in NVS?
int16_t volume = (AUDIO_VOLUME_DEFAULT + USB_VOLUME_OFFSET) * 256;

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  esp_event_post(USB_EVENT, USB_EVENT_MOUNT, NULL, 0, portMAX_DELAY);
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  esp_event_post(USB_EVENT, USB_EVENT_UNMOUNT, NULL, 0, portMAX_DELAY);
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us  to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  esp_event_post(USB_EVENT, USB_EVENT_SUSPEND, NULL, 0, portMAX_DELAY);
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  esp_event_post(USB_EVENT, USB_EVENT_RESUME, NULL, 0, portMAX_DELAY);
}

// Helper for clock get requests
static bool tud_audio_clock_get_request(uint8_t rhport, audio_control_request_t const *request)
{
  TU_ASSERT(request->bEntityID == UAC2_ENTITY_CLOCK);

  if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ)
  {
    if (request->bRequest == AUDIO_CS_REQ_CUR)
    {
      audio_control_cur_4_t curf = {(int32_t)tu_htole32(current_sample_rate)};
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &curf, sizeof(curf));
    }
    else if (request->bRequest == AUDIO_CS_REQ_RANGE)
    {
      audio_control_range_4_n_t(N_SAMPLE_RATES) rangef =
          {
              .wNumSubRanges = tu_htole16(N_SAMPLE_RATES)};
      for (uint8_t i = 0; i < N_SAMPLE_RATES; i++)
      {
        rangef.subrange[i].bMin = (int32_t)sample_rates[i];
        rangef.subrange[i].bMax = (int32_t)sample_rates[i];
        rangef.subrange[i].bRes = 0;
      }

      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &rangef, sizeof(rangef));
    }
  }
  else if (request->bControlSelector == AUDIO_CS_CTRL_CLK_VALID &&
           request->bRequest == AUDIO_CS_REQ_CUR)
  {
    audio_control_cur_1_t cur_valid = {.bCur = 1};
    return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_valid, sizeof(cur_valid));
  }
  ESP_LOGW(TAG, "Clock get request not supported, entity = %u, selector = %u, request = %u",
          request->bEntityID, request->bControlSelector, request->bRequest);
  return false;
}

// Helper for clock set requests
static bool tud_audio_clock_set_request(uint8_t rhport, audio_control_request_t const *request, uint8_t const *buf)
{
  TU_ASSERT(request->bEntityID == UAC2_ENTITY_CLOCK);
  TU_VERIFY(request->bRequest == AUDIO_CS_REQ_CUR);

  if (request->bControlSelector == AUDIO_CS_CTRL_SAM_FREQ)
  {
    TU_VERIFY(request->wLength == sizeof(audio_control_cur_4_t));

    current_sample_rate = (uint32_t)((audio_control_cur_4_t const *)buf)->bCur;
    return true;
  }
  else
  {
    ESP_LOGW(TAG, "Clock set request not supported, entity = %u, selector = %u, request = %u",
            request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
  }
}

// Helper for feature unit get requests
static bool tud_audio_feature_unit_get_request(uint8_t rhport, audio_control_request_t const *request)
{
  TU_ASSERT(request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT);

  if (request->bControlSelector == AUDIO_FU_CTRL_MUTE && request->bRequest == AUDIO_CS_REQ_CUR)
  {
    audio_control_cur_1_t mute1 = {.bCur = mute};
    return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &mute1, sizeof(mute1));
  }
  else if (UAC2_ENTITY_SPK_FEATURE_UNIT && request->bControlSelector == AUDIO_FU_CTRL_VOLUME)
  {
    if (request->bRequest == AUDIO_CS_REQ_RANGE)
    {
      audio_control_range_2_n_t(1) range_vol = {
          .wNumSubRanges = tu_htole16(1),
          .subrange[0] = {.bMin = tu_htole16((AUDIO_VOLUME_MIN + USB_VOLUME_OFFSET) * 256), tu_htole16((USB_VOLUME_MAX + USB_VOLUME_OFFSET) * 256), tu_htole16(AUDIO_VOLUME_RES * 256)},
        };
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &range_vol, sizeof(range_vol));
    }
    else if (request->bRequest == AUDIO_CS_REQ_CUR)
    {
      audio_control_cur_2_t cur_vol = {.bCur = tu_htole16(volume)};
      return tud_audio_buffer_and_schedule_control_xfer(rhport, (tusb_control_request_t const *)request, &cur_vol, sizeof(cur_vol));
    }
  }
  ESP_LOGW(TAG, "Feature unit get request not supported, entity = %u, selector = %u, request = %u",
          request->bEntityID, request->bControlSelector, request->bRequest);

  return false;
}

// Helper for feature unit set requests
static bool tud_audio_feature_unit_set_request(uint8_t rhport, audio_control_request_t const *request, uint8_t const *buf)
{
  TU_ASSERT(request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT);
  TU_VERIFY(request->bRequest == AUDIO_CS_REQ_CUR);

  if (request->bControlSelector == AUDIO_FU_CTRL_MUTE)
  {
    TU_VERIFY(request->wLength == sizeof(audio_control_cur_1_t));

    int8_t mute_target = ((audio_control_cur_1_t const *)buf)->bCur;
    if(audio_set_mute(request->bChannelNumber, mute_target) == ESP_OK) {
      mute = mute_target;
    } else {
      ESP_LOGE(TAG, "Failed to set mute");
    }

    return true;
  }
  else if (request->bControlSelector == AUDIO_FU_CTRL_VOLUME)
  {
    TU_VERIFY(request->wLength == sizeof(audio_control_cur_2_t));

    int16_t volume_target = ((audio_control_cur_2_t const *)buf)->bCur;
    if(audio_set_volume(((float)volume_target + USB_VOLUME_OFFSET) / 256.f) == ESP_OK) {
      volume = volume_target;
    } else {
      ESP_LOGE(TAG, "Failed to set volume");
    }

    return true;
  }
  else
  {
    ESP_LOGW(TAG, "Feature unit set request not supported, entity = %u, selector = %u, request = %u",
            request->bEntityID, request->bControlSelector, request->bRequest);
    return false;
  }
}

//--------------------------------------------------------------------+
// Audio Callback API Implementations
//--------------------------------------------------------------------+

// Invoked when audio class specific get request received for an entity
bool tud_audio_get_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
  audio_control_request_t const *request = (audio_control_request_t const *)p_request;

  if (request->bEntityID == UAC2_ENTITY_CLOCK)
    return tud_audio_clock_get_request(rhport, request);
  if (request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT)
    return tud_audio_feature_unit_get_request(rhport, request);
  else
  {
    ESP_LOGW(TAG, "Get request not handled, entity = %d, selector = %d, request = %d",
            request->bEntityID, request->bControlSelector, request->bRequest);
  }
  return false;
}

// Invoked when audio class specific set request received for an entity
bool tud_audio_set_req_entity_cb(uint8_t rhport, tusb_control_request_t const *p_request, uint8_t *buf)
{
  audio_control_request_t const *request = (audio_control_request_t const *)p_request;

  if (request->bEntityID == UAC2_ENTITY_SPK_FEATURE_UNIT)
    return tud_audio_feature_unit_set_request(rhport, request, buf);
  if (request->bEntityID == UAC2_ENTITY_CLOCK)
    return tud_audio_clock_set_request(rhport, request, buf);
  ESP_LOGW(TAG, "Set request not handled, entity = %d, selector = %d, request = %d",
          request->bEntityID, request->bControlSelector, request->bRequest);

  return false;
}

bool tud_audio_set_itf_close_EP_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
  (void)rhport;

  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

  ESP_LOGD(TAG, "Set interface close EP %d alt %d", itf, alt);

  if (ITF_NUM_AUDIO_STREAMING_SPK == itf) {
    // state: streaming -> idle
    audio_stop();
    esp_event_post(USB_EVENT, USB_EVENT_STREAM_STOP, NULL, 0, portMAX_DELAY);
  }
  
  return true;
}

bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request)
{
  uint8_t const itf = tu_u16_low(tu_le16toh(p_request->wIndex));
  uint8_t const alt = tu_u16_low(tu_le16toh(p_request->wValue));

  ESP_LOGD(TAG, "Set interface %d alt %d", itf, alt);

  if (ITF_NUM_AUDIO_STREAMING_SPK == itf && alt != 0) {
    audio_stream_config_t cfg;
    cfg.sample_rate_hz = current_sample_rate;
    cfg.bits_per_sample = CFG_TUD_AUDIO_FUNC_1_FORMAT_1_RESOLUTION_RX;

    if(alt == 2) {
      cfg.bits_per_sample = CFG_TUD_AUDIO_FUNC_1_FORMAT_2_RESOLUTION_RX;
    }

    audio_start(&cfg);
    esp_event_post(USB_EVENT, USB_EVENT_STREAM_START, NULL, 0, portMAX_DELAY);
  }

  return true;
}

bool tud_audio_rx_done_pre_read_cb(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting)
{
  uint8_t spk_buf[CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ];
  int spk_data_size = tud_audio_read(spk_buf, n_bytes_received);

  // TODO use optimized algorithm
  if(cur_alt_setting == 2 && CFG_TUD_AUDIO_FUNC_1_FORMAT_2_RESOLUTION_RX == 24 && CFG_TUD_AUDIO_FUNC_1_FORMAT_2_N_BYTES_PER_SAMPLE_RX == 4) {
    // 32bit to 24bit
    uint8_t *dst = spk_buf;
    for(int i=0; i<spk_data_size; i+= 4) {
      memcpy(dst, spk_buf + i + 1, 3);
      dst += 3;
    }
    spk_data_size = spk_data_size / 4 * 3;
  }

  return audio_write(spk_data_size, spk_buf) == ESP_OK;
}

//--------------------------------------------------------------------+
// HID Callback API Implementations
//--------------------------------------------------------------------+


// Invoked when received SET_PROTOCOL request
// protocol is either HID_PROTOCOL_BOOT (0) or HID_PROTOCOL_REPORT (1)
void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol)
{
  (void) instance;
  (void) protocol;

  // nothing to do since we use the same compatible boot report for both Boot and Report mode.
  // TODO set a indicator for user
}

// Invoked when sent REPORT successfully to host
// Application can use this to send the next report
// Note: For composite reports, report[0] is report ID
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, /*uint16_t*/ uint8_t len )
{
  (void) instance;
  (void) report;
  (void) len;

  // nothing to do
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
  
}
