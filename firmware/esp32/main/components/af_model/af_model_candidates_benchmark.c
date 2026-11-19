#include "af_model_candidates_benchmark.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>

#if defined(AF_MODEL_BENCHMARK_LOGISTIC)
#include "af_model_logistic_int8.h"
#define MODEL_NAME "logistic_int8"
#define MODEL_FLASH_BYTES af_lr_flash_bytes
#define MODEL_RAM_BYTES af_lr_working_ram_bytes
#define MODEL_INFER(features) af_lr_predict_probability(features)
#elif defined(AF_MODEL_BENCHMARK_RF)
#include "af_model_rf_int8.h"
#define MODEL_NAME "random_forest_int8"
#define MODEL_FLASH_BYTES af_rf_flash_bytes
#define MODEL_RAM_BYTES af_rf_working_ram_bytes
#define MODEL_INFER(features) af_rf_predict(features).score
#else
#error "Select an AF candidate model benchmark"
#endif

#define ITERATIONS 10000U
#define BATCHES 101U
static const char *TAG = "AF_CANDIDATE_BENCH";
static const float features[5] = {0.27586207f, 2.14174223f, 36.95027542f,
                                  0.90978462f, 1116.0f};

static void sort_u32(uint32_t *values, uint32_t count)
{
    for (uint32_t i = 1; i < count; ++i) {
        uint32_t value = values[i];
        uint32_t j = i;
        while (j > 0U && values[j - 1U] > value) {
            values[j] = values[j - 1U];
            --j;
        }
        values[j] = value;
    }
}

void af_model_candidates_benchmark_run(void)
{
    uint32_t timings[BATCHES];
    const uint32_t per_batch = (ITERATIONS + BATCHES - 1U) / BATCHES;
    uint32_t completed = 0U;
    uint32_t count_batches = 0U;
    volatile float checksum = 0.0f;
    const size_t heap_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const size_t min_heap_before = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    const UBaseType_t stack_before = uxTaskGetStackHighWaterMark(NULL);

    while (completed < ITERATIONS) {
        uint32_t count = ITERATIONS - completed;
        if (count > per_batch) {
            count = per_batch;
        }
        const int64_t start = esp_timer_get_time();
        for (uint32_t i = 0; i < count; ++i) {
            checksum += (float)MODEL_INFER(features);
        }
        timings[count_batches++] = (uint32_t)(esp_timer_get_time() - start);
        completed += count;
        /* Keep the main task watchdog serviced without contaminating timed batches. */
        vTaskDelay(1);
    }
    sort_u32(timings, count_batches);

    const size_t heap_after = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const size_t min_heap_after = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    const UBaseType_t stack_after = uxTaskGetStackHighWaterMark(NULL);
    const uint32_t p50 = (count_batches - 1U) / 2U;
    const uint32_t p95 = ((count_batches - 1U) * 95U) / 100U;
    const uint32_t p99 = ((count_batches - 1U) * 99U) / 100U;
    ESP_LOGI(TAG, "AF_CANDIDATE_JSON:{\"model\":\"%s\",\"iterations\":%u,"
             "\"latency_us_p50\":%.3f,\"latency_us_p95\":%.3f,"
             "\"latency_us_p99\":%.3f,\"latency_us_max\":%.3f,"
             "\"heap_delta_bytes\":%ld,\"minimum_free_heap_bytes\":%u,"
             "\"stack_high_water_words\":%u,\"model_bytes\":%u,"
             "\"working_ram_bytes\":%u,\"checksum\":%.3f}",
             MODEL_NAME, ITERATIONS,
             (double)timings[p50] / per_batch,
             (double)timings[p95] / per_batch,
             (double)timings[p99] / per_batch,
             (double)timings[count_batches - 1U] / per_batch,
             (long)heap_before - (long)heap_after,
             (unsigned)(min_heap_after < min_heap_before ? min_heap_after : min_heap_before),
             (unsigned)(stack_after < stack_before ? stack_after : stack_before),
             (unsigned)MODEL_FLASH_BYTES(), (unsigned)MODEL_RAM_BYTES(),
             (double)checksum);
}
