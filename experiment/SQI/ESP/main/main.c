/* Local entry point: one selected SQI kernel benchmark, fixed input fixtures. */
#include <stdio.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "components/sqi_gate/sqi_device_benchmark.h"

#define ECG_RANDOM_SEED 42U

void app_main(void)
{
    srand(ECG_RANDOM_SEED);
    printf("LATENCY_MEMORY_BEGIN,variant=no-conditioning,scope=kernel_only\n");
    printf("ECG_REPRODUCIBILITY,seed=%u,input=fixed_synthetic_fixture\n", ECG_RANDOM_SEED);
    sqi_device_benchmark_run();
    printf("ECG_RESOURCE,profile=%s,cpu_mhz=%d,internal_heap_free_bytes=%u,internal_heap_min_bytes=%u,main_stack_min_free_bytes=%u,scope=whole_benchmark_image\n",
           ECG_PROFILE_NAME, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)uxTaskGetStackHighWaterMark(NULL));
    printf("ECG_BENCHMARK_END\n");
}
