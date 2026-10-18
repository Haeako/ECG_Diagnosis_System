#include <stdbool.h>
#include <stdint.h>

#include "StateMachine.h"

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

#define RECORD_BUTTON_GPIO           GPIO_NUM_0
#define RECORD_LED_GPIO              GPIO_NUM_21
#define SD_DETECT_GPIO               GPIO_NUM_35

#define STATE_TASK_PERIOD_MS         20U
#define STATE_MACHINE_CORE           0
#define STATE_TRANSITION_QUEUE_LEN   8U

#define APP_MIN_FREE_HEAP            (20U * 1024U)
#define APP_MIN_STATE_STACK          512U
#define SD_CARD_PRESENT_LEVEL        1
#define PREPARE_FAIL_LIMIT           20U

#define APP_NOTIFY_SD_DETECT         (1UL << 0)

typedef struct {
    MODE target_mode;
    state_cause_t event;
    esp_err_t error;
} state_transition_t;

typedef struct {
    state_cause_t event;
    esp_err_t error;
    ecg_pipeline_event_t recovery_event;
    bool recovery_pending;
    uint32_t fail_count;
} prepare_context_t;

static void sd_detect_isr(void *arg);
static void pipeline_event_handler(ecg_pipeline_event_t event, esp_err_t error,
                                   const char *detail);
static void ble_command_handler(ecg_ble_command_t command);

static const char *TAG_APP = "ECG_APP";
static MODE current_mode = FAULT_MODE;
static TaskHandle_t state_task_handle = NULL;
static QueueHandle_t state_transition_queue = NULL;
static volatile bool pipeline_start_complete = false;

static prepare_context_t prepare_context = {
    .event = STATE_CAUSE_NONE,
    .error = ESP_OK,
    .recovery_event = ECG_PIPELINE_EVENT_SD_FAULT,
    .recovery_pending = false,
    .fail_count = 0U,
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
    last_sd_detect_level = gpio_get_level(SD_DETECT_GPIO);

    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(ret);
    }
    ESP_ERROR_CHECK(gpio_isr_handler_add(SD_DETECT_GPIO, sd_detect_isr, NULL));
}

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

static bool event_requests_prepare(state_cause_t event)
{
    switch (event) {
    case STATE_CAUSE_SD_CHANGED:
    case STATE_CAUSE_SD_FAIL:
    case STATE_CAUSE_ADC_FAIL:
    case STATE_CAUSE_BUFFER_FAIL:
    case STATE_CAUSE_TASK_FAIL:
    case STATE_CAUSE_HEALTH_CHECK_FAIL:
    case STATE_CAUSE_RECORD_START_FAIL:
        return true;
    default:
        return false;
    }
}

static bool transition_is_allowed(MODE from, MODE to, state_cause_t event)
{
    if (from == to) {
        return true;
    }

    switch (from) {
    case FAULT_MODE:
        return to == PREPARE_MODE && event == STATE_CAUSE_BOOT;
    case PREPARE_MODE:
        return (to == IDLE_MODE && event == STATE_CAUSE_PREPARE_READY)
            || (to == FAULT_MODE
                && (event == STATE_CAUSE_PREPARE_RETRY_EXCEEDED
                    || event == STATE_CAUSE_INVALID_MODE));
    case IDLE_MODE:
        return (to == RECORD_MODE
                && (event == STATE_CAUSE_USER_START
                    || event == STATE_CAUSE_BLE_START))
            || (to == PREPARE_MODE && event_requests_prepare(event));
    case RECORD_MODE:
        return (to == IDLE_MODE
                && (event == STATE_CAUSE_USER_STOP
                    || event == STATE_CAUSE_BLE_STOP))
            || (to == PREPARE_MODE && event_requests_prepare(event));
    default:
        return to == FAULT_MODE;
    }
}

static bool cause_requires_recovery(state_cause_t event,
                                    ecg_pipeline_event_t *pipeline_event)
{
    switch (event) {
    case STATE_CAUSE_SD_CHANGED:
    case STATE_CAUSE_SD_FAIL:
    case STATE_CAUSE_RECORD_START_FAIL:
        *pipeline_event = ECG_PIPELINE_EVENT_SD_FAULT;
        return true;
    case STATE_CAUSE_ADC_FAIL:
        *pipeline_event = ECG_PIPELINE_EVENT_ADC_FAULT;
        return true;
    case STATE_CAUSE_BUFFER_FAIL:
        *pipeline_event = ECG_PIPELINE_EVENT_BUFFER_FAULT;
        return true;
    case STATE_CAUSE_TASK_FAIL:
        *pipeline_event = ECG_PIPELINE_EVENT_TASK_FAULT;
        return true;
    default:
        return false;
    }
}

