/* CONDITIONING REMOVED: latency/memory variant, not the audited accuracy implementation. */
#include "wim_zhai.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

/* Latency-only variant: conditioning stages are intentionally excluded. */
static size_t latency_copy_stage(const float *input, float *output, size_t count)
{
    if (input != output) memmove(output, input, count * sizeof(*output));
    return count;
}

#define PI_F 3.14159265358979323846f
#define SECTIONS 2U
#define MIN_DISTANCE 98U
#define TEMPLATE_HALF 79U
#define PEAK_ESTIMATE_INDEX 33U

static size_t distance_peaks(const float *x, size_t n, float height,
                             uint16_t *out, size_t capacity)
{
    size_t selected = 0U;
    while (selected < capacity) {
        float best_height = height;
        size_t best_pos = n;
        size_t i = 1U;
        while (i + 1U < n) {
            if (x[i] <= x[i - 1U]) { ++i; continue; }
            size_t right = i;
            while (right + 1U < n && x[right + 1U] == x[i]) ++right;
            if (right + 1U < n && x[right] > x[right + 1U]) {
                const size_t pos = (i + right) / 2U;
                if (x[pos] >= height && (best_pos == n || x[pos] > best_height)) {
                    bool too_close = false;
                    for (size_t j = 0U; j < selected; ++j) {
                        const size_t prior = out[j];
                        const size_t delta = pos > prior ? pos - prior : prior - pos;
                        if (delta < MIN_DISTANCE) { too_close = true; break; }
                    }
                    if (!too_close) { best_pos = pos; best_height = x[pos]; }
                }
            }
            i = right + 1U;
        }
        if (best_pos == n) break;
        out[selected++] = (uint16_t)best_pos;
    }
    for (size_t a = 1U; a < selected; ++a) {
        const uint16_t value = out[a];
        size_t b = a;
        while (b > 0U && out[b - 1U] > value) { out[b] = out[b - 1U]; --b; }
        out[b] = value;
    }
    return selected;
}

size_t wim_zhai_detect_360(const float *samples, size_t count,
                           uint16_t *peaks, size_t peak_capacity,
                           wim_zhai_workspace_t *ws)
{
    if (samples == NULL || peaks == NULL || ws == NULL || count < 64U ||
        count > WIM_ZHAI_MAX_SAMPLES || peak_capacity == 0U) return 0U;
    if (!latency_copy_stage(samples, ws->filtered, count)) return 0U;

    const size_t width = 13U;
    memset(ws->scratch, 0, count * sizeof(float));
    for (size_t i = width; i + width < count; ++i) {
        const float center = ws->filtered[i];
        const float left = center - ws->filtered[i - width];
        const float right = center - ws->filtered[i + width];
        const float value = left * right;
        ws->scratch[i] = value > 0.0f ? value : 0.0f;
    }
    if (!latency_copy_stage(ws->scratch, ws->filtered, count)) return 0U;

    float min_value = INFINITY, max_value = -INFINITY;
    for (size_t i = 0U; i < count; ++i) {
        const float value = ws->filtered[i] * ws->filtered[i];
        ws->scratch[i] = value;
        if (value < min_value) min_value = value;
        if (value > max_value) max_value = value;
    }
    const float range = max_value - min_value;
    if (!(range > 0.0f)) return 0U;
    double sum = 0.0;
    for (size_t i = 0U; i < count; ++i) {
        ws->scratch[i] = (ws->scratch[i] - min_value) / range;
        sum += ws->scratch[i];
    }
    const float mean = (float)(sum / (double)count);
    const size_t li_count = distance_peaks(ws->scratch, count, mean,
        ws->selected, WIM_ZHAI_MAX_PEAKS);

    memset(ws->template_signal, 0, sizeof(ws->template_signal));
    size_t templates = 0U;
    for (size_t i = 0U; i < li_count; ++i) {
        const int32_t begin = (int32_t)ws->selected[i] - (int32_t)TEMPLATE_HALF;
        if (begin < 0 || (size_t)begin + WIM_ZHAI_TEMPLATE >= count) continue;
        for (size_t j = 0U; j < WIM_ZHAI_TEMPLATE; ++j)
            ws->template_signal[j] += ws->filtered[(size_t)begin + j];
        ++templates;
    }
    if (templates == 0U) return 0U;
    for (size_t j = 0U; j < WIM_ZHAI_TEMPLATE; ++j)
        ws->template_signal[j] /= (float)templates;

    float top[PEAK_ESTIMATE_INDEX + 1U];
    size_t top_count = 0U;
    for (size_t i = 0U; i < count; ++i) {
        float corr = 0.0f;
        for (size_t j = 0U; j < WIM_ZHAI_TEMPLATE; ++j) {
            const int32_t signal_index = (int32_t)i + (int32_t)j - (int32_t)TEMPLATE_HALF;
            if (signal_index >= 0 && (size_t)signal_index < count)
                corr += ws->filtered[(size_t)signal_index] * ws->template_signal[j];
        }
        ws->scratch[i] = corr;
        const bool full = top_count == PEAK_ESTIMATE_INDEX + 1U;
        size_t insert = top_count;
        if (insert > PEAK_ESTIMATE_INDEX) insert = PEAK_ESTIMATE_INDEX;
        if (top_count < PEAK_ESTIMATE_INDEX + 1U) ++top_count;
        if (full && corr <= top[PEAK_ESTIMATE_INDEX]) continue;
        while (insert > 0U && top[insert - 1U] < corr) {
            if (insert <= PEAK_ESTIMATE_INDEX) top[insert] = top[insert - 1U];
            --insert;
        }
        if (insert <= PEAK_ESTIMATE_INDEX) top[insert] = corr;
    }
    const float height = 0.1f * top[PEAK_ESTIMATE_INDEX];
    const size_t found = distance_peaks(ws->scratch, count, height,
        peaks, peak_capacity);
    return found;
}
