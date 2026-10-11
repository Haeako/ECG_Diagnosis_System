#ifndef ECG_SIGNAL_H
#define ECG_SIGNAL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t timestamp_ms;
    uint16_t raw_adc;
    int16_t raw_centered;
    int16_t notch_49_51hz;
    float ma_qrs_100ms;
    float threshold;
    /* Zero: the breakout board already provides the analog 0.5 Hz HPF. */
    int16_t baseline;
    /* Same as raw_centered; retained for CSV/BLE compatibility. */
    int16_t corrected;
    /* Causal aggressive 5-18 Hz QRS-band signal, not morphology-preserving. */
    int16_t filtered;
    uint8_t is_peak;
    uint32_t r_peak_timestamp_ms;
    uint16_t rr_interval_ms;
    uint16_t instant_bpm;
    uint16_t bpm;
} ecg_record_t;

#include <math.h>
#include <string.h>

#include "components/filter/SBfilter.h"

/* Breakout board already provides an analog 0.5-40 Hz band-pass. */
#define ADC_BASELINE             2048
#define ECG_SAMPLE_RATE_HZ       360U
#define QRS_MA_SAMPLES           36U   /* 100 ms */
#define BEAT_MA_SAMPLES          216U  /* 600 ms */
#define MIN_BLOCK_SAMPLES        22U   /* 60 ms */
#define DETECTOR_WARMUP_SAMPLES  126U  /* 350 ms: reject startup/P-wave */
#define RAW_HISTORY_SAMPLES      128U
#define ALIGN_BACK_MS            90U
#define ALIGN_FORWARD_MS         15U
#define PAN_REFRACTORY_MS        300U
#define BPM_UPDATE_MS            5000U
#define TWO_MA_BETA              0.08f
#define PAN_THRESHOLD_FRACTION   0.15f
#define LEVEL_NEW_WEIGHT         0.125f
#define LEVEL_OLD_WEIGHT         0.875f

typedef struct {
    float b0, b1, b2, a1, a2;
    float z1, z2;
} biquad_t;

typedef struct {
    float values[BEAT_MA_SAMPLES];
    float sum;
    uint16_t index;
    uint16_t count;
    uint16_t window;
} energy_ma_t;

typedef struct {
    int16_t centered[RAW_HISTORY_SAMPLES];
    uint32_t timestamp_ms[RAW_HISTORY_SAMPLES];
    uint16_t index;
    uint16_t count;
} raw_history_t;

typedef struct {
    biquad_t highpass;
    biquad_t lowpass;
    energy_ma_t qrs_ma;
    energy_ma_t beat_ma;
    raw_history_t raw_history;
    uint32_t sample_count;
    float mean_energy;
    bool in_block;
    uint16_t block_samples;
    float block_candidate_energy;
    float block_largest_bandpass;
    uint32_t block_filtered_peak_ms;
    bool pan_initialized;
    float pan_signal_level;
    float pan_noise_level;
    uint32_t last_peak_ms;
    uint32_t current_peak_ms;
    uint16_t current_rr_ms;
    uint16_t current_instant_bpm;
    uint32_t bpm_window_start_ms;
    uint32_t rr_sum_ms;
    uint16_t rr_count;
    uint16_t bpm;
} qrs_detector_t;


void ecg_signal_init(uint32_t now_ms);
void ecg_signal_reset(uint32_t now_ms);
ecg_record_t ecg_signal_process(int raw_adc, uint32_t now_ms);

#endif
