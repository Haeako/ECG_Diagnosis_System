#include "wim_xia.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define XIA_WAVELET_LENGTH 16U
#define XIA_KMEANS_MIN_DISTANCE 99U

static const double butter_b[7] = {
    0.0020976317068894901, 0.0, -0.0062928951206684697, 0.0,
    0.0062928951206684697, 0.0, -0.0020976317068894901,
};
static const double butter_a[7] = {
    1.0, -5.2565248356639751, 11.692019173665967,
    -14.085814792740752, 9.695162990499643,
    -3.6156037158460372, 0.57100506416302577,
};
static const double butter_zi[6] = {
    -0.002097631706887712, -0.0020976317068970591,
    0.0041952634137922013, 0.0041952634137671536,
    -0.002097631706884076, -0.0020976317068905053,
};

static const double dec_lo[XIA_WAVELET_LENGTH] = {
    -0.0033824159510061256, -0.0005421323317911481,
    0.03169508781149298, 0.007607487324917605,
    -0.1432942383508097, -0.061273359067658524,
    0.4813596512583722, 0.7771857517005235,
    0.3644418948353314, -0.05194583810770904,
    -0.027219029917056003, 0.049137179673607506,
    0.003808752013890615, -0.01495225833704823,
    -0.0003029205147213668, 0.0018899503327594609,
};
static const double dec_hi[XIA_WAVELET_LENGTH] = {
    -0.0018899503327594609, -0.0003029205147213668,
    0.01495225833704823, 0.003808752013890615,
    -0.049137179673607506, -0.027219029917056003,
    0.05194583810770904, 0.3644418948353314,
    -0.7771857517005235, 0.4813596512583722,
    0.061273359067658524, -0.1432942383508097,
    -0.007607487324917605, 0.03169508781149298,
    0.0005421323317911481, -0.0033824159510061256,
};
static const double rec_lo[XIA_WAVELET_LENGTH] = {
    0.0018899503327594609, -0.0003029205147213668,
    -0.01495225833704823, 0.003808752013890615,
    0.049137179673607506, -0.027219029917056003,
    -0.05194583810770904, 0.3644418948353314,
    0.7771857517005235, 0.4813596512583722,
    -0.061273359067658524, -0.1432942383508097,
    0.007607487324917605, 0.03169508781149298,
    -0.0005421323317911481, -0.0033824159510061256,
};
static const double rec_hi[XIA_WAVELET_LENGTH] = {
    -0.0033824159510061256, 0.0005421323317911481,
    0.03169508781149298, -0.007607487324917605,
    -0.1432942383508097, 0.061273359067658524,
    0.4813596512583722, -0.7771857517005235,
    0.3644418948353314, 0.05194583810770904,
    -0.027219029917056003, -0.049137179673607506,
    0.003808752013890615, 0.01495225833704823,
    -0.0003029205147213668, -0.0018899503327594609,
};

static void lfilter(double *x, size_t n)
{
    double state[6];
    const double first = n > 0U ? x[0] : 0.0;
    for (size_t i = 0U; i < 6U; ++i) state[i] = butter_zi[i] * first;
    for (size_t i = 0U; i < n; ++i) {
        const double in = x[i];
        const double out = butter_b[0] * in + state[0];
        for (size_t k = 0U; k < 5U; ++k)
            state[k] = butter_b[k + 1U] * in - butter_a[k + 1U] * out + state[k + 1U];
        state[5] = butter_b[6] * in - butter_a[6] * out;
        x[i] = out;
    }
}

static double adc_to_mv(uint16_t value) { return ((double)value - 2048.0) / 200.0; }

static bool filter_signal(const uint16_t *input, wim_xia_workspace_t *ws)
{
    const size_t n = WIM_XIA_SAMPLES;
    const size_t pad = WIM_XIA_FILTER_PAD;
    const size_t length = n + 2U * pad;
    double *work = ws->scratch.filter_work;
    for (size_t i = 0U; i < pad; ++i) {
        work[i] = 2.0 * adc_to_mv(input[0]) - adc_to_mv(input[pad - i]);
        work[pad + n + i] = 2.0 * adc_to_mv(input[n - 1U]) - adc_to_mv(input[n - 2U - i]);
    }
    for (size_t i = 0U; i < n; ++i) work[pad + i] = adc_to_mv(input[i]);
    lfilter(work, length);
    for (size_t i = 0U; i < length / 2U; ++i) {
        const double t = work[i]; work[i] = work[length - 1U - i]; work[length - 1U - i] = t;
    }
    lfilter(work, length);
    for (size_t i = 0U; i < length / 2U; ++i) {
        const double t = work[i]; work[i] = work[length - 1U - i]; work[length - 1U - i] = t;
    }
    return true;
}

