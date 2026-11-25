#include "sqi_device_benchmark.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sqi_clifford.h"

#define CLIFFORD_FS_HZ 125.0f
#define CLIFFORD_N 1250U
#define CLIFFORD_RUNS 101U

static float samples[CLIFFORD_N];
static int64_t elapsed_us[CLIFFORD_RUNS];
static sqi_clifford_workspace_t workspace;

static int compare_i64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

void sqi_device_benchmark_run(void)
{
    for (uint32_t i = 0U; i < CLIFFORD_N; ++i) {
        float t = (float)i / CLIFFORD_FS_HZ;
        float phase = fmodf(t, 0.8333333f);
        float qrs = phase < 0.04f ? 1.5f * (1.0f - phase / 0.04f) : 0.0f;
        samples[i] = 0.08f * sinf(6.2831853f * 0.3f * t)
                   + 0.03f * sinf(6.2831853f * 17.0f * t) + qrs;
    }
    sqi_clifford_result_t result = {0};
    for (uint32_t r = 0U; r < CLIFFORD_RUNS; ++r) {
        int64_t before = esp_timer_get_time();
        result = sqi_clifford_extract(samples, CLIFFORD_N, CLIFFORD_FS_HZ,
                                      1000.0f, &workspace);
        elapsed_us[r] = esp_timer_get_time() - before;
        /* Keep watchdog/idle-task health separate from the timed call. */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    qsort(elapsed_us, CLIFFORD_RUNS, sizeof(elapsed_us[0]), compare_i64);
    ESP_LOGI("SQI_BENCH", "RESULT method=Clifford-single-lead cpu_mhz=%d samples=%u fs_hz=125 runs=%u "
             "finalize_p50_us=%" PRId64 " finalize_p99_us=%" PRId64 " finalize_max_us=%" PRId64 " "
             "features_valid=%d psqi=%.7f ssqi=%.7f ksqi=%.7f fsqi=%.7f bassqi=%.7f "
             "input_bytes=%u workspace_bytes=%u timing_bytes=%u min_free_heap=%u stack_high_water_words=%u",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, CLIFFORD_N, CLIFFORD_RUNS,
             elapsed_us[CLIFFORD_RUNS / 2U], elapsed_us[(99U * CLIFFORD_RUNS) / 100U],
             elapsed_us[CLIFFORD_RUNS - 1U], result.valid ? 1 : 0,
             (double)result.psqi, (double)result.ssqi, (double)result.ksqi,
             (double)result.fsqi, (double)result.bassqi, (unsigned)sizeof(samples),
             (unsigned)sizeof(workspace),
             (unsigned)sizeof(elapsed_us), (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
