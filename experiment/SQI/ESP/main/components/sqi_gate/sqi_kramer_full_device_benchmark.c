#include "sqi_device_benchmark.h"

#include <inttypes.h>
#include <stdlib.h>

#include "esp_cpu.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sqi_kramer_full.h"

#define CPU_MHZ ((double)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)

static sqi_kramer_full_t state;
static uint16_t push_cycles[SQI_KRAMER_INPUT_SAMPLES];

static int compare_u16(const void *left, const void *right)
{
    uint16_t a = *(const uint16_t *)left, b = *(const uint16_t *)right;
    return (a > b) - (a < b);
}

void sqi_device_benchmark_run(void)
{
    sqi_kramer_full_reset(&state);
    for (uint32_t i = 0U; i < SQI_KRAMER_INPUT_SAMPLES; ++i) {
        uint32_t phase = i % 208U;
        float sample = phase < 12U
            ? (float)(phase <= 6U ? phase : 12U - phase) * 0.15f : 0.0f;
        uint32_t before = esp_cpu_get_cycle_count();
        (void)sqi_kramer_full_push(&state, sample);
        uint32_t elapsed = esp_cpu_get_cycle_count() - before;
        push_cycles[i] = elapsed > UINT16_MAX ? UINT16_MAX : (uint16_t)elapsed;
    }
    qsort(push_cycles, SQI_KRAMER_INPUT_SAMPLES, sizeof(push_cycles[0]), compare_u16);
    uint16_t p99 = push_cycles[(99U * SQI_KRAMER_INPUT_SAMPLES) / 100U - 1U];

    uint16_t peaks = 0U; float ratio = 0.0f;
    int64_t before_us = esp_timer_get_time();
    sqi_kramer_result_t result = sqi_kramer_full_finalize(&state, &peaks, &ratio);
    int64_t elapsed_us = esp_timer_get_time() - before_us;
    ESP_LOGI("SQI_BENCH", "RESULT method=Kramer-full cpu_mhz=%d "
             "samples=%u peaks=%u push_p99_cycles=%u push_p99_us=%.6f "
             "finalize_n=1 finalize_us=%" PRId64 " snr_ratio=%.6f "
             "score=%.6f decision=%d",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, SQI_KRAMER_INPUT_SAMPLES,
             peaks, p99, p99 / CPU_MHZ, elapsed_us, (double)ratio,
             (double)result.score, (int)result.accept);
}
