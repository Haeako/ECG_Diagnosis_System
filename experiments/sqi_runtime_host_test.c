#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "sqi_runtime.h"

static ecg_sqi_result_t run_window(int mode)
{
    ecg_sqi_result_t result = {0};
    sqi_runtime_reset(0U);
    for (uint32_t i = 0; i <= 3600U; ++i) {
        int16_t ecg;
        bool is_peak = i >= 360U && i < 3600U && i % 360U == 0U;
        if (mode == 0) {
            uint32_t phase = i % 180U;
            ecg = (int16_t)(3 * (int32_t)(phase < 90U ? phase : 180U - phase));
        } else if (mode == 1) {
            ecg = 0;
        } else {
            ecg = (i & 1U) ? -2000 : 2000;
        }
        uint32_t now_ms = (i * 1000U) / 360U;
        result = sqi_runtime_push(ecg, is_peak, is_peak ? now_ms : 0U, now_ms);
    }
    return result;
}

int main(void)
{
    sqi_runtime_reset(0U);
    ecg_sqi_result_t early = sqi_runtime_push(0, false, 0U, 100U);
    assert(early.status == ECG_SQI_INSUFFICIENT);
    assert(early.score == ECG_SQI_INSUFFICIENT_SCORE);

    ecg_sqi_result_t clean = run_window(0);
    assert(clean.status == ECG_SQI_USABLE);
    assert(clean.score >= ECG_SQI_USABLE_THRESHOLD);
    assert(clean.rule_mask == (ECG_SQI_RULE_HR_PASS
        | ECG_SQI_RULE_MAX_RR_PASS | ECG_SQI_RULE_RR_RATIO_PASS
        | ECG_SQI_RULE_TEMPLATE_PASS));
    assert(clean.heart_rate_bpm == 60U);
    assert(clean.max_rr_ms >= 997U && clean.max_rr_ms <= 1000U);
    assert(clean.rr_ratio_x1000 >= 999U && clean.rr_ratio_x1000 <= 1002U);
    assert(clean.template_correlation_x1000 >= 660);
    assert(clean.window_id == 0U);

    ecg_sqi_result_t flat = run_window(1);
    assert(flat.status == ECG_SQI_UNUSABLE);
    assert(flat.score == 0U);

    ecg_sqi_result_t alternating = run_window(2);
    assert(alternating.status == ECG_SQI_USABLE);

    printf("PASS clean=%u flat=%u alternating=%u\n",
           clean.score, flat.score, alternating.score);
    return 0;
}
