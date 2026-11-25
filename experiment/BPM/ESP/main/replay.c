#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "replay_inputs.h"
#include "replay_dispatch.h"

#define CASE_COUNT 66U
static float input[3600];
static uint16_t peaks[128];
static int64_t elapsed_us[CASE_COUNT];
static volatile size_t result_sink;

static int compare_us(const void *left, const void *right)
{
    const int64_t a = *(const int64_t *)left;
    const int64_t b = *(const int64_t *)right;
    return (a > b) - (a < b);
}

void app_main(void)
{
    srand(42U);
    printf("LATENCY_MEMORY_BEGIN,variant=no-conditioning,cases=%u,cpu_mhz=%d\n",
           CASE_COUNT, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    for (unsigned method = 0; method < 12; ++method) {
        const size_t bytes = workspace_size(method);
        if (!bytes) continue;
        const size_t heap_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        void *workspace = heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!workspace) {
            printf("LATENCY_MEMORY_SKIP,method=%s,reason=workspace_allocation,bytes=%u\n",
                   method_name(method), (unsigned)bytes);
            continue;
        }
        const size_t heap_allocated = heap_before - heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        for (unsigned c = 0; c < CASE_COUNT; ++c) {
            for (unsigned i = 0; i < 3600; ++i)
                input[i] = ((float)replay_adc[c][i] - 2048.0f) / 200.0f;
            memset(workspace, 0, bytes);
            const int64_t start = esp_timer_get_time();
            const size_t count = detect(method, input, peaks, workspace);
            elapsed_us[c] = esp_timer_get_time() - start;
            result_sink += count; /* Retain computation without scoring accuracy. */
            vTaskDelay(1);
        }
        qsort(elapsed_us, CASE_COUNT, sizeof(elapsed_us[0]), compare_us);
        printf("LATENCY_MEMORY,method=%s,variant=no-conditioning,latency_p50_ms=%.6f,latency_p99_ms=%.6f,allocated_workspace_bytes=%u,heap_allocation_bytes=%u,stack_min_free_bytes=%u,input_buffer_bytes=%u\n",
               method_name(method), elapsed_us[(CASE_COUNT - 1U) / 2U] / 1000.0,
               elapsed_us[(99U * (CASE_COUNT - 1U)) / 100U] / 1000.0,
               (unsigned)bytes, (unsigned)heap_allocated,
               (unsigned)uxTaskGetStackHighWaterMark(NULL), (unsigned)sizeof(input));
        free(workspace);
    }
    printf("ECG_BENCHMARK_END\n");
}
