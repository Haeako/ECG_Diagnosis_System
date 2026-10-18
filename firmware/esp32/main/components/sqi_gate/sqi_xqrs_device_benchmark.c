#include "sqi_device_benchmark.h"

#include <inttypes.h>
#include <stdint.h>

#include "esp_cpu.h"
#include "esp_log.h"

#include "sqi_bsqi.h"
#include "sqi_xqrs.h"

#define CPU_MHZ ((double)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)

static sqi_xqrs_workspace_t workspace;

void sqi_device_benchmark_run(void)
{
    for (uint32_t i = 0U; i < SQI_XQRS_MAX_SAMPLES; ++i) {
        uint32_t phase = i % SQI_XQRS_FS;
        workspace.filtered[i] = phase < 10U
            ? (float)(phase <= 5U ? phase : 10U - phase) * 0.2f : 0.0f;
    }

    uint32_t before = esp_cpu_get_cycle_count();
    size_t secondary_count = sqi_xqrs_detect_250(
        workspace.filtered, SQI_XQRS_MAX_SAMPLES,
        workspace.filtered_candidates, SQI_XQRS_MAX_PEAKS, &workspace);

    sqi_bsqi_state_t bsqi;
    sqi_bsqi_reset(&bsqi);
    for (uint32_t peak = SQI_XQRS_FS; peak < SQI_XQRS_MAX_SAMPLES;
         peak += SQI_XQRS_FS) {
        (void)sqi_bsqi_push_primary(&bsqi,
            (peak * 1000U) / SQI_XQRS_FS);
    }
    for (size_t i = 0U; i < secondary_count; ++i) {
        (void)sqi_bsqi_push_secondary(&bsqi,
            ((uint32_t)workspace.filtered_candidates[i] * 1000U)
            / SQI_XQRS_FS);
    }
    float score = sqi_bsqi_finalize(&bsqi, 50U);
    uint32_t cycles = esp_cpu_get_cycle_count() - before;

    ESP_LOGI("SQI_BENCH", "RESULT method=bSQI-full cpu_mhz=%d "
             "samples=%u secondary_peaks=%u finalize_n=1 "
             "finalize_cycles=%" PRIu32 " finalize_us=%.6f score=%.6f",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, SQI_XQRS_MAX_SAMPLES,
             (unsigned)secondary_count, cycles, cycles / CPU_MHZ,
             (double)score);
}
