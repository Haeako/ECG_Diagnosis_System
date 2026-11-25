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
typedef struct { uint32_t packets, samples, retries, failures, not_ready, indication_acks, ack_timeouts, ack_max_us; } ecg_ble_stats_t;
bool ecg_ble_ready(void);
void ecg_ble_flush(void);
void ecg_ble_begin_session(void);
ecg_ble_stats_t ecg_ble_stats(void);
void ecg_ble_notify(const ecg_record_t *sample);
/* BLE_TX only, including idle. Adds packet type 3 without changing ECG v1. */
void ecg_ble_notify_battery(void);

#endif
