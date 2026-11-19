#include "wim_xu.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#define PI_F 3.14159265358979323846f
#define SHANNON_WINDOW 49U
#define BOX_HALF 24U
#define LOWPASS_PAD 6U

/* scipy.signal.cheby1(3, 1, [8,25], btype="band", fs=360, output="ba"). */
static const float cheby_b[7] = {
    0.0013933791080476177f, 0.0f, -0.0041801373241428528f,
    0.0f, 0.0041801373241428528f, 0.0f, -0.0013933791080476177f,
};
static const float cheby_a[7] = {
    1.0f, -5.4742464455879452f, 12.700720573039145f,
    -15.986065461775173f, 11.514501297634054f,
    -4.5009400815315379f, 0.74622356172292215f,
};
static const float low_b[2] = {0.017155612596462977f, 0.017155612596462977f};
static const float low_a[2] = {1.0f, -0.96568877480707405f};

static void lfilter_cheby(const float *input, float *output, size_t n)
{
    float state[6] = {0};
    for (size_t i = 0U; i < n; ++i) {
        const float x = input[i];
        const float y = cheby_b[0] * x + state[0];
        for (size_t j = 0U; j < 5U; ++j)
            state[j] = cheby_b[j + 1U] * x - cheby_a[j + 1U] * y + state[j + 1U];
        state[5] = cheby_b[6] * x - cheby_a[6] * y;
        output[i] = y;
    }
}

static void lfilter_lowpass(float *data, size_t n, float initial)
{
    const float zi = (low_b[1] - low_a[1] * low_b[0]) / (low_a[0] + low_a[1]);
    float state = zi * initial;
    for (size_t i = 0U; i < n; ++i) {
        const float x = data[i];
        const float y = low_b[0] * x + state;
        state = low_b[1] * x - low_a[1] * y;
        data[i] = y;
    }
}

static bool lowpass_filtfilt(const float *input, float *output, size_t n,
                             wim_xu_workspace_t *ws)
{
    if (n <= LOWPASS_PAD) return false;
    const size_t length = n + 2U * LOWPASS_PAD;
    float *extended = ws->scratch;
    for (size_t i = 0U; i < LOWPASS_PAD; ++i) {
        extended[i] = 2.0f * input[0] - input[LOWPASS_PAD - i];
        extended[LOWPASS_PAD + n + i] =
            2.0f * input[n - 1U] - input[n - 2U - i];
    }
    memcpy(extended + LOWPASS_PAD, input, n * sizeof(float));
    lfilter_lowpass(extended, length, extended[0]);
    for (size_t i = 0U; i < length / 2U; ++i) {
        const float tmp = extended[i];
        extended[i] = extended[length - 1U - i];
        extended[length - 1U - i] = tmp;
    }
    lfilter_lowpass(extended, length, extended[0]);
    for (size_t i = 0U; i < length / 2U; ++i) {
        const float tmp = extended[i];
        extended[i] = extended[length - 1U - i];
        extended[length - 1U - i] = tmp;
    }
    memcpy(output, extended + LOWPASS_PAD, n * sizeof(float));
    return true;
}

static float derivative_at(const float *x, size_t i, size_t n)
{
    return i + 1U < n ? x[i + 1U] - x[i] : x[n - 1U] - x[n - 2U];
}

static void hilbert_kernel(float *kernel, size_t n)
{
    for (size_t q = 1U; q < n / 2U; q += 2U) {
        kernel[q / 2U] = (2.0f / (float)n) / tanf(PI_F * (float)q / (float)n);
    }
}

static void discrete_hilbert(const float *real, float *imag, size_t n,
                             const float *kernel)
{
    for (size_t i = 0U; i < n; ++i) {
        float sum = 0.0f;
        for (size_t q = 1U; q < n; q += 2U) {
            const bool positive = q < n / 2U;
            const size_t reflected = positive ? q : n - q;
            const size_t index = i >= q ? i - q : i + n - q;
            const float weight = kernel[reflected / 2U] * (positive ? 1.0f : -1.0f);
            sum += real[index] * weight;
        }
        imag[i] = sum;
    }
}

size_t wim_xu_detect_360(const float *samples, size_t count,
                         uint16_t *peaks, size_t peak_capacity,
                         wim_xu_workspace_t *ws)
{
    if (samples == NULL || peaks == NULL || ws == NULL || count < 32U ||
        count > WIM_XU_MAX_SAMPLES || peak_capacity == 0U) return 0U;

    lfilter_cheby(samples, ws->filtered, count);
    float max_abs = 0.0f;
    for (size_t i = 0U; i < count; ++i) {
        const float value = derivative_at(ws->filtered, i, count);
        const float magnitude = fabsf(value);
        if (magnitude > max_abs) max_abs = magnitude;
    }
    if (!(max_abs > 0.0f)) return 0U;

    for (size_t i = 0U; i < count; ++i) {
        const float normalized = derivative_at(ws->filtered, i, count) / max_abs;
        const float squared = normalized * normalized;
        ws->scratch[i] = -squared * logf(squared);
    }
    for (size_t i = 0U; i < count; ++i) {
        float sum = 0.0f;
        const size_t start = i > BOX_HALF ? i - BOX_HALF : 0U;
        const size_t end = i + BOX_HALF + 1U < count ? i + BOX_HALF + 1U : count;
        for (size_t j = start; j < end; ++j) sum += ws->scratch[j];
        ws->filtered[i] = sum / (float)SHANNON_WINDOW;
    }
    if (!lowpass_filtfilt(ws->filtered, ws->filtered, count, ws)) return 0U;

    hilbert_kernel(ws->hilbert_kernel, count);
    discrete_hilbert(ws->filtered, ws->scratch, count, ws->hilbert_kernel);

    size_t found = 0U;
    float previous_phase = atan2f(ws->scratch[0], ws->filtered[0]);
    for (size_t i = 1U; i < count && found < peak_capacity; ++i) {
        const float phase = atan2f(ws->scratch[i], ws->filtered[i]);
        if (previous_phase < 0.0f && phase >= 0.0f) peaks[found++] = (uint16_t)i;
        previous_phase = phase;
    }
    return found;
}
