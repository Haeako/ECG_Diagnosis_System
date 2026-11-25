#ifndef SQI_KRAMER_FULL_H
#define SQI_KRAMER_FULL_H

#include <stddef.h>
#include <stdint.h>

#include "sqi_kramer.h"

#define SQI_KRAMER_INPUT_SAMPLES 2500U
#define SQI_KRAMER_RESAMPLED_SAMPLES 5000U
#define SQI_KRAMER_FILTER_PAD 36U

typedef struct {
    sqi_kramer_state_t rules;
    /* The first 2500 entries hold input; finalize reuses the full allocation. */
    float input[SQI_KRAMER_RESAMPLED_SAMPLES];
    float resampled[SQI_KRAMER_RESAMPLED_SAMPLES];
    float scratch[SQI_KRAMER_RESAMPLED_SAMPLES + 2U * SQI_KRAMER_FILTER_PAD];
    uint16_t input_count;
} sqi_kramer_full_t;

void sqi_kramer_full_reset(sqi_kramer_full_t *state);
int sqi_kramer_full_push(sqi_kramer_full_t *state, float sample);
sqi_kramer_result_t sqi_kramer_full_finalize(sqi_kramer_full_t *state,
                                             uint16_t *peak_count,
                                             float *snr_ratio);
size_t sqi_kramer_full_static_bytes(void);

#endif
