#include "sqi_device_benchmark.h"
#include "sqi_zhao.h"

#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdlib.h>

#define INPUT_N SQI_ZHAO_MAX_SAMPLES
#define FINALIZE_REPEATS 1U
#define CPU_MHZ ((double)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)

static sqi_zhao_state_t state;
static uint32_t push_cycles[INPUT_N];
static uint32_t sorted_cycles[INPUT_N];

static int compare_u32(const void *left, const void *right)
{
    const uint32_t a = *(const uint32_t *)left;
    const uint32_t b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

static int compare_i64(const void *left, const void *right)
{
    const int64_t a = *(const int64_t *)left;
    const int64_t b = *(const int64_t *)right;
    return (a > b) - (a < b);
}

void sqi_device_benchmark_run(void)
{
    sqi_zhao_reset(&state);
    for (uint32_t i = 0U; i < INPUT_N; ++i) {
        const uint32_t phase = i % 360U;
        const float sample = (phase >= 70U && phase <= 74U)
            ? (phase == 72U ? 1.0f : 0.4f) : 0.02f * (float)(phase % 11U);
        const uint32_t before = esp_cpu_get_cycle_count();
        (void)sqi_zhao_push(&state, sample);
        push_cycles[i] = esp_cpu_get_cycle_count() - before;
    }
    for (uint32_t i = 0U; i < INPUT_N; ++i) sorted_cycles[i] = push_cycles[i];
    qsort(sorted_cycles, INPUT_N, sizeof(sorted_cycles[0]), compare_u32);

    for (uint32_t i = 0U; i < 12U; ++i) {
        (void)sqi_zhao_push_peak(&state, 800U * i);
    }
    int64_t finalize_us[FINALIZE_REPEATS];
    volatile float sink = 0.0f;
    for (uint32_t i = 0U; i < FINALIZE_REPEATS; ++i) {
        const int64_t before = esp_timer_get_time();
        const sqi_zhao_result_t result = sqi_zhao_finalize(&state, 360U);
        finalize_us[i] = esp_timer_get_time() - before;
        sink += result.score;
    }
    qsort(finalize_us, FINALIZE_REPEATS, sizeof(finalize_us[0]), compare_i64);
    ESP_LOGI("SQI_BENCH", "RESULT method=Zhao samples=%u cpu_mhz=%d "
             "push_p50_us=%.3f push_p99_us=%.3f finalize_n=%u "
             "finalize_p50_us=%lld finalize_p99_us=%lld state_bytes=%u "
             "scratch_bytes=%u sink=%.2f",
             INPUT_N, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
             sorted_cycles[INPUT_N / 2U] / CPU_MHZ,
             sorted_cycles[(99U * (INPUT_N - 1U)) / 100U] / CPU_MHZ,
             FINALIZE_REPEATS, (long long)finalize_us[0],
             (long long)finalize_us[0],
             (unsigned)sizeof(state),
             (unsigned)(sizeof(push_cycles) + sizeof(sorted_cycles) + sizeof(finalize_us)),
             (double)sink);
}
