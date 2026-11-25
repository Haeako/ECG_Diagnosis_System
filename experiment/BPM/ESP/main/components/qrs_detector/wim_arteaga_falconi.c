/* CONDITIONING REMOVED: latency/memory variant, not the audited accuracy implementation. */
#include "wim_arteaga_falconi.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

/* Latency-only variant: conditioning stages are intentionally excluded. */
static size_t latency_copy_stage(const float *input, float *output, size_t count)
{
    if (input != output) memmove(output, input, count * sizeof(*output));
    return count;
}

#define SECTIONS 5U


static int compare_position_values(uint16_t a, uint16_t b, const float *values)
{
    if (values[a] < values[b]) return -1;
    if (values[a] > values[b]) return 1;
    return (a > b) - (a < b);
}

/* Small in-place heapsort avoids platform-specific qsort_r interfaces. */
static void sift_down(uint16_t *order, size_t root, size_t end, const float *values)
{
    while (root * 2U + 1U < end) {
        size_t child = root * 2U + 1U;
        if (child + 1U < end && compare_position_values(order[child], order[child + 1U], values) < 0) ++child;
        if (compare_position_values(order[root], order[child], values) >= 0) return;
        const uint16_t tmp = order[root]; order[root] = order[child]; order[child] = tmp;
        root = child;
    }
}

static void sort_positions(uint16_t *order, size_t n, const float *values)
{
    for (size_t start = n / 2U; start > 0U; --start) sift_down(order, start - 1U, n, values);
    for (size_t end = n; end > 1U; --end) {
        const uint16_t tmp = order[0]; order[0] = order[end - 1U]; order[end - 1U] = tmp;
        sift_down(order, 0U, end - 1U, values);
    }
}

size_t wim_af_detect_360(const float *samples, size_t count,
                         uint16_t *peaks, size_t peak_capacity,
                         wim_af_workspace_t *ws)
{
    if (samples == NULL || peaks == NULL || ws == NULL || count < 64U ||
        count > WIM_AF_MAX_SAMPLES || peak_capacity == 0U) return 0U;
    if (!latency_copy_stage(samples, ws->filtered, count) ||
        !latency_copy_stage(ws->filtered, ws->filtered, count)) return 0U;

    for (size_t i = 0U; i + 2U < count; ++i) {
        const float second = ws->filtered[i + 2U] - 2.0f * ws->filtered[i + 1U] + ws->filtered[i];
        ws->inverted[i] = -second;
    }
    if (count >= 5U) {
        ws->inverted[count - 2U] = ws->inverted[count - 5U];
        ws->inverted[count - 1U] = ws->inverted[count - 4U];
    }
    memset(ws->peak_mask, 0, count * sizeof(ws->peak_mask[0]));
    for (size_t i = 0U; i < count; ++i) ws->order[i] = (uint16_t)i;
    sort_positions(ws->order, count, ws->inverted);
    size_t lr = (size_t)(0.018f * 360.0f * (220.0f / 60.0f) * ((float)count / 360.0f));
    if (lr > count) lr = count;
    const size_t window = (size_t)(0.34f * 360.0f);
    for (size_t k = 0U; k < lr; ++k) {
        const size_t pos = ws->order[k];
        const size_t start = pos > window ? pos - window : 0U;
        const size_t raw_end = pos + window;
        const size_t end = raw_end < count ? raw_end : count;
        if (end <= start) continue;
        size_t best = start;
        for (size_t j = start + 1U; j < end; ++j)
            if (ws->inverted[j] > ws->inverted[best]) best = j;
        ws->peak_mask[best] = 1U;
    }
    size_t written = 0U;
    for (size_t i = 0U; i < count && written < peak_capacity; ++i)
        if (ws->peak_mask[i] != 0U) peaks[written++] = (uint16_t)i;
    return written;
}
