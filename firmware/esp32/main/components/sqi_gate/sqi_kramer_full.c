#include "sqi_kramer_full.h"

#include <math.h>
#include <string.h>

#define PI_D 3.14159265358979323846

static const double hf_b[12] = {
    2.997376745049491e-10, 3.29711441955444e-09, 1.6485572097772198e-08,
    4.94567162933166e-08, 9.89134325866332e-08, 1.3847880562128648e-07,
    1.3847880562128648e-07, 9.89134325866332e-08, 4.94567162933166e-08,
    1.6485572097772198e-08, 3.29711441955444e-09, 2.997376745049491e-10};
static const double hf_a[12] = {
    1.0, -8.90977263672493, 36.25073701287967, -88.880525832858,
    145.87762513194664, -168.2513532903864, 139.12514668645744,
    -82.46187447327412, 34.328704460270195, -9.557953249638187,
    1.601619078703803, -0.12235227351338311};
static const double hf_zi[11] = {
    1.0000000281342363, -7.909772865228014, 28.340965161918447,
    -60.53956324762245, 85.33806593329075, -82.91329227962837,
    56.2118582242305, -26.25001869267551, 8.078686694239318,
    -1.4792668436549956, 0.12235227729208165};
static const double base_b[3] = {
    6.393681633648538e-05, 0.00012787363267297076, 6.393681633648538e-05};
static const double base_a[3] = {1.0, -1.9772562430322471, 0.9775119902975931};
static const double base_zi[2] = {0.9999360631836685, -0.9774480534812613};
static const double pan_b[3] = {0.059190703818405424, 0.0, -0.059190703818405424};
static const double pan_a[3] = {1.0, -1.8704724177979166, 0.8816185923631893};

static void lfilter_in_place(float *x, size_t n, const double *b,
                             const double *a, const double *zi, size_t order,
                             int reverse)
{
    double z[11] = {0};
    const size_t first = reverse ? n - 1U : 0U;
    for (size_t j = 0; j < order; ++j) z[j] = zi[j] * x[first];
    for (size_t k = 0; k < n; ++k) {
        size_t i = reverse ? n - 1U - k : k;
        double in = x[i];
        double out = b[0] * in + z[0];
        for (size_t j = 0; j + 1U < order; ++j)
            z[j] = b[j + 1U] * in + z[j + 1U] - a[j + 1U] * out;
        z[order - 1U] = b[order] * in - a[order] * out;
        x[i] = (float)out;
    }
}

static void filtfilt(const float *input, float *output, float *work, size_t n,
                     const double *b, const double *a, const double *zi,
                     size_t order, size_t pad)
{
    size_t total = n + 2U * pad;
    for (size_t i = 0; i < pad; ++i)
        work[i] = 2.0f * input[0] - input[pad - i];
    memcpy(work + pad, input, n * sizeof(float));
    for (size_t i = 0; i < pad; ++i)
        work[pad + n + i] = 2.0f * input[n - 1U] - input[n - 2U - i];
    lfilter_in_place(work, total, b, a, zi, order, 0);
    lfilter_in_place(work, total, b, a, zi, order, 1);
    memcpy(output, work + pad, n * sizeof(float));
}

static void fourier_resample_x2(const float *input, float *output,
                                float *kernel, size_t n)
{
    for (size_t i = 0; i < n; ++i) output[2U * i] = input[i];
    for (int delta = -(int)(n - 1U); delta <= (int)(n - 1U); ++delta) {
        double d = (double)delta + 0.5;
        double numerator = (delta & 1) ? -1.0 : 1.0;
        kernel[delta + (int)(n - 1U)] = (float)(numerator /
            ((double)n * tan(PI_D * d / (double)n)));
    }
    for (size_t m = 0; m < n; ++m) {
        double sum = 0.0;
        for (size_t k = 0; k < n; ++k)
            sum += (double)input[k] * kernel[(int)m - (int)k + (int)(n - 1U)];
        output[2U * m + 1U] = (float)sum;
    }
}

