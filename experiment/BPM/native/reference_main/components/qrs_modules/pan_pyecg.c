#include "pan_pyecg.h"

#include <stdbool.h>
#include <string.h>

#define DIFF_MWA_N 54U
#define ZERO_N (2U * DIFF_MWA_N)
#define MIN_DISTANCE 108U

static void handle_candidate(pan_pyecg_workspace_t *w, uint16_t peak,
                             double value, double *spki, double *npki,
                             double *threshold1, double *threshold2,
                             uint16_t *rr_missed)
{
    if (w->candidate_count >= PAN_PYECG_MAX_CANDIDATES) return;
    const uint16_t sequence = w->candidate_count++;
    w->candidate_index[sequence] = peak;
    w->candidate_value[sequence] = value;

    if (value > *threshold1 &&
        ((uint16_t)(peak - w->signal_peak[w->signal_count - 1U])) > MIN_DISTANCE) {
        if (w->signal_count < PAN_PYECG_MAX_CANDIDATES + 1U &&
            w->signal_candidate_count < PAN_PYECG_MAX_CANDIDATES) {
            w->signal_peak[w->signal_count++] = peak;
            w->signal_candidate[w->signal_candidate_count++] = sequence;
        }
        *spki = 0.125 * value + 0.875 * *spki;

        if (*rr_missed != 0U) {
            const uint16_t previous = w->signal_peak[w->signal_count - 2U];
            const uint16_t interval = (uint16_t)(peak - previous);
            if (interval > *rr_missed && w->signal_candidate_count >= 2U) {
                const uint16_t first_candidate =
                    w->signal_candidate[w->signal_candidate_count - 2U];
                const uint16_t last_candidate =
                    w->signal_candidate[w->signal_candidate_count - 1U];
                double best_value = *threshold2;
                uint16_t missed_peak = 0U;
                bool found = false;
                for (uint16_t c = (uint16_t)(first_candidate + 1U);
                     c < last_candidate; ++c) {
                    const uint16_t candidate_peak = w->candidate_index[c];
                    if ((uint16_t)(candidate_peak - previous) > 90U &&
                        (uint16_t)(peak - candidate_peak) > 90U &&
                        w->candidate_value[c] > best_value) {
                        best_value = w->candidate_value[c];
                        missed_peak = candidate_peak;
                        found = true;
                    }
                }
                if (found && w->signal_count < PAN_PYECG_MAX_CANDIDATES + 1U) {
                    w->signal_peak[w->signal_count] = w->signal_peak[w->signal_count - 1U];
                    ++w->signal_count;
                    w->signal_peak[w->signal_count - 2U] = missed_peak;
                }
            }
        }
    } else {
        *npki = 0.125 * value + 0.875 * *npki;
    }

    *threshold1 = *npki + 0.25 * (*spki - *npki);
    *threshold2 = 0.5 * *threshold1;

    if (w->signal_count > 8U) {
        uint32_t rr_sum = 0U;
        for (uint16_t i = (uint16_t)(w->signal_count - 8U);
             i < w->signal_count; ++i) {
            rr_sum += (uint16_t)(w->signal_peak[i] - w->signal_peak[i - 1U]);
        }
        const uint16_t rr_average = (uint16_t)(rr_sum / 8U);
        *rr_missed = (uint16_t)(1.66 * rr_average);
    }
}

size_t pan_pyecg_detect_360(const float *input_samples, size_t count,
                            uint16_t *peaks, size_t peak_capacity,
                            pan_pyecg_workspace_t *w)
{
    if (input_samples == NULL || peaks == NULL || w == NULL || count < 3U ||
        count > PAN_PYECG_MAX_SAMPLES || peak_capacity == 0U) return 0U;
    memset(w, 0, sizeof(*w));
    for (size_t j=0; j<count; ++j) w->input[j]=input_samples[j];
    const double *samples=w->input;

    w->workspace_bytes = sizeof(*w);
    w->signal_peak[w->signal_count++] = 0U;

    /* scipy.signal.butter(1, [10/360, 30/360], btype="bandpass") and
     * scipy.signal.lfilter's direct-form-II-transposed zero-state behavior. */
    const double b0 = 0.08045018441135998;
    const double b1 = 0.0;
    const double b2 = -0.08045018441135998;
    const double a1 = -1.8180779106881744;
    const double a2 = 0.8390996311772797;

    double previous_filtered = 0.0;
    double moving_sum = 0.0;
    double spki = 0.0, npki = 0.0;
    double threshold1 = 0.0, threshold2 = 0.0;
    uint16_t rr_missed = 0U;
    double detection_previous2 = 0.0, detection_previous1 = 0.0;

    for (size_t i = 0U; i < count; ++i) {
        const double filtered = b0 * samples[i] + w->filter_state[0];
        w->filter_state[0] = b1 * samples[i] - a1 * filtered + w->filter_state[1];
        w->filter_state[1] = b2 * samples[i] - a2 * filtered;

        if (i == 0U) {
            previous_filtered = filtered;
            continue;
        }
        const size_t detection_index = i - 1U;
        const double derivative = filtered - previous_filtered;
        previous_filtered = filtered;
        const double squared = derivative * derivative;
        double detection;
        if (detection_index < DIFF_MWA_N) {
            w->mwa_ring[detection_index] = squared;
            moving_sum += squared;
            detection = moving_sum / (double)(detection_index + 1U);
        } else {
            const size_t slot = detection_index % DIFF_MWA_N;
            moving_sum += squared - w->mwa_ring[slot];
            w->mwa_ring[slot] = squared;
            detection = moving_sum / (double)DIFF_MWA_N;
        }
        if (detection_index < ZERO_N) detection = 0.0;

        if (detection_index >= 2U &&
            detection_previous2 < detection_previous1 &&
            detection < detection_previous1) {
            handle_candidate(w, (uint16_t)(detection_index - 1U),
                             detection_previous1, &spki, &npki,
                             &threshold1, &threshold2, &rr_missed);
        }
        detection_previous2 = detection_previous1;
        detection_previous1 = detection;
    }

    const size_t output_count = w->signal_count > 0U ? w->signal_count - 1U : 0U;
    const size_t copy_count = output_count < peak_capacity ? output_count : peak_capacity;
    for (size_t i = 0U; i < copy_count; ++i) peaks[i] = w->signal_peak[i + 1U];
    return copy_count;
}
