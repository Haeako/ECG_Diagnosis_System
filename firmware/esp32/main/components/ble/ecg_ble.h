#ifndef ECG_BLE_H
#define ECG_BLE_H

#include "esp_err.h"
#include "ecg_signal.h"

esp_err_t ecg_ble_init(void);
void ecg_ble_notify(const ecg_record_t *sample);

#endif
