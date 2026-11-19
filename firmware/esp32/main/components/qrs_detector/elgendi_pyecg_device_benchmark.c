#include "../sqi_gate/sqi_device_benchmark.h"
#include "elgendi_pyecg.h"

#include "esp_cpu.h"
#include "esp_log.h"

#include <stdlib.h>

#define REPEATS 7U
#define CPU_MHZ ((double)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)

static float input[ELGENDI_PYECG_MAX_SAMPLES];
static uint16_t peaks[ELGENDI_PYECG_MAX_PEAKS];
static elgendi_pyecg_workspace_t workspace;
static uint32_t cycles[REPEATS];

static int compare_u32(const void *left, const void *right)
{
    const uint32_t a = *(const uint32_t *)left;
    const uint32_t b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

void sqi_device_benchmark_run(void)
{
    for (uint32_t i = 0U; i < ELGENDI_PYECG_MAX_SAMPLES; ++i) {
        const float time_s = (float)i / (float)ELGENDI_PYECG_FS;
        float value = 0.03f * __builtin_sinf(6.2831853f * 0.25f * time_s);
        for (uint32_t beat = 1U; beat <= 12U; ++beat) {
            const float delta = time_s - (0.5f + 0.8f * (float)beat);
            const float z = delta / 0.012f;
            value += __builtin_expf(-0.5f * z * z);
        }
        input[i] = value;
    }

    size_t detected = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        detected = elgendi_pyecg_detect_360(input, ELGENDI_PYECG_MAX_SAMPLES,
                                            peaks, ELGENDI_PYECG_MAX_PEAKS,
                                            &workspace);
        cycles[repeat] = esp_cpu_get_cycle_count() - before;
    }
    qsort(cycles, REPEATS, sizeof(cycles[0]), compare_u32);
    ESP_LOGI("QRS_BENCH", "RESULT method=Elgendi-two-average-pyecg-C-port fs=%u "
             "samples=%u repeats=%u detections=%u p50_batch_us=%.3f "
             "max_batch_us=%.3f workspace_bytes=%u input_bytes=%u "
             "output_bytes=%u",
             ELGENDI_PYECG_FS, ELGENDI_PYECG_MAX_SAMPLES, REPEATS,
             (unsigned)detected, cycles[REPEATS / 2U] / CPU_MHZ,
             cycles[REPEATS - 1U] / CPU_MHZ, (unsigned)sizeof(workspace),
             (unsigned)sizeof(input), (unsigned)sizeof(peaks));
}
