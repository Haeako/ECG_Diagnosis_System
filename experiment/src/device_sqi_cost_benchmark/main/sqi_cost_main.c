/* Current cost captures for existing kernels; no SQI accuracy is inferred. */
#include <inttypes.h>
#include <math.h>
#include <stdint.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sqi_bsqi.h"
#include "sqi_logistic_runtime.h"
#include "sqi_xqrs.h"

#define CONTEXT_N 3500U
#define CORE_START 500U
#define CORE_N 2500U
#define WARMUP 3U
#define REPEATS 21U
static const char *TAG = "SQI_COST";
static sqi_xqrs_workspace_t workspace;
static sqi_bsqi_state_t agreement;
static sqi_logistic_runtime_t logistic;
static float input[CONTEXT_N];
static uint16_t peaks[SQI_XQRS_MAX_PEAKS];

static void prepare_input(void)
{
    for (uint32_t i = 0; i < CONTEXT_N; ++i) {
        const uint32_t phase = i % 250U;
        input[i] = phase < 10U ? (float)(phase <= 5U ? phase : 10U-phase)*0.2f : 0.0f;
    }
}

static float bsqi_cost(void)
{
    sqi_bsqi_reset(&agreement);
    const size_t n = sqi_xqrs_detect_250(input, CONTEXT_N, peaks, SQI_XQRS_MAX_PEAKS, &workspace);
    for (uint32_t p = 250U; p < CORE_N; p += 250U)
        sqi_bsqi_push_primary(&agreement, p*1000U/250U);
    for (size_t i = 0; i < n; ++i)
        if (peaks[i] >= CORE_START && peaks[i] < CORE_START+CORE_N)
            sqi_bsqi_push_secondary(&agreement, (peaks[i]-CORE_START)*1000U/250U);
    return sqi_bsqi_finalize(&agreement, 50U);
}

static sqi_logistic_result_t logistic_cost(void)
{
    sqi_logistic_reset(&logistic);
    for (uint32_t i = CORE_START; i < CORE_START+CORE_N; ++i)
        sqi_logistic_push(&logistic, input[i]);
    const size_t n = sqi_xqrs_detect_250(input, CONTEXT_N, peaks, SQI_XQRS_MAX_PEAKS, &workspace);
    for (uint32_t p = 250U; p < CORE_N; p += 250U)
        sqi_logistic_push_primary(&logistic, p);
    for (size_t i = 0; i < n; ++i)
        if (peaks[i] >= CORE_START && peaks[i] < CORE_START+CORE_N)
            sqi_logistic_push_secondary(&logistic, peaks[i]-CORE_START);
    return sqi_logistic_finalize(&logistic, 250U);
}

void app_main(void)
{
    prepare_input();
    ESP_LOGI(TAG, "BEGIN cpu_mhz=%d fs=250 core_n=%u context_n=%u repeats=%u warmup=%u scope=secondary-XQRS-plus-external-primary",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, CORE_N, CONTEXT_N, REPEATS, WARMUP);
    ESP_LOGI(TAG, "MEM workspace=%u input=%u peaks=%u bsqi_state=%u logistic_state=%u classifier_flash=%u classifier_working=%u free_heap=%u",
             (unsigned)sizeof(workspace), (unsigned)sizeof(input), (unsigned)sizeof(peaks),
             (unsigned)sizeof(agreement), (unsigned)sizeof(logistic),
             (unsigned)sqi_gate_flash_bytes(), (unsigned)sqi_gate_working_ram_bytes(), esp_get_free_heap_size());
    for (unsigned route = 0; route < 2U; ++route) {
        for (unsigned run = 0; run < REPEATS+WARMUP; ++run) {
            const int64_t start = esp_timer_get_time();
            float score;
            int decision = -1;
            if (route == 0) score = bsqi_cost();
            else {
                const sqi_logistic_result_t value = logistic_cost();
                score = value.probability;
                decision = value.decision;
            }
            const int64_t elapsed = esp_timer_get_time()-start;
            ESP_LOGI(TAG, "SAMPLE route=%s run=%u warmup=%u us=%" PRId64 " score=%.9g decision=%d",
                     route == 0 ? "bsqi-secondary-full" : "logistic-secondary-features-head",
                     run, run < WARMUP, elapsed, (double)score, decision);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    ESP_LOGI(TAG, "END min_free_heap=%u largest_internal=%u stack_watermark=%u",
             esp_get_minimum_free_heap_size(),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
             (unsigned)uxTaskGetStackHighWaterMark(NULL));
}
