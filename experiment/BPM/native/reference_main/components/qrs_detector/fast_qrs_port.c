#include "fast_qrs_port.h"

#include <stdbool.h>
#include <string.h>

#define FILTER_PAD 15U
#define INTEGRATION_N 36U
#define INITIAL_SEARCH 300U
#define LONG_DISTANCE 399U
#define NO_PEAK_DISTANCE 234U
#define PEAK_DISTANCE 118U
#define QRS_MIN_DISTANCE 43U

static const float hp_sos[3][6] = {
    {0.868267425142f, -0.868267425142f, 0.0f, 1.0f, -0.916331174017f, 0.0f},
    {1.0f, -2.0f, 1.0f, 1.0f, -1.861158240779f, 0.868267562901f},
    {1.0f, -2.0f, 1.0f, 1.0f, -1.940136465811f, 0.947547471922f},
};
static const float hp_zi[3][2] = {{-0.868267425142f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}};
static const float lp_sos[3][6] = {
    {0.000148377775f, 0.000296755551f, 0.000148377775f, 1.0f, -0.674508516842f, 0.0f},
    {1.0f, 2.0f, 1.0f, 1.0f, -1.423083585697f, 0.534845088908f},
    {1.0f, 1.0f, 0.0f, 1.0f, -1.661977518100f, 0.792500494820f},
};
static const float lp_zi[3][2] = {
    {0.001675052735f, 0.000148377775f},
    {0.063438057850f, -0.033081356034f},
    {0.934738511640f, -0.792500494820f},
};

static void filter_sos(float *data, size_t n, const float sos[3][6],
                       const float zi[3][2], float initial)
{
    for (size_t section = 0U; section < 3U; ++section) {
        const float *s = sos[section];
        float z1 = zi[section][0] * initial;
        float z2 = zi[section][1] * initial;
        for (size_t i = 0U; i < n; ++i) {
            const float x = data[i];
            const float y = s[0] * x + z1;
            z1 = s[1] * x - s[4] * y + z2;
            z2 = s[2] * x - s[5] * y;
            data[i] = y;
        }
    }
}

static bool filtfilt_5(const float *input, size_t n, float *output,
                       float *extended, const float sos[3][6], const float zi[3][2])
{
    if (n <= FILTER_PAD || n > FAST_QRS_MAX_SAMPLES) return false;
    const size_t length = n + 2U * FILTER_PAD;
    for (size_t i = 0; i < FILTER_PAD; ++i) {
        extended[i] = 2.0f * input[0] - input[FILTER_PAD - i];
        extended[FILTER_PAD + n + i] = 2.0f * input[n - 1U] - input[n - 2U - i];
    }
    memcpy(extended + FILTER_PAD, input, n * sizeof(float));
    filter_sos(extended, length, sos, zi, extended[0]);
    for (size_t i = 0; i < length / 2U; ++i) {
        const float v = extended[i];
        extended[i] = extended[length - 1U - i];
        extended[length - 1U - i] = v;
    }
    filter_sos(extended, length, sos, zi, extended[0]);
    for (size_t i = 0; i < length / 2U; ++i) {
        const float v = extended[i];
        extended[i] = extended[length - 1U - i];
        extended[length - 1U - i] = v;
    }
    memcpy(output, extended + FILTER_PAD, n * sizeof(float));
    return true;
}

static bool preprocess(const float *signal, size_t n, fast_qrs_workspace_t *w)
{
    if (!filtfilt_5(signal, n, w->filtered, w->scratch, hp_sos, hp_zi)) return false;
    if (!filtfilt_5(w->filtered, n, w->filtered, w->scratch, lp_sos, lp_zi)) return false;

    for (size_t i = 0; i < n; ++i) {
        const float d = i == 0U ? 0.0f : w->filtered[i] - w->filtered[i - 1U];
        w->integrated[i] = d * d;
    }
    const size_t left = INTEGRATION_N / 2U;
    for (size_t i = 0; i < n; ++i) {
        const size_t first = i > left ? i - left : 0U;
        const size_t last = i + (INTEGRATION_N - left) < n ? i + (INTEGRATION_N - left) : n;
        float sum = 0.0f;
        for (size_t j = first; j < last; ++j) sum += w->integrated[j];
        w->filtered[i] = sum / (float)INTEGRATION_N;
    }
    memcpy(w->integrated, w->filtered, n * sizeof(float));
    return true;
}

