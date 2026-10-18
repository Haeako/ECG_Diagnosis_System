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
    STATE_CAUSE_BOOT,
    STATE_CAUSE_USER_START,
    STATE_CAUSE_USER_STOP,
    STATE_CAUSE_BLE_START,
    STATE_CAUSE_BLE_STOP,
    STATE_CAUSE_SD_CHANGED,
    STATE_CAUSE_SD_FAIL,
    STATE_CAUSE_ADC_FAIL,
    STATE_CAUSE_BUFFER_FAIL,
    STATE_CAUSE_TASK_FAIL,
    STATE_CAUSE_BLE_FAIL,
    STATE_CAUSE_HEALTH_CHECK_FAIL,
    STATE_CAUSE_RECORD_START_FAIL,
    STATE_CAUSE_PREPARE_READY,
    STATE_CAUSE_PREPARE_RETRY_EXCEEDED,
    STATE_CAUSE_INVALID_MODE,
} state_cause_t;

/* Queue a transition request; only the state-machine task applies it. */
esp_err_t state_switch(MODE target_mode, state_cause_t event, esp_err_t error);

/* Initialize application services and start the Core 0 control task. */
esp_err_t state_machine_start(void);

#endif
