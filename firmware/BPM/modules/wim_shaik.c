#include "wim_shaik.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define PI_F 3.14159265358979323846
#define STFT_N 75U
#define STFT_HOP 10U
#define STFT_LEFT_PAD 37
#define SMOOTH_SIGNAL 108U
#define SMOOTH_FINAL 72U
#define SEGMENT_SAMPLES 1800U
#define MIN_DISTANCE 98U

static void stft_components(const double *x, size_t n, double *out1, double *out2)
{
    double hann[STFT_N], cosine2[STFT_N], cosine3[STFT_N];
    for (size_t j = 0U; j < STFT_N; ++j) {
        hann[j] = 0.5 - 0.5 * cos((2.0 * PI_F * (double)j) / (double)STFT_N);
        cosine2[j] = cos((2.0 * PI_F * 2.0 * (double)j) / (double)STFT_N);
        cosine3[j] = cos((2.0 * PI_F * 3.0 * (double)j) / (double)STFT_N);
    }
    for (size_t frame = 0U; frame < WIM_SHAIK_STFT_FRAMES; ++frame) {
        double re2 = 0.0, re3 = 0.0;
        const int start = (int)(frame * STFT_HOP) - STFT_LEFT_PAD;
        for (size_t j = 0U; j < STFT_N; ++j) {
            const int sample_index = start + (int)j;
            const double sample = (sample_index >= 0 && (size_t)sample_index < n)
                ? x[sample_index] : 0.0;
            re2 += sample * hann[j] * cosine2[j];
            re3 += sample * hann[j] * cosine3[j];
        }
        out1[frame] = fabs(re2);
        out2[frame] = fabs(re3);
    }
}

static bool normalize_interp(const double *frames, double *out, size_t n)
{
    double minimum = frames[0], maximum = frames[0];
    for (size_t i = 1U; i < WIM_SHAIK_STFT_FRAMES; ++i) {
        if (frames[i] < minimum) minimum = frames[i];
        if (frames[i] > maximum) maximum = frames[i];
    }
    const double range = maximum - minimum;
    if (!(range > 0.0)) return false;
    for (size_t i = 0U; i < n; ++i) {
        const double position = (double)i * (double)WIM_SHAIK_STFT_FRAMES / (double)(n - 1U);
        size_t left = (size_t)position;
        if (left >= WIM_SHAIK_STFT_FRAMES - 1U) {
            out[i] = (frames[WIM_SHAIK_STFT_FRAMES - 1U] - minimum) / range;
        } else {
            const double fraction = position - (double)left;
            const double value = frames[left] + fraction * (frames[left + 1U] - frames[left]);
            out[i] = (value - minimum) / range;
        }
    }
    return true;
}

static void smooth_box(const double *input, double *output, double *prefix,
                       size_t n, size_t width)
{
    prefix[0] = 0.0;
    for (size_t i = 0U; i < n; ++i) prefix[i + 1U] = prefix[i] + input[i];
    const int left_offset = (int)((width - 1U) / 2U);
    for (size_t i = 0U; i < n; ++i) {
        const int end_unclipped = (int)i + left_offset;
        const int start_unclipped = end_unclipped - (int)width + 1;
        const size_t start = start_unclipped < 0 ? 0U : (size_t)start_unclipped;
        const size_t end = end_unclipped < 0 ? 0U :
            ((size_t)end_unclipped >= n ? n : (size_t)end_unclipped + 1U);
        output[i] = (prefix[end] - prefix[start]) / (double)width;
    }
}

static size_t local_maxima(const double *x, size_t n, uint16_t *indices,
                           size_t capacity, bool apply_height, double height)
{
    size_t count = 0U, i = 1U;
    while (i + 1U < n) {
        if ((apply_height && x[i] < height) || x[i] <= x[i - 1U]) {
            ++i;
            continue;
        }
        size_t right = i;
        while (right + 1U < n && x[right + 1U] == x[i]) ++right;
        if (right + 1U < n && x[right] > x[right + 1U] && count < capacity) {
            const size_t peak = (i + right) / 2U;
            if (!apply_height || x[peak] >= height) indices[count++] = (uint16_t)peak;
        }
        i = right + 1U;
    }
    return count;
}

static double mean_at_peaks(const double *x, size_t start, size_t length,
                           uint16_t *indices)
{
    const size_t count = local_maxima(x + start, length, indices,
                                      WIM_SHAIK_MAX_CANDIDATES, false, 0.0);
    if (count == 0U) return NAN;
    double sum = 0.0;
    for (size_t i = 0U; i < count; ++i) sum += x[start + indices[i]];
    return sum / (double)count;
}

static int compare_height(uint16_t a, uint16_t b, const double *x)
{
    if (x[a] < x[b]) return -1;
    if (x[a] > x[b]) return 1;
    return (a > b) - (a < b);
}

