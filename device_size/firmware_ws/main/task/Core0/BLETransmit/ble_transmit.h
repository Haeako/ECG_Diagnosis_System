#ifndef ECG_BLE_TRANSMIT_H
#define ECG_BLE_TRANSMIT_H

#include "esp_err.h"
#include "ExtractFeature.h"

/* Core 1 producer: non-blocking handoff of telemetry to the core 0 BLE task. */
esp_err_t ble_transmit_start(void);
void ble_transmit_enqueue(const ecg_record_t *sample);

#endif
