#include "sqi_orphanidou.h"

#include <stddef.h>

#define SQI_MAX_PEAKS       32U
#define SQI_MIN_PEAKS       3U
#define SQI_HR_MIN_BPM      40.0f
#define SQI_HR_MAX_BPM      180.0f
#define SQI_MAX_RR_MS       3000U
#define SQI_MAX_RR_RATIO    2.2f

static void sort_u32(uint32_t *values, uint8_t count)
{
    for (uint8_t i = 1U; i < count; ++i) {
        uint32_t value = values[i];
        uint8_t j = i;
        while (j > 0U && values[j - 1U] > value) {
            values[j] = values[j - 1U];
            --j;
        }
        values[j] = value;
    }
}

sqi_orphanidou_result_t sqi_orphanidou_evaluate(const uint32_t *peak_ms,
                                                 uint8_t peak_count)
{
    sqi_orphanidou_result_t result = {0};
    if (peak_ms == NULL || peak_count < SQI_MIN_PEAKS
        || peak_count > SQI_MAX_PEAKS) {
        return result;
    }

    uint32_t rr_ms[SQI_MAX_PEAKS - 1U];
    uint8_t rr_count = (uint8_t)(peak_count - 1U);
    uint32_t rr_sum = 0U;
    uint32_t rr_min = UINT32_MAX;
    uint32_t rr_max = 0U;

    for (uint8_t i = 0U; i < rr_count; ++i) {
        uint32_t rr = peak_ms[i + 1U] - peak_ms[i];
        if (rr == 0U) return result;
        rr_ms[i] = rr;
        rr_sum += rr;
        if (rr < rr_min) rr_min = rr;
        if (rr > rr_max) rr_max = rr;
    }

    float heart_rate = 60000.0f * (float)rr_count / (float)rr_sum;
    result.heart_rate_bpm = (uint16_t)(heart_rate + 0.5f);
    result.max_rr_ms = rr_max;
    result.rr_ratio = (float)rr_max / (float)rr_min;
    result.heart_rate_passed = heart_rate >= SQI_HR_MIN_BPM
        && heart_rate <= SQI_HR_MAX_BPM;
    result.max_rr_passed = rr_max <= SQI_MAX_RR_MS;
    result.rr_ratio_passed = result.rr_ratio < SQI_MAX_RR_RATIO;

    sort_u32(rr_ms, rr_count);
    result.median_rr_ms = (rr_count & 1U) != 0U
        ? rr_ms[rr_count / 2U]
        : (rr_ms[rr_count / 2U - 1U] + rr_ms[rr_count / 2U]) / 2U;
    result.passed = result.heart_rate_passed && result.max_rr_passed
        && result.rr_ratio_passed;
    return result;
}
