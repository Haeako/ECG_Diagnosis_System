#include <stdio.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "components/af_model/af_model_esp32_benchmark.h"
#include "components/af_model/af_model_int8.h"

void app_main(void)
{
    srand(42U);
    printf("LATENCY_MEMORY_BEGIN,variant=classifier_only,scope=kernel_only\n");
    static const int8_t features[AF_MODEL_FEATURE_COUNT] = {-32, -48, 24, 10, 31};
    af_model_esp32_benchmark(features, 10000U);
    printf("ECG_RESOURCE,profile=af-extra-trees,cpu_mhz=%d,internal_heap_free_bytes=%u,internal_heap_min_bytes=%u,main_stack_min_free_bytes=%u,scope=whole_benchmark_image\n",
           CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)uxTaskGetStackHighWaterMark(NULL));
    printf("ECG_BENCHMARK_END\n");
}
