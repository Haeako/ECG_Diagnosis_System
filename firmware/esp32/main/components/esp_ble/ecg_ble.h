#ifndef ECG_BLE_H
#define ECG_BLE_H

#include "esp_err.h"
#include "ExtractFeature.h"

typedef enum {
    ECG_BLE_COMMAND_NONE,
    ECG_BLE_COMMAND_START,
    ECG_BLE_COMMAND_STOP,
} ecg_ble_command_t;

typedef void (*ecg_ble_command_cb_t)(ecg_ble_command_t command);

void ecg_ble_set_command_callback(ecg_ble_command_cb_t callback);
esp_err_t ecg_ble_init(void);
void ecg_ble_notify(const ecg_record_t *sample);

#endif
