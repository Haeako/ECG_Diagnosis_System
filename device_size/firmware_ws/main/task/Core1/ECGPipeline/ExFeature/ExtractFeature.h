#ifndef ECG_SIGNAL_H
#define ECG_SIGNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t sample_index; /* Acquisition sequence; gaps expose dropped samples. */
    uint64_t sample_time_us, scheduled_us, r_peak_time_us;
    uint32_t isr_delay_us, adc_duration_us;
    uint32_t timestamp_ms;
    uint16_t raw_adc;
    int16_t raw_centered;
    int16_t notch_49_51hz;
    float ma_qrs_100ms;
    float threshold;
    /* Zero: the breakout board already provides the analog 0.5 Hz HPF. */
    int16_t baseline;
    /* Notch output; retained for CSV/BLE compatibility. */
    int16_t corrected;
    /* Causal aggressive 5-18 Hz QRS-band signal, not morphology-preserving. */
    int16_t filtered;
    uint8_t is_peak;
    /* Retrospective Fast-QRS event; never a second waveform sample. */
    uint8_t analysis_event;
    uint32_t r_peak_timestamp_ms;
    uint16_t rr_interval_ms;
    uint16_t instant_bpm;
    uint16_t bpm;
    /* 0..100; 255 means the 10-second SQI window is not ready. */
    uint8_t sqi_score;
    uint8_t sqi_status;
    /* Orphanidou shadow telemetry; it does not gate the ECG/AF path. */
    uint8_t sqi_rule_mask;
    uint16_t sqi_heart_rate_bpm;
    uint16_t sqi_max_rr_ms;
    uint16_t sqi_rr_ratio_x1000;
    int16_t sqi_template_correlation_x1000;
    uint32_t sqi_window_id;
    uint32_t sqi_finalize_us;
    /* On-device AF screening telemetry from the frozen INT8 Extra Trees model. */
    uint16_t af_probability_x10000;
    uint16_t af_score;
    uint16_t af_event_id;
    uint32_t af_window_id;
    uint8_t af_valid;
    uint8_t af_raw_prediction;
    uint8_t af_confirmed;
    uint8_t af_new_window;
    uint8_t af_new_event;
} ecg_record_t;

/* Breakout board already provides an analog 0.5-40 Hz band-pass. */
#define ADC_BASELINE             2048
#define ECG_SAMPLE_RATE_HZ       360U


void ecg_signal_init(uint32_t now_ms);
ecg_record_t ecg_signal_process(int raw_adc, uint32_t now_ms);

#define ECG_WINDOW_SAMPLES 3600U
#define ECG_HOP_SAMPLES 1800U
typedef struct {
    const float *samples; /* notch output / 200 ADC counts */
    const uint16_t *peaks;
    size_t peak_count;
    uint32_t start_ms;
    uint32_t sequence;
} ecg_signal_window_t;
/* Valid until the next ecg_signal_process() call, or NULL without a new hop. */
const ecg_signal_window_t *ecg_signal_completed_window(void);

#endif
