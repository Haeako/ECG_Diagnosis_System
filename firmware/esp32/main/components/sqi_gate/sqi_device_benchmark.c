#include "sqi_device_benchmark.h"

#include <inttypes.h>
#include <stdint.h>

#include "esp_cpu.h"
#include "esp_log.h"

#include "sqi_runtime.h"
#include "sqi_bsqi.h"
#include "sqi_xqrs.h"
#include "sqi_gate_logistic.h"
#include "sqi_zhao.h"
#include "sqi_kramer.h"
#include "sqi_logistic_runtime.h"
#include "task/Core1/ECGPipeline/ExFeature/ExtractFeature.h"

#define SAMPLE_COUNT 3600U
#define FINALIZE_REPEATS 200U
#define CPU_MHZ ((double)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)

static uint32_t push_cycles[SAMPLE_COUNT];
static uint32_t finalize_cycles[FINALIZE_REPEATS];
static sqi_zhao_state_t zhao_state;
static sqi_kramer_state_t kramer_state;
static uint32_t kramer_push_cycles[5000U];
static uint32_t qrs_push_cycles[SAMPLE_COUNT];
static sqi_logistic_runtime_t logistic_state;
static sqi_xqrs_workspace_t xqrs_workspace;
static float xqrs_samples[SQI_XQRS_MAX_SAMPLES];
static uint16_t xqrs_peaks[SQI_XQRS_MAX_PEAKS];

static void sort_u32(uint32_t *values, uint32_t count)
{
    for (uint32_t i = 1U; i < count; ++i) {
        uint32_t value = values[i], j = i;
        while (j > 0U && values[j - 1U] > value) {
            values[j] = values[j - 1U];
            --j;
        }
        values[j] = value;
    }
}

