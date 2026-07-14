#ifndef ECG_AFIB_DETECTOR_H
#define ECG_AFIB_DETECTOR_H

#include <stdint.h>

typedef enum {
    AFIB_STATUS_UNKNOWN = 0,
    AFIB_STATUS_NORMAL = 1,
    AFIB_STATUS_SUSPECTED = 2,
} afib_status_t;

typedef struct {
    afib_status_t status;
    uint8_t score;
    uint8_t rr_count;
    uint16_t mean_rr_ms;
    uint8_t pnn50_percent;
    uint8_t rr_cv_percent;
    uint8_t poincare_sd1_sd2_percent;
    uint8_t sample_entropy_percent;
    uint8_t irregularity_percent;
} afib_result_t;

void afib_detector_init(void);
void afib_detector_reset(void);
afib_result_t afib_detector_process_rr(uint16_t rr_ms);
afib_result_t afib_detector_get_result(void);

#endif