#ifdef WIM_XIA_HOST_TEST
void wim_xia_debug_filter_360(const uint16_t *samples, double *filtered,
                              wim_xia_workspace_t *ws)
{
    if (samples != NULL && filtered != NULL && ws != NULL && filter_signal(samples, ws))
        memcpy(filtered, ws->scratch.filter_work + WIM_XIA_FILTER_PAD,
               WIM_XIA_SAMPLES * sizeof(double));
}
#endif

static size_t symmetric_index(long index, size_t length)
{
    while (index < 0L || (size_t)index >= length) {
        if (index < 0L) index = -index - 1L;
        else index = (long)(2U * length) - 1L - index;
    }
    return (size_t)index;
}

static size_t dwt(const float *input, size_t n, float *approx, float *detail)
{
    const size_t out_n = (n + XIA_WAVELET_LENGTH - 1U) / 2U;
    for (size_t i = 0U; i < out_n; ++i) {
        double lo = 0.0, hi = 0.0;
        for (size_t j = 0U; j < XIA_WAVELET_LENGTH; ++j) {
            const size_t at = symmetric_index((long)(2U * i + 1U) - (long)j, n);
            lo += input[at] * dec_lo[j];
            hi += input[at] * dec_hi[j];
        }
        approx[i] = (float)lo;
        detail[i] = (float)hi;
    }
    return out_n;
}

static void idwt(const float *approx, const float *detail, size_t coeff_n,
                 size_t output_n, float *output)
{
    for (size_t i = 0U; i < output_n; ++i) {
        double value = 0.0;
        for (size_t k = 0U; k < coeff_n; ++k) {
            const long at = (long)i + (long)XIA_WAVELET_LENGTH - 2L - 2L * (long)k;
            if (at >= 0L && at < (long)XIA_WAVELET_LENGTH) {
                value += approx[k] * rec_lo[(size_t)at] + detail[k] * rec_hi[(size_t)at];
            }
        }
        output[i] = (float)value;
    }
}

static int compare_float(const void *left, const void *right)
{
    const float a = *(const float *)left, b = *(const float *)right;
    return (a > b) - (a < b);
}

static int compare_candidate_height(const void *left, const void *right)
{
    const wim_xia_candidate_t *a = (const wim_xia_candidate_t *)left;
    const wim_xia_candidate_t *b = (const wim_xia_candidate_t *)right;
    if (a->height < b->height) return 1;
    if (a->height > b->height) return -1;
    return (a->index > b->index) - (a->index < b->index);
}

static float cluster_threshold(float *sorted, size_t n)
{
    qsort(sorted, n, sizeof(float), compare_float);
    double total = 0.0, total_sq = 0.0;
    for (size_t i = 0U; i < n; ++i) {
        total += sorted[i];
        total_sq += (double)sorted[i] * sorted[i];
    }
    double left_sum = 0.0, left_sq = 0.0;
    double best = HUGE_VAL;
    size_t best_split = n / 2U;
    for (size_t split = 1U; split < n; ++split) {
        const double value = sorted[split - 1U];
        left_sum += value;
        left_sq += value * value;
        const size_t right_n = n - split;
        if (sorted[split] == sorted[split - 1U]) continue;
        const double right_sum = total - left_sum;
        const double right_sq = total_sq - left_sq;
        const double sse = left_sq - left_sum * left_sum / split +
                           right_sq - right_sum * right_sum / right_n;
        if (sse < best) { best = sse; best_split = split; }
    }
    return 0.5f * (sorted[best_split - 1U] + sorted[best_split]);
}

static void append_candidate(wim_xia_workspace_t *ws, size_t index, uint16_t height)
{
    if (ws->candidate_count < WIM_XIA_MAX_CANDIDATES) {
        ws->candidates[ws->candidate_count].index = (uint16_t)index;
        ws->candidates[ws->candidate_count].height = height;
        ++ws->candidate_count;
    }
}

