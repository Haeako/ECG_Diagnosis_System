#include "sqi_device_benchmark.h"
#include "sqi_runtime.h"

#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <stdlib.h>

#define N 3600U
#define CPU_MHZ ((double)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)

static uint32_t push_cycles[N];

static int compare_u32(const void *left, const void *right)
{
    const uint32_t a = *(const uint32_t *)left;
    const uint32_t b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

void sqi_device_benchmark_run(void)
{
    sqi_runtime_reset(0U);
    for (uint32_t i = 0U; i < N; ++i) {
        const uint32_t phase = (i + 180U) % 288U;
        int16_t sample = 0;
        if (phase == 107U) sample = -400;
        else if (phase == 108U) sample = 1500;
        else if (phase == 109U) sample = -250;
        const bool is_peak = (phase == 108U);
        const uint32_t now_ms = (i * 1000U) / 360U;
        const uint32_t before = esp_cpu_get_cycle_count();
        (void)sqi_runtime_push(sample, is_peak, now_ms, now_ms);
        push_cycles[i] = esp_cpu_get_cycle_count() - before;
    }
    qsort(push_cycles, N, sizeof(push_cycles[0]), compare_u32);

    const int64_t before_us = esp_timer_get_time();
    const ecg_sqi_result_t result = sqi_runtime_benchmark_finalize();
    const int64_t finalize_us = esp_timer_get_time() - before_us;
    ESP_LOGI("SQI_BENCH", "RESULT method=Orphanidou-template samples=%u cpu_mhz=%d "
             "push_p50_us=%.3f push_p99_us=%.3f finalize_us=%lld "
             "status=%d score=%u correlation_x1000=%d window_state_bytes=unknown "
             "template_static_bytes=4320",
             N, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
             push_cycles[N / 2U] / CPU_MHZ,
             push_cycles[(99U * (N - 1U)) / 100U] / CPU_MHZ,
             (long long)finalize_us, (int)result.status, result.score,
             (int)result.template_correlation_x1000);
}
