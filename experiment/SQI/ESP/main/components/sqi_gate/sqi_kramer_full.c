/* CONDITIONING REMOVED: latency/memory variant; no accuracy equivalence claimed. */
#include "sqi_kramer_full.h"

#include <math.h>
#include <string.h>

#define PI_D 3.14159265358979323846

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
