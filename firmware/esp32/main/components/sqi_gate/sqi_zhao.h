#ifndef SQI_ZHAO_H
#define SQI_ZHAO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SQI_ZHAO_MAX_SAMPLES 3600U
#define SQI_ZHAO_MAX_PEAKS 32U

typedef enum {
    SQI_ZHAO_UNACCEPTABLE = 0,
    SQI_ZHAO_BARELY_ACCEPTABLE = 1,
    SQI_ZHAO_EXCELLENT = 2
} sqi_zhao_label_t;

typedef struct {
    float samples[SQI_ZHAO_MAX_SAMPLES];
    uint32_t peak_ms[SQI_ZHAO_MAX_PEAKS];
    uint16_t sample_count;
    uint8_t peak_count;
    bool overflow;
} sqi_zhao_state_t;

typedef struct {
    float score;
    float kurtosis;
    float power_ratio;
    float baseline_sqi;
    sqi_zhao_label_t label;
} sqi_zhao_result_t;

void sqi_zhao_reset(sqi_zhao_state_t *state);
bool sqi_zhao_push(sqi_zhao_state_t *state, float sample);
bool sqi_zhao_push_peak(sqi_zhao_state_t *state, uint32_t timestamp_ms);
sqi_zhao_result_t sqi_zhao_finalize(const sqi_zhao_state_t *state,
                                    uint32_t sample_rate_hz);
size_t sqi_zhao_static_bytes(void);

#endif
