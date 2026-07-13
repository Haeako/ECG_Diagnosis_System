#ifndef ECG_PIPELINE_H
#define ECG_PIPELINE_H

#include "esp_err.h"

typedef enum {
    ECG_PIPELINE_EVENT_SD_FAULT,
    ECG_PIPELINE_EVENT_ADC_FAULT,
    ECG_PIPELINE_EVENT_BUFFER_FAULT,
    ECG_PIPELINE_EVENT_TASK_FAULT,
    ECG_PIPELINE_EVENT_BLE_FAULT,
} ecg_pipeline_event_t;

typedef void (*ecg_pipeline_event_cb_t)(ecg_pipeline_event_t event,
                                        esp_err_t error,
                                        const char *detail);

void ecg_pipeline_set_event_callback(ecg_pipeline_event_cb_t callback);
esp_err_t ecg_pipeline_init(void);
void ecg_pipeline_start(void);
esp_err_t ecg_pipeline_enter_recording(void);
void ecg_pipeline_enter_idle(void);
void ecg_pipeline_enter_init(void);

#endif
