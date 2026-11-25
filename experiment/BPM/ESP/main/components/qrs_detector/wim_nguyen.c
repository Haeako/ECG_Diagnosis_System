/* CONDITIONING REMOVED: latency/memory variant, not the audited accuracy implementation. */
#include "wim_nguyen.h"

#include <math.h>
#include <string.h>

/* Latency-only variant: conditioning stages are intentionally excluded. */
static size_t latency_copy_stage(const float *input, float *output, size_t count)
{
    if (input != output) memmove(output, input, count * sizeof(*output));
    return count;
}

#define SOS_SECTIONS 2U


static size_t collect_maxima(const float *x, size_t n, float threshold,
                             uint16_t *indices)
{
    size_t count = 0U;
    size_t i = 1U;
    while (i + 1U < n) {
        if (x[i] < threshold || x[i] <= x[i - 1U]) {
            ++i;
            continue;
        }
        size_t right = i;
        while (right + 1U < n && x[right + 1U] == x[i]) ++right;
        if (right + 1U < n && x[right] > x[right + 1U]) {
            indices[count++] = (uint16_t)((i + right) / 2U);
        }
        i = right + 1U;
    }
    return count;
}

size_t wim_nguyen_detect_360(const float *samples, size_t count,
                             uint16_t *peaks, size_t peak_capacity,
                             wim_nguyen_workspace_t *ws)
{
    if (samples == NULL || peaks == NULL || ws == NULL || count < 64U ||
        count > WIM_NGUYEN_MAX_SAMPLES || peak_capacity == 0U) return 0U;

    
    if (latency_copy_stage(samples, ws->filtered, count) != count)
        return 0U;

    const size_t wl = (size_t)(WIM_NGUYEN_FS * 0.025f);
    memset(ws->template_signal, 0, count * sizeof(float));
    for (size_t i = wl; i + wl < count; ++i) {
        const float left = ws->filtered[i] - ws->filtered[i - wl];
        const float right = ws->filtered[i] - ws->filtered[i + wl];
        const float value = left * right;
        ws->template_signal[i] = value > 0.0f ? value : 0.0f;
    }
    if (latency_copy_stage(ws->template_signal, ws->filtered, count) != count) return 0U;

    float mean = 0.0f;
    for (size_t i = 0U; i < count; ++i) mean += ws->filtered[i];
    const float threshold = 2.5f * mean / (float)count;
    const size_t candidates = collect_maxima(ws->filtered, count, threshold,
                                              ws->candidate_index);
    memset(ws->candidate_used, 0, candidates * sizeof(ws->candidate_used[0]));

    /* scipy.find_peaks(distance=97) removes the lower peak first. */
    size_t accepted = 0U;
    const size_t min_distance = (size_t)(WIM_NGUYEN_FS * 0.272f);
    while (accepted < peak_capacity) {
        size_t best = candidates;
        float best_value = -INFINITY;
        for (size_t j = 0U; j < candidates; ++j) {
            const uint16_t idx = ws->candidate_index[j];
            if (ws->candidate_used[j] == 0U && ws->filtered[idx] > best_value) {
                best = j;
                best_value = ws->filtered[idx];
            }
        }
        if (best == candidates) break;
        ws->candidate_used[best] = 1U;
        const uint16_t idx = ws->candidate_index[best];
        peaks[accepted++] = idx;
        for (size_t j = 0U; j < candidates; ++j) {
            if (ws->candidate_used[j] == 0U) {
                const uint16_t other = ws->candidate_index[j];
                const size_t distance = idx > other ? idx - other : other - idx;
                if (distance < min_distance) ws->candidate_used[j] = 2U;
            }
        }
    }

    for (size_t i = 1U; i < accepted; ++i) {
        const uint16_t value = peaks[i];
        size_t j = i;
        while (j > 0U && peaks[j - 1U] > value) {
            peaks[j] = peaks[j - 1U];
            --j;
        }
        peaks[j] = value;
    }
    return accepted;
}
