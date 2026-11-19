/* Exactly one algorithm benchmark is linked into each device build. */
#include <inttypes.h>
#include <stdio.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if defined(ECG_RUNTIME_BENCHMARK)
void ecg_runtime_benchmark_run(void);
#elif defined(BPM_CANDIDATES_BENCHMARK)
void ecg_bpm_candidates_run(void);
#elif defined(AF_MODEL_CANDIDATE_BENCHMARK_ENABLE)
#include "components/af_model/af_model_candidates_benchmark.h"
#elif defined(AF_MODEL_BENCHMARK_ENABLE)
#include "components/af_model/af_model_esp32_benchmark.h"
#include "components/af_model/af_model_int8.h"
#elif defined(SQI_BENCHMARK_ENABLE)
#include "components/sqi_gate/sqi_device_benchmark.h"
#else
#error "Select a supported ECG benchmark profile"
#endif

void app_main(void)
{
#if defined(ECG_RUNTIME_BENCHMARK)
    ecg_runtime_benchmark_run();
#elif defined(BPM_CANDIDATES_BENCHMARK)
    ecg_bpm_candidates_run();
#elif defined(AF_MODEL_CANDIDATE_BENCHMARK_ENABLE)
    af_model_candidates_benchmark_run();
#elif defined(AF_MODEL_BENCHMARK_ENABLE)
    static const int8_t features[AF_MODEL_FEATURE_COUNT] = {-32, -48, 24, 10, 31};
    af_model_esp32_benchmark(features, 10000U);
#elif defined(SQI_BENCHMARK_ENABLE)
    sqi_device_benchmark_run();
#endif
    printf("ECG_RESOURCE,profile=%s,cpu_mhz=%d,internal_heap_free_bytes=%u,internal_heap_min_bytes=%u,main_stack_min_free_bytes=%u,scope=whole_benchmark_image\n",
           ECG_PROFILE_NAME, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)uxTaskGetStackHighWaterMark(NULL));
    printf("ECG_BENCHMARK_END\n");
}
