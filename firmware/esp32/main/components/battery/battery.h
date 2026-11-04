#ifndef ECG_BATTERY_H
#define ECG_BATTERY_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    uint32_t timestamp_ms;
    uint16_t millivolts;
    uint8_t percent;
    bool valid;
    bool low;
} BatteryReading;

/* Initialize calibration before tasks. Failure leaves telemetry unavailable. */
esp_err_t battery_init(void);
/* ECG_Task only. At most one ADC read/250 ms, no sleep or retry loop. */
void battery_poll(uint32_t nowMs);
/* Copy a coherent snapshot for BLE_TX; invalidates samples older than 10 s. */
BatteryReading battery_snapshot(uint32_t nowMs);

#endif
