#include "wim_hamilton.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define HAMILTON_SECTIONS 2U

/* scipy.signal.butter(2, [10.5, 24], btype="band", fs=360, output="sos"). */
static const float band_sos[HAMILTON_SECTIONS][6] = {
    {0.011857682643f, 0.023715365286f, 0.011857682643f,
     1.0f, -1.678629400327f, 0.806137977136f},
    {1.0f, -2.0f, 1.0f, 1.0f, -1.849991569580f, 0.888971731676f},
};

static void sos_filter(float *data, size_t length)
{
    for (size_t section = 0U; section < HAMILTON_SECTIONS; ++section) {
        const float *c = band_sos[section];
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

static bool sos_filtfilt(const float *input, float *output, size_t count,
                         wim_hamilton_workspace_t *ws)
{
    const size_t pad = 15U;
    if (count <= pad || pad > WIM_HAMILTON_PAD) return false;
    const size_t length = count + 2U * pad;
    float *forward = ws->work_a;
    float *reverse = ws->work_b;
    for (size_t i = 0U; i < pad; ++i) {
        forward[i] = 2.0f * input[0] - input[pad - i];
        forward[pad + count + i] =
            2.0f * input[count - 1U] - input[count - 2U - i];
    }
    memcpy(forward + pad, input, count * sizeof(float));
    sos_filter(forward, length);
    for (size_t i = 0U; i < length; ++i) reverse[i] = forward[length - 1U - i];
    sos_filter(reverse, length);
    for (size_t i = 0U; i < length; ++i) forward[i] = reverse[length - 1U - i];
    memcpy(output, forward + pad, count * sizeof(float));
    return true;
}

static size_t local_maxima(const float *x, size_t n, uint16_t *indices,
                           float *values, size_t capacity)
{
    size_t used = 0U;
    size_t i = 1U;
    while (i + 1U < n) {
        if (x[i] <= x[i - 1U]) {
            ++i;
            continue;
        }
        size_t right = i;
        while (right + 1U < n && x[right + 1U] == x[i]) ++right;
        if (right + 1U < n && x[right] > x[right + 1U] && used < capacity) {
            indices[used] = (uint16_t)((i + right) / 2U);
            values[used] = x[indices[used]];
            ++used;
        }
        i = right + 1U;
    }
    return used;
}

static size_t distance_select(wim_hamilton_workspace_t *ws, size_t n,
                              size_t minimum_distance)
{
    uint16_t chosen[WIM_HAMILTON_QRS_CAPACITY];
    memset(ws->candidate_used, 0, n * sizeof(ws->candidate_used[0]));
    size_t count = 0U;
    for (;;) {
        size_t best = n;
        float best_value = -INFINITY;
        for (size_t i = 0U; i < n; ++i) {
            if (ws->candidate_used[i] == 0U && ws->candidate_value[i] > best_value) {
                best = i;
                best_value = ws->candidate_value[i];
            }
        }
        if (best == n || count >= WIM_HAMILTON_QRS_CAPACITY) break;
        ws->candidate_used[best] = 1U;
        chosen[count] = ws->candidate_index[best];
        ++count;
        const uint16_t pos = ws->candidate_index[best];
        for (size_t i = 0U; i < n; ++i) {
            if (ws->candidate_used[i] == 0U) {
                const uint16_t other = ws->candidate_index[i];
                const size_t delta = pos > other ? pos - other : other - pos;
                if (delta < minimum_distance) ws->candidate_used[i] = 2U;
            }
        }
    }
    for (size_t i = 1U; i < count; ++i) {
        const uint16_t value = chosen[i];
        size_t j = i;
        while (j > 0U && chosen[j - 1U] > value) {
            chosen[j] = chosen[j - 1U];
            --j;
        }
        chosen[j] = value;
    }
    for (size_t i = 0U; i < count; ++i)
        ws->candidate_value[i] = ws->filtered[chosen[i]];
    memmove(ws->candidate_index, chosen, count * sizeof(chosen[0]));
    return count;
}

static float mean_difference(const float *x, size_t start, size_t end)
{
    if (end <= start + 1U) return 0.0f;
    return (x[end - 1U] - x[start]) / (float)(end - start - 1U);
}

static size_t sedghamiz_threshold(const float *x, size_t length,
                                 const uint16_t *peak_locs,
                                 const float *peak_values, size_t peak_count,
                                 uint16_t *out, size_t out_capacity,
                                 wim_hamilton_workspace_t *ws)
{
    float *qrsi = ws->qrs_index;
    memset(qrsi, 0, WIM_HAMILTON_QRS_CAPACITY * sizeof(float));
    if (peak_count == 0U) return 0U;

    const float init_max = x[0];
    float max_first = init_max;
    float sum_first = 0.0f;
    const size_t initial = length < 2U * WIM_HAMILTON_FS ? length : 2U * WIM_HAMILTON_FS;
    for (size_t i = 0U; i < initial; ++i) {
        if (x[i] > max_first) max_first = x[i];
        sum_first += x[i];
    }
    float signal_threshold1 = max_first / 3.0f;
    float noise_threshold1 = (sum_first / (float)initial) / 2.0f;
    float signal_threshold2 = signal_threshold1;
    float noise_threshold2 = noise_threshold1;
    float signal_level1 = signal_threshold1, noise_level1 = noise_threshold1;
    float signal_level2 = signal_threshold2, noise_level2 = noise_threshold2;
    float current_rr = 0.0f, mean_rr = 0.0f, y_i = 0.0f;
    size_t beats = 0U;
    size_t written = 0U;

    for (size_t i = 0U; i < peak_count; ++i) {
        const int32_t loc = peak_locs[i];
        const float value = peak_values[i];
        const int32_t min_dist = (int32_t)lroundf(0.272f * WIM_HAMILTON_FS);
        const int32_t sample_range = (int32_t)lroundf(0.150f * WIM_HAMILTON_FS);
        bool skip = false;
        if (loc >= sample_range + 1 && (size_t)loc <= length) {
            if (loc >= min_dist && (size_t)loc < length) {
                int32_t best = loc - min_dist;
                for (int32_t j = best + 1; j < loc; ++j)
                    if (x[j] > x[best]) best = j;
                y_i = x[best];
            } else {
                y_i = 0.0f;
            }
        } else if (i == 0U && loc > 0) {
            int32_t best = 0;
            for (int32_t j = 1; j < loc; ++j)
                if (x[j] > x[best]) best = j;
            y_i = x[best];
        }

        if (beats >= 9U) {
            float rr_sum = 0.0f;
            for (size_t j = beats - 8U; j + 1U < beats; ++j)
                rr_sum += qrsi[j + 1U] - qrsi[j];
            mean_rr = rr_sum / 7.0f;
            const float comp = qrsi[beats] - qrsi[beats - 1U];
            if (comp <= 0.92f * mean_rr || comp >= 1.16f * mean_rr) {
                signal_threshold1 *= 0.5f;
                signal_threshold2 *= 0.5f;
            } else {
                current_rr = mean_rr;
            }
        }

        const float test_m = current_rr != 0.0f ? current_rr : mean_rr;
        if (test_m > 0.0f && (float)loc - qrsi[beats] >= roundf(1.66f * test_m)) {
            const int32_t start = (int32_t)qrsi[beats] + (int32_t)roundf(0.200f * WIM_HAMILTON_FS);
            const int32_t end = loc - (int32_t)roundf(0.200f * WIM_HAMILTON_FS);
            if (start >= 0 && end > start && (size_t)end <= length) {
                int32_t best = start;
                for (int32_t j = start + 1; j < end; ++j)
                    if (x[j] > x[best]) best = j;
                const float pks_temp = x[best];
                int32_t locs_temp = (int32_t)qrsi[beats] + 72 + best - 1;
                if (pks_temp > noise_threshold1 && beats + 1U < WIM_HAMILTON_QRS_CAPACITY) {
                    ++beats;
                    qrsi[beats] = (float)locs_temp;
                    if (locs_temp > 0 && (size_t)locs_temp < length) {
                        int32_t l = locs_temp - min_dist;
                        if (l < 0) l = 0;
                        int32_t r = locs_temp;
                        int32_t best2 = l;
                        for (int32_t j = l + 1; j < r; ++j)
                            if (x[j] > x[best2]) best2 = j;
                        const float y_i_t = x[best2];
                        if (y_i_t > noise_threshold2)
                            signal_level2 = 0.25f * y_i_t + 0.75f * signal_level2;
                    }
                    signal_level1 = 0.25f * pks_temp + 0.75f * signal_level1;
                }
            }
        }

        if (value >= signal_threshold1) {
            if (beats >= 3U && (float)loc - qrsi[beats] <= roundf(0.3600f * WIM_HAMILTON_FS)) {
                const int32_t sw = (int32_t)roundf(0.075f * WIM_HAMILTON_FS);
                float slope1 = 0.0f, slope2 = 0.0f;
                if (loc - sw >= 0 && (size_t)loc < length)
                    slope1 = mean_difference(x, (size_t)(loc - sw), (size_t)loc);
                const int32_t prior = (int32_t)qrsi[beats];
                if (prior - sw >= 0 && (size_t)prior < length)
                    slope2 = mean_difference(x, (size_t)(prior - sw), (size_t)prior);
                if (fabsf(slope1) <= fabsf(0.5f * slope2)) {
                    skip = true;
                    noise_level2 = 0.125f * y_i + 0.875f * noise_level2;
                    noise_level1 = 0.125f * value + 0.875f * noise_level1;
                }
            }
            if (!skip && beats + 1U < WIM_HAMILTON_QRS_CAPACITY) {
                ++beats;
                qrsi[beats] = (float)loc;
                if (y_i >= signal_threshold2)
                    signal_level2 = 0.125f * y_i + 0.875f * signal_level2;
                signal_level1 = 0.125f * value + 0.875f * signal_level1;
            }
        } else if (value >= noise_threshold1) {
            noise_level2 = 0.125f * y_i + 0.875f * noise_level2;
            noise_level1 = 0.125f * value + 0.875f * noise_level1;
        } else {
            noise_level2 = 0.125f * y_i + 0.875f * noise_level2;
            noise_level1 = 0.125f * value + 0.875f * noise_level1;
        }

        if (noise_level1 != 0.0f || signal_level1 != 0.0f) {
            signal_threshold1 = noise_level1 + 0.25f * fabsf(signal_level1 - noise_level1);
            noise_threshold1 = 0.5f * signal_threshold1;
        }
        if (noise_level2 != 0.0f || signal_level2 != 0.0f) {
            signal_threshold2 = noise_level2 + 0.25f * fabsf(signal_level2 - noise_level2);
            noise_threshold2 = 0.5f * signal_threshold2;
        }
    }

    for (size_t i = 0U; i < peak_count && written < out_capacity; ++i) {
        const float raw = qrsi[i];
        if (raw >= 0.0f && raw < (float)length) out[written++] = (uint16_t)raw;
    }
    for (size_t i = 1U; i < written; ++i) {
        const uint16_t value = out[i];
        size_t j = i;
        while (j > 0U && out[j - 1U] > value) {
            out[j] = out[j - 1U];
            --j;
        }
        out[j] = value;
    }
    size_t unique = 0U;
    for (size_t i = 0U; i < written; ++i)
        if (unique == 0U || out[i] != out[unique - 1U]) out[unique++] = out[i];
    return unique;
}

size_t wim_hamilton_detect_360(const float *samples, size_t count,
                               uint16_t *peaks, size_t peak_capacity,
                               wim_hamilton_workspace_t *ws)
{
    if (samples == NULL || peaks == NULL || ws == NULL || count < 64U ||
        count > WIM_HAMILTON_MAX_SAMPLES || peak_capacity == 0U) return 0U;
    if (!sos_filtfilt(samples, ws->filtered, count, ws)) return 0U;

    for (size_t i = 0U; i + 1U < count; ++i) {
        const float d = ws->filtered[i + 1U] - ws->filtered[i];
        ws->filtered[i] = d * d;
    }
    const size_t energy_count = count - 1U;
    /* The forward SOS scratch is dead after filtfilt; reuse it for prefix sums. */
    float *prefix = ws->work_a;
    prefix[0] = 0.0f;
    for (size_t i = 0U; i < energy_count; ++i)
        prefix[i + 1U] = prefix[i] + ws->filtered[i];

    const size_t width = (size_t)(0.08f * WIM_HAMILTON_FS);
    const size_t left = width - 1U - (width - 1U) / 2U;
    const size_t right = (width - 1U) / 2U;
    const float scale = (float)WIM_HAMILTON_FS / (float)width;
    for (size_t i = 0U; i < energy_count; ++i) {
        const size_t start = i > left ? i - left : 0U;
        const size_t raw_end = i + right + 1U;
        const size_t end = raw_end < energy_count ? raw_end : energy_count;
        ws->filtered[i] = (prefix[end] - prefix[start]) * scale;
    }
    ws->filtered[count - 1U] = ws->filtered[count - 2U];

    const size_t candidates = local_maxima(ws->filtered, count,
        ws->candidate_index, ws->candidate_value, WIM_HAMILTON_MAX_CANDIDATES);
    const size_t spaced = distance_select(ws, candidates,
        (size_t)ceilf(0.272f * WIM_HAMILTON_FS));
    return sedghamiz_threshold(ws->filtered, count, ws->candidate_index, ws->candidate_value,
                               spaced, peaks, peak_capacity, ws);
}
