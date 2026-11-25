#include "pan_tompkins_plus_plus_port.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define PANPP_BAND_ORDER 6U
#define PANPP_DERIV_ORDER 9U
#define PANPP_BAND_PAD 18U
#define PANPP_DERIV_PAD 27U
#define PANPP_MWA 54U
#define PANPP_SMOOTH 21U
#define PANPP_REFRACTORY 83U
#define PANPP_ROUND(x) ((size_t)lroundf((float)(x)))

static const float band_b[7] = {
    0.0011782201214798047f, 0.0f, -0.0035346603644394142f,
    0.0f, 0.0035346603644394142f, 0.0f, -0.0011782201214798047f,
};
static const float band_a[7] = {
    1.0f, -5.4709583877707928f, 12.552011783691182f,
    -15.460374111497291f, 10.783446123029661f,
    -4.0387015876553107f, 0.63459269472991975f,
};
static const float band_zi[6] = {
    -0.0011782201214798047f, -0.0011782201214798047f,
    0.0023564402429596095f, 0.0023564402429596095f,
    -0.0011782201214798047f, -0.0011782201214798047f,
};
static const float derivative_b[10] = {
    45.0f, 65.0f, 85.0f, 60.0f, 20.0f,
    -20.0f, -60.0f, -85.0f, -65.0f, -45.0f,
};
static const float derivative_a[10] = {1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
static const float derivative_zi[9] = {
    -45.0f, -110.0f, -195.0f, -255.0f, -275.0f,
    -255.0f, -195.0f, -110.0f, -45.0f,
};
static const float flattop_21[21] = {
    -9.7665407990367572e-05f, -0.00077246995522616848f,
    -0.0036178852282716446f, -0.0095385539557083093f,
    -0.015706743496122347f, -0.012696551749558737f,
    0.012651971652554486f, 0.067193311659301397f,
    0.14076778437611634f, 0.20583868035318942f,
    0.23195624350343189f, 0.20583868035318942f,
    0.14076778437611634f, 0.067193311659301397f,
    0.012651971652554486f, -0.012696551749558737f,
    -0.015706743496122347f, -0.0095385539557083093f,
    -0.0036178852282716446f, -0.00077246995522616848f,
    -9.7665407990367572e-05f,
};

static void lfilter(float *x, size_t n, const float *b, const float *a,
                    size_t order, const float *zi)
{
    float state[PANPP_DERIV_ORDER] = {0.0f};
    const float first = n > 0U ? x[0] : 0.0f;
    for (size_t k = 0U; k < order; ++k) state[k] = zi[k] * first;
    for (size_t i = 0U; i < n; ++i) {
        const float in = x[i];
        const float out = b[0] * in + state[0];
        for (size_t k = 0U; k + 1U < order; ++k)
            state[k] = b[k + 1U] * in - a[k + 1U] * out + state[k + 1U];
        state[order - 1U] = b[order] * in - a[order] * out;
        x[i] = out;
    }
}

static bool filtfilt(const float *input, float *output, size_t n,
                     const float *b, const float *a, size_t order,
                     size_t pad, const float *zi, panpp_workspace_t *ws)
{
    if (n <= pad || n + 2U * pad > PANPP_MAX_SAMPLES + 2U * PANPP_PAD) return false;
    const size_t length = n + 2U * pad;
    float *forward = ws->band_work_a;
    for (size_t i = 0U; i < pad; ++i) {
        forward[i] = 2.0f * input[0] - input[pad - i];
        forward[pad + n + i] = 2.0f * input[n - 1U] - input[n - 2U - i];
    }
    memcpy(forward + pad, input, n * sizeof(float));
    lfilter(forward, length, b, a, order, zi);
    for (size_t i = 0U; i < length / 2U; ++i) {
        const float temp = forward[i];
        forward[i] = forward[length - 1U - i];
        forward[length - 1U - i] = temp;
    }
    lfilter(forward, length, b, a, order, zi);
    for (size_t i = 0U; i < length / 2U; ++i) {
        const float temp = forward[i];
        forward[i] = forward[length - 1U - i];
        forward[length - 1U - i] = temp;
    }
    memcpy(output, forward + pad, n * sizeof(float));
    return true;
}

static float max_abs(const float *x, size_t n)
{
    float result = 0.0f;
    for (size_t i = 0U; i < n; ++i) {
        const float value = fabsf(x[i]);
        if (value > result) result = value;
    }
    return result;
}

static float max_value(const float *x, size_t start, size_t end)
{
    if (start >= end) return 0.0f;
    float result = x[start];
    for (size_t i = start + 1U; i < end; ++i) if (x[i] > result) result = x[i];
    return result;
}

static size_t argmax_value(const float *x, size_t start, size_t end)
{
    if (start >= end) return start;
    size_t best = start;
    for (size_t i = start + 1U; i < end; ++i) if (x[i] > x[best]) best = i;
    return best;
}

static float mean_value(const float *x, size_t start, size_t end)
{
    if (start >= end) return 0.0f;
    double sum = 0.0;
    for (size_t i = start; i < end; ++i) sum += x[i];
    return (float)(sum / (double)(end - start));
}

static bool higher_peak(uint16_t lhs, uint16_t rhs, const float *y)
{
    if (y[lhs] != y[rhs]) return y[lhs] > y[rhs];
    return lhs < rhs;
}

static bool is_blocked(const uint8_t *bits, size_t index)
{
    return (bits[index >> 3U] & (uint8_t)(1U << (index & 7U))) != 0U;
}

static void block_range(uint8_t *bits, size_t left, size_t right)
{
    for (size_t i = left; i < right; ++i)
        bits[i >> 3U] |= (uint8_t)(1U << (i & 7U));
}

static void sift_down(uint16_t *heap, size_t root, size_t end, const float *y)
{
    while (root * 2U + 1U < end) {
        size_t child = root * 2U + 1U;
        if (child + 1U < end && higher_peak(heap[child + 1U], heap[child], y)) ++child;
        if (!higher_peak(heap[child], heap[root], y)) return;
        const uint16_t temp = heap[root]; heap[root] = heap[child]; heap[child] = temp;
        root = child;
    }
}

static void sort_peak_indices(uint16_t *values, size_t n, const float *y)
{
    for (size_t i = n / 2U; i > 0U; --i) sift_down(values, i - 1U, n, y);
    for (size_t end = n; end > 1U; --end) {
        const uint16_t temp = values[0]; values[0] = values[end - 1U]; values[end - 1U] = temp;
        sift_down(values, 0U, end - 1U, y);
    }
}

static size_t find_local_peaks(const float *y, size_t n, uint16_t *out, size_t cap)
{
    size_t count = 0U;
    size_t i = 1U;
    while (i + 1U < n && count < cap) {
        if (y[i] <= y[i - 1U]) { ++i; continue; }
        size_t last = i;
        while (last + 1U < n && y[last + 1U] == y[i]) ++last;
        if (last + 1U < n && y[last] > y[last + 1U]) {
            const size_t peak = i + (last - i) / 2U;
            out[count++] = (uint16_t)peak;
        }
        i = last + 1U;
    }
    return count;
}

static size_t peakutils_select(const float *y, size_t n, uint16_t *indices,
                               uint16_t *selected_buf, uint8_t *blocked,
                               size_t cap)
{
    size_t candidates = find_local_peaks(y, n, indices, cap);
    if (candidates < 2U) return candidates;
    sort_peak_indices(indices, candidates, y); /* ascending amplitude */
    memset(blocked, 0, PANPP_BLOCKED_BYTES);
    size_t selected = 0U;
    for (size_t k = candidates; k > 0U; --k) {
        const uint16_t peak = indices[k - 1U];
        if (is_blocked(blocked, peak)) continue;
        if (selected < PANPP_MAX_BEATS) selected_buf[selected++] = peak;
        const size_t left = peak > PANPP_REFRACTORY ? peak - PANPP_REFRACTORY : 0U;
        size_t right = (size_t)peak + PANPP_REFRACTORY + 1U;
        if (right > n) right = n;
        block_range(blocked, left, right);
        blocked[peak >> 3U] &= (uint8_t)~(1U << (peak & 7U));
    }
    for (size_t i = 1U; i < selected; ++i) {
        const uint16_t v = selected_buf[i]; size_t j = i;
        while (j > 0U && selected_buf[j - 1U] > v) { selected_buf[j] = selected_buf[j - 1U]; --j; }
        selected_buf[j] = v;
    }
    memcpy(indices, selected_buf, selected * sizeof(*indices));
    return selected;
}

static bool locate_band_peak(const float *filtered, size_t n, size_t integrated_at,
                             size_t span, uint16_t *raw_at, float *amplitude)
{
    size_t start = integrated_at + 1U >= span ? integrated_at - span + 1U : 0U;
    size_t end = integrated_at + 2U;
    if (end > n) end = n;
    if (start >= end) return false;
    const size_t best = argmax_value(filtered, start, end);
    *raw_at = (uint16_t)(integrated_at >= span ? integrated_at - span + best - start
                                             : best + 1U);
    *amplitude = filtered[best];
    return true;
}

size_t panpp_detect_360(const float *samples, size_t count,
                        uint16_t *peaks, size_t peak_capacity,
                        panpp_workspace_t *ws)
{
    if (samples == NULL || peaks == NULL || ws == NULL || count < 721U ||
        count > PANPP_MAX_SAMPLES || peak_capacity == 0U) return 0U;
    memset(ws->qrs_integrated, 0, sizeof(ws->qrs_integrated));
    memset(ws->qrs_raw, 0, sizeof(ws->qrs_raw));
    ws->beat_count = ws->raw_count = 0U;

    if (!filtfilt(samples, ws->filtered, count, band_b, band_a,
                  PANPP_BAND_ORDER, PANPP_BAND_PAD, band_zi, ws)) return 0U;
    const float band_scale = max_abs(ws->filtered, count);
    if (!(band_scale > 0.0f)) return 0U;
    for (size_t i = 0U; i < count; ++i) ws->filtered[i] /= band_scale;

    if (!filtfilt(ws->filtered, ws->derivative, count, derivative_b,
                  derivative_a, PANPP_DERIV_ORDER, PANPP_DERIV_PAD,
                  derivative_zi, ws)) return 0U;
    float derivative_scale = -INFINITY;
    for (size_t i = 0U; i < count; ++i)
        if (ws->derivative[i] > derivative_scale) derivative_scale = ws->derivative[i];
    if (!(derivative_scale > 0.0f)) return 0U;
    for (size_t i = 0U; i < count; ++i) {
        const float d = ws->derivative[i] / derivative_scale;
        ws->derivative[i] = d * d;
    }
    for (size_t i = 0U; i < count; ++i) {
        double sum = 0.0;
        for (size_t k = 0U; k < PANPP_SMOOTH; ++k) {
            int32_t index = (int32_t)i + 10 - (int32_t)k;
            if (index < 0) index = 0;
            if ((size_t)index >= count) index = (int32_t)count - 1;
            sum += (double)flattop_21[k] * ws->derivative[(size_t)index];
        }
        ws->smooth[i] = (float)sum;
    }

    const size_t integrated_n = count + PANPP_MWA - 1U;
    for (size_t j = 0U; j < integrated_n; ++j) {
        const size_t start = j >= PANPP_MWA - 1U ? j - (PANPP_MWA - 1U) : 0U;
        const size_t end = j < count ? j + 1U : count;
        ws->integrated[j] = mean_value(ws->smooth, start, end) *
                            ((float)(end - start) / (float)PANPP_MWA);
    }

    ws->candidate_count = peakutils_select(ws->integrated, integrated_n,
                                            ws->candidates, ws->qrs_integrated,
                                            ws->blocked,
                                            PANPP_MAX_CANDIDATES);
    if (ws->candidate_count == 0U) return 0U;
    memset(ws->qrs_integrated, 0, sizeof(ws->qrs_integrated));

    const size_t train_n = 721U;
    float max_int = max_value(ws->integrated, 0U, train_n);
    float mean_int = mean_value(ws->integrated, 0U, train_n);
    float max_band = max_value(ws->filtered, 0U, train_n);
    float mean_band = mean_value(ws->filtered, 0U, train_n);
    float signal_level = max_int / 3.0f, noise_level = mean_int / 2.0f;
    float signal_level1 = max_band / 3.0f, noise_level1 = mean_band / 2.0f;
    float threshold_signal = noise_level + 0.25f * fabsf(signal_level - noise_level);
    float threshold_noise = 0.4f * threshold_signal;
    float threshold_signal1 = noise_level1 + 0.25f * fabsf(signal_level1 - noise_level1);
    float threshold_noise1 = 0.4f * threshold_signal1;
    float mean_rr = 0.0f, selected_rr = 0.0f;
    float y_i = 0.0f;

    for (size_t i = 0U; i < ws->candidate_count; ++i) {
        const size_t loc = ws->candidates[i];
        size_t x_i = 0U;
        if (loc >= 54U && loc <= count) {
            const size_t start = loc - 54U;
            const size_t best = argmax_value(ws->filtered, start, loc + 1U);
            y_i = ws->filtered[best]; x_i = best - start;
        } else if (i == 0U) {
            const size_t end = loc + 1U < count ? loc + 1U : count;
            const size_t best = argmax_value(ws->filtered, 0U, end);
            y_i = end > 0U ? ws->filtered[best] : 0.0f; x_i = best;
        } else if (loc >= count) {
            const size_t start = loc >= 54U ? loc - 54U : 0U;
            const size_t best = argmax_value(ws->filtered, start, count);
            y_i = count > start ? ws->filtered[best] : 0.0f; x_i = best - start;
        }

        const size_t previous = ws->beat_count > 0U ? ws->qrs_integrated[ws->beat_count - 1U] : 0U;
        const int32_t gap = (int32_t)loc - (int32_t)previous;
        if (ws->beat_count >= 9U) {
            double rr_sum = 0.0;
            for (size_t k = ws->beat_count - 8U; k < ws->beat_count; ++k)
                rr_sum += (double)(ws->qrs_integrated[k] - ws->qrs_integrated[k - 1U]);
            mean_rr = (float)(rr_sum / 8.0);
            selected_rr = mean_rr;
        }
        const float test_m = selected_rr != 0.0f ? selected_rr : mean_rr;
        bool skip = false;

        if (gap >= 504) {
            const size_t start = previous + 130U;
            const size_t end = loc + 1U < integrated_n ? loc + 1U : integrated_n;
            if (start < end) {
                const size_t best = argmax_value(ws->integrated, start, end);
                const float peak = ws->integrated[best];
                if (peak > threshold_noise * 0.2f && ws->beat_count < PANPP_MAX_BEATS) {
                    ws->qrs_integrated[ws->beat_count++] = (uint16_t)best;
                    uint16_t band_peak = 0U; float band_amp = 0.0f;
                    if (locate_band_peak(ws->filtered, count, best, 54U, &band_peak, &band_amp) &&
                        band_amp > threshold_noise1 * 0.2f && ws->raw_count < peak_capacity &&
                        ws->raw_count < PANPP_MAX_BEATS) {
                        ws->qrs_raw[ws->raw_count++] = band_peak;
                        signal_level1 = 0.75f * band_amp + 0.25f * signal_level1;
                    }
                    signal_level = 0.75f * peak + 0.25f * signal_level;
                }
            }
        } else if (test_m > 0.0f &&
                   ((float)gap >= 1.66f * test_m || gap > 360)) {
            const size_t start = previous + 130U;
            const size_t end = loc + 1U < integrated_n ? loc + 1U : integrated_n;
            if (start < end) {
                const size_t best = argmax_value(ws->integrated, start, end);
                const float peak = ws->integrated[best];
                float noise_temp = threshold_noise;
                if (i + 3U < ws->candidate_count) {
                    const size_t qrs3 = ws->beat_count >= 3U ? ws->qrs_integrated[ws->beat_count - 3U] : 0U;
                    const size_t lo = qrs3 + 130U < integrated_n ? qrs3 + 130U : integrated_n;
                    const size_t hi0 = ws->candidates[i + 3U] + 1U;
                    const size_t hi = hi0 < integrated_n ? hi0 : integrated_n;
                    if (lo < hi) noise_temp = 0.5f * threshold_noise + 0.25f * mean_value(ws->integrated, lo, hi);
                }
                if (peak > noise_temp && ws->beat_count < PANPP_MAX_BEATS) {
                    ws->qrs_integrated[ws->beat_count++] = (uint16_t)best;
                    uint16_t band_peak = 0U; float band_amp = 0.0f;
                    if (locate_band_peak(ws->filtered, count, best, 54U, &band_peak, &band_amp)) {
                        float noise_temp1 = threshold_noise1;
                        if (i + 3U < ws->candidate_count) {
                            const size_t qrs3 = ws->beat_count >= 4U ? ws->qrs_integrated[ws->beat_count - 4U] : 0U;
                            const size_t lo0 = qrs3 + 130U - 54U + 1U;
                            const size_t lo = lo0 < count ? lo0 : count;
                            const size_t hi0 = ws->candidates[i + 3U] + 1U;
                            const size_t hi = hi0 < count ? hi0 : count;
                            if (lo < hi) noise_temp1 = 0.5f * threshold_noise1 + 0.25f * mean_value(ws->filtered, lo, hi);
                        }
                        if (band_amp > noise_temp1 && ws->raw_count < peak_capacity &&
                            ws->raw_count < PANPP_MAX_BEATS) {
                            ws->qrs_raw[ws->raw_count++] = band_peak;
                            signal_level1 = 0.75f * band_amp + 0.25f * signal_level1;
                        }
                    }
                    signal_level = 0.75f * peak + 0.25f * signal_level;
                }
            }
        }

        if (ws->integrated[loc] >= threshold_signal) {
            if (ws->beat_count >= 3U) {
                const size_t last = ws->qrs_integrated[ws->beat_count - 1U];
                const int32_t current_gap = (int32_t)loc - (int32_t)last;
                bool check_flag = test_m > 0.0f && (float)current_gap <= 0.5f * test_m;
                if (current_gap <= 130 || check_flag) {
                    const size_t start1 = loc > 25U ? loc - 25U : 0U;
                    const size_t end1 = loc + 1U < integrated_n ? loc + 1U : integrated_n;
                    const size_t start2 = last > 26U ? last - 26U : 0U;
                    const size_t end2 = last + 1U < integrated_n ? last + 1U : integrated_n;
                    const float slope1 = end1 > start1 + 1U ?
                        (ws->integrated[end1 - 1U] - ws->integrated[start1]) / (float)(end1 - start1 - 1U) : 0.0f;
                    const float slope2 = end2 > start2 + 1U ?
                        (ws->integrated[end2 - 1U] - ws->integrated[start2]) / (float)(end2 - start2 - 1U) : 0.0f;
                    if (fabsf(slope1) <= fabsf(0.6f * slope2)) skip = true;
                }
            }
            if (!skip && ws->beat_count < PANPP_MAX_BEATS) {
                ws->qrs_integrated[ws->beat_count++] = (uint16_t)loc;
                if (y_i >= threshold_signal1 && ws->raw_count < peak_capacity &&
                    ws->raw_count < PANPP_MAX_BEATS) {
                    const size_t raw = loc >= 54U ? loc - 54U + x_i : x_i + 1U;
                    ws->qrs_raw[ws->raw_count++] = (uint16_t)(raw < count ? raw : count - 1U);
                    signal_level1 = 0.125f * y_i + 0.875f * signal_level1;
                }
                signal_level = 0.125f * ws->integrated[loc] + 0.875f * signal_level;
            }
        } else if (ws->integrated[loc] >= threshold_noise) {
            noise_level1 = 0.125f * y_i + 0.875f * noise_level1;
            noise_level = 0.125f * ws->integrated[loc] + 0.875f * noise_level;
        } else {
            noise_level1 = 0.125f * y_i + 0.875f * noise_level1;
            noise_level = 0.125f * ws->integrated[loc] + 0.875f * noise_level;
        }
        if (noise_level != 0.0f || signal_level != 0.0f) {
            threshold_signal = noise_level + 0.25f * fabsf(signal_level - noise_level);
            threshold_noise = 0.4f * threshold_signal;
        }
        if (noise_level1 != 0.0f || signal_level1 != 0.0f) {
            threshold_signal1 = noise_level1 + 0.25f * fabsf(signal_level1 - noise_level1);
            threshold_noise1 = 0.4f * threshold_signal1;
        }
    }

    size_t out = ws->raw_count < peak_capacity ? ws->raw_count : peak_capacity;
    for (size_t i = 0U; i < out; ++i) peaks[i] = ws->qrs_raw[i];
    return out;
}
