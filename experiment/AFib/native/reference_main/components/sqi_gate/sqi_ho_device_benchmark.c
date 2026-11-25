#include "sqi_device_benchmark.h"
#include "sqi_ho.h"

#include "esp_cpu.h"
#include "esp_log.h"

#include <stdlib.h>

#define SAMPLE_COUNT 3600U
#define PEAK_COUNT 10U
#define PEAK_CAPACITY 64U
#define REPEATS 1001U
#define CPU_MHZ ((double)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)

static float samples[SAMPLE_COUNT];
static uint32_t primary[PEAK_COUNT];
static uint32_t secondary[PEAK_COUNT];
static uint32_t primary_scratch[PEAK_CAPACITY];
static uint32_t secondary_scratch[PEAK_CAPACITY];
static uint8_t quality[SAMPLE_COUNT];
static uint32_t cycles[REPEATS];

static int compare_u32(const void *left, const void *right)
{
    const uint32_t a = *(const uint32_t *)left;
    const uint32_t b = *(const uint32_t *)right;
    return (a > b) - (a < b);
}

void sqi_device_benchmark_run(void)
{
    for (uint32_t i = 0U; i < SAMPLE_COUNT; ++i) {
        samples[i] = 0.01f * (float)(i % 17U);
    }
    for (uint32_t i = 0U; i < PEAK_COUNT; ++i) {
        primary[i] = 100U + i * 360U;
        secondary[i] = primary[i] + 2U;
        samples[primary[i]] = 1.0f;
        samples[secondary[i]] = 0.9f;
    }

    volatile uint32_t sink = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        const int ok = sqi_ho_ici_quality(
            samples, SAMPLE_COUNT,
            primary, PEAK_COUNT, secondary, PEAK_COUNT,
            360U, 50U,
            primary_scratch, PEAK_CAPACITY,
            secondary_scratch, PEAK_CAPACITY,
            quality);
        cycles[repeat] = esp_cpu_get_cycle_count() - before;
        sink += (uint32_t)ok + quality[SAMPLE_COUNT / 2U];
    }
    qsort(cycles, REPEATS, sizeof(cycles[0]), compare_u32);
    const uint32_t p50 = cycles[(REPEATS - 1U) / 2U];
    const uint32_t p99 = cycles[(99U * (REPEATS - 1U)) / 100U];
    const size_t scratch_bytes = sizeof(primary_scratch) + sizeof(secondary_scratch)
                               + sizeof(quality);
    ESP_LOGI("SQI_HO_BENCH", "HO_ICI_JSON:{\"method\":\"Ho-NeuroKit-ICI-kernel\","
             "\"sampling_rate_hz\":360,\"samples\":%u,\"beats\":%u,"
             "\"tolerance_ms\":50,\"repeats\":%u,"
             "\"latency_us_p50\":%.3f,\"latency_us_p99\":%.3f,"
             "\"state_bytes\":0,\"caller_scratch_bytes\":%u,"
             "\"input_buffer_bytes\":%u,\"sink\":%u}",
             SAMPLE_COUNT, PEAK_COUNT, REPEATS,
             p50 / CPU_MHZ, p99 / CPU_MHZ,
             (unsigned)scratch_bytes, (unsigned)sizeof(samples), (unsigned)sink);
}