size_t wim_xia_detect_360(const uint16_t *samples, size_t count,
                          uint16_t *peaks, size_t peak_capacity,
                          wim_xia_workspace_t *ws)
{
    if (samples == NULL || peaks == NULL || ws == NULL ||
        count != WIM_XIA_SAMPLES || peak_capacity == 0U) return 0U;
    ws->candidate_count = 0U;
    ws->detail_count = 0U;
    if (!filter_signal(samples, ws)) return 0U;

    const double *filtered = ws->scratch.filter_work + WIM_XIA_FILTER_PAD;
    for (size_t i = 0U; i < count; ++i) ws->approximation_a[i] = (float)filtered[i];
    float *current = ws->approximation_a;
    float *next = ws->scratch.approximation_b;
    ws->level_length[0] = (uint16_t)count;
    for (size_t level = 1U; level <= WIM_XIA_LEVELS; ++level) {
        const size_t n = ws->level_length[level - 1U];
        const size_t dlen = (n + XIA_WAVELET_LENGTH - 1U) / 2U;
        if (ws->detail_count + dlen > WIM_XIA_DETAIL_CAPACITY || dlen > WIM_XIA_SAMPLES) return 0U;
        ws->detail_offset[level] = (uint16_t)ws->detail_count;
        const size_t got = dwt(current, n, next, ws->details + ws->detail_count);
        ws->level_length[level] = (uint16_t)got;
        ws->detail_count += got;
        float *swap = current; current = next; next = swap;
    }

    memset(current, 0, ws->level_length[WIM_XIA_LEVELS] * sizeof(float));
    for (size_t level = WIM_XIA_LEVELS; level > 0U; --level) {
        const size_t coeff_n = ws->level_length[level];
        const size_t output_n = ws->level_length[level - 1U];
        float *detail = ws->details + ws->detail_offset[level];
        /* The upstream tuple names ca8/cd1/cd2/cd3, but wavedec returns
         * [cA10, cD10, cD9, cD8, ...]. Those first four arrays are zeroed. */
        if (level >= 8U) memset(detail, 0, coeff_n * sizeof(float));
        idwt(current, detail, coeff_n, output_n, next);
        float *swap = current; current = next; next = swap;
    }

    const float *reconstructed = current;
    for (size_t i = 0U; i < count; ++i) {
        const double slope = i + 1U < count ? reconstructed[i + 1U] - reconstructed[i]
                                            : reconstructed[i] - reconstructed[i - 1U];
        ws->details[i] = (float)fabs(slope);
    }
    const float threshold = cluster_threshold(ws->details, count);
    size_t high_count = 0U;
    for (size_t i = 0U; i < count; ++i) {
        const double slope = i + 1U < count ? reconstructed[i + 1U] - reconstructed[i]
                                            : reconstructed[i] - reconstructed[i - 1U];
        if (fabs(slope) > threshold) ++high_count;
    }
    const bool high_is_rare = high_count <= count - high_count;
    const double first_slope = fabs(reconstructed[1] - reconstructed[0]);
    const bool first_high = first_slope > threshold;
    uint16_t cumulative = (high_is_rare ? first_high : !first_high) ? 1U : 0U;
    ws->cumulative[0] = cumulative;
    for (size_t i = 1U; i < count; ++i) {
        const double slope = i + 1U < count ? fabs(reconstructed[i + 1U] - reconstructed[i])
                                             : fabs(reconstructed[i] - reconstructed[i - 1U]);
        const bool high = slope > threshold;
        const bool cluster_one = high_is_rare ? high : !high;
        if (cluster_one) ++cumulative;
        else if (cumulative > 0U) --cumulative;
        else cumulative = 0U;
        ws->cumulative[i] = cumulative;
    }
    /* WIM's code duplicates the same condition in its second elif; the second
     * branch is unreachable, so nonpositive runs remain at zero as above. */
    for (size_t i = 1U; i + 1U < count;) {
        if (ws->cumulative[i] > ws->cumulative[i - 1U]) {
            size_t end = i;
            while (end + 1U < count && ws->cumulative[end + 1U] == ws->cumulative[i]) ++end;
            if (end + 1U < count && ws->cumulative[end] > ws->cumulative[end + 1U]) {
                append_candidate(ws, (i + end) / 2U, ws->cumulative[end]);
            }
            i = end + 1U;
        } else ++i;
    }
    qsort(ws->candidates, ws->candidate_count, sizeof(ws->candidates[0]), compare_candidate_height);
    uint8_t accepted[WIM_XIA_MAX_CANDIDATES] = {0};
    size_t accepted_count = 0U;
    for (size_t i = 0U; i < ws->candidate_count; ++i) {
        const size_t at = ws->candidates[i].index;
        bool separated = true;
        for (size_t j = 0U; j < ws->candidate_count; ++j) {
            if (accepted[j] != 0U) {
                const size_t prior = ws->candidates[j].index;
                const size_t delta = at > prior ? at - prior : prior - at;
                if (delta < XIA_KMEANS_MIN_DISTANCE) { separated = false; break; }
            }
        }
        if (separated) { accepted[i] = 1U; ++accepted_count; }
    }
    size_t out = 0U;
    for (size_t i = 0U; i < ws->candidate_count; ++i) {
        if (accepted[i] != 0U && out < peak_capacity) peaks[out++] = ws->candidates[i].index;
    }
    for (size_t i = 1U; i < out; ++i) {
        uint16_t v = peaks[i]; size_t j = i;
        while (j > 0U && peaks[j - 1U] > v) { peaks[j] = peaks[j - 1U]; --j; }
        peaks[j] = v;
    }
    (void)accepted_count;
    return out;
}