esp_err_t state_switch(MODE target_mode, state_cause_t event, esp_err_t error)
{
    if (target_mode < FAULT_MODE || target_mode > RECORD_MODE) {
        return ESP_ERR_INVALID_ARG;
    }
    if (state_transition_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    state_transition_t transition = {
        .target_mode = target_mode,
        .event = event,
        .error = error,
    };
    if (xQueueSend(state_transition_queue, &transition, 0) != pdTRUE) {
        ESP_LOGE(TAG_APP, "State transition queue full");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

static void update_prepare_context(const state_transition_t *transition,
                                   bool entering_prepare)
{
    ecg_pipeline_event_t recovery_event;

    prepare_context.event = transition->event;
    prepare_context.error = transition->error;
    if (cause_requires_recovery(transition->event, &recovery_event)) {
        prepare_context.recovery_event = recovery_event;
        prepare_context.recovery_pending = true;
    }

    if (entering_prepare) {
        prepare_context.fail_count = 0U;
        last_prepare_attempt = 0;
    }
}

static void handle_transition(const state_transition_t *transition)
{
    MODE previous_mode = current_mode;

    if (!transition_is_allowed(previous_mode, transition->target_mode,
                               transition->event)) {
        ESP_LOGW(TAG_APP, "Rejected %s -> %s event=%d error=%s",
                 mode_name(previous_mode), mode_name(transition->target_mode),
                 transition->event, esp_err_to_name(transition->error));
        return;
    }

    if (transition->target_mode == PREPARE_MODE) {
        update_prepare_context(transition, previous_mode != PREPARE_MODE);
    }
    if (previous_mode == transition->target_mode) {
        return;
    }

    esp_err_t ret = ecg_pipeline_apply_mode(transition->target_mode);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_APP, "Pipeline rejected %s -> %s (%s)",
                 mode_name(previous_mode), mode_name(transition->target_mode),
                 esp_err_to_name(ret));
        if (transition->target_mode == RECORD_MODE) {
            (void)state_switch(PREPARE_MODE,
                               STATE_CAUSE_RECORD_START_FAIL, ret);
        }
        return;
    }

    current_mode = transition->target_mode;
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
             transition->event, esp_err_to_name(transition->error));
}

static void prepare_state_handler(TickType_t now)
{
    if (last_prepare_attempt != 0
        && (now - last_prepare_attempt) < pdMS_TO_TICKS(PREPARE_RETRY_MS)) {
        return;
    }
    last_prepare_attempt = now;

    if (prepare_context.recovery_pending) {
        esp_err_t ret = ecg_pipeline_recover_from_event(
            prepare_context.recovery_event);
        if (ret == ESP_OK) {
            prepare_context.recovery_pending = false;
        } else {
            prepare_context.error = ret;
        }
    }

    bool health_ok = pipeline_start_complete
        && app_recovery_health_check(
            SD_DETECT_GPIO, SD_CARD_PRESENT_LEVEL,
            APP_MIN_FREE_HEAP, APP_MIN_STATE_STACK, "prepare");

    if (!prepare_context.recovery_pending && health_ok) {
        (void)state_switch(IDLE_MODE, STATE_CAUSE_PREPARE_READY, ESP_OK);
        return;
    }

    prepare_context.fail_count++;
    ESP_LOGW(TAG_APP, "PREPARE attempt %lu/%u failed: event=%d error=%s",
             (unsigned long)prepare_context.fail_count,
             PREPARE_FAIL_LIMIT, prepare_context.event,
             esp_err_to_name(prepare_context.error));

    if (prepare_context.fail_count >= PREPARE_FAIL_LIMIT) {
        (void)state_switch(FAULT_MODE,
                           STATE_CAUSE_PREPARE_RETRY_EXCEEDED,
                           prepare_context.error == ESP_OK
                               ? ESP_FAIL : prepare_context.error);
    }
}

static void idle_state_handler(TickType_t now)
{
    if (last_idle_health_check == 0
        || (now - last_idle_health_check)
            >= pdMS_TO_TICKS(IDLE_HEALTH_CHECK_MS)) {
        last_idle_health_check = now;
        if (!app_recovery_health_check(
                SD_DETECT_GPIO, SD_CARD_PRESENT_LEVEL,
                APP_MIN_FREE_HEAP, APP_MIN_STATE_STACK, "idle")) {
            (void)state_switch(PREPARE_MODE,
                               STATE_CAUSE_HEALTH_CHECK_FAIL, ESP_FAIL);
            return;
        }
    }

    if (user_button_take_press()) {
        (void)state_switch(RECORD_MODE, STATE_CAUSE_USER_START, ESP_OK);
    }
}

