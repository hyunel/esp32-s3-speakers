#include "touchsensor.h"
#include "global.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/touch_sensor.h"
#include "esp_log.h"

static const char *TAG = "touch";

#define TOUCH_BUTTON_NUM    2
#define TOUCH_BUTTON_WATERPROOF_ENABLE 0
#define TOUCH_BUTTON_DENOISE_ENABLE    1

static const touch_pad_t button[TOUCH_BUTTON_NUM] = {
    (touch_pad_t)PIN_TOUCH_L,     // 'Left' button.
    (touch_pad_t)PIN_TOUCH_R,     // 'Right' button.
};

/*
 * Touch threshold. The threshold determines the sensitivity of the touch.
 * This threshold is derived by testing changes in readings from different touch channels.
 * If (raw_data - benchmark) > benchmark * threshold, the pad be activated.
 * If (raw_data - benchmark) < benchmark * threshold, the pad be inactivated.
 */
static const float button_threshold[TOUCH_BUTTON_NUM] = {
    0.03, // 3%.
    0.03, // 3%.
};

static QueueHandle_t que_touch = NULL;
typedef struct touch_msg {
    touch_pad_intr_mask_t intr_mask;
    uint32_t pad_num;
    uint32_t pad_status;
    uint32_t pad_val;
    uint32_t slp_proxi_cnt;
    uint32_t slp_proxi_base;
} touch_event_t;

static void touchsensor_set_thresholds(void)
{
    uint32_t touch_value;
    for (int i = 0; i < TOUCH_BUTTON_NUM; i++) {
        //read benchmark value
        touch_pad_read_benchmark(button[i], &touch_value);
        //set interrupt threshold.
        touch_pad_set_thresh(button[i], touch_value * button_threshold[i]);
        ESP_LOGI(TAG, "touch pad [%d] base %"PRIu32", thresh %"PRIu32, \
                 button[i], touch_value, (uint32_t)(touch_value * button_threshold[i]));
    }
}

static void touchsensor_filter_set(touch_filter_mode_t mode)
{
    /* Filter function */
    touch_filter_config_t filter_info = {
        .mode = mode,           // Test jitter and filter 1/4.
        .debounce_cnt = 1,      // 1 time count.
        .noise_thr = 0,         // 50%
        .jitter_step = 4,       // use for jitter mode.
        .smh_lvl = TOUCH_PAD_SMOOTH_IIR_2,
    };
    touch_pad_filter_set_config(&filter_info);
    touch_pad_filter_enable();
    ESP_LOGI(TAG, "touch pad filter init");
}

/*
  Handle an interrupt triggered when a pad is touched.
  Recognize what pad has been touched and save it in a table.
 */
