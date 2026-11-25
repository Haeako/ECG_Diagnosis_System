/* CONDITIONING REMOVED: latency/memory variant, not the audited accuracy implementation. */
#include "fast_qrs_port.h"

#include <stdbool.h>
#include <string.h>

/* Latency-only variant: conditioning stages are intentionally excluded. */
static size_t latency_copy_stage(const float *input, float *output, size_t count)
{
    if (input != output) memmove(output, input, count * sizeof(*output));
    return count;
}

#define FILTER_PAD 15U
#define INTEGRATION_N 36U
#define INITIAL_SEARCH 300U
#define LONG_DISTANCE 399U
#define NO_PEAK_DISTANCE 234U
#define PEAK_DISTANCE 118U
#define QRS_MIN_DISTANCE 43U

static bool preprocess(const float *signal, size_t n, fast_qrs_workspace_t *w)
{
    if (!latency_copy_stage(signal, w->filtered, n)) return false;
    if (!latency_copy_stage(w->filtered, w->filtered, n)) return false;

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
