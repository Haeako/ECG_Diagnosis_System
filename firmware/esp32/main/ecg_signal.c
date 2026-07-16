#include "ecg_signal.h"

#include <math.h>
#include <string.h>

#include "components/afib/afib_detector.h"
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

static qrs_detector_t detector;
static SBfilterType notch_filter;

static float biquad_push(biquad_t *filter, float input)
{
    float output = filter->b0 * input + filter->z1;
    filter->z1 = filter->b1 * input - filter->a1 * output + filter->z2;
    filter->z2 = filter->b2 * input - filter->a2 * output;
    return output;
}

static void energy_ma_init(energy_ma_t *average, uint16_t window)
{
    memset(average, 0, sizeof(*average));
    average->window = window;
}

static float energy_ma_push(energy_ma_t *average, float value)
{
    if (average->count < average->window) {
        average->values[average->index] = value;
        average->sum += value;
        average->count++;
    } else {
        average->sum -= average->values[average->index];
        average->values[average->index] = value;
        average->sum += value;
    }
    average->index = (uint16_t)((average->index + 1U) % average->window);
    return average->sum / (float)average->count;
}

static void raw_history_push(raw_history_t *history, int16_t centered,
                             uint32_t now_ms)
{
    history->centered[history->index] = centered;
    history->timestamp_ms[history->index] = now_ms;
    history->index = (uint16_t)((history->index + 1U) % RAW_HISTORY_SAMPLES);
    if (history->count < RAW_HISTORY_SAMPLES) history->count++;
}

static uint32_t align_r_peak(const raw_history_t *history,
                             uint32_t filtered_peak_ms)
{
    uint32_t from_ms = filtered_peak_ms > ALIGN_BACK_MS
        ? filtered_peak_ms - ALIGN_BACK_MS : 0U;
    uint32_t to_ms = filtered_peak_ms + ALIGN_FORWARD_MS;
    int16_t best_value = INT16_MIN;
    uint32_t best_ms = filtered_peak_ms;

    for (uint16_t offset = 0; offset < history->count; ++offset) {
        uint16_t slot = (uint16_t)((history->index + RAW_HISTORY_SAMPLES
            - history->count + offset) % RAW_HISTORY_SAMPLES);
        uint32_t timestamp = history->timestamp_ms[slot];
        if (timestamp >= from_ms && timestamp <= to_ms
            && history->centered[slot] > best_value) {
            best_value = history->centered[slot];
            best_ms = timestamp;
        }
    }
    return best_ms;
}

static int16_t clamp_int16(float value)
{
    if (value > 32767.0f) return INT16_MAX;
    if (value < -32768.0f) return INT16_MIN;
    return (int16_t)value;
}

static void update_bpm_window(uint32_t now_ms)
{
    if ((now_ms - detector.bpm_window_start_ms) < BPM_UPDATE_MS) return;
    if (detector.rr_count > 0U && detector.rr_sum_ms > 0U) {
        detector.bpm = (uint16_t)((60000UL * detector.rr_count)
            / detector.rr_sum_ms);
    }
    detector.rr_sum_ms = 0U;
    detector.rr_count = 0U;
    detector.bpm_window_start_ms = now_ms;
}

static bool pan_accept_block(void)
{
    float candidate = detector.block_candidate_energy;
    float threshold;
    uint32_t peak_ms = align_r_peak(&detector.raw_history,
                                    detector.block_filtered_peak_ms);

    if (!detector.pan_initialized) {
        detector.pan_signal_level = candidate;
        threshold = PAN_THRESHOLD_FRACTION * candidate;
        detector.pan_initialized = true;
    } else {
        threshold = detector.pan_noise_level + PAN_THRESHOLD_FRACTION
            * (detector.pan_signal_level - detector.pan_noise_level);
    }

    /* Refractory duplicates/T waves must not inflate the learned noise. */
    if (detector.last_peak_ms != 0U
        && (peak_ms - detector.last_peak_ms) < PAN_REFRACTORY_MS) {
        return false;
    }
    if (candidate < threshold) {
        detector.pan_noise_level = LEVEL_NEW_WEIGHT * candidate
            + LEVEL_OLD_WEIGHT * detector.pan_noise_level;
        return false;
    }

    /* Do not let an ADC-clipped spike lock the adaptive threshold high. */
    float learned = candidate;
    if (detector.pan_signal_level > 0.0f
        && learned > 2.0f * detector.pan_signal_level) {
        learned = 2.0f * detector.pan_signal_level;
    }
    detector.pan_signal_level = LEVEL_NEW_WEIGHT * learned
        + LEVEL_OLD_WEIGHT * detector.pan_signal_level;

    detector.current_peak_ms = peak_ms;
    if (detector.last_peak_ms != 0U) {
        uint32_t rr_ms = peak_ms - detector.last_peak_ms;
        if (rr_ms >= 250U && rr_ms <= 2000U) {
            detector.current_rr_ms = (uint16_t)rr_ms;
            detector.current_instant_bpm = (uint16_t)(60000UL / rr_ms);
            detector.rr_sum_ms += rr_ms;
            detector.rr_count++;
        }
    }
    detector.last_peak_ms = peak_ms;
    return true;
}

