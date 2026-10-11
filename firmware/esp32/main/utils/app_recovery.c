#include "app_recovery.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "APP_RECOVERY";

bool app_recovery_health_check(gpio_num_t sd_detect_pin, int present_level,
                               size_t minimum_free_heap, UBaseType_t minimum_stack_words,
                               const char *context)
{
    if (!heap_caps_check_integrity_all(false)) {
        ESP_LOGE(TAG, "Health[%s]: heap integrity failed", context);
        return false;
    }
    if (esp_get_free_heap_size() < minimum_free_heap) {
        ESP_LOGE(TAG, "Health[%s]: low heap", context);
        return false;
    }
    if (uxTaskGetStackHighWaterMark(NULL) < minimum_stack_words) {
        ESP_LOGE(TAG, "Health[%s]: low state task stack", context);
        return false;
    }
    return gpio_get_level(sd_detect_pin) == present_level;
}

bool app_recovery_event_requires_reset(uint32_t pipeline_events)
{
    const uint32_t reset_events = APP_RECOVERY_PIPELINE_EVENT_BIT(ECG_PIPELINE_EVENT_SD_FAULT)
        | APP_RECOVERY_PIPELINE_EVENT_BIT(ECG_PIPELINE_EVENT_ADC_FAULT)
        | APP_RECOVERY_PIPELINE_EVENT_BIT(ECG_PIPELINE_EVENT_BUFFER_FAULT)
        | APP_RECOVERY_PIPELINE_EVENT_BIT(ECG_PIPELINE_EVENT_TASK_FAULT);
    return (pipeline_events & reset_events) != 0U;
}

ecg_pipeline_event_t app_recovery_event_from_notifications(uint32_t pipeline_events)
{
    if (pipeline_events & APP_RECOVERY_PIPELINE_EVENT_BIT(ECG_PIPELINE_EVENT_SD_FAULT)) {
        return ECG_PIPELINE_EVENT_SD_FAULT;
    }
    if (pipeline_events & APP_RECOVERY_PIPELINE_EVENT_BIT(ECG_PIPELINE_EVENT_ADC_FAULT)) {
        return ECG_PIPELINE_EVENT_ADC_FAULT;
    }
    if (pipeline_events & APP_RECOVERY_PIPELINE_EVENT_BIT(ECG_PIPELINE_EVENT_BUFFER_FAULT)) {
        return ECG_PIPELINE_EVENT_BUFFER_FAULT;
    }
    if (pipeline_events & APP_RECOVERY_PIPELINE_EVENT_BIT(ECG_PIPELINE_EVENT_TASK_FAULT)) {
        return ECG_PIPELINE_EVENT_TASK_FAULT;
    }
    return ECG_PIPELINE_EVENT_BLE_FAULT;
}
