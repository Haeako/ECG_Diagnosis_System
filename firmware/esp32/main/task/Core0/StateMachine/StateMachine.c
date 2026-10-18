#include <stdbool.h>
#include <stdint.h>

#include "StateMachine.h"
#include "components/metrics/pipeline_metrics.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "components/esp_ble/ecg_ble.h"
#include "ecg_pipeline.h"
#include "utils/app_recovery.h"
#include "utils/user_button.h"

#define PREPARE_RETRY_MS             500U
#define IDLE_HEALTH_CHECK_MS         1000U
#define FAULT_LED_TOGGLE_MS          500U
#define SD_DETECT_DEBOUNCE_MS        250U

#define RECORD_BUTTON_GPIO_NUM       GPIO_NUM_0
#define RECORD_LED_GPIO_NUM          GPIO_NUM_21
#define SD_DET_GPIO_NUM              GPIO_NUM_35
#define SD_DET_GPIO_BIT_MASK         1ULL << SD_DET_GPIO_NUM
#define SD_DET_GPIO_ALLOC_FLAG       0
#define TASK_PERIOD_MS               20U //ms //50Hz
#define TASK_ASSIGNED_CORE           0
#define STATE_EVENT_QUEUE_LEN        16U

#define APP_MIN_FREE_HEAP            (20U * 1024U)
#define APP_MIN_STATE_STACK          512U
#define SD_CARD_PRESENT_LEVEL        1
#define PREPARE_FAIL_LIMIT           20U

#define TASK_TICK_TO_WAIT            pdMS_TO_TICKS(TASK_PERIOD_MS)
#define APP_NOTIFY_SD_DETECT         (1UL << 0)


static void sd_detect_isr(void *arg);
static void pipeline_event_handler(ecg_pipeline_event_t event, esp_err_t error, const char *detail);
static void ble_command_handler(ecg_ble_command_t command);

static const char *TAG_APP = "ECG_APP";
static MODE current_mode = FAULT_MODE;
static TaskHandle_t state_task_handle = NULL;
static QueueHandle_t state_event_queue = NULL;
static volatile bool pipeline_start_complete = false;

typedef struct {
    state_cause_t event;
    esp_err_t error;
} state_event_t;

typedef struct {
    state_cause_t event;
    esp_err_t error;
    ecg_pipeline_event_t recovery_event;
    bool recovery_pending;
    uint32_t fail_count;
} prepare_context_t;

static prepare_context_t prepare_context = {
    .event              = STATE_CAUSE_NONE,
    .error              = ESP_OK,
    .recovery_event     = ECG_PIPELINE_EVENT_SD_FAULT,
    .recovery_pending   = false,
    .fail_count         = 0U,
};

static TickType_t last_prepare_attempt = 0;
static TickType_t last_idle_health_check = 0;
static TickType_t last_fault_led_toggle = 0;
static bool fault_led_level = false;
static int last_sd_detect_level = -1;
static bool sd_detect_pending = false;
static TickType_t sd_detect_last_edge = 0;

static void app_storage_init(void)
{
    esp_err_t ret = nvs_flash_init();

    if (ret != ESP_OK) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

/**
 * @brief Install the GPIO interrupt service.
 *
 */
static void app_user_button_init(void)
{
    ESP_ERROR_CHECK(user_button_init(RECORD_BUTTON_GPIO_NUM, RECORD_LED_GPIO_NUM));

    gpio_config_t sd_det_pin_cfg = {
        .pin_bit_mask   = SD_DET_GPIO_BIT_MASK,
        .mode           = GPIO_MODE_INPUT,
        .pull_up_en     = GPIO_PULLUP_DISABLE,
        .pull_down_en   = GPIO_PULLDOWN_DISABLE,
        .intr_type      = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&sd_det_pin_cfg));
    last_sd_detect_level = gpio_get_level(SD_DET_GPIO_NUM);

    esp_err_t ret = gpio_install_isr_service(SD_DET_GPIO_ALLOC_FLAG);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(SD_DET_GPIO_NUM, sd_detect_isr, NULL));
}
/**
 * @brief Return mode name as text.
 *
 * @param mode[IN] input mode
 * @return const char*[OUT] equivalent text
 */
static const char *mode_name(MODE mode)
{
    switch (mode) {
    case FAULT_MODE:   return "FAULT";
    case PREPARE_MODE: return "PREPARE";
    case IDLE_MODE:    return "IDLE";
    case RECORD_MODE:  return "RECORD";
    default:           return "UNKNOWN";
    }
}
/**
 * @brief Check whether an event requires PREPARE/recovery handling.
 * @param event[in] event
 * @return true when recovery is required
 */