static size_t find_candidate_peaks(const float *x, size_t n, uint16_t *indices,
                                   float *values, size_t capacity)
{
    const size_t seed_n = n < NO_PEAK_DISTANCE ? n : NO_PEAK_DISTANCE;
    size_t best = 0U;
    for (size_t i = 1U; i < seed_n; ++i) if (x[i] > x[best]) best = i;
    size_t count = 1U;
    indices[0] = (uint16_t)best;
    values[0] = x[best];
    float last_max = -1.0e30f;
    int32_t last_pos = -1;
    for (size_t i = 0U; i < n && count < capacity; ++i) {
        const float current = x[i];
        if (current > last_max) { last_max = current; last_pos = (int32_t)i; }
        if (current <= last_max / 2.0f || (int32_t)i - last_pos >= (int32_t)NO_PEAK_DISTANCE) {
            if (last_pos < 0) continue;
            if (last_pos - (int32_t)indices[count - 1U] < (int32_t)PEAK_DISTANCE) {
                if (last_max > values[count - 1U]) {
                    indices[count - 1U] = (uint16_t)last_pos;
                    values[count - 1U] = last_max;
                }
            } else {
                indices[count] = (uint16_t)last_pos;
                values[count++] = last_max;
            }
            last_max = current;
            last_pos = (int32_t)i;
        }
    }
    return count;
}

static float max_prefix(const float *x, size_t n)
{
    float m = x[0];
    for (size_t i = 1U; i < n; ++i) if (x[i] > m) m = x[i];
    return m;
}

static size_t threshold_candidates(const float *x, const uint16_t *cand,
                                   size_t nc, size_t n, uint16_t *out,
                                   size_t capacity, fast_qrs_workspace_t *w)
{
    const size_t init_n = n < INITIAL_SEARCH + 1U ? n : INITIAL_SEARCH + 1U;
    float spk = 0.13f * max_prefix(x, init_n);
    float npk = 0.1f * spk;
    float threshold = 0.25f * spk + 0.75f * npk;
    size_t found = 0U, qbuf_n = 0U;
    uint16_t last_qrs = 0U;
    for (size_t i = 0U; i < nc && found < capacity; ++i) {
        const uint16_t peak = cand[i];
        const float value = x[peak];
        if (value > threshold) {
            if (found > 0U && (uint16_t)(peak - out[found - 1U]) < QRS_MIN_DISTANCE) {
                if (value > x[out[found - 1U]]) out[found - 1U] = peak;
            } else {
                out[found++] = peak;
                last_qrs = peak;
            }
            spk = 0.25f * value + 0.75f * spk;
            if (qbuf_n == 10U) {
                memmove(w->rr_buffer, w->rr_buffer + 1, 9U * sizeof(uint16_t));
                qbuf_n = 9U;
            }
            w->rr_buffer[qbuf_n++] = peak;
        } else {
            npk = 0.25f * value + 0.75f * npk;
        }
        threshold = 0.25f * spk + 0.75f * npk;
        if ((uint16_t)(peak - last_qrs) > LONG_DISTANCE) {
            spk *= 0.5f;
            threshold = 0.25f * spk + 0.75f * npk;
            const size_t begin = i > 5U ? i - 5U : 0U;
            for (size_t j = begin; j <= i; ++j) {
                const uint16_t candidate = cand[j];
                if (candidate != last_qrs && candidate > last_qrs && candidate < peak && x[candidate] > threshold) {
                    if (found < capacity) out[found++] = candidate;
                    spk = 0.875f * spk + 0.125f * x[candidate];
                    threshold = 0.25f * spk + 0.75f * npk;
                    last_qrs = candidate;
                    break;
                }
            }
        }
        if (qbuf_n > 1U) {
            uint32_t rr_sum = 0U;
            for (size_t j = 1U; j < qbuf_n; ++j) rr_sum += (uint16_t)(w->rr_buffer[j] - w->rr_buffer[j - 1U]);
            const float mean_rr = (float)rr_sum / (float)(qbuf_n - 1U);
            if ((float)(uint16_t)(peak - last_qrs) > 1.5f * mean_rr) {
                spk *= 0.5f;
                threshold = 0.25f * spk + 0.75f * npk;
            }
        }
    }
    return found;
}

size_t fast_qrs_detect_360(const float *signal, size_t count,
                           uint16_t *peaks, size_t peak_capacity,
                           fast_qrs_workspace_t *w)
{
    if (signal == NULL || peaks == NULL || w == NULL || count < 32U ||
        count > FAST_QRS_MAX_SAMPLES || peak_capacity == 0U) return 0U;
    if (!preprocess(signal, count, w)) return 0U;
    const size_t nc = find_candidate_peaks(w->integrated, count, w->candidates,
                                            w->candidate_values, FAST_QRS_MAX_SAMPLES);
    return threshold_candidates(w->integrated, w->candidates, nc, count,
                                peaks, peak_capacity, w);
}
