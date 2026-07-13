#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "ecg_pipeline.h"

#define RECORD_BUTTON_GPIO     GPIO_NUM_0
#define RECORD_LED_GPIO        GPIO_NUM_21
#define SD_DETECT_GPIO         GPIO_NUM_35
#define RECORD_BUTTON_DEBOUNCE 50
#define RECORD_BUTTON_POLL     20
#define INIT_HEALTH_RETRY_MS   500
#define IDLE_HEALTH_CHECK_MS   1000
#define APP_MIN_FREE_HEAP      (20 * 1024)
#define APP_MIN_STATE_STACK    512
#define SD_CARD_PRESENT_LEVEL 1
typedef enum {
    ECG_STATE_INIT,
    ECG_STATE_IDLE,
    ECG_STATE_RECORD,
} ecg_state_t;

static const char *TAG_APP = "ECG_APP";
static ecg_state_t ecg_state = ECG_STATE_INIT;
static TaskHandle_t state_task_handle = NULL;
static volatile bool sd_detect_event_pending = false;
static volatile uint32_t pipeline_event_pending = 0;
static volatile ecg_pipeline_event_t last_pipeline_event = ECG_PIPELINE_EVENT_SD_FAULT;
static volatile esp_err_t last_pipeline_error = ESP_OK;
static TickType_t last_init_health_check = 0;
static TickType_t last_idle_health_check = 0;

#define PIPELINE_EVENT_BIT(event) (1UL << (uint32_t)(event))

static void IRAM_ATTR sd_detect_isr(void *arg)
{
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)arg;
    sd_detect_event_pending = true;
    if (state_task_handle != NULL) {
        vTaskNotifyGiveFromISR(state_task_handle, &higher_priority_task_woken);
    }
    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}

static void app_pipeline_event_handler(ecg_pipeline_event_t event,
                                       esp_err_t error,
                                       const char *detail)
{
    (void)detail;

    last_pipeline_event = event;
    last_pipeline_error = error;
    pipeline_event_pending |= PIPELINE_EVENT_BIT(event);

    if (state_task_handle != NULL) {
        xTaskNotifyGive(state_task_handle);
    }
}

static void app_storage_init(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);
}

static void app_controls_init(void)
{
    gpio_config_t button_cfg = {
        .pin_bit_mask = 1ULL << RECORD_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button_cfg));

    gpio_config_t led_cfg = {
        .pin_bit_mask = 1ULL << RECORD_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&led_cfg));
    ESP_ERROR_CHECK(gpio_set_level(RECORD_LED_GPIO, 0));

    gpio_config_t sd_detect_cfg = {
        .pin_bit_mask = 1ULL << SD_DETECT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&sd_detect_cfg));
    esp_err_t isr_ret = gpio_install_isr_service(0);
    if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(isr_ret);
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(SD_DETECT_GPIO, sd_detect_isr, NULL));
}

static bool record_button_pressed(void)
{
    static int stable_level = 1;
    static int previous_level = 1;
    static TickType_t last_change = 0;
    int level = gpio_get_level(RECORD_BUTTON_GPIO);
    TickType_t now = xTaskGetTickCount();

    if (level != previous_level) {
        previous_level = level;
        last_change = now;
    }

    if (level != stable_level
        && (now - last_change) >= pdMS_TO_TICKS(RECORD_BUTTON_DEBOUNCE)) {
        stable_level = level;
        return stable_level == 0;
    }

    return false;
}

static bool app_health_check(const char *context)
{
    bool heap_ok = heap_caps_check_integrity_all(false);
    uint32_t free_heap = esp_get_free_heap_size();
    UBaseType_t state_stack = uxTaskGetStackHighWaterMark(NULL);
    int sd_det_level = gpio_get_level(SD_DETECT_GPIO);

    if (!heap_ok) {
        ESP_LOGE(TAG_APP, "Health[%s]: heap integrity failed", context);
        return false;
    }

    if (free_heap < APP_MIN_FREE_HEAP) {
        ESP_LOGE(TAG_APP,
                 "Health[%s]: low heap (%lu bytes)",
                 context,
                 (unsigned long)free_heap);
        return false;
    }

    if (state_stack < APP_MIN_STATE_STACK) {
        ESP_LOGE(TAG_APP,
                 "Health[%s]: low state task stack (%lu words)",
                 context,
                 (unsigned long)state_stack);
        return false;
    }
    if (gpio_get_level(SD_DETECT_GPIO) != SD_CARD_PRESENT_LEVEL) {
        return false;
    }
    ESP_LOGD(TAG_APP,
             "Health[%s]: OK heap=%lu stack=%lu sd_det=%d",
             context,
             (unsigned long)free_heap,
             (unsigned long)state_stack,
             sd_det_level);
    return true;
}

