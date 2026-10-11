#ifndef ECG_UTILS_APP_RECOVERY_H
#define ECG_UTILS_APP_RECOVERY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "ecg_pipeline.h"
#include "freertos/FreeRTOS.h"

#define APP_RECOVERY_PIPELINE_EVENT_BIT(event) (1UL << ((uint32_t)(event) + 8U))
#define APP_RECOVERY_PIPELINE_EVENT_MASK       (0x1FUL << 8)

bool app_recovery_health_check(gpio_num_t sd_detect_pin, int present_level,
                               size_t minimum_free_heap, UBaseType_t minimum_stack_words,
                               const char *context);
bool app_recovery_event_requires_reset(uint32_t pipeline_events);
ecg_pipeline_event_t app_recovery_event_from_notifications(uint32_t pipeline_events);

#endif
