#include <math.h>
#include <stddef.h>
#include "components/sqi_gate/sqi_kramer_full.h"

/* Single caller. Each invocation resets both states; no persistent window
 * history or external learned classifier is introduced by this adapter. */
static sqi_kramer_full_t full_state;
static sqi_kramer_state_t rule_state;

int kramer_window(const float *samples, size_t count, double *out)
{
    if (!samples || !out || count != SQI_KRAMER_INPUT_SAMPLES) return 1;
    sqi_kramer_full_reset(&full_state);
    sqi_kramer_reset(&rule_state);
    for (size_t i = 0; i < count; ++i) {
        if (!isfinite(samples[i]) || !sqi_kramer_full_push(&full_state, samples[i])) return 2;
        sqi_kramer_push(&rule_state, samples[i]);
    }
    uint16_t peaks = 0;
    float ratio = 0;
    sqi_kramer_result_t full = sqi_kramer_full_finalize(&full_state, &peaks, &ratio);
    /* Conditional rules scope: peak count and SNR flag provided by the full
     * C waveform implementation, rather than cached Python features. */
    sqi_kramer_result_t rules = sqi_kramer_finalize(&rule_state, peaks, full.snr_bad);
    if (full.score != rules.score || full.accept != rules.accept ||
        full.stationary_bad != rules.stationary_bad ||
        full.heart_rate_bad != rules.heart_rate_bad || full.snr_bad != rules.snr_bad) return 3;
    out[0] = full.score;
    out[1] = full.accept;
    out[2] = peaks;
    out[3] = ratio;
    out[4] = full.stationary_bad;
    out[5] = full.heart_rate_bad;
    out[6] = full.snr_bad;
    return 0;
}

size_t kramer_full_state_bytes(void) { return sizeof(full_state); }
size_t kramer_rules_state_bytes(void) { return sizeof(rule_state); }
