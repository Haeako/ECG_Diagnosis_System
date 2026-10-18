#include "sqi_device_benchmark.h"

#include <inttypes.h>
#include <stdint.h>

#include "esp_cpu.h"
#include "esp_log.h"

#include "sqi_logistic_runtime.h"
#include "sqi_xqrs.h"

#define CONTEXT_SAMPLES 3500U
#define CORE_OFFSET 500U
#define CORE_SAMPLES 2500U
#define CPU_MHZ ((double)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)

static sqi_xqrs_workspace_t xqrs_workspace;
static sqi_logistic_runtime_t logistic_state;

void sqi_device_benchmark_run(void)
{
    sqi_logistic_reset(&logistic_state);
    for (uint32_t i = 0U; i < CONTEXT_SAMPLES; ++i) {
        uint32_t phase = i % SQI_XQRS_FS;
        float sample = phase < 10U
            ? (float)(phase <= 5U ? phase : 10U - phase) * 0.2f : 0.0f;
        xqrs_workspace.filtered[i] = sample;
        if (i >= CORE_OFFSET && i < CORE_OFFSET + CORE_SAMPLES)
            (void)sqi_logistic_push(&logistic_state, sample);
    }

    uint32_t before = esp_cpu_get_cycle_count();
    size_t secondary_count = sqi_xqrs_detect_250(
        xqrs_workspace.filtered, CONTEXT_SAMPLES,
        xqrs_workspace.filtered_candidates, SQI_XQRS_MAX_PEAKS,
        &xqrs_workspace);

    for (uint32_t peak = SQI_XQRS_FS; peak < CORE_SAMPLES;
         peak += SQI_XQRS_FS)
        (void)sqi_logistic_push_primary(&logistic_state, (uint16_t)peak);
    for (size_t i = 0U; i < secondary_count; ++i) {
        uint16_t peak = xqrs_workspace.filtered_candidates[i];
        if (peak >= CORE_OFFSET && peak < CORE_OFFSET + CORE_SAMPLES)
            (void)sqi_logistic_push_secondary(
                &logistic_state, (uint16_t)(peak - CORE_OFFSET));
    }
    sqi_logistic_result_t result = sqi_logistic_finalize(
        &logistic_state, SQI_XQRS_FS);
    uint32_t cycles = esp_cpu_get_cycle_count() - before;

    ESP_LOGI("SQI_BENCH", "RESULT method=Logistic-full cpu_mhz=%d "
             "context_samples=%u core_samples=%u secondary_peaks=%u "
             "finalize_n=1 finalize_cycles=%" PRIu32
             " finalize_us=%.6f probability=%.6f decision=%d",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, CONTEXT_SAMPLES, CORE_SAMPLES,
             (unsigned)secondary_count, cycles, cycles / CPU_MHZ,
             (double)result.probability, (int)result.decision);
}
