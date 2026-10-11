#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "components/esp_ble/ecg_ble.h"
#include "ecg_pipeline.h"
#include "utils/app_recovery.h"
#include "utils/user_button.h"

/*===========================================================*/
/*SYSTEM MACRO                                               */
/*===========================================================*/
#define INIT_HEALTH_RETRY_MS        500
#define IDLE_HEALTH_CHECK_MS        1000
/*===========================================================*/
/*APP MACRO                                               */
/*===========================================================*/
#define RECORD_BUTTON_GPIO          GPIO_NUM_0
#define RECORD_LED_GPIO             GPIO_NUM_21
#define SD_DETECT_GPIO              GPIO_NUM_35

#define RECORD_BUTTON_POLL          20

#define APP_MIN_FREE_HEAP           (20 * 1024)
#define APP_MIN_STATE_STACK         512
#define SD_CARD_PRESENT_LEVEL       1
#define INIT_RESTART_FAIL_LIMIT     20

#define APP_NOTIFY_SD_DETECT        (1UL << 0)
#define APP_NOTIFY_BLE_COMMAND      (1UL << 1)

/*===========================================================*/
/* Private typedef                                           */
/*===========================================================*/
typedef enum {
    ECG_STATE_INIT,
    ECG_STATE_IDLE,
    ECG_STATE_RECORD,
} ecg_state_t;


/*===========================================================*/
/* Function prototpye                                        */
/*===========================================================*/

static void sd_detect_isr       (void *arg);
static void event_handler       (ecg_pipeline_event_t event, esp_err_t error, const char *detail);
static void ble_command_handler (ecg_ble_command_t command);

static void app_storage_init    (void);
static void app_user_button_init(void);

static const char                   *TAG_APP                    = "ECG_APP";
static ecg_state_t                  ecg_state                   = ECG_STATE_INIT;
static TaskHandle_t                 state_task_handle           = NULL;

static ecg_pipeline_event_t         pending_event               = ECG_PIPELINE_EVENT_SD_FAULT;
static volatile ecg_ble_command_t   pending_ble_command         = ECG_BLE_COMMAND_NONE;
static bool                         pipeline_recovery_pending   = false;

static TickType_t                   last_init_health_check      = 0;
static TickType_t                   last_idle_health_check      = 0;
static uint32_t                     init_health_fail_count      = 0;

static void app_storage_init(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ESP_OK != ret) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    ESP_ERROR_CHECK(ret);
}

static void app_user_button_init(void)
{
    ESP_ERROR_CHECK(user_button_init(RECORD_BUTTON_GPIO, RECORD_LED_GPIO));

    gpio_config_t sd_detect_config = {
        .pin_bit_mask = 1ULL << SD_DETECT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&sd_detect_config));

    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(SD_DETECT_GPIO, sd_detect_isr, NULL));
}

static void enter_idle_state(void)
{
    ecg_pipeline_stop_record();
    user_button_set_recording_led(false);
    ecg_state = ECG_STATE_IDLE;
    init_health_fail_count = 0;
}

static void enter_init_state(void)
{
    ecg_pipeline_stop_record();
    user_button_set_recording_led(false);
    ecg_state = ECG_STATE_INIT;
    last_init_health_check = 0;
}

static void enter_record_state(void)
{
    esp_err_t ret = ecg_pipeline_start_recording();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG_APP, "RECORD blocked (%s)", esp_err_to_name(ret));
        enter_init_state();
        return;
    }

    user_button_set_recording_led(true);
    ecg_state = ECG_STATE_RECORD;
}

static void handle_ble_command(ecg_ble_command_t command)
{
    switch (command) {
    case ECG_BLE_COMMAND_START:
        if (ecg_state == ECG_STATE_IDLE) {
            enter_record_state();
        }
        break;
    case ECG_BLE_COMMAND_STOP:
        if (ecg_state == ECG_STATE_RECORD) {
            enter_idle_state();
        }
        break;
    default:
        break;
    }
}

