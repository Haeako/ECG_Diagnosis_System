#include "af_model_esp32_benchmark.h"

#include "af_model_int8.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define AF_BENCH_BATCHES 101U

static void sort_u32(uint32_t *values, uint32_t count)
{
    for (uint32_t i = 1; i < count; ++i) {
        uint32_t value = values[i];
        uint32_t j = i;
        while (j > 0 && values[j - 1] > value) {
            values[j] = values[j - 1];
            --j;
        }
        values[j] = value;
    }
}

static const char *TAG = "AF_MODEL_BENCH";

void af_model_esp32_benchmark(const int8_t features[AF_MODEL_FEATURE_COUNT], uint32_t iterations)
{
    if (iterations == 0) {
        return;
    }
    uint32_t batch_us[AF_BENCH_BATCHES];
    uint32_t per_batch = (iterations + AF_BENCH_BATCHES - 1U) / AF_BENCH_BATCHES;
    size_t heap_before = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t minimum_heap_before = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    UBaseType_t stack_before = uxTaskGetStackHighWaterMark(NULL);
    uint32_t comparisons = 0;
    volatile uint32_t checksum = 0;
    uint32_t completed = 0;
    uint32_t batches = 0;
    while (completed < iterations) {
        uint32_t count = iterations - completed;
        if (count > per_batch) {
            count = per_batch;
        }
        int64_t start_us = esp_timer_get_time();
        for (uint32_t i = 0; i < count; ++i) {
            af_model_result_t result = af_model_predict_int8(features);
            checksum += result.score;
            comparisons += result.comparisons;
        }
        batch_us[batches++] = (uint32_t)(esp_timer_get_time() - start_us);
        completed += count;
    }
    sort_u32(batch_us, batches);
    size_t heap_after = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t minimum_heap_after = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);
    UBaseType_t stack_after = uxTaskGetStackHighWaterMark(NULL);
    uint32_t p50_index = (batches - 1U) / 2U;
    uint32_t p95_index = ((batches - 1U) * 95U) / 100U;
    double p50_us = (double)batch_us[p50_index] / per_batch;
    double p95_us = (double)batch_us[p95_index] / per_batch;
    double max_us = (double)batch_us[batches - 1U] / per_batch;
    ESP_LOGI(TAG, "AF_PROFILE_JSON:{\"iterations\":%lu,\"latency_us_p50\":%.3f,"
             "\"latency_us_p95\":%.3f,\"latency_us_max\":%.3f,"
             "\"comparisons_mean\":%.2f,\"heap_delta_bytes\":%ld,"
             "\"minimum_free_heap_bytes\":%u,\"stack_high_water_words\":%u,"
             "\"model_bytes\":%u,\"checksum\":%lu}",
             (unsigned long)iterations,
             p50_us, p95_us, max_us,
             (double)comparisons / iterations,
             (long)heap_before - (long)heap_after,
             (unsigned)(minimum_heap_after < minimum_heap_before
                            ? minimum_heap_after : minimum_heap_before),
             (unsigned)(stack_after < stack_before ? stack_after : stack_before),
             (unsigned)af_model_flash_bytes(),
             (unsigned long)checksum);
}