static void sift_down(uint16_t *items, size_t root, size_t end, const double *x)
{
    while (root * 2U + 1U < end) {
        size_t child = root * 2U + 1U;
        if (child + 1U < end && compare_height(items[child], items[child + 1U], x) < 0) ++child;
        if (compare_height(items[root], items[child], x) >= 0) return;
        const uint16_t tmp = items[root]; items[root] = items[child]; items[child] = tmp;
        root = child;
    }
}

static void sort_by_height(uint16_t *items, size_t n, const double *x)
{
    for (size_t start = n / 2U; start > 0U; --start) sift_down(items, start - 1U, n, x);
    for (size_t end = n; end > 1U; --end) {
        const uint16_t tmp = items[0]; items[0] = items[end - 1U]; items[end - 1U] = tmp;
        sift_down(items, 0U, end - 1U, x);
    }
}

size_t wim_shaik_detect_360(const float *input_samples, size_t count,
                            uint16_t *peaks, size_t peak_capacity,
                            wim_shaik_workspace_t *ws)
{
    if (input_samples == NULL || peaks == NULL || ws == NULL || count < 2U ||
        count > WIM_SHAIK_MAX_SAMPLES || peak_capacity == 0U) return 0U;
    for (size_t j=0; j<count; ++j) ws->input[j]=input_samples[j];
    const double *samples=ws->input;

    stft_components(samples, count, ws->frame1, ws->frame2);
    if (!normalize_interp(ws->frame1, ws->signal1, count) ||
        !normalize_interp(ws->frame2, ws->signal2, count)) return 0U;

    smooth_box(ws->signal1, ws->scratch, ws->threshold1, count, SMOOTH_SIGNAL);
    memcpy(ws->signal1, ws->scratch, count * sizeof(double));
    smooth_box(ws->signal2, ws->scratch, ws->threshold1, count, SMOOTH_SIGNAL);
    memcpy(ws->signal2, ws->scratch, count * sizeof(double));

    const size_t segment_count = (count + SEGMENT_SAMPLES - 1U) / SEGMENT_SAMPLES;
    for (size_t segment = 0U; segment < segment_count; ++segment) {
        const size_t start = segment * SEGMENT_SAMPLES;
        const size_t length = (start + SEGMENT_SAMPLES <= count)
            ? SEGMENT_SAMPLES : count - start;
        const double threshold1 = mean_at_peaks(ws->signal1, start, length, ws->candidates);
        const double threshold2 = mean_at_peaks(ws->signal2, start, length, ws->candidates);
        ws->segment_threshold2[segment] = threshold2;
        for (size_t i = start; i < start + length; ++i) {
            ws->threshold1[i] = threshold1;
        }
    }
    for (size_t i = 0U; i < count; ++i) ws->signal1[i] += ws->signal2[i];
    smooth_box(ws->threshold1, ws->scratch, ws->signal2, count, SMOOTH_SIGNAL);
    memcpy(ws->threshold1, ws->scratch, count * sizeof(double));
    for (size_t i = 0U; i < count; ++i)
        ws->threshold1[i] = 0.8 * ws->threshold1[i] + 0.8 * ws->segment_threshold2[i / SEGMENT_SAMPLES];
    smooth_box(ws->threshold1, ws->scratch, ws->signal2, count, SMOOTH_FINAL);

    for (size_t i = 0U; i < count; ++i) {
        if (ws->signal1[i] < ws->scratch[i]) ws->signal1[i] = 0.0;
    }
    const size_t candidates = local_maxima(ws->signal1, count, ws->candidates,
        WIM_SHAIK_MAX_CANDIDATES, false, 0.0);
    if (candidates == 0U) return 0U;
    sort_by_height(ws->candidates, candidates, ws->signal1);
    size_t selected = 0U;
    for (size_t k = candidates; k > 0U && selected < peak_capacity &&
         selected < WIM_SHAIK_MAX_PEAKS; --k) {
        const size_t pos = ws->candidates[k - 1U];
        bool too_close = false;
        for (size_t j = 0U; j < selected; ++j) {
            const size_t chosen = ws->selected[j];
            const size_t delta = pos > chosen ? pos - chosen : chosen - pos;
            if (delta < MIN_DISTANCE) { too_close = true; break; }
        }
        if (too_close) continue;
        ws->selected[selected++] = (uint16_t)pos;
    }
    for (size_t a = 1U; a < selected; ++a) {
        const uint16_t value = ws->selected[a];
        size_t b = a;
        while (b > 0U && ws->selected[b - 1U] > value) {
            ws->selected[b] = ws->selected[b - 1U];
            --b;
        }
        ws->selected[b] = value;
    }
    memcpy(peaks, ws->selected, selected * sizeof(peaks[0]));
    return selected;
}
