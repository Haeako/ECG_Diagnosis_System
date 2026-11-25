#include "sqi_kramer.h"

#include <string.h>

void sqi_kramer_reset(sqi_kramer_state_t *state)
{
    if (state != NULL) memset(state, 0, sizeof(*state));
}

void sqi_kramer_push(sqi_kramer_state_t *state, float sample)
{
    if (state == NULL) return;
    state->recent[state->recent_head] = sample;
    state->recent_head = (uint8_t)((state->recent_head + 1U) % SQI_KRAMER_FLAT_WINDOW);
    if (state->recent_count < SQI_KRAMER_FLAT_WINDOW) ++state->recent_count;
    ++state->sample_count;
    if (state->recent_count == SQI_KRAMER_FLAT_WINDOW &&
        ((state->sample_count - SQI_KRAMER_FLAT_WINDOW) % 10U) == 0U) {
        float minimum = state->recent[0], maximum = state->recent[0];
        for (uint32_t i = 1U; i < SQI_KRAMER_FLAT_WINDOW; ++i) {
            if (state->recent[i] < minimum) minimum = state->recent[i];
            if (state->recent[i] > maximum) maximum = state->recent[i];
        }
        if (maximum == minimum) state->stationary_bad = true;
    }
}

sqi_kramer_result_t sqi_kramer_finalize(const sqi_kramer_state_t *state,
                                        uint16_t filtered_peak_count,
                                        bool snr_bad)
{
    sqi_kramer_result_t out;
    out.stationary_bad = state == NULL || state->stationary_bad;
    out.heart_rate_bad = filtered_peak_count > 50U || filtered_peak_count < 4U;
    out.snr_bad = snr_bad;
    unsigned failures = out.stationary_bad + out.heart_rate_bad + out.snr_bad;
    out.score = 1.0f - failures / 3.0f;
    out.accept = failures == 0U;
    return out;
}

size_t sqi_kramer_static_bytes(void)
{
    return sizeof(sqi_kramer_state_t);
}
