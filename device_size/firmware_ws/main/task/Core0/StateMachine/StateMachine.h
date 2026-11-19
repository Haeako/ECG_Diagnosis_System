#ifndef ECG_STATE_MACHINE_H
#define ECG_STATE_MACHINE_H

#include "esp_err.h"

typedef enum {
    FAULT_MODE   = 0,
    PREPARE_MODE = 1,
    IDLE_MODE    = 2,
    RECORD_MODE  = 3,
} MODE;

typedef enum {
    STATE_CAUSE_NONE = 0,
    SM_BOOT_EVENT,
    SM_GPIO_START_REC_EVENT,
    SM_GPIO_STOP_REC_EVENT,
    SM_BLE_START_CMD_EVENT,
    SM_BLE_STOP_CMD_EVENT,
    SM_SD_CHANGED_EVENT,
    SM_SD_FAIL_EVENT,
    SM_ADC_FAIL_EVENT,
    SM_BUFFER_FAIL_EVENT,
    SM_TASK_FAIL_EVENT,
    SM_BLE_FAIL_EVENT,
    SM_HEALTH_CHECK_FAIL_EVENT,
    SM_RECORD_START_FAIL_EVENT,
    SM_STATE_PREPARE_DONE_EVENT,
    SM_PREPARE_FAIL_EVENT,
    SM_INVALID_MODE_EVENT,
} state_cause_t;

/* Post a fact to the supervisor. Only the supervisor chooses the next mode. */
esp_err_t state_machine_post_event(state_cause_t event, esp_err_t error);

/* Initialize application services and start the Core 0 control task. */
esp_err_t state_machine_start(void);

/* Read-only status for diagnostics. */
MODE state_machine_get_mode(void);

#endif
