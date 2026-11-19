#include "sqi_device_benchmark.h"

#include <stdlib.h>
#include <stdint.h>

#include "esp_cpu.h"
#include "esp_log.h"

#include "sqi_bsqi.h"
#include "sqi_xqrs.h"

#define CPU_MHZ ((double)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)

#define REPEATS 7U
static sqi_xqrs_workspace_t workspace;
static float input[SQI_XQRS_MAX_SAMPLES];
static uint16_t peaks[SQI_XQRS_MAX_PEAKS];
static uint32_t cycles[REPEATS];

static int compare_u32(const void *left, const void *right)
{
    const uint32_t a = *(const uint32_t *)left;
    const uint32_t b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

void sqi_device_benchmark_run(void)
{
    for (uint32_t i = 0U; i < SQI_XQRS_MAX_SAMPLES; ++i) {
        uint32_t phase = i % SQI_XQRS_FS;
        input[i] = phase < 10U
            ? (float)(phase <= 5U ? phase : 10U - phase) * 0.2f : 0.0f;
    }

    size_t detected = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        detected = sqi_xqrs_detect_250(input, SQI_XQRS_MAX_SAMPLES,
                                       peaks, SQI_XQRS_MAX_PEAKS, &workspace);
        cycles[repeat] = esp_cpu_get_cycle_count() - before;
    }
    qsort(cycles, REPEATS, sizeof(cycles[0]), compare_u32);

    ESP_LOGI("SQI_BENCH", "RESULT method=WFDB-XQRS-C-port batch_fs=%u "
             "samples=%u repeats=%u peaks=%u p50_us=%.3f p99_us=%.3f "
             "workspace_bytes=%u input_bytes=%u peak_buffer_bytes=%u",
             SQI_XQRS_FS, SQI_XQRS_MAX_SAMPLES, REPEATS, (unsigned)detected,
             cycles[REPEATS / 2U] / CPU_MHZ,
             cycles[REPEATS - 1U] / CPU_MHZ,
             (unsigned)sizeof(workspace), (unsigned)sizeof(input),
             (unsigned)sizeof(peaks));
}