static bool qrs_detector_push(int16_t centered, uint32_t now_ms,
                              int16_t *notched_out,
                              int16_t *filtered_out,
                              float *ma_qrs_out,
                              float *threshold_out)
{
    float notch_input = (float)centered;
    SBfilter_writeInput(&notch_filter, notch_input);
    float notched = SBfilter_readOutput(&notch_filter);
    float bandpass = biquad_push(&detector.highpass, notched);
    bandpass = biquad_push(&detector.lowpass, bandpass);
    float positive = bandpass > 0.0f ? bandpass : 0.0f;
    float energy = positive * positive;
    float ma_qrs = energy_ma_push(&detector.qrs_ma, energy);
    float ma_beat = energy_ma_push(&detector.beat_ma, energy);

    raw_history_push(&detector.raw_history, centered, now_ms);
    *filtered_out = clamp_int16(bandpass);
    detector.current_peak_ms = 0U;
    detector.current_rr_ms = 0U;

    detector.sample_count++;
    uint32_t mean_count = detector.sample_count < 3600U
        ? detector.sample_count : 3600U;
    detector.mean_energy += (energy - detector.mean_energy) / (float)mean_count;
    float threshold = ma_beat + TWO_MA_BETA * detector.mean_energy;
    *notched_out = clamp_int16(notched);
    *ma_qrs_out = ma_qrs;
    *threshold_out = threshold;
    bool above = detector.sample_count >= DETECTOR_WARMUP_SAMPLES
        && ma_qrs > threshold;
    bool is_peak = false;

    if (above) {
        float magnitude = fabsf(bandpass);
        if (!detector.in_block) {
            detector.in_block = true;
            detector.block_samples = 0U;
            detector.block_candidate_energy = ma_qrs;
            detector.block_largest_bandpass = magnitude;
            detector.block_filtered_peak_ms = now_ms;
        }
        detector.block_samples++;
        if (ma_qrs > detector.block_candidate_energy) {
            detector.block_candidate_energy = ma_qrs;
        }
        if (magnitude > detector.block_largest_bandpass) {
            detector.block_largest_bandpass = magnitude;
            detector.block_filtered_peak_ms = now_ms;
        }
    } else if (detector.in_block) {
        if (detector.block_samples >= MIN_BLOCK_SAMPLES) {
            is_peak = pan_accept_block();
        }
        detector.in_block = false;
    }

    update_bpm_window(now_ms);
    return is_peak;
}

void ecg_signal_init(uint32_t now_ms)
{
    SBfilter_init(&notch_filter);
    afib_detector_init();
    ecg_signal_reset(now_ms);
}

void ecg_signal_reset(uint32_t now_ms)
{
    memset(&detector, 0, sizeof(detector));
    SBfilter_reset(&notch_filter);
    /* 2nd-order Butterworth sections designed for fs=360 Hz. */
    detector.highpass = (biquad_t){
        .b0 = 0.940156963896f, .b1 = -1.88031392779f,
        .b2 = 0.940156963896f, .a1 = -1.87672952684f,
        .a2 = 0.883898328745f,
    };
    detector.lowpass = (biquad_t){
        .b0 = 0.0200833655642f, .b1 = 0.0401667311284f,
        .b2 = 0.0200833655642f, .a1 = -1.5610180758f,
        .a2 = 0.641351538058f,
    };
    energy_ma_init(&detector.qrs_ma, QRS_MA_SAMPLES);
    energy_ma_init(&detector.beat_ma, BEAT_MA_SAMPLES);
    detector.bpm_window_start_ms = now_ms;
    afib_detector_reset();
}

ecg_record_t ecg_signal_process(int raw_adc, uint32_t now_ms)
{
    int16_t raw_centered = (int16_t)(raw_adc - ADC_BASELINE);
    int16_t notched = 0;
    int16_t filtered = 0;
    float ma_qrs = 0.0f;
    float threshold = 0.0f;
    bool is_peak = qrs_detector_push(raw_centered, now_ms, &notched,
                                     &filtered, &ma_qrs, &threshold);
    ecg_record_t sample = {
        .timestamp_ms = now_ms,
        .raw_adc = (uint16_t)raw_adc,
        .raw_centered = raw_centered,
        .notch_49_51hz = notched,
        .ma_qrs_100ms = ma_qrs,
        .threshold = threshold,
        /* Analog HPF already removes baseline wander. */
        .baseline = 0,
        .corrected = raw_centered,
        .filtered = filtered,
        .is_peak = is_peak ? 1U : 0U,
        .r_peak_timestamp_ms = detector.current_peak_ms,
        .rr_interval_ms = detector.current_rr_ms,
        .instant_bpm = detector.current_instant_bpm,
        .bpm = detector.bpm,
    };

    if (sample.rr_interval_ms > 0U) {
        afib_result_t afib = afib_detector_process_rr(sample.rr_interval_ms);
        sample.afib_status = (uint8_t)afib.status;
        sample.afib_score = afib.score;
    } else {
        afib_result_t afib = afib_detector_get_result();
        sample.afib_status = (uint8_t)afib.status;
        sample.afib_score = afib.score;
    }
    return sample;
}
