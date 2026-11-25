/* CONDITIONING REMOVED: latency/memory variant, not the audited accuracy implementation. */
#include "elgendi_pyecg.h"

#include <stdbool.h>
#include <string.h>

#define QRS_MWA_N 43U
#define BEAT_MWA_N 216U
#define MIN_BLOCK_DURATION 28U
#define REFRACTORY_SAMPLES 108U

size_t elgendi_pyecg_detect_360(const float *samples, size_t count,
                               uint16_t *peaks, size_t peak_capacity,
                               elgendi_pyecg_workspace_t *w)
{
    if (samples == NULL || peaks == NULL || w == NULL || count < 3U ||
        count > ELGENDI_PYECG_MAX_SAMPLES || peak_capacity == 0U) return 0U;
    memset(w, 0, sizeof(*w));
    w->workspace_bytes = sizeof(*w);

    memcpy(w->filtered, samples, count * sizeof(float));

    float block_height = w->filtered[0];
    for (size_t i = 1U; i < count; ++i) {
        if (w->filtered[i] > block_height) block_height = w->filtered[i];
    }
    /* In the Python implementation a zero block_height makes true blocks
     * indistinguishable from the initialized zero array, so no block exists. */
    if (block_height == 0.0f) return 0U;

    float qrs_sum = 0.0f, beat_sum = 0.0f;
    bool previous_active = false;
    uint16_t start = 0U;
    size_t detected = 0U;
    uint16_t previous_peak = 0U;
    bool have_previous_peak = false;

    for (size_t i = 0U; i < count; ++i) {
        const float absolute = (w->filtered[i] < 0.0f)
            ? -w->filtered[i] : w->filtered[i];
        float qrs_average, beat_average;
        if (i < QRS_MWA_N) {
            qrs_sum += absolute;
            qrs_average = qrs_sum / (float)(i + 1U);
        } else {
            qrs_sum += absolute - w->mwa_ring[(i - QRS_MWA_N) % BEAT_MWA_N];
            qrs_average = qrs_sum / (float)QRS_MWA_N;
        }
        if (i < BEAT_MWA_N) {
            beat_sum += absolute;
            beat_average = beat_sum / (float)(i + 1U);
        } else {
            beat_sum += absolute - w->mwa_ring[i % BEAT_MWA_N];
            beat_average = beat_sum / (float)BEAT_MWA_N;
        }
        w->mwa_ring[i % BEAT_MWA_N] = absolute;

        const bool active = qrs_average > beat_average;
        if (i > 0U) {
            if (!previous_active && active) {
                start = (uint16_t)i;
            } else if (previous_active && !active) {
                const uint16_t end = (uint16_t)(i - 1U);
                if ((uint16_t)(end - start) > MIN_BLOCK_DURATION) {
                    uint16_t candidate = start;
                    for (uint16_t j = (uint16_t)(start + 1U); j <= end; ++j) {
                        if (w->filtered[j] > w->filtered[candidate]) candidate = j;
                    }
                    if ((!have_previous_peak ||
                         (uint16_t)(candidate - previous_peak) > REFRACTORY_SAMPLES) &&
                        detected < peak_capacity) {
                        peaks[detected++] = candidate;
                        previous_peak = candidate;
                        have_previous_peak = true;
                    }
                }
            }
        }
        previous_active = active;
    }
    return detected;
}
