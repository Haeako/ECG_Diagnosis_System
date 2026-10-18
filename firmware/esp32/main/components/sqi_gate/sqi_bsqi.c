#include "sqi_bsqi.h"

#include <math.h>
#include <string.h>

void sqi_bsqi_reset(sqi_bsqi_state_t *state)
{
    if (state != NULL) memset(state, 0, sizeof(*state));
}

static bool append(uint32_t *peaks, uint8_t *count, bool *overflow,
                   uint32_t timestamp_ms)
{
    if (*count >= SQI_BSQI_MAX_PEAKS) {
        *overflow = true;
        return false;
    }
    if (*count > 0U && timestamp_ms <= peaks[*count - 1U]) return false;
    peaks[(*count)++] = timestamp_ms;
    return true;
}

bool sqi_bsqi_push_primary(sqi_bsqi_state_t *state, uint32_t timestamp_ms)
{
    return state != NULL && append(state->primary_ms, &state->primary_count,
                                   &state->overflow, timestamp_ms);
}

bool sqi_bsqi_push_secondary(sqi_bsqi_state_t *state, uint32_t timestamp_ms)
{
    return state != NULL && append(state->secondary_ms, &state->secondary_count,
                                   &state->overflow, timestamp_ms);
}

float sqi_bsqi_finalize(const sqi_bsqi_state_t *state, uint32_t tolerance_ms)
{
    if (state == NULL || state->overflow) return NAN;
    uint8_t i = 0U, j = 0U, matches = 0U;
    while (i < state->primary_count && j < state->secondary_count) {
        int64_t delta = (int64_t)state->primary_ms[i]
                      - (int64_t)state->secondary_ms[j];
        if (delta < -(int64_t)tolerance_ms) {
            ++i;
        } else if (delta > (int64_t)tolerance_ms) {
            ++j;
        } else {
            ++matches;
            ++i;
            ++j;
        }
    }
    uint16_t total = (uint16_t)state->primary_count + state->secondary_count;
    return total > 0U ? (2.0f * matches) / total : NAN;
}

bool sqi_bsqi_accept(const sqi_bsqi_state_t *state, uint32_t tolerance_ms,
                     float threshold)
{
    float score = sqi_bsqi_finalize(state, tolerance_ms);
    return isfinite(score) && score >= threshold;
}

size_t sqi_bsqi_static_bytes(void)
{
    return sizeof(sqi_bsqi_state_t);
}