static bool event_requests_prepare(state_cause_t event)
{
    switch (event) {
    case SM_SD_CHANGED_EVENT:
    case SM_SD_FAIL_EVENT:
    case SM_ADC_FAIL_EVENT:
    case SM_BUFFER_FAIL_EVENT:
    case SM_TASK_FAIL_EVENT:
    case SM_HEALTH_CHECK_FAIL_EVENT:
    case SM_RECORD_START_FAIL_EVENT:
        return true;
    default:
        return false;
    }
}
/**
 * @brief check if transition if valid
 *  check hard coded rule base on system design
 * @param this_state current system state
 * @param next_state next state
 * @param event event cause the transition
 * @return true if transistion is valid
 * @return false if transistion is invalid
 */
static bool select_target_mode(MODE this_state, state_cause_t event,
                               MODE *next_state)
{
    switch (this_state) {
    case FAULT_MODE:
        if (event == SM_BOOT_EVENT) {
            *next_state = PREPARE_MODE;
            return true;
        }
        break;
    case PREPARE_MODE:
        if (event == SM_STATE_PREPARE_DONE_EVENT) {
            *next_state = IDLE_MODE;
            return true;
        }
        if (event == SM_PREPARE_FAIL_EVENT || event == SM_INVALID_MODE_EVENT) {
            *next_state = FAULT_MODE;
            return true;
        }
        if (event_requests_prepare(event)) {
            *next_state = PREPARE_MODE;
            return true;
        }
        break;
    case IDLE_MODE:
        if (event == SM_GPIO_START_REC_EVENT || event == SM_BLE_START_CMD_EVENT) {
            *next_state = RECORD_MODE;
            return true;
        }
        if (event_requests_prepare(event)) {
            *next_state = PREPARE_MODE;
            return true;
        }
        break;
    case RECORD_MODE:
        if (event == SM_GPIO_STOP_REC_EVENT || event == SM_BLE_STOP_CMD_EVENT) {
            *next_state = IDLE_MODE;
            return true;
        }
        if (event_requests_prepare(event)) {
            *next_state = PREPARE_MODE;
            return true;
        }
        break;
    default:
        *next_state = FAULT_MODE;
        return true;
    }
    return false;
}

static bool cause_requires_recovery(state_cause_t event, ecg_pipeline_event_t *pipeline_event)
{
    switch (event) {
    case SM_SD_CHANGED_EVENT:
    case SM_SD_FAIL_EVENT:
    case SM_RECORD_START_FAIL_EVENT:
        *pipeline_event = ECG_PIPELINE_EVENT_SD_FAULT;
        return true;
    case SM_ADC_FAIL_EVENT:
        *pipeline_event = ECG_PIPELINE_EVENT_ADC_FAULT;
        return true;
    case SM_BUFFER_FAIL_EVENT:
        *pipeline_event = ECG_PIPELINE_EVENT_BUFFER_FAULT;
        return true;
    case SM_TASK_FAIL_EVENT:
        *pipeline_event = ECG_PIPELINE_EVENT_TASK_FAULT;
        return true;
    default:
        return false;
    }
}

