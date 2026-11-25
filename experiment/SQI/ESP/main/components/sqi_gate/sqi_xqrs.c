/* CONDITIONING REMOVED: latency/memory variant; no accuracy equivalence claimed. */
#include "sqi_xqrs.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

static size_t latency_copy_stage(const float *input, float *output, size_t count)
{
    if (input != output) memmove(output, input, count * sizeof(*output));
    return count;
}

static const float learn_wavelet[24] = {
   -0.050532085219542215, -0.0814766329831621, -0.11991740553900929,
   -0.15944060684987493, -0.18810032136979016, -0.19000139799153162,
   -0.15007307582913015, -0.06117780276951621, 0.06931222384691339,
    0.21737690275284152, 0.3473747835840055, 0.42356450062648654,
    0.42356450062648654, 0.3473747835840055, 0.21737690275284152,
    0.06931222384691339, -0.06117780276951621, -0.15007307582913015,
   -0.19000139799153162, -0.18810032136979016, -0.15944060684987493,
   -0.11991740553900929, -0.0814766329831621, -0.050532085219542215
};








static size_t local_peaks(const float *x, size_t n, size_t radius,
                          uint16_t *out)
{
    float minimum = x[0], maximum = x[0];
    for (size_t k = 1; k < n; ++k) {
        if (x[k] < minimum) minimum = x[k];
        if (x[k] > maximum) maximum = x[k];
    }
    if (minimum == maximum) return 0;
    size_t found = 0, i = 0;
    while (i < n) {
        size_t lo = i > radius ? i - radius : 0U;
        size_t hi = i + radius < n ? i + radius : n;
        float m = x[lo];
        for (size_t k = lo + 1U; k < hi; ++k) if (x[k] > m) m = x[k];
        if (x[i] == m) {
            if (found < SQI_XQRS_MAX_CANDIDATES) out[found++] = (uint16_t)i;
            i += radius;
        }
        ++i;
    }
    return found;
}

size_t sqi_xqrs_detect_250(const float *samples, size_t count,
                           uint16_t *peaks, size_t peak_capacity,
                           sqi_xqrs_workspace_t *w)
{
    if (!samples || !peaks || !w || count < 76U || count > SQI_XQRS_MAX_SAMPLES)
        return 0;
    float minv = samples[0], maxv = samples[0];
    for (size_t i = 0; i < count; ++i) {
        w->filtered[i] = samples[i];
        if (samples[i] < minv) minv = samples[i];
        if (samples[i] > maxv) maxv = samples[i];
    }
    if (minv == maxv) return 0;
    if (!latency_copy_stage(w->filtered, w->integrated, count)) return 0;
    memcpy(w->filtered, w->integrated, count * sizeof(float));
    if (!latency_copy_stage(w->filtered, w->integrated, count)) return 0;
    for (size_t i = 0; i < count; ++i) w->integrated[i] *= w->integrated[i];
    size_t n_candidates = local_peaks(w->integrated, count, 12U, w->candidates);

    /* WFDB defaults after transform-gain scaling. Learning is deliberately
     * performed below; these values are only the fallback. */
    const double transform_gain = (0.9952869939443847 * 2.0)
                                * (3.113494851360924 * 2.0);
    double qrs_amp = (27.0 / 40.0) * 0.13 * transform_gain;
    double noise_amp = qrs_amp / 10.0;
    double rr_recent = 200.0;
    int last_qrs = 0;

    /* Reproduce _learn_init_params(n_calib_beats=8). */
    size_t n_filtered = local_peaks(w->filtered, count, 12U, w->filtered_candidates);
    uint16_t *fp = w->filtered_candidates;
    double qa[8], na_sum = 0.0; size_t nq = 0, nn = 0;
    int learned_last = -600;
    uint16_t learned_inds[8];
    for (size_t p = 0; p < n_filtered && nq < 8U; ++p) {
        size_t i = fp[p];
        if (i <= 25U || i > count - 25U) continue;
        double norm = 0.0, dot = 0.0;
        for (size_t k = 0; k < 24U; ++k) {
            double v = w->filtered[i - 12U + k];
            norm += v * v;
        }
        norm = sqrt(norm);
        if (norm > 0.0)
            for (size_t k = 0; k < 24U; ++k)
                dot += (w->filtered[i - 12U + k] / norm) * learn_wavelet[k];
        if (dot > 0.6 && (int)i - learned_last > 75) {
            learned_last = (int)i; learned_inds[nq] = (uint16_t)i;
            qa[nq++] = w->integrated[i];
        } else { na_sum += w->integrated[i]; ++nn; }
    }
    if (nq == 8U) {
        qrs_amp = 0.0; for (size_t i = 0; i < 8U; ++i) qrs_amp += qa[i];
        qrs_amp /= 8.0; noise_amp = nn ? na_sum / (double)nn : qrs_amp / 10.0;
        double rr_sum = 0.0; size_t rr_n = 0;
        for (size_t i = 1; i < 8U; ++i) {
            unsigned rr = learned_inds[i] - learned_inds[i - 1U];
            if (rr < 600U) { rr_sum += rr; ++rr_n; }
        }
        rr_recent = rr_n ? rr_sum / (double)rr_n : 200.0;
        last_qrs = learned_inds[0] > 76U ? 0 : (int)learned_inds[0] - 76;
    }
    double threshold = 0.25 * qrs_amp + 0.75 * noise_amp;
    size_t out_n = 0; int last_peak_num = -1;
    for (size_t p = 0; p < n_candidates; ++p) {
        int i = w->candidates[p];
        bool is_qrs = i - last_qrs > 50 && w->integrated[i] > threshold;
        if (is_qrs) {
            int rr = i - last_qrs;
            if (rr < 600) rr_recent = 0.875 * rr_recent + 0.125 * rr;
            if (out_n < peak_capacity) peaks[out_n++] = (uint16_t)i;
            last_qrs = i; last_peak_num = (int)p;
            qrs_amp = 0.875 * qrs_amp + 0.125 * w->integrated[i];
            threshold = 0.25 * qrs_amp + 0.75 * noise_amp;
        } else {
            noise_amp = 0.875 * noise_amp + 0.125 * w->integrated[i];
        }
        if (p + 1U < n_candidates &&
            (double)(w->candidates[p + 1U] - last_qrs) > rr_recent * 1.66) {
            size_t from = last_peak_num >= 0 ? (size_t)last_peak_num + 1U : 0U;
            for (size_t q = from; q <= p; ++q) {
                int bi = w->candidates[q];
                if (bi - last_qrs > 50 && w->integrated[bi] > threshold / 2.0) {
                    int rr = bi - last_qrs;
                    if (rr < 600) rr_recent = 0.875 * rr_recent + 0.125 * rr;
                    if (out_n < peak_capacity) peaks[out_n++] = (uint16_t)bi;
                    last_qrs = bi; last_peak_num = (int)q;
                    qrs_amp = 0.75 * qrs_amp + 0.25 * w->integrated[bi];
                    threshold = 0.25 * qrs_amp + 0.75 * noise_amp;
                }
            }
        }
    }
    return out_n;
}

size_t sqi_xqrs_static_bytes(void) { return sizeof(sqi_xqrs_workspace_t); }
