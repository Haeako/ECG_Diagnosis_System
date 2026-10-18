#ifndef SQI_ORPHANIDOU_H
#define SQI_ORPHANIDOU_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool passed;
    bool heart_rate_passed;
    bool max_rr_passed;
    bool rr_ratio_passed;
    uint16_t heart_rate_bpm;
    uint32_t max_rr_ms;
    float rr_ratio;
    uint32_t median_rr_ms;
} sqi_orphanidou_result_t;

sqi_orphanidou_result_t sqi_orphanidou_evaluate(const uint32_t *peak_ms,
                                                 uint8_t peak_count);

#endif