static uint16_t pan_tompkins_count(float *x, float *work, size_t n)
{
    double z0 = 0.0, z1 = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double in = x[i];
        double y = pan_b[0] * in + z0;
        z0 = pan_b[1] * in + z1 - pan_a[1] * y;
        z1 = pan_b[2] * in - pan_a[2] * y;
        x[i] = (float)y;
    }
    const size_t detection_n = n - 1U; /* numpy.diff */
    for (size_t i = 0; i < detection_n; ++i) {
        float diff = x[i + 1U] - x[i];
        float square = diff * diff;
        work[i] = square;
    }
    double running = 0.0;
    const size_t window = 75U;
    for (size_t i = 0; i < detection_n; ++i) {
        running += work[i];
        if (i >= window) running -= work[i - window];
        size_t denom = i + 1U < window ? i + 1U : window;
        x[i] = i < 150U ? 0.0f : (float)(running / (double)denom);
    }

    uint16_t signal[64] = {0};
    uint16_t candidates[1024];
    uint16_t signal_count = 1U, candidate_count = 0U;
    double spki = 0.0, npki = 0.0, threshold1 = 0.0, threshold2 = 0.0;
    uint16_t rr_missed = 0U;
    for (uint16_t i = 1U; i + 1U < detection_n; ++i) {
        if (!(x[i - 1U] < x[i] && x[i + 1U] < x[i])) continue;
        if (candidate_count < 1024U) candidates[candidate_count] = i;
        uint16_t candidate_index = candidate_count++;
        if (x[i] > threshold1 && i - signal[signal_count - 1U] > 150U) {
            if (signal_count < 64U) signal[signal_count++] = i;
            spki = 0.125 * x[i] + 0.875 * spki;
            if (rr_missed && signal_count >= 3U &&
                signal[signal_count - 1U] - signal[signal_count - 2U] > rr_missed) {
                uint16_t best = 0U; float best_value = -1.0f;
                uint16_t left = signal[signal_count - 2U], right = signal[signal_count - 1U];
                for (uint16_t q = 0; q < candidate_index; ++q) {
                    uint16_t p = candidates[q];
                    if (p - left > 125U && right - p > 125U &&
                        x[p] > threshold2 && x[p] > best_value) {
                        best = p; best_value = x[p];
                    }
                }
                if (best_value >= 0.0f && signal_count < 64U) {
                    signal[signal_count] = signal[signal_count - 1U];
                    signal[signal_count - 1U] = best;
                    ++signal_count;
                }
            }
        } else {
            npki = 0.125 * x[i] + 0.875 * npki;
        }
        threshold1 = npki + 0.25 * (spki - npki);
        threshold2 = 0.5 * threshold1;
        if (signal_count > 8U) {
            uint32_t sum = 0U;
            for (uint16_t q = signal_count - 8U; q < signal_count; ++q)
                sum += signal[q] - signal[q - 1U];
            rr_missed = (uint16_t)(1.66 * (double)(sum / 8U));
        }
    }
    return signal_count - 1U;
}

static float periodogram_ratio(const float *x, size_t n)
{
    double mean = 0.0, energy = 0.0, selected = 0.0;
    for (size_t i = 0; i < n; ++i) mean += x[i];
    mean /= n;
    for (size_t i = 0; i < n; ++i) { double v = x[i] - mean; energy += v * v; }
    if (energy == 0.0) return INFINITY;
    for (size_t k = 20U; k < 400U; ++k) {
        double re = 0.0, im = 0.0;
        double step = 2.0 * PI_D * (double)k / (double)n;
        double cs = cos(step), sn = sin(step), c = 1.0, s = 0.0;
        for (size_t i = 0; i < n; ++i) {
            double v = x[i] - mean;
            re += v * c; im -= v * s;
            double next_c = c * cs - s * sn;
            s = s * cs + c * sn;
            c = next_c;
        }
        selected += 2.0 * (re * re + im * im) / (double)n;
    }
    return (float)(selected / (energy - selected));
}

void sqi_kramer_full_reset(sqi_kramer_full_t *state)
{
    if (state == NULL) return;
    state->input_count = 0U;
    sqi_kramer_reset(&state->rules);
}

int sqi_kramer_full_push(sqi_kramer_full_t *state, float sample)
{
    if (state == NULL || state->input_count >= SQI_KRAMER_INPUT_SAMPLES) return 0;
    state->input[state->input_count++] = sample;
    sqi_kramer_push(&state->rules, sample);
    return 1;
}

sqi_kramer_result_t sqi_kramer_full_finalize(sqi_kramer_full_t *state,
                                             uint16_t *peak_count,
                                             float *snr_ratio)
{
    if (state == NULL || state->input_count != SQI_KRAMER_INPUT_SAMPLES)
        return sqi_kramer_finalize(NULL, 0U, true);
    fourier_resample_x2(state->input, state->resampled, state->scratch,
                        state->input_count);
    float ratio = periodogram_ratio(state->resampled, SQI_KRAMER_RESAMPLED_SAMPLES);
    filtfilt(state->resampled, state->input, state->scratch,
             SQI_KRAMER_RESAMPLED_SAMPLES, hf_b, hf_a, hf_zi, 11U, 36U);
    filtfilt(state->resampled, state->scratch, state->input,
             SQI_KRAMER_RESAMPLED_SAMPLES, base_b, base_a, base_zi, 2U, 9U);
    for (size_t i = 0; i < SQI_KRAMER_RESAMPLED_SAMPLES; ++i)
        state->resampled[i] = state->input[i] - state->scratch[i];
    uint16_t count = pan_tompkins_count(state->resampled, state->scratch,
                                        SQI_KRAMER_RESAMPLED_SAMPLES);
    if (peak_count) *peak_count = count;
    if (snr_ratio) *snr_ratio = ratio;
    return sqi_kramer_finalize(&state->rules, count, ratio < 0.5f);
}

size_t sqi_kramer_full_static_bytes(void)
{
    return sizeof(sqi_kramer_full_t);
}