static void touchsensor_interrupt_cb(void *arg)
{
    int task_awoken = pdFALSE;
    touch_event_t evt;

    evt.intr_mask = (touch_pad_intr_mask_t) touch_pad_read_intr_status_mask();
    evt.pad_status = touch_pad_get_status();
    evt.pad_num = touch_pad_get_current_meas_channel();

    xQueueSendFromISR(que_touch, &evt, &task_awoken);
    if (task_awoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static uint8_t button_state = 0;
static void touchsensor_read_task(void *pvParameter)
{
    touch_event_t evt = {};

    /* Wait touch sensor init done */
    vTaskDelay(50 / portTICK_PERIOD_MS);
    touchsensor_set_thresholds();

    while (1) {
        int ret = xQueueReceive(que_touch, &evt, (TickType_t)portMAX_DELAY);
        if (ret != pdTRUE) continue;

        if (evt.intr_mask & TOUCH_PAD_INTR_MASK_ACTIVE) {
            ESP_LOGD(TAG, "TouchSensor [%"PRIu32"] be activated, status mask 0x%"PRIu32"", evt.pad_num, evt.pad_status);
            if (evt.pad_num == button[0]) {
                button_state |= 1;
                esp_event_post(BUTTON_EVENT, BUTTON_EVENT_LBUTTONDOWN, &button_state, sizeof(button_state), portMAX_DELAY);
            } else if (evt.pad_num == button[1]) {
                button_state |= (1 << 1);
                esp_event_post(BUTTON_EVENT, BUTTON_EVENT_RBUTTONDOWN, &button_state, sizeof(button_state), portMAX_DELAY);
            }
        }

        if (evt.intr_mask & TOUCH_PAD_INTR_MASK_INACTIVE) {
            ESP_LOGD(TAG, "TouchSensor [%"PRIu32"] be inactivated, status mask 0x%"PRIu32, evt.pad_num, evt.pad_status);
            if (evt.pad_num == button[0]) {
                button_state &= ~(1);
                esp_event_post(BUTTON_EVENT, BUTTON_EVENT_LBUTTONUP, &button_state, sizeof(button_state), portMAX_DELAY);
            } else if (evt.pad_num == button[1]) {
                button_state &= ~(1 << 1);
                esp_event_post(BUTTON_EVENT, BUTTON_EVENT_RBUTTONUP, &button_state, sizeof(button_state), portMAX_DELAY);
            }
        }

        if (evt.intr_mask & TOUCH_PAD_INTR_MASK_TIMEOUT) {
            /* Add your exception handling in here. */
            ESP_LOGW(TAG, "Touch sensor channel %"PRIu32" measure timeout. Skip this exception channel!!", evt.pad_num);
            touch_pad_timeout_resume(); // Point on the next channel to measure.
        }
    }
}

void touchsensor_init() {
    if (que_touch == NULL) {
        que_touch = xQueueCreate(TOUCH_BUTTON_NUM, sizeof(touch_event_t));
    }

    // Initialize touch pad peripheral, it will start a timer to run a filter
    ESP_LOGI(TAG, "Initializing touch pad");
    /* Initialize touch pad peripheral. */
    touch_pad_init();
    for (int i = 0; i < TOUCH_BUTTON_NUM; i++) {
        touch_pad_config(button[i]);
    }
    

#if TOUCH_BUTTON_DENOISE_ENABLE
    /* Denoise setting at TouchSensor 0. */
    touch_pad_denoise_t denoise = {
        /* The bits to be cancelled are determined according to the noise level. */
        .grade = TOUCH_PAD_DENOISE_BIT4,
        /* By adjusting the parameters, the reading of T0 should be approximated to the reading of the measured channel. */
        .cap_level = TOUCH_PAD_DENOISE_CAP_L4,
    };
    touch_pad_denoise_set_config(&denoise);
    touch_pad_denoise_enable();
    ESP_LOGI(TAG, "Denoise function init");
#endif

#if TOUCH_BUTTON_WATERPROOF_ENABLE
    /* Waterproof function */
    touch_pad_waterproof_t waterproof = {
        .guard_ring_pad = button[3],   // If no ring pad, set 0;
        /* It depends on the number of the parasitic capacitance of the shield pad.
           Based on the touch readings of T14 and T0, estimate the size of the parasitic capacitance on T14
           and set the parameters of the appropriate hardware. */
        .shield_driver = TOUCH_PAD_SHIELD_DRV_L2,
    };
    touch_pad_waterproof_set_config(&waterproof);
    touch_pad_waterproof_enable();
    ESP_LOGI(TAG, "touch pad waterproof init");
#endif

    /* Filter setting */
    touchsensor_filter_set(TOUCH_PAD_FILTER_IIR_16);
    touch_pad_timeout_set(true, SOC_TOUCH_PAD_THRESHOLD_MAX);
    /* Register touch interrupt ISR, enable intr type. */
    touch_pad_isr_register(touchsensor_interrupt_cb, NULL, TOUCH_PAD_INTR_MASK_ALL);
    /* If you have other touch algorithm, you can get the measured value after the `TOUCH_PAD_INTR_MASK_SCAN_DONE` interrupt is generated. */
    touch_pad_intr_enable(TOUCH_PAD_INTR_MASK_ACTIVE | TOUCH_PAD_INTR_MASK_INACTIVE | TOUCH_PAD_INTR_MASK_TIMEOUT);

    /* Enable touch sensor clock. Work mode is "timer trigger". */
    touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
    touch_pad_fsm_start();
    
    // Start a task to show what pads have been touched
    xTaskCreate(&touchsensor_read_task, "task_touch", 4096, NULL, 10, NULL);
}