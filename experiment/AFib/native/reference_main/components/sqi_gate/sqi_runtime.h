#ifndef SQI_RUNTIME_H
#define SQI_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#define ECG_SQI_INSUFFICIENT_SCORE 255U
#define ECG_SQI_USABLE_THRESHOLD   66U

#define ECG_SQI_RULE_HR_PASS       (1U << 0)
#define ECG_SQI_RULE_MAX_RR_PASS   (1U << 1)
#define ECG_SQI_RULE_RR_RATIO_PASS (1U << 2)
#define ECG_SQI_RULE_TEMPLATE_PASS (1U << 3)

typedef enum {
    ECG_SQI_UNUSABLE = 0,
    ECG_SQI_USABLE = 1,
    ECG_SQI_INSUFFICIENT = 2,
} ecg_sqi_status_t;

typedef struct {
    uint8_t score;
    ecg_sqi_status_t status;
    uint8_t rule_mask;
    uint16_t heart_rate_bpm;
    uint16_t max_rr_ms;
    uint16_t rr_ratio_x1000;
    int16_t template_correlation_x1000;
    uint32_t window_id;
    uint32_t finalize_us;
} ecg_sqi_result_t;

void sqi_runtime_reset(uint32_t now_ms);
ecg_sqi_result_t sqi_runtime_push(int16_t ecg_sample,
                                  bool is_r_peak,
                                  uint32_t r_peak_timestamp_ms,
                                  uint32_t now_ms);

#ifdef SQI_BENCHMARK_ENABLE
ecg_sqi_result_t sqi_runtime_benchmark_finalize(void);
#endif

#endif