static void state_machine_task(void *pvParameters)
{
    (void)pvParameters;
    enter_idle_state();
    while (1) {
        uint32_t notifications = 0;
        (void)xTaskNotifyWait(0, UINT32_MAX, &notifications, pdMS_TO_TICKS(RECORD_BUTTON_POLL));
        TickType_t now = xTaskGetTickCount();

        if (notifications & APP_NOTIFY_SD_DETECT) {
            enter_init_state();
        }

        uint32_t pipeline_events = notifications & APP_RECOVERY_PIPELINE_EVENT_MASK;
        if (pipeline_events != 0U) {
            ESP_LOGW(TAG_APP, "Pipeline fault event(s)=0x%lx",
                     (unsigned long)pipeline_events);
            if (app_recovery_event_requires_reset(pipeline_events)) {
                pending_event = app_recovery_event_from_notifications(pipeline_events);
                pipeline_recovery_pending = true;
                enter_init_state();
            }
        }

        if (notifications & APP_NOTIFY_BLE_COMMAND) {
            handle_ble_command(pending_ble_command);
        }

        if (ecg_state == ECG_STATE_INIT
            && (last_init_health_check == 0 || (now - last_init_health_check) >= pdMS_TO_TICKS(INIT_HEALTH_RETRY_MS))) {
            last_init_health_check = now;

            if (pipeline_recovery_pending) {
                esp_err_t recovery_ret = ecg_pipeline_recover_from_event(pending_event);
                if (recovery_ret == ESP_OK) {
                    pipeline_recovery_pending = false;
                } else {
                    ESP_LOGW(TAG_APP, "Recovery from event=%d failed (%s), staying in INIT",
                             pending_event, esp_err_to_name(recovery_ret));
                }
            }

            if (!pipeline_recovery_pending
                && app_recovery_health_check(SD_DETECT_GPIO, SD_CARD_PRESENT_LEVEL,
                                             APP_MIN_FREE_HEAP, APP_MIN_STATE_STACK,
                                             "init")) {
                enter_idle_state();
            } else {
                init_health_fail_count++;
                ESP_LOGW(TAG_APP, "INIT health check failed, staying in INIT");
                if (init_health_fail_count >= INIT_RESTART_FAIL_LIMIT
                    && gpio_get_level(SD_DETECT_GPIO) == SD_CARD_PRESENT_LEVEL) {
                    ESP_LOGE(TAG_APP,
                             "INIT failed %lu times with SD present; restarting system",
                             (unsigned long)init_health_fail_count);
                    esp_restart();
                }
            }
        }

        if (ecg_state == ECG_STATE_IDLE
            && (last_idle_health_check == 0
                || (now - last_idle_health_check) >= pdMS_TO_TICKS(IDLE_HEALTH_CHECK_MS))) {
            last_idle_health_check = now;
            if (!app_recovery_health_check(SD_DETECT_GPIO, SD_CARD_PRESENT_LEVEL,
                                           APP_MIN_FREE_HEAP, APP_MIN_STATE_STACK,
                                           "idle")) {
                enter_init_state();
            }
        }

        if (user_button_take_press()) {
            if (ecg_state == ECG_STATE_IDLE) {
                enter_record_state();
            } else if (ecg_state == ECG_STATE_RECORD) {
                enter_idle_state();
            }
        }
    }
}

void app_main(void)
{
    /* JTAG is the primary debug transport; retain warnings and faults on UART. */
    esp_log_level_set("*", ESP_LOG_WARN);
    app_storage_init();
    app_user_button_init();
    ecg_pipeline_set_event_callback(event_handler);
    ecg_ble_set_command_callback(ble_command_handler);

    if (ecg_pipeline_init() != ESP_OK) {
        ESP_LOGE(TAG_APP, "ECG pipeline init failed");
        return;
    }

    if (xTaskCreatePinnedToCore(state_machine_task,
                                "State_Task",
                                3072,
                                NULL,
                                2,
                                &state_task_handle,
                                 0) != pdPASS) {
        ESP_LOGE(TAG_APP, "State task creation failed");
        return;
    }
    ecg_pipeline_start_task();
}

static void IRAM_ATTR sd_detect_isr(void *arg) {
    BaseType_t higher_priority_task_woken = pdFALSE;

    (void)arg;
    if (state_task_handle != NULL) {
        xTaskNotifyFromISR(state_task_handle, APP_NOTIFY_SD_DETECT, eSetBits,
                           &higher_priority_task_woken);
    }
    if (higher_priority_task_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}


static void event_handler(ecg_pipeline_event_t event, esp_err_t error, const char *detail)
{
    (void)error;
    (void)detail;

    if (state_task_handle != NULL) {
        xTaskNotify(state_task_handle, APP_RECOVERY_PIPELINE_EVENT_BIT(event), eSetBits);
    }
}

static void ble_command_handler(ecg_ble_command_t command)
{
    pending_ble_command = command;
    if (state_task_handle != NULL) {
        xTaskNotify(state_task_handle, APP_NOTIFY_BLE_COMMAND, eSetBits);
    }
}
