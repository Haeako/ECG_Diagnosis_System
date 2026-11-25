#include "components/sqi_gate/sqi_kramer.h"

/* Timing adapter only; peak count/SNR are externally supplied frozen inputs. */
int kramer_rules_window(const float *samples, size_t count,
                        unsigned short peaks, int snr_bad, double *out)
{
    if (!samples || !out || count != 2500) return 1;
    sqi_kramer_state_t state;
    sqi_kramer_reset(&state);
    for (size_t i = 0; i < count; ++i) sqi_kramer_push(&state, samples[i]);
    sqi_kramer_result_t result = sqi_kramer_finalize(&state, peaks, snr_bad != 0);
    out[0] = result.score;
    out[1] = result.accept;
    out[2] = result.stationary_bad;
    out[3] = result.heart_rate_bad;
    out[4] = result.snr_bad;
    return 0;
}