esp_err_t state_machine_post_event(state_cause_t event, esp_err_t error)
{
    if (event <= STATE_CAUSE_NONE || event > SM_INVALID_MODE_EVENT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (state_event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    state_event_t state_event = {
        .event = event,
        .error = error,
    };
    if (xQueueSend(state_event_queue, &state_event, 0) != pdTRUE) {
        ESP_LOGE(TAG_APP, "State event queue full: event=%d", event);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void update_prepare_context(const state_event_t *state_event,
                                   bool entering_prepare)
{
    ecg_pipeline_event_t recovery_event;

    prepare_context.event = state_event->event;
    prepare_context.error = state_event->error;
    if (cause_requires_recovery(state_event->event, &recovery_event)) {
        prepare_context.recovery_event = recovery_event;
        prepare_context.recovery_pending = true;
    }

    if (entering_prepare) {
        prepare_context.fail_count = 0U;
        last_prepare_attempt = 0;
    }
}

static void event_handler(const state_event_t *state_event)
{
    MODE previous_mode = current_mode;
    MODE target_mode = previous_mode;

    if (!select_target_mode(previous_mode, state_event->event, &target_mode)) {
        ESP_LOGW(TAG_APP, "Ignored event=%d in %s error=%s",
                 state_event->event, mode_name(previous_mode),
                 esp_err_to_name(state_event->error));
        return;
    }

    if (target_mode == PREPARE_MODE) {
        update_prepare_context(state_event, previous_mode != PREPARE_MODE);
    }
    if (previous_mode == target_mode) {
        return;
    }

    esp_err_t ret = ESP_OK;
    if (target_mode == RECORD_MODE) {
        ret = ecg_pipeline_start_recording();
    } else {
        ret = ecg_pipeline_stop_record();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_APP, "Switch %s -> %s fail (%s)",
                 mode_name(previous_mode), mode_name(target_mode),
                 esp_err_to_name(ret));
        if (target_mode == RECORD_MODE) {
            (void)state_machine_post_event(SM_RECORD_START_FAIL_EVENT, ret);
        }
        return;
    }

    current_mode = target_mode;
    user_button_set_recording_led(current_mode == RECORD_MODE);

    if (current_mode == IDLE_MODE) {
        prepare_context.fail_count = 0U;
        last_idle_health_check = 0;
    } else if (current_mode == FAULT_MODE) {
        fault_led_level = false;
        last_fault_led_toggle = 0;
    }

    ESP_LOGW(TAG_APP, "%s -> %s event=%d error=%s",
             mode_name(previous_mode), mode_name(current_mode),
             state_event->event, esp_err_to_name(state_event->error));
}

static void prepare_state_handler(TickType_t now)
{
    if (last_prepare_attempt != 0
        && (now - last_prepare_attempt) < pdMS_TO_TICKS(PREPARE_RETRY_MS)) {
        return;
    }
    last_prepare_attempt = now;

    if (prepare_context.recovery_pending) {
        esp_err_t ret = ecg_pipeline_recover_from_event(prepare_context.recovery_event);
        if (ret == ESP_OK) {
            prepare_context.recovery_pending = false;
        } else {
            prepare_context.error = ret;
        }
    }

    bool health_ok = pipeline_start_complete
        && app_recovery_health_check(
            SD_DET_GPIO_NUM, SD_CARD_PRESENT_LEVEL,
            APP_MIN_FREE_HEAP, APP_MIN_STATE_STACK, "prepare");

    if (!prepare_context.recovery_pending && health_ok) {
        (void)state_machine_post_event(SM_STATE_PREPARE_DONE_EVENT, ESP_OK);
        return;
    }

    prepare_context.fail_count++;
    ESP_LOGW(TAG_APP, "PREPARE attempt %lu/%u failed: event=%d error=%s",
             (unsigned long)prepare_context.fail_count,
             PREPARE_FAIL_LIMIT, prepare_context.event,
             esp_err_to_name(prepare_context.error));

    if (prepare_context.fail_count >= PREPARE_FAIL_LIMIT) {
        (void)state_machine_post_event(SM_PREPARE_FAIL_EVENT,
                           prepare_context.error == ESP_OK
                               ? ESP_FAIL : prepare_context.error);
    }
}

static void idle_state_handler(TickType_t now)
{
    if (last_idle_health_check == 0 || (now - last_idle_health_check) >= pdMS_TO_TICKS(IDLE_HEALTH_CHECK_MS)) {
        last_idle_health_check = now;
        if (!app_recovery_health_check(
                SD_DET_GPIO_NUM, SD_CARD_PRESENT_LEVEL,
                APP_MIN_FREE_HEAP, APP_MIN_STATE_STACK, "idle")) {
            (void)state_machine_post_event(SM_HEALTH_CHECK_FAIL_EVENT, ESP_FAIL);
            return;
        }
    }

    if (user_button_take_press()) {
        (void)state_machine_post_event(SM_GPIO_START_REC_EVENT, ESP_OK);
    }
}

static void record_state_handler(void)
{
    if (user_button_take_press()) {
        (void)state_machine_post_event(SM_GPIO_STOP_REC_EVENT, ESP_OK);
    }
}

static void fault_state_handler(TickType_t now)
{
    if (last_fault_led_toggle == 0
        || (now - last_fault_led_toggle)
            >= pdMS_TO_TICKS(FAULT_LED_TOGGLE_MS)) {
        last_fault_led_toggle = now;
        fault_led_level = !fault_led_level;
        user_button_set_recording_led(fault_led_level);
    }
}
/**
 * @brief Run the periodic behavior for the current state.
 *  Enter a state by calling it's handler function, if state is invalid, switch to fault mode
 * @param now Current scheduler tick
 */
static void enter_state(TickType_t now)
{
    switch (current_mode) {
    case PREPARE_MODE: prepare_state_handler(now); break;
    case IDLE_MODE:    idle_state_handler(now); break;
    case RECORD_MODE:  record_state_handler(); break;
    case FAULT_MODE:   fault_state_handler(now); break;
    default:
        (void)state_machine_post_event(SM_INVALID_MODE_EVENT,
                           ESP_ERR_INVALID_STATE);
        break;
    }
}

static void state_machine_task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        state_event_t state_event;
        if (xQueueReceive(state_event_queue, &state_event,
                          TASK_TICK_TO_WAIT) == pdTRUE) {
            event_handler(&state_event);
        }

        uint32_t notifications = 0;
        (void)xTaskNotifyWait(0, UINT32_MAX, &notifications, 0);
        if ((notifications & APP_NOTIFY_SD_DETECT) != 0U) {
            /* GPIO35 has no internal pull-up. Wait for a quiet interval before
             * accepting a new card-detect level; never enqueue transitions
             * directly this_state a burst of electrical edges. */
            sd_detect_pending = true;
            sd_detect_last_edge = xTaskGetTickCount();
        }

        TickType_t now = xTaskGetTickCount();
        if (sd_detect_pending
            && (now - sd_detect_last_edge)
                >= pdMS_TO_TICKS(SD_DETECT_DEBOUNCE_MS)) {
            sd_detect_pending = false;
            int level = gpio_get_level(SD_DET_GPIO_NUM);
            if (level != last_sd_detect_level) {
                last_sd_detect_level = level;
                (void)state_machine_post_event(SM_SD_CHANGED_EVENT, ESP_FAIL);
            }
        }

        enter_state(now);
    }
}

esp_err_t state_machine_start(void)
{
    esp_log_level_set("*", ESP_LOG_WARN);
    app_storage_init();
    app_user_button_init();

    state_event_queue = xQueueCreate(STATE_EVENT_QUEUE_LEN,
                                     sizeof(state_event_t));
    if (state_event_queue == NULL) {
        ESP_LOGE(TAG_APP, "State event queue allocation failed");
        return ESP_ERR_NO_MEM;
    }

    ecg_pipeline_set_event_callback(pipeline_event_handler);
    ecg_ble_set_command_callback(ble_command_handler);

    if (ecg_pipeline_init() != ESP_OK) {
        ESP_LOGE(TAG_APP, "ECG pipeline init failed");
        return ESP_FAIL;
    }

    if (state_machine_post_event(SM_BOOT_EVENT, ESP_OK) != ESP_OK) {
        return ESP_FAIL;
    }

    if (xTaskCreatePinnedToCore(state_machine_task, "State_Task", 3072,
                                NULL, 2, &state_task_handle,
                                TASK_ASSIGNED_CORE) != pdPASS) {
        ESP_LOGE(TAG_APP, "State task creation failed");
        return ESP_ERR_NO_MEM;
    }
    pipeline_metrics_register_task(METRICS_TASK_STATE, state_task_handle);

    esp_err_t pipeline_task_status = ecg_pipeline_start_tasks();
    if (pipeline_task_status != ESP_OK) {
        ESP_LOGE(TAG_APP, "Pipeline task startup failed (%s)",
                 esp_err_to_name(pipeline_task_status));
        return pipeline_task_status;
    }
    pipeline_start_complete = true;
    return ESP_OK;
}

static void IRAM_ATTR sd_detect_isr(void *arg)
{
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

static state_cause_t pipeline_event_to_cause(ecg_pipeline_event_t event)
{
    switch (event) {
    case ECG_PIPELINE_EVENT_SD_FAULT:     return SM_SD_FAIL_EVENT;
    case ECG_PIPELINE_EVENT_ADC_FAULT:    return SM_ADC_FAIL_EVENT;
    case ECG_PIPELINE_EVENT_BUFFER_FAULT: return SM_BUFFER_FAIL_EVENT;
    case ECG_PIPELINE_EVENT_TASK_FAULT:   return SM_TASK_FAIL_EVENT;
    case ECG_PIPELINE_EVENT_BLE_FAULT:    return SM_BLE_FAIL_EVENT;
    default:                              return SM_INVALID_MODE_EVENT;
    }
}

static void pipeline_event_handler(ecg_pipeline_event_t event, esp_err_t error,
                                   const char *detail)
{
    (void)detail;

    if (event == ECG_PIPELINE_EVENT_BLE_FAULT) {
        ESP_LOGW(TAG_APP, "Non-critical BLE fault: %s",
                 esp_err_to_name(error));
        return;
    }
    (void)state_machine_post_event(pipeline_event_to_cause(event), error);
}

static void ble_command_handler(ecg_ble_command_t command)
{
    switch (command) {
    case ECG_BLE_COMMAND_START:
        (void)state_machine_post_event(SM_BLE_START_CMD_EVENT, ESP_OK);
        break;
    case ECG_BLE_COMMAND_STOP:
        (void)state_machine_post_event(SM_BLE_STOP_CMD_EVENT, ESP_OK);
        break;
    default:
        break;
    }
}

MODE state_machine_get_mode(void)
{
    return current_mode;
}