void sqi_device_benchmark_run(void)
{
    ecg_signal_reset(0U);
    uint32_t detected_peaks = 0U;
    for (uint32_t i = 0U; i < SAMPLE_COUNT; ++i) {
        uint32_t phase = i % 360U;
        int raw = 2048 + (int)(phase < 8U ? (phase <= 4U ? phase * 180U : (8U - phase) * 180U) : 0U);
        uint32_t before = esp_cpu_get_cycle_count();
        ecg_record_t record = ecg_signal_process(raw, (i * 1000U) / 360U);
        qrs_push_cycles[i] = esp_cpu_get_cycle_count() - before;
        detected_peaks += record.is_peak != 0U;
    }
    sort_u32(qrs_push_cycles, SAMPLE_COUNT);
    uint32_t qrs_push_p99 = qrs_push_cycles[(99U * (SAMPLE_COUNT - 1U)) / 100U];
    ESP_LOGI("SQI_BENCH", "RESULT method=Production-QRS cpu_mhz=%d "
             "push_n=%u push_p99_cycles=%" PRIu32 " push_p99_us=%.6f detected_peaks=%u",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, SAMPLE_COUNT,
             qrs_push_p99, qrs_push_p99 / CPU_MHZ, detected_peaks);

    sqi_runtime_reset(0U);
    for (uint32_t i = 0U; i < SAMPLE_COUNT; ++i) {
        uint32_t phase = i % 180U;
        int16_t sample = (int16_t)(3 * (int32_t)(phase < 90U ? phase : 180U - phase));
        bool peak = i >= 360U && (i % 360U) == 0U;
        uint32_t now_ms = (i * 1000U) / 360U;
        uint32_t before = esp_cpu_get_cycle_count();
        (void)sqi_runtime_push(sample, peak, peak ? now_ms : 0U, now_ms);
        push_cycles[i] = esp_cpu_get_cycle_count() - before;
    }
    volatile uint8_t sink = 0U;
    for (uint32_t i = 0U; i < FINALIZE_REPEATS; ++i) {
        uint32_t before = esp_cpu_get_cycle_count();
        ecg_sqi_result_t result = sqi_runtime_benchmark_finalize();
        finalize_cycles[i] = esp_cpu_get_cycle_count() - before;
        sink ^= result.score;
    }
    sort_u32(push_cycles, SAMPLE_COUNT);
    sort_u32(finalize_cycles, FINALIZE_REPEATS);
    uint32_t push_p99 = push_cycles[(99U * (SAMPLE_COUNT - 1U)) / 100U];
    uint32_t finalize_p99 = finalize_cycles[(99U * (FINALIZE_REPEATS - 1U)) / 100U];
    ESP_LOGI("SQI_BENCH", "RESULT method=Orphanidou-template cpu_mhz=%d "
             "push_n=%u push_p99_cycles=%" PRIu32 " push_p99_us=%.6f "
             "finalize_n=%u finalize_p99_cycles=%" PRIu32 " finalize_p99_us=%.6f sink=%u",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
             SAMPLE_COUNT, push_p99, push_p99 / CPU_MHZ,
             FINALIZE_REPEATS, finalize_p99, finalize_p99 / CPU_MHZ, sink);

    sqi_bsqi_state_t bsqi;
    uint32_t bsqi_push_index = 0U;
    for (uint32_t repeat = 0U; repeat < FINALIZE_REPEATS; ++repeat) {
        sqi_bsqi_reset(&bsqi);
        for (uint32_t peak = 500U; peak < 9500U; peak += 1000U) {
            uint32_t before = esp_cpu_get_cycle_count();
            (void)sqi_bsqi_push_primary(&bsqi, peak);
            push_cycles[bsqi_push_index++] = esp_cpu_get_cycle_count() - before;
            before = esp_cpu_get_cycle_count();
            (void)sqi_bsqi_push_secondary(&bsqi, peak + (peak % 3U));
            push_cycles[bsqi_push_index++] = esp_cpu_get_cycle_count() - before;
        }
        uint32_t before = esp_cpu_get_cycle_count();
        sink ^= (uint8_t)(sqi_bsqi_finalize(&bsqi, 50U) * 100.0f);
        finalize_cycles[repeat] = esp_cpu_get_cycle_count() - before;
    }
    sort_u32(push_cycles, bsqi_push_index);
    sort_u32(finalize_cycles, FINALIZE_REPEATS);
    uint32_t bsqi_push_p99 = push_cycles[(99U * (bsqi_push_index - 1U)) / 100U];
    uint32_t bsqi_finalize_p99 = finalize_cycles[(99U * (FINALIZE_REPEATS - 1U)) / 100U];
    ESP_LOGI("SQI_BENCH", "RESULT method=bSQI-kernel cpu_mhz=%d "
             "push_n=%u push_p99_cycles=%" PRIu32 " push_p99_us=%.6f "
             "finalize_n=%u finalize_p99_cycles=%" PRIu32 " finalize_p99_us=%.6f sink=%u",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
             bsqi_push_index, bsqi_push_p99, bsqi_push_p99 / CPU_MHZ,
             FINALIZE_REPEATS, bsqi_finalize_p99, bsqi_finalize_p99 / CPU_MHZ, sink);

    for (uint32_t i = 0U; i < SQI_XQRS_MAX_SAMPLES; ++i) {
        uint32_t phase = i % SQI_XQRS_FS;
        xqrs_samples[i] = phase < 10U
            ? (float)(phase <= 5U ? phase : 10U - phase) * 0.2f : 0.0f;
    }
    uint32_t bsqi_full_before = esp_cpu_get_cycle_count();
    size_t secondary_count = sqi_xqrs_detect_250(
        xqrs_samples, SQI_XQRS_MAX_SAMPLES, xqrs_peaks,
        SQI_XQRS_MAX_PEAKS, &xqrs_workspace);
    sqi_bsqi_reset(&bsqi);
    for (uint32_t peak = SQI_XQRS_FS; peak < SQI_XQRS_MAX_SAMPLES;
         peak += SQI_XQRS_FS) {
        (void)sqi_bsqi_push_primary(&bsqi,
            (peak * 1000U) / SQI_XQRS_FS);
    }
    for (size_t i = 0U; i < secondary_count; ++i) {
        (void)sqi_bsqi_push_secondary(&bsqi,
            ((uint32_t)xqrs_peaks[i] * 1000U) / SQI_XQRS_FS);
    }
    float bsqi_full_score = sqi_bsqi_finalize(&bsqi, 50U);
    uint32_t bsqi_full_cycles = esp_cpu_get_cycle_count() - bsqi_full_before;
    sink ^= (uint8_t)(bsqi_full_score * 100.0f);
    ESP_LOGI("SQI_BENCH", "RESULT method=bSQI-full cpu_mhz=%d "
             "samples=%u secondary_peaks=%u finalize_n=1 "
             "finalize_cycles=%" PRIu32 " finalize_us=%.6f score=%.6f sink=%u",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, SQI_XQRS_MAX_SAMPLES,
             (unsigned)secondary_count, bsqi_full_cycles,
             bsqi_full_cycles / CPU_MHZ, (double)bsqi_full_score, sink);

    const float logistic_features[5] = {1.0f, 0.75f, 0.25f, 0.95f, 0.1f};
    for (uint32_t repeat = 0U; repeat < FINALIZE_REPEATS; ++repeat) {
        uint32_t before = esp_cpu_get_cycle_count();
        sink ^= (uint8_t)sqi_gate_predict(logistic_features);
        finalize_cycles[repeat] = esp_cpu_get_cycle_count() - before;
    }
    sort_u32(finalize_cycles, FINALIZE_REPEATS);
    uint32_t logistic_p99 = finalize_cycles[(99U * (FINALIZE_REPEATS - 1U)) / 100U];
    ESP_LOGI("SQI_BENCH", "RESULT method=Logistic-decision cpu_mhz=%d "
             "finalize_n=%u finalize_p99_cycles=%" PRIu32 " finalize_p99_us=%.6f sink=%u",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
             FINALIZE_REPEATS, logistic_p99, logistic_p99 / CPU_MHZ, sink);

    sqi_logistic_reset(&logistic_state);
    for (uint32_t i = 0U; i < SAMPLE_COUNT; ++i) {
        uint32_t phase = i % 180U;
        float sample = (float)(phase < 90U ? phase : 180U - phase);
        uint32_t before = esp_cpu_get_cycle_count();
        (void)sqi_logistic_push(&logistic_state, sample);
        push_cycles[i] = esp_cpu_get_cycle_count() - before;
        if (i >= 360U && (i % 360U) == 0U) {
            (void)sqi_logistic_push_primary(&logistic_state, (uint16_t)i);
            (void)sqi_logistic_push_secondary(&logistic_state, (uint16_t)(i + (i % 3U)));
        }
    }
    sort_u32(push_cycles, SAMPLE_COUNT);
    uint32_t logistic_full_push = push_cycles[(99U * (SAMPLE_COUNT - 1U)) / 100U];
    uint32_t logistic_full_before = esp_cpu_get_cycle_count();
    sqi_logistic_result_t logistic_full = sqi_logistic_finalize(&logistic_state, 360U);
    uint32_t logistic_full_finalize = esp_cpu_get_cycle_count() - logistic_full_before;
    sink ^= (uint8_t)(logistic_full.probability * 100.0f);
    ESP_LOGI("SQI_BENCH", "RESULT method=Logistic-5SQI cpu_mhz=%d "
             "push_n=%u push_p99_cycles=%" PRIu32 " push_p99_us=%.6f "
             "finalize_n=1 finalize_cycles=%" PRIu32 " finalize_us=%.6f sink=%u",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
             SAMPLE_COUNT, logistic_full_push, logistic_full_push / CPU_MHZ,
             logistic_full_finalize, logistic_full_finalize / CPU_MHZ, sink);

    sqi_zhao_reset(&zhao_state);
    for (uint32_t i = 0U; i < SAMPLE_COUNT; ++i) {
        uint32_t phase = i % 180U;
        float sample = (float)(phase < 90U ? phase : 180U - phase);
        uint32_t before = esp_cpu_get_cycle_count();
        (void)sqi_zhao_push(&zhao_state, sample);
        push_cycles[i] = esp_cpu_get_cycle_count() - before;
        if (i >= 360U && (i % 360U) == 0U) (void)sqi_zhao_push_peak(&zhao_state, (i * 1000U) / 360U);
    }
    sort_u32(push_cycles, SAMPLE_COUNT);
    uint32_t zhao_push_p99 = push_cycles[(99U * (SAMPLE_COUNT - 1U)) / 100U];
    uint32_t zhao_before = esp_cpu_get_cycle_count();
    sqi_zhao_result_t zhao = sqi_zhao_finalize(&zhao_state, 360U);
    uint32_t zhao_finalize = esp_cpu_get_cycle_count() - zhao_before;
    sink ^= (uint8_t)(zhao.score * 100.0f);
    ESP_LOGI("SQI_BENCH", "RESULT method=Zhao-simple cpu_mhz=%d "
             "push_n=%u push_p99_cycles=%" PRIu32 " push_p99_us=%.6f "
             "finalize_n=1 finalize_cycles=%" PRIu32 " finalize_us=%.6f sink=%u",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
             SAMPLE_COUNT, zhao_push_p99, zhao_push_p99 / CPU_MHZ,
             zhao_finalize, zhao_finalize / CPU_MHZ, sink);

    sqi_kramer_reset(&kramer_state);
    for (uint32_t i = 0U; i < 5000U; ++i) {
        float sample = (float)(i % 101U);
        uint32_t before = esp_cpu_get_cycle_count();
        sqi_kramer_push(&kramer_state, sample);
        kramer_push_cycles[i] = esp_cpu_get_cycle_count() - before;
    }
    for (uint32_t i = 0U; i < FINALIZE_REPEATS; ++i) {
        uint32_t before = esp_cpu_get_cycle_count();
        sqi_kramer_result_t result = sqi_kramer_finalize(&kramer_state, 10U, false);
        finalize_cycles[i] = esp_cpu_get_cycle_count() - before;
        sink ^= (uint8_t)(result.score * 100.0f);
    }
    sort_u32(kramer_push_cycles, 5000U);
    sort_u32(finalize_cycles, FINALIZE_REPEATS);
    uint32_t kramer_push_p99 = kramer_push_cycles[(99U * (5000U - 1U)) / 100U];
    uint32_t kramer_finalize_p99 = finalize_cycles[(99U * (FINALIZE_REPEATS - 1U)) / 100U];
    ESP_LOGI("SQI_BENCH", "RESULT method=Kramer-rules cpu_mhz=%d "
             "push_n=5000 push_p99_cycles=%" PRIu32 " push_p99_us=%.6f "
             "finalize_n=%u finalize_p99_cycles=%" PRIu32 " finalize_p99_us=%.6f sink=%u",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
             kramer_push_p99, kramer_push_p99 / CPU_MHZ,
             FINALIZE_REPEATS, kramer_finalize_p99, kramer_finalize_p99 / CPU_MHZ, sink);
}
