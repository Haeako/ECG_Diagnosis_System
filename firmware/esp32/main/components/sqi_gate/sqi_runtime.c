#include "sqi_runtime.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#endif

#include "sqi_orphanidou.h"
#include "sqi_template_matching.h"

#define SQI_SAMPLE_RATE_HZ          360U
#define SQI_WINDOW_MS               10000U
#define SQI_WINDOW_SAMPLES          (SQI_SAMPLE_RATE_HZ * SQI_WINDOW_MS / 1000U)
#define SQI_MAX_PEAKS               32U
#define SQI_MIN_PEAKS               3U
#define SQI_CORRELATION_THRESHOLD   0.66f

#define VALID_SAMPLE(x) (x >= SQI_WINDOW_SAMPLES - 1U)

typedef struct {
    uint32_t window_start_ms;
    uint32_t window_id;
    uint16_t sample_count;
    int16_t samples[SQI_WINDOW_SAMPLES];
    uint32_t peak_ms[SQI_MAX_PEAKS];
    uint8_t peak_count;
    bool peak_overflow;
    ecg_sqi_result_t latest;
} sqi_runtime_state_t;

static sqi_runtime_state_t state;

static ecg_sqi_result_t unusable_result(void)
{
    ecg_sqi_result_t result = {
        .score = 0U,
        .status = ECG_SQI_UNUSABLE,
        .template_correlation_x1000 = -1000,
        .window_id = state.window_id,
    };
    return result;
}

static void reset_window(uint32_t now_ms)
{
    ecg_sqi_result_t latest = state.latest;
    uint32_t next_window_id = state.window_id + 1U;
    memset(&state, 0, sizeof(state));
    state.window_start_ms = now_ms;
    state.window_id = next_window_id;
    state.latest = latest;
}

void sqi_runtime_reset(uint32_t now_ms)
{
    memset(&state, 0, sizeof(state));
    state.window_start_ms = now_ms;
    state.latest.score = ECG_SQI_INSUFFICIENT_SCORE;
    state.latest.status = ECG_SQI_INSUFFICIENT;
    state.latest.template_correlation_x1000 = -1000;
}

static ecg_sqi_result_t evaluate_window(void)
{
    if (!VALID_SAMPLE(state.sample_count) || state.peak_count < SQI_MIN_PEAKS || state.peak_overflow) 
    {
        return unusable_result();
    }

    sqi_orphanidou_result_t rules = sqi_orphanidou_evaluate(
        state.peak_ms, state.peak_count);
    ecg_sqi_result_t result = unusable_result();
    result.heart_rate_bpm = rules.heart_rate_bpm;
    result.max_rr_ms = rules.max_rr_ms > UINT16_MAX
        ? UINT16_MAX : (uint16_t)rules.max_rr_ms;
    float ratio_x1000 = rules.rr_ratio * 1000.0f;
    result.rr_ratio_x1000 = ratio_x1000 > (float)UINT16_MAX
        ? UINT16_MAX : (uint16_t)(ratio_x1000 + 0.5f);
    if (rules.heart_rate_passed) result.rule_mask |= ECG_SQI_RULE_HR_PASS;
    if (rules.max_rr_passed) result.rule_mask |= ECG_SQI_RULE_MAX_RR_PASS;
    if (rules.rr_ratio_passed) result.rule_mask |= ECG_SQI_RULE_RR_RATIO_PASS;
    if (!rules.passed) return result;

    float correlation = sqi_template_average_correlation(
        state.samples, state.sample_count, SQI_SAMPLE_RATE_HZ,
        state.peak_ms, state.peak_count, state.window_start_ms,
        rules.median_rr_ms);
    if (!isfinite(correlation)) return result;
    if (correlation < 0.0f) correlation = 0.0f;
    if (correlation > 1.0f) correlation = 1.0f;

    result.score = (uint8_t)(correlation * 100.0f + 0.5f);
    result.template_correlation_x1000 =
        (int16_t)(correlation * 1000.0f + 0.5f);
    if (correlation >= SQI_CORRELATION_THRESHOLD) {
        result.rule_mask |= ECG_SQI_RULE_TEMPLATE_PASS;
        result.status = ECG_SQI_USABLE;
    }
    return result;
}

#ifdef SQI_BENCHMARK_ENABLE
ecg_sqi_result_t sqi_runtime_benchmark_finalize(void)
{
    return evaluate_window();
}
#endif

ecg_sqi_result_t sqi_runtime_push(int16_t ecg_sample,
                                  bool is_r_peak,
                                  uint32_t r_peak_timestamp_ms,
                                  uint32_t now_ms)
{
    if ((now_ms - state.window_start_ms) >= SQI_WINDOW_MS) {
#ifdef ESP_PLATFORM
        int64_t finalize_started_us = esp_timer_get_time();
#endif
        state.latest = evaluate_window();
#ifdef ESP_PLATFORM
        state.latest.finalize_us = (uint32_t)(esp_timer_get_time()
            - finalize_started_us);
#endif
        ecg_sqi_result_t completed = state.latest;
        reset_window(now_ms);
        state.latest = completed;
    }

    if (state.sample_count < SQI_WINDOW_SAMPLES) {
        state.samples[state.sample_count++] = ecg_sample;
    }
    if (is_r_peak && r_peak_timestamp_ms >= state.window_start_ms) {
        if (state.peak_count < SQI_MAX_PEAKS) {
            state.peak_ms[state.peak_count++] = r_peak_timestamp_ms;
        } else {
            state.peak_overflow = true;
        }
    }
    return state.latest;
}
