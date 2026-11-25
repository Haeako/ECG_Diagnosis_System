#ifndef SQI_LOGISTIC_RUNTIME_H
#define SQI_LOGISTIC_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sqi_gate_logistic.h"

#ifndef SQI_LOGISTIC_MAX_SAMPLES
#define SQI_LOGISTIC_MAX_SAMPLES 3600U
#endif
#define SQI_LOGISTIC_MAX_PEAKS 32U

typedef struct {
    float samples[SQI_LOGISTIC_MAX_SAMPLES];
    uint16_t primary[SQI_LOGISTIC_MAX_PEAKS];
    uint16_t secondary[SQI_LOGISTIC_MAX_PEAKS];
    uint16_t sample_count;
    uint8_t primary_count;
    uint8_t secondary_count;
    bool overflow;
} sqi_logistic_runtime_t;

typedef struct {
    float features[SQI_GATE_FEATURE_COUNT];
    float probability;
    sqi_gate_decision_t decision;
} sqi_logistic_result_t;

void sqi_logistic_reset(sqi_logistic_runtime_t *state);
bool sqi_logistic_push(sqi_logistic_runtime_t *state, float sample);
bool sqi_logistic_push_primary(sqi_logistic_runtime_t *state, uint16_t sample_index);
bool sqi_logistic_push_secondary(sqi_logistic_runtime_t *state, uint16_t sample_index);
sqi_logistic_result_t sqi_logistic_finalize(const sqi_logistic_runtime_t *state,
                                            uint32_t sample_rate_hz);
size_t sqi_logistic_static_bytes(void);

#endif
