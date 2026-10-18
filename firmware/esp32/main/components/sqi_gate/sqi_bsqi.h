#ifndef SQI_BSQI_H
#define SQI_BSQI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SQI_BSQI_MAX_PEAKS 32U

typedef struct {
    uint32_t primary_ms[SQI_BSQI_MAX_PEAKS];
    uint32_t secondary_ms[SQI_BSQI_MAX_PEAKS];
    uint8_t primary_count;
    uint8_t secondary_count;
    bool overflow;
} sqi_bsqi_state_t;

void sqi_bsqi_reset(sqi_bsqi_state_t *state);
bool sqi_bsqi_push_primary(sqi_bsqi_state_t *state, uint32_t timestamp_ms);
bool sqi_bsqi_push_secondary(sqi_bsqi_state_t *state, uint32_t timestamp_ms);
float sqi_bsqi_finalize(const sqi_bsqi_state_t *state, uint32_t tolerance_ms);
bool sqi_bsqi_accept(const sqi_bsqi_state_t *state, uint32_t tolerance_ms,
                     float threshold);
size_t sqi_bsqi_static_bytes(void);

#endif
