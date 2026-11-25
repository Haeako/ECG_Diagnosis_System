#include "wim_nguyen.h"

#include <math.h>
#include <string.h>

#define SOS_SECTIONS 2U

/* scipy.signal.butter(order=3, fs=360, output="sos") coefficients used by
 * the upstream Nguyen implementation (9-Hz HP and 11.5-Hz LP). */
static const float hp_sos[SOS_SECTIONS][6] = {
    {0.85449723f, -0.85449723f, 0.0f, 1.0f, -0.85408069f, 0.0f},
    {1.0f, -2.0f, 1.0f, 1.0f, -1.83207671f, 0.85491378f},
};
static const float lp_sos[SOS_SECTIONS][6] = {
    {0.000835033334f, 0.00167006667f, 0.000835033334f, 1.0f, -0.817034270f, 0.0f},
    {1.0f, 1.0f, 0.0f, 1.0f, -1.78219331f, 0.818704337f},
};

static void sos_filter(float *data, size_t length,
                       const float sos[SOS_SECTIONS][6])
{
    for (size_t section = 0U; section < SOS_SECTIONS; ++section) {
        const float *c = sos[section];
        const float first = data[0];
        const float dc_gain = (c[0] + c[1] + c[2]) /
                              (c[3] + c[4] + c[5]);
        float z1 = (dc_gain - c[0]) * first;
        float z2 = (c[2] - c[5] * dc_gain) * first;
        for (size_t i = 0U; i < length; ++i) {
            const float x = data[i];
            const float y = c[0] * x + z1;
            z1 = c[1] * x - c[4] * y + z2;
            z2 = c[2] * x - c[5] * y;
            data[i] = y;
        }
    }
}

static size_t sos_filtfilt(const float *input, float *output, size_t count,
                           const float sos[SOS_SECTIONS][6], size_t pad,
                           wim_nguyen_workspace_t *ws)
{
    if (count <= pad || pad > WIM_NGUYEN_PAD) return 0U;
    const size_t length = count + 2U * pad;
    float *forward = ws->work_a;
    float *reverse = ws->work_b;

    for (size_t i = 0U; i < pad; ++i) {
        forward[i] = 2.0f * input[0] - input[pad - i];
        forward[pad + count + i] =
            2.0f * input[count - 1U] - input[count - 2U - i];
    }
    memcpy(forward + pad, input, count * sizeof(float));
    sos_filter(forward, length, sos);
    for (size_t i = 0U; i < length; ++i) reverse[i] = forward[length - 1U - i];
    sos_filter(reverse, length, sos);
    for (size_t i = 0U; i < length; ++i) forward[i] = reverse[length - 1U - i];
    memcpy(output, forward + pad, count * sizeof(float));
    return count;
}

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

    /* scipy sosfiltfilt default odd padding lengths for these SOS filters. */
    if (sos_filtfilt(samples, ws->filtered, count, hp_sos, 12U, ws) != count)
        return 0U;

    const size_t wl = (size_t)(WIM_NGUYEN_FS * 0.025f);
    memset(ws->template_signal, 0, count * sizeof(float));
    for (size_t i = wl; i + wl < count; ++i) {
        const float left = ws->filtered[i] - ws->filtered[i - wl];
        const float right = ws->filtered[i] - ws->filtered[i + wl];
        const float value = left * right;
        ws->template_signal[i] = value > 0.0f ? value : 0.0f;
    }
    if (sos_filtfilt(ws->template_signal, ws->filtered, count,
                     lp_sos, 15U, ws) != count) return 0U;

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
