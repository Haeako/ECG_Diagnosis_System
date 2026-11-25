#include "wim_arteaga_falconi.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define SECTIONS 5U

/* scipy.signal.butter(5, [6/(360/2), 17.5/(360/2)], btype="band", output="sos"). */
static const float sos[SECTIONS][6] = {
    {7.47649396266e-06f, 1.49529879253e-05f, 7.47649396266e-06f, 1.0f, -1.74516915395f, 0.803974218392f},
    {1.0f, 2.0f, 1.0f, 1.0f, -1.78795523944f, 0.817034269886f},
    {1.0f, 0.0f, -1.0f, 1.0f, -1.88346095739f, 0.898311970998f},
    {1.0f, -2.0f, 1.0f, 1.0f, -1.82789166611f, 0.913290046597f},
    {1.0f, -2.0f, 1.0f, 1.0f, -1.95667066912f, 0.967857277332f},
};

static void sos_filter(float *x, size_t n, float scale)
{
    float section_scale = scale;
    for (size_t s = 0U; s < SECTIONS; ++s) {
        const float *c = sos[s];
        const float dc = (c[0] + c[1] + c[2]) / (c[3] + c[4] + c[5]);
        const float zi0 = (c[1] - c[4] * c[0] + c[2] - c[5] * c[0]) /
                          (c[3] + c[4] + c[5]);
        const float zi1 = c[2] - c[5] * c[0] - c[5] * zi0;
        float z1 = zi0 * section_scale;
        float z2 = zi1 * section_scale;
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
                         wim_af_workspace_t *ws)
{
    const size_t pad = 33U; /* scipy sosfiltfilt default for 5 SOS sections */
    if (n <= pad || pad > WIM_AF_PAD) return false;
    const size_t length = n + 2U * pad;
    float *forward = ws->work_a;
    float *reverse = ws->work_b;
    for (size_t i = 0U; i < pad; ++i) {
        forward[i] = 2.0f * input[0] - input[pad - i];
        forward[pad + n + i] = 2.0f * input[n - 1U] - input[n - 2U - i];
    }
    memcpy(forward + pad, input, n * sizeof(float));
    sos_filter(forward, length, forward[0]);
    for (size_t i = 0U; i < length; ++i) reverse[i] = forward[length - 1U - i];
    sos_filter(reverse, length, reverse[0]);
    for (size_t i = 0U; i < n; ++i) output[i] = reverse[length - 1U - (pad + i)];
    return true;
}

static void fir_filter(float *x, size_t n, float scale)
{
    static const float b[5] = {0.0f, -45.0f, -90.0f, 90.0f, 45.0f};
    float z1 = 0.0f, z2 = 0.0f, z3 = 0.0f, z4 = 0.0f;
    /* lfilter_zi for this FIR, scaled by the odd-extension endpoint. */
    const float zi0 = b[1] + b[2] + b[3] + b[4];
    const float zi1 = b[2] + b[3] + b[4];
    const float zi2 = b[3] + b[4];
    const float zi3 = b[4];
    z1 = zi0 * scale; z2 = zi1 * scale; z3 = zi2 * scale; z4 = zi3 * scale;
    for (size_t i = 0U; i < n; ++i) {
        const float in = x[i];
        const float out = b[0] * in + z1;
        z1 = b[1] * in + z2;
        z2 = b[2] * in + z3;
        z3 = b[3] * in + z4;
        z4 = b[4] * in;
        x[i] = out;
    }
}

static bool fir_filtfilt(const float *input, float *output, size_t n,
                         wim_af_workspace_t *ws)
{
    const size_t pad = 15U;
    if (n <= pad || pad > WIM_AF_PAD) return false;
    const size_t length = n + 2U * pad;
    float *forward = ws->work_a;
    float *reverse = ws->work_b;
    for (size_t i = 0U; i < pad; ++i) {
        forward[i] = 2.0f * input[0] - input[pad - i];
        forward[pad + n + i] = 2.0f * input[n - 1U] - input[n - 2U - i];
    }
    memcpy(forward + pad, input, n * sizeof(float));
    fir_filter(forward, length, forward[0]);
    for (size_t i = 0U; i < length; ++i) reverse[i] = forward[length - 1U - i];
    fir_filter(reverse, length, reverse[0]);
    for (size_t i = 0U; i < n; ++i) output[i] = reverse[length - 1U - (pad + i)];
    return true;
}

static int compare_position_values(uint16_t a, uint16_t b, const float *values)
{
    if (values[a] < values[b]) return -1;
    if (values[a] > values[b]) return 1;
    return (a > b) - (a < b);
}

/* Small in-place heapsort avoids platform-specific qsort_r interfaces. */
static void sift_down(uint16_t *order, size_t root, size_t end, const float *values)
{
    while (root * 2U + 1U < end) {
        size_t child = root * 2U + 1U;
        if (child + 1U < end && compare_position_values(order[child], order[child + 1U], values) < 0) ++child;
        if (compare_position_values(order[root], order[child], values) >= 0) return;
        const uint16_t tmp = order[root]; order[root] = order[child]; order[child] = tmp;
        root = child;
    }
}

static void sort_positions(uint16_t *order, size_t n, const float *values)
{
    for (size_t start = n / 2U; start > 0U; --start) sift_down(order, start - 1U, n, values);
    for (size_t end = n; end > 1U; --end) {
        const uint16_t tmp = order[0]; order[0] = order[end - 1U]; order[end - 1U] = tmp;
        sift_down(order, 0U, end - 1U, values);
    }
}

size_t wim_af_detect_360(const float *samples, size_t count,
                         uint16_t *peaks, size_t peak_capacity,
                         wim_af_workspace_t *ws)
{
    if (samples == NULL || peaks == NULL || ws == NULL || count < 64U ||
        count > WIM_AF_MAX_SAMPLES || peak_capacity == 0U) return 0U;
    if (!sos_filtfilt(samples, ws->filtered, count, ws) ||
        !fir_filtfilt(ws->filtered, ws->filtered, count, ws)) return 0U;

    for (size_t i = 0U; i + 2U < count; ++i) {
        const float second = ws->filtered[i + 2U] - 2.0f * ws->filtered[i + 1U] + ws->filtered[i];
        ws->inverted[i] = -second;
    }
    if (count >= 5U) {
        ws->inverted[count - 2U] = ws->inverted[count - 5U];
        ws->inverted[count - 1U] = ws->inverted[count - 4U];
    }
    memset(ws->peak_mask, 0, count * sizeof(ws->peak_mask[0]));
    for (size_t i = 0U; i < count; ++i) ws->order[i] = (uint16_t)i;
    sort_positions(ws->order, count, ws->inverted);
    size_t lr = (size_t)(0.018f * 360.0f * (220.0f / 60.0f) * ((float)count / 360.0f));
    if (lr > count) lr = count;
    const size_t window = (size_t)(0.34f * 360.0f);
    for (size_t k = 0U; k < lr; ++k) {
        const size_t pos = ws->order[k];
        const size_t start = pos > window ? pos - window : 0U;
        const size_t raw_end = pos + window;
        const size_t end = raw_end < count ? raw_end : count;
        if (end <= start) continue;
        size_t best = start;
        for (size_t j = start + 1U; j < end; ++j)
            if (ws->inverted[j] > ws->inverted[best]) best = j;
        ws->peak_mask[best] = 1U;
    }
    size_t written = 0U;
    for (size_t i = 0U; i < count && written < peak_capacity; ++i)
        if (ws->peak_mask[i] != 0U) peaks[written++] = (uint16_t)i;
    return written;
}
