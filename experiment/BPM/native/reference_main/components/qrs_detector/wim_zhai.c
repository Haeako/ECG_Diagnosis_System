#include "wim_zhai.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define PI_F 3.14159265358979323846f
#define SECTIONS 2U
#define MIN_DISTANCE 98U
#define TEMPLATE_HALF 79U
#define PEAK_ESTIMATE_INDEX 33U

static const float high_sos[SECTIONS][6] = {
    {0.83309753162164f, -1.6661950632433f, 0.83309753162164f, 1.0f, -1.7548933858968f, 0.77213972790915f},
    {1.0f, -2.0f, 1.0f, 1.0f, -1.8803880966122f, 0.89886774699476f},
};
static const float low_sos[SECTIONS][6] = {
    {9.2764620292312e-05f, 0.00018552924058462f, 9.2764620292312e-05f, 1.0f, -1.6410697372354f, 0.67773221138026f},
    {1.0f, 2.0f, 1.0f, 1.0f, -1.8121154003512f, 0.85259913635912f},
};

static void sos_filter(float *x, size_t n, const float coefficients[SECTIONS][6], float scale)
{
    float section_scale = scale;
    for (size_t s = 0U; s < SECTIONS; ++s) {
        const float *c = coefficients[s];
        const float dc = (c[0] + c[1] + c[2]) / (c[3] + c[4] + c[5]);
        const float zi0 = (c[1] - c[4] * c[0] + c[2] - c[5] * c[0]) /
                          (c[3] + c[4] + c[5]);
        const float zi1 = c[2] - c[5] * c[0] - c[5] * zi0;
        float z1 = zi0 * section_scale, z2 = zi1 * section_scale;
        for (size_t i = 0U; i < n; ++i) {
            const float in = x[i];
            const float out = c[0] * in + z1;
            z1 = c[1] * in - c[4] * out + z2;
            z2 = c[2] * in - c[5] * out;
            x[i] = out;
        }
        section_scale *= dc;
    }
}

static bool sos_filtfilt(const float *input, float *output, size_t n,
                         const float coefficients[SECTIONS][6],
                         wim_zhai_workspace_t *ws)
{
    const size_t pad = 15U;
    if (n <= pad || pad > WIM_ZHAI_PAD) return false;
    const size_t length = n + 2U * pad;
    float *forward = ws->work_a, *reverse = ws->work_b;
    for (size_t i = 0U; i < pad; ++i) {
        forward[i] = 2.0f * input[0] - input[pad - i];
        forward[pad + n + i] = 2.0f * input[n - 1U] - input[n - 2U - i];
    }
    memcpy(forward + pad, input, n * sizeof(float));
    sos_filter(forward, length, coefficients, forward[0]);
    for (size_t i = 0U; i < length; ++i) reverse[i] = forward[length - 1U - i];
    sos_filter(reverse, length, coefficients, reverse[0]);
    for (size_t i = 0U; i < n; ++i) output[i] = reverse[length - 1U - (pad + i)];
    return true;
}

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
    if (!sos_filtfilt(samples, ws->filtered, count, high_sos, ws)) return 0U;

    const size_t width = 13U;
    memset(ws->scratch, 0, count * sizeof(float));
    for (size_t i = width; i + width < count; ++i) {
        const float center = ws->filtered[i];
        const float left = center - ws->filtered[i - width];
        const float right = center - ws->filtered[i + width];
        const float value = left * right;
        ws->scratch[i] = value > 0.0f ? value : 0.0f;
    }
    if (!sos_filtfilt(ws->scratch, ws->filtered, count, low_sos, ws)) return 0U;

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
