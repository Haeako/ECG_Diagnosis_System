#ifndef ECG_SIGNAL_H
#define ECG_SIGNAL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t timestamp_ms;
    uint16_t raw_adc;
    int16_t raw_centered;
    /* Moving-average estimate of slow baseline wander. */
    int16_t baseline;
    /* Baseline-removed signal: raw_centered - baseline. */
    int16_t corrected;
    int16_t filtered;
    uint8_t is_peak;
    uint32_t r_peak_timestamp_ms;
    uint16_t rr_interval_ms;
    uint16_t instant_bpm;
    uint16_t bpm;
    uint8_t afib_status;
    uint8_t afib_score;
} ecg_record_t;

void ecg_signal_init(uint32_t now_ms);
void ecg_signal_reset(uint32_t now_ms);
ecg_record_t ecg_signal_process(int raw_adc, uint32_t now_ms);

#endif