static void record_state_handler(void)
{
    if (user_button_take_press()) {
        (void)state_switch(IDLE_MODE, STATE_CAUSE_USER_STOP, ESP_OK);
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

static void run_current_state(TickType_t now)
{
    switch (current_mode) {
    case PREPARE_MODE: prepare_state_handler(now); break;
    case IDLE_MODE:    idle_state_handler(now); break;
    case RECORD_MODE:  record_state_handler(); break;
    case FAULT_MODE:   fault_state_handler(now); break;
    default:
        (void)state_switch(FAULT_MODE, STATE_CAUSE_INVALID_MODE,
                           ESP_ERR_INVALID_STATE);
        break;
    }
}

static void state_machine_task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        state_transition_t transition;
        if (xQueueReceive(state_transition_queue, &transition,
                          pdMS_TO_TICKS(STATE_TASK_PERIOD_MS)) == pdTRUE) {
            handle_transition(&transition);
        }

        uint32_t notifications = 0;
        (void)xTaskNotifyWait(0, UINT32_MAX, &notifications, 0);
        if ((notifications & APP_NOTIFY_SD_DETECT) != 0U) {
            /* GPIO35 has no internal pull-up. Wait for a quiet interval before
             * accepting a new card-detect level; never enqueue transitions
             * directly from a burst of electrical edges. */
            sd_detect_pending = true;
            sd_detect_last_edge = xTaskGetTickCount();
        }

        TickType_t now = xTaskGetTickCount();
        if (sd_detect_pending
            && (now - sd_detect_last_edge)
                >= pdMS_TO_TICKS(SD_DETECT_DEBOUNCE_MS)) {
            sd_detect_pending = false;
            int level = gpio_get_level(SD_DETECT_GPIO);
            if (level != last_sd_detect_level) {
                last_sd_detect_level = level;
                (void)state_switch(PREPARE_MODE, STATE_CAUSE_SD_CHANGED,
                                   ESP_FAIL);
            }
        }

        run_current_state(now);
    }
}

esp_err_t state_machine_start(void)
{
    esp_log_level_set("*", ESP_LOG_WARN);
    app_storage_init();
    app_user_button_init();

    state_transition_queue = xQueueCreate(
        STATE_TRANSITION_QUEUE_LEN, sizeof(state_transition_t));
    if (state_transition_queue == NULL) {
        ESP_LOGE(TAG_APP, "State transition queue allocation failed");
        return ESP_ERR_NO_MEM;
    }

    ecg_pipeline_set_event_callback(pipeline_event_handler);
    ecg_ble_set_command_callback(ble_command_handler);

    if (ecg_pipeline_init() != ESP_OK) {
        ESP_LOGE(TAG_APP, "ECG pipeline init failed");
        return ESP_FAIL;
    }

    if (state_switch(PREPARE_MODE, STATE_CAUSE_BOOT, ESP_OK) != ESP_OK) {
        return ESP_FAIL;
    }

    if (xTaskCreatePinnedToCore(state_machine_task, "State_Task", 3072,
                                NULL, 2, &state_task_handle,
                                STATE_MACHINE_CORE) != pdPASS) {
        ESP_LOGE(TAG_APP, "State task creation failed");
        return ESP_ERR_NO_MEM;
    }

    ecg_pipeline_start_task();
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
    case ECG_PIPELINE_EVENT_SD_FAULT:     return STATE_CAUSE_SD_FAIL;
    case ECG_PIPELINE_EVENT_ADC_FAULT:    return STATE_CAUSE_ADC_FAIL;
    case ECG_PIPELINE_EVENT_BUFFER_FAULT: return STATE_CAUSE_BUFFER_FAIL;
    case ECG_PIPELINE_EVENT_TASK_FAULT:   return STATE_CAUSE_TASK_FAIL;
    case ECG_PIPELINE_EVENT_BLE_FAULT:    return STATE_CAUSE_BLE_FAIL;
    default:                              return STATE_CAUSE_INVALID_MODE;
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
    (void)state_switch(PREPARE_MODE, pipeline_event_to_cause(event), error);
}

static void ble_command_handler(ecg_ble_command_t command)
{
    switch (command) {
    case ECG_BLE_COMMAND_START:
        (void)state_switch(RECORD_MODE, STATE_CAUSE_BLE_START, ESP_OK);
        break;
    case ECG_BLE_COMMAND_STOP:
        (void)state_switch(IDLE_MODE, STATE_CAUSE_BLE_STOP, ESP_OK);
        break;
    default:
        break;
    }
}
