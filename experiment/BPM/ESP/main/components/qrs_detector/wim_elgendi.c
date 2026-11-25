/* CONDITIONING REMOVED: latency/memory variant, not the audited accuracy implementation. */
#include "wim_elgendi.h"

#include <stdbool.h>
#include <math.h>
#include <string.h>

/* Latency-only variant: conditioning stages are intentionally excluded. */
static size_t latency_copy_stage(const float *input, float *output, size_t count)
{
    if (input != output) memmove(output, input, count * sizeof(*output));
    return count;
}

#define WIM_ELGENDI_SECTIONS 3U


static float centered_box_mean(const float *prefix, size_t n,
                               size_t index, size_t width)
{
    const size_t left = width - 1U - (width - 1U) / 2U;
    const size_t right = (width - 1U) / 2U;
    const size_t raw_start = index > left ? index - left : 0U;
    const size_t raw_end = index + right + 1U;
    const size_t start = raw_start;
    const size_t end = raw_end < n ? raw_end : n;
    return (prefix[end] - prefix[start]) / (float)width;
}

size_t wim_elgendi_detect_360(const float *samples, size_t count,
                              uint16_t *peaks, size_t peak_capacity,
                              wim_elgendi_workspace_t *ws)
{
    if (samples == NULL || peaks == NULL || ws == NULL || count < 64U ||
        count > WIM_ELGENDI_MAX_SAMPLES || peak_capacity == 0U) return 0U;
    if (!latency_copy_stage(samples, ws->filtered, count)) return 0U;

    float minimum = ws->filtered[0];
    float maximum = ws->filtered[0];
    float mean_square = 0.0f;
    ws->prefix[0] = 0.0f;
    for (size_t i = 0U; i < count; ++i) {
        const float v = ws->filtered[i];
        if (v < minimum) minimum = v;
        if (v > maximum) maximum = v;
        const float square = v * v;
        mean_square += square;
        ws->prefix[i + 1U] = ws->prefix[i] + square;
    }
    const float range = maximum - minimum;
    if (!(range > 0.0f)) return 0U;
    for (size_t i = 0U; i < count; ++i)
        ws->filtered[i] = (ws->filtered[i] - minimum) / range;

    const size_t w1 = (size_t)(0.2f * WIM_ELGENDI_FS);
    const size_t w2 = (size_t)(0.82f * WIM_ELGENDI_FS);
    const float alpha = 0.08f * mean_square / (float)count;
    memset(ws->peak_flags, 0, count * sizeof(ws->peak_flags[0]));

    bool in_block = false;
    size_t block_start = 0U;
    for (size_t i = 0U; i <= count; ++i) {
        const bool active = i < count &&
            centered_box_mean(ws->prefix, count, i, w1) >
            centered_box_mean(ws->prefix, count, i, w2) + alpha;
        if (active && !in_block) {
            in_block = true;
            block_start = i;
        } else if (!active && in_block) {
            size_t block_peak = block_start;
            for (size_t j = block_start + 1U; j < i; ++j)
                if (ws->filtered[j] > ws->filtered[block_peak]) block_peak = j;

            const size_t search = (size_t)(0.13275f * WIM_ELGENDI_FS);
            if (block_peak >= search && block_peak + search < count) {
                size_t best = block_peak - search;
                float best_value = fabsf(samples[best]);
                const size_t end = block_peak + search;
                for (size_t j = best + 1U; j < end; ++j) {
                    const float value = fabsf(samples[j]);
                    if (value > best_value) {
                        best = j;
                        best_value = value;
                    }
                }
                ws->peak_flags[best] = 1U;
            }
            in_block = false;
        }
    }

    size_t found = 0U;
    for (size_t i = 0U; i < count && found < peak_capacity; ++i)
        if (ws->peak_flags[i] != 0U) peaks[found++] = (uint16_t)i;
    return found;
}
