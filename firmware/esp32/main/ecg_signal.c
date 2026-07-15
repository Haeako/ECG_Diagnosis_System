#include "ecg_signal.h"

#include <string.h>

#include "components/filter/HPfilter.h"
#include "components/filter/Kalman.h"
#include "components/filter/LPfilter.h"
#include "components/filter/SBfilter.h"
#include "components/afib/afib_detector.h"

#define PAN_REFRACTORY_MS  400
#define PAN_R_MIN_ABS     900.0f
#define BPM_UPDATE_MS     5000
#define ADC_BASELINE      2048
#define BASELINE_WINDOW_SAMPLES  360

typedef struct {
    float signal_level;
    float noise_level;
    uint32_t last_peak_ms;
    uint32_t current_peak_ms;
    uint16_t current_rr_ms;
    uint16_t current_instant_bpm;
    uint32_t bpm_window_start_ms;
    uint32_t rr_sum_ms;
    uint16_t rr_count;
    uint16_t bpm;
    float previous_abs;
    float candidate_abs;
    uint32_t candidate_ms;
} pan_tompkins_t;

typedef struct {
    int16_t buffer[BASELINE_WINDOW_SAMPLES];
    int64_t sum;
    uint16_t index;
    uint16_t count;
} moving_average_t;

static HPfilterType hp_filter;
static SBfilterType sb_filter;
static LPfilterType lp_filter;
static pan_tompkins_t pan_detector;
static moving_average_t baseline_filter;

static void moving_average_reset(moving_average_t *filter)
{
    memset(filter, 0, sizeof(*filter));
}

static int16_t moving_average_push(moving_average_t *filter, int16_t sample)
{
    if (filter->count < BASELINE_WINDOW_SAMPLES) {
        filter->buffer[filter->index] = sample;
        filter->sum += sample;
        filter->count++;
    } else {
        filter->sum -= filter->buffer[filter->index];
        filter->buffer[filter->index] = sample;
        filter->sum += sample;
    }

    filter->index = (filter->index + 1) % BASELINE_WINDOW_SAMPLES;
    return (int16_t)(filter->sum / filter->count);
}

static int16_t ecg_filter_sample(int16_t corrected)
{
    float filtered = (float)corrected;

    HPfilter_writeInput(&hp_filter, filtered);
    filtered = HPfilter_readOutput(&hp_filter);
    SBfilter_writeInput(&sb_filter, filtered);
    filtered = SBfilter_readOutput(&sb_filter);
    LPfilter_writeInput(&lp_filter, filtered);
    filtered = LPfilter_readOutput(&lp_filter);

    return (int16_t)KFupdateEstimate(filtered);
}

/*
 * R-peak detector on the filtered ECG. It waits for a local positive/negative
 * extreme and uses adaptive amplitude plus refractory time to avoid detecting
 * the T wave after the QRS complex.
 */
static bool pan_tompkins_peak(int16_t filtered, uint32_t now_ms)
{
    float current_abs = filtered < 0 ? -(float)filtered : (float)filtered;
    float threshold = pan_detector.noise_level
        + 0.60f * (pan_detector.signal_level - pan_detector.noise_level);
    bool is_peak = false;
    bool candidate_is_local_extreme;

    pan_detector.current_peak_ms = 0;
    pan_detector.current_rr_ms = 0;
    pan_detector.current_instant_bpm = 0;

    if (pan_detector.signal_level == 0.0f) {
        pan_detector.signal_level = current_abs;
        threshold = PAN_R_MIN_ABS;
    }

    if (threshold < PAN_R_MIN_ABS) {
        threshold = PAN_R_MIN_ABS;
    }

    candidate_is_local_extreme =
        pan_detector.candidate_abs >= pan_detector.previous_abs
        && pan_detector.candidate_abs > current_abs;

    if (candidate_is_local_extreme
        && pan_detector.candidate_abs >= threshold
        && (pan_detector.last_peak_ms == 0
            || (pan_detector.candidate_ms - pan_detector.last_peak_ms) >= PAN_REFRACTORY_MS)) {
        uint32_t rr_ms = pan_detector.candidate_ms - pan_detector.last_peak_ms;

        is_peak = true;
        pan_detector.signal_level = 0.125f * pan_detector.candidate_abs
            + 0.875f * pan_detector.signal_level;

        pan_detector.current_peak_ms = pan_detector.candidate_ms;
        if (pan_detector.last_peak_ms != 0 && rr_ms >= 250 && rr_ms <= 2000) {
            pan_detector.rr_sum_ms += rr_ms;
            pan_detector.rr_count++;
            pan_detector.current_rr_ms = (uint16_t)rr_ms;
            pan_detector.current_instant_bpm = (uint16_t)(60000UL / rr_ms);
        }
        pan_detector.last_peak_ms = pan_detector.candidate_ms;
    } else if (candidate_is_local_extreme) {
        pan_detector.noise_level = 0.125f * pan_detector.candidate_abs
            + 0.875f * pan_detector.noise_level;
    }

    pan_detector.previous_abs = pan_detector.candidate_abs;
    pan_detector.candidate_abs = current_abs;
    pan_detector.candidate_ms = now_ms;

    if ((now_ms - pan_detector.bpm_window_start_ms) >= BPM_UPDATE_MS) {
        if (pan_detector.rr_count > 0 && pan_detector.rr_sum_ms > 0) {
            pan_detector.bpm = (uint16_t)(
                (60000UL * pan_detector.rr_count) / pan_detector.rr_sum_ms);
        }
        pan_detector.rr_sum_ms = 0;
        pan_detector.rr_count = 0;
        pan_detector.bpm_window_start_ms = now_ms;
    }

    return is_peak;
}

void ecg_signal_init(uint32_t now_ms)
{
    HPfilter_init(&hp_filter);
    SBfilter_init(&sb_filter);
    LPfilter_init(&lp_filter);
    KalmanFilter_init(1.0f, 1.0f, 0.001f);
    afib_detector_init();
    ecg_signal_reset(now_ms);
}

void ecg_signal_reset(uint32_t now_ms)
{
    HPfilter_reset(&hp_filter);
    SBfilter_reset(&sb_filter);
    LPfilter_reset(&lp_filter);
    KalmanFilter_init(1.0f, 1.0f, 0.001f);
    moving_average_reset(&baseline_filter);
    afib_detector_reset();

    memset(&pan_detector, 0, sizeof(pan_detector));
    pan_detector.bpm_window_start_ms = now_ms;
}

ecg_record_t ecg_signal_process(int raw_adc, uint32_t now_ms)
{
    int16_t raw_centered = (int16_t)(raw_adc - ADC_BASELINE);
    int16_t baseline = moving_average_push(&baseline_filter, raw_centered);
    int16_t corrected = raw_centered - baseline;

    ecg_record_t sample = {
        .timestamp_ms = now_ms,
        .raw_adc = (uint16_t)raw_adc,
        .raw_centered = raw_centered,
        .baseline = baseline,
        .corrected = corrected,
        .filtered = ecg_filter_sample(corrected),
    };

    sample.is_peak = pan_tompkins_peak(sample.filtered, now_ms) ? 1 : 0;
    sample.r_peak_timestamp_ms = pan_detector.current_peak_ms;
    sample.rr_interval_ms = pan_detector.current_rr_ms;
    sample.instant_bpm = pan_detector.current_instant_bpm;
    sample.bpm = pan_detector.bpm;
    if (sample.rr_interval_ms > 0) {
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