static const char *pipeline_event_name(ecg_pipeline_event_t event)
{
    switch (event) {
    case ECG_PIPELINE_EVENT_SD_FAULT:
        return "SD_FAULT";
    case ECG_PIPELINE_EVENT_ADC_FAULT:
        return "ADC_FAULT";
    case ECG_PIPELINE_EVENT_BUFFER_FAULT:
        return "BUFFER_FAULT";
    case ECG_PIPELINE_EVENT_TASK_FAULT:
        return "TASK_FAULT";
    case ECG_PIPELINE_EVENT_BLE_FAULT:
        return "BLE_FAULT";
    default:
        return "UNKNOWN";
    }
}

static void enter_idle_state(void)
{
    ecg_pipeline_enter_idle();
    ESP_ERROR_CHECK(gpio_set_level(RECORD_LED_GPIO, 0));
    ecg_state = ECG_STATE_IDLE;
    ESP_LOGI(TAG_APP, "State: IDLE");
}

static void enter_init_state(void)
{
    ecg_pipeline_enter_init();
    ESP_ERROR_CHECK(gpio_set_level(RECORD_LED_GPIO, 0));
    ecg_state = ECG_STATE_INIT;
    last_init_health_check = 0;
    ESP_LOGI(TAG_APP, "State: INIT (SD DET level=%d)",
             gpio_get_level(SD_DETECT_GPIO));
}

static void enter_record_state(void)
{
    esp_err_t ret = ecg_pipeline_enter_recording();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG_APP, "RECORD blocked (%s)", esp_err_to_name(ret));
        enter_init_state();
        return;
    }

    ESP_ERROR_CHECK(gpio_set_level(RECORD_LED_GPIO, 1));
    ecg_state = ECG_STATE_RECORD;
    ESP_LOGI(TAG_APP, "State: RECORD");
}

static void ecg_state_task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        TickType_t now = xTaskGetTickCount();

        if (sd_detect_event_pending) {
            sd_detect_event_pending = false;
            enter_init_state();
        }

        if (pipeline_event_pending != 0) {
            uint32_t events = pipeline_event_pending;
            pipeline_event_pending = 0;
            ESP_LOGW(TAG_APP,
                     "Pipeline fault event(s)=0x%lx last=%s error=%s",
                     (unsigned long)events,
                     pipeline_event_name(last_pipeline_event),
                     esp_err_to_name(last_pipeline_error));
            if (events & (PIPELINE_EVENT_BIT(ECG_PIPELINE_EVENT_SD_FAULT)
                          | PIPELINE_EVENT_BIT(ECG_PIPELINE_EVENT_ADC_FAULT)
                          | PIPELINE_EVENT_BIT(ECG_PIPELINE_EVENT_BUFFER_FAULT)
                          | PIPELINE_EVENT_BIT(ECG_PIPELINE_EVENT_TASK_FAULT))) {
                enter_init_state();
            }
        }

        if (ecg_state == ECG_STATE_INIT
            && (last_init_health_check == 0
                || (now - last_init_health_check) >= pdMS_TO_TICKS(INIT_HEALTH_RETRY_MS))) {
            last_init_health_check = now;
            if (app_health_check("init")) {
                enter_idle_state();
            } else {
                ESP_LOGW(TAG_APP, "INIT health check failed, staying in INIT");
            }
        }

        if (ecg_state == ECG_STATE_IDLE
            && (last_idle_health_check == 0
                || (now - last_idle_health_check) >= pdMS_TO_TICKS(IDLE_HEALTH_CHECK_MS))) {
            last_idle_health_check = now;
            if (!app_health_check("idle")) {
                enter_init_state();
            }
        }

        if (record_button_pressed()) {
            if (ecg_state == ECG_STATE_IDLE) {
                enter_record_state();
            } else if (ecg_state == ECG_STATE_RECORD) {
                enter_idle_state();
            }
        }
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(RECORD_BUTTON_POLL));
    }
}

void app_main(void)
{
    app_storage_init();
    app_controls_init();
    ecg_pipeline_set_event_callback(app_pipeline_event_handler);

    if (ecg_pipeline_init() != ESP_OK) {
        ESP_LOGE(TAG_APP, "ECG pipeline init failed");
        return;
    }

    ecg_pipeline_start();
    enter_idle_state();

    if (xTaskCreatePinnedToCore(ecg_state_task, "State_Task", 3072, NULL, 2,
                                &state_task_handle, 0) != pdPASS) {
        ESP_LOGE(TAG_APP, "State task creation failed");
    }
}
