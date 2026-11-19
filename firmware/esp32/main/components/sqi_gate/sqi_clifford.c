#include "sqi_clifford.h"

#include <math.h>
#include <string.h>

#define PI_D 3.14159265358979323846264338327950288
#define PI_F 3.14159265358979323846f

static void add_band_power(double *sum, double *previous, size_t *number,
                           double power, double df)
{
    if (*number > 0U) *sum += 0.5 * (*previous + power) * df;
    *previous = power;
    ++(*number);
}

static void fft(float *real, float *imag, size_t n, bool inverse)
{
    for (size_t i = 1U, j = 0U; i < n; ++i) {
        size_t bit = n >> 1U;
        for (; (j & bit) != 0U; bit >>= 1U) j ^= bit;
        j ^= bit;
        if (i < j) {
            float t = real[i]; real[i] = real[j]; real[j] = t;
            t = imag[i]; imag[i] = imag[j]; imag[j] = t;
        }
    }
    for (size_t length = 2U; length <= n; length <<= 1U) {
        float angle = (inverse ? 2.0f : -2.0f) * PI_F / (float)length;
        float step_r = cosf(angle), step_i = sinf(angle);
        size_t half = length >> 1U;
        for (size_t base = 0U; base < n; base += length) {
            float wr = 1.0f, wi = 0.0f;
            for (size_t j = 0U; j < half; ++j) {
                size_t even = base + j, odd = even + half;
                float tr = real[odd] * wr - imag[odd] * wi;
                float ti = real[odd] * wi + imag[odd] * wr;
                float er = real[even], ei = imag[even];
                real[even] = er + tr; imag[even] = ei + ti;
                real[odd] = er - tr; imag[odd] = ei - ti;
                float next_wr = wr * step_r - wi * step_i;
                wi = wr * step_i + wi * step_r;
                wr = next_wr;
            }
        }
    }
    if (inverse) {
        float scale = 1.0f / (float)n;
        for (size_t i = 0U; i < n; ++i) { real[i] *= scale; imag[i] *= scale; }
    }
}

static size_t next_power_two(size_t n)
{
    size_t p = 1U;
    while (p < n) p <<= 1U;
    return p;
}

static bool periodogram_bands(const float *x, size_t n, float fs, double mean,
                              sqi_clifford_workspace_t *w,
                              double *p5_15, double *p5_40,
                              double *p0_1, double *p0_40)
{
    size_t m = next_power_two(2U * n - 1U);
    if (m > SQI_CLIFFORD_FFT_SIZE || n > SQI_CLIFFORD_MAX_SAMPLES) return false;
    memset(w->real, 0, m * sizeof(float));
    memset(w->imag, 0, m * sizeof(float));
    memset(w->kernel_real, 0, m * sizeof(float));
    memset(w->kernel_imag, 0, m * sizeof(float));
    for (size_t i = 0U; i < n; ++i) {
        double phase = PI_D * (double)i * (double)i / (double)n;
        float c = (float)cos(phase), s = (float)sin(phase);
        float value = x[i] - (float)mean;
        w->real[i] = value * c;
        w->imag[i] = -value * s;
        w->kernel_real[i] = c;
        w->kernel_imag[i] = s;
        if (i != 0U) {
            w->kernel_real[m - i] = c;
            w->kernel_imag[m - i] = s;
        }
    }
    fft(w->real, w->imag, m, false);
    fft(w->kernel_real, w->kernel_imag, m, false);
    for (size_t i = 0U; i < m; ++i) {
        float re = w->real[i] * w->kernel_real[i] - w->imag[i] * w->kernel_imag[i];
        float im = w->real[i] * w->kernel_imag[i] + w->imag[i] * w->kernel_real[i];
        w->real[i] = re; w->imag[i] = im;
    }
    fft(w->real, w->imag, m, true);

    *p5_15 = *p5_40 = *p0_1 = *p0_40 = 0.0;
    double prev_5_15 = 0.0, prev_5_40 = 0.0, prev_0_1 = 0.0, prev_0_40 = 0.0;
    size_t c5_15 = 0U, c5_40 = 0U, c0_1 = 0U, c0_40 = 0U;
    double df = (double)fs / (double)n;
    for (size_t k = 0U; k <= n / 2U; ++k) {
        double phase = PI_D * (double)k * (double)k / (double)n;
        double c = cos(phase), s = sin(phase);
        double re = (double)w->real[k] * c + (double)w->imag[k] * s;
        double im = (double)w->imag[k] * c - (double)w->real[k] * s;
        double power = (re * re + im * im) / ((double)fs * (double)n);
        if (k != 0U && !(n % 2U == 0U && k == n / 2U)) power *= 2.0;
        double f = (double)k * df;
        if (f >= 5.0 && f <= 15.0) add_band_power(p5_15, &prev_5_15, &c5_15, power, df);
        if (f >= 5.0 && f <= 40.0) add_band_power(p5_40, &prev_5_40, &c5_40, power, df);
        if (f >= 0.0 && f <= 1.0) add_band_power(p0_1, &prev_0_1, &c0_1, power, df);
        if (f >= 0.0 && f <= 40.0) add_band_power(p0_40, &prev_0_40, &c0_40, power, df);
    }
    return c5_15 >= 2U && c5_40 >= 2U && c0_1 >= 2U && c0_40 >= 2U;
}

sqi_clifford_result_t sqi_clifford_extract(const float *x, size_t n,
                                            float fs, float flat_scale,
                                            sqi_clifford_workspace_t *workspace)
{
    sqi_clifford_result_t out = {NAN, NAN, NAN, NAN, NAN, false};
    if (x == NULL || workspace == NULL || n < 4U || n > SQI_CLIFFORD_MAX_SAMPLES ||
        !(fs > 0.0f) || !(flat_scale > 0.0f)) return out;

    double mean = 0.0;
    for (size_t i = 0U; i < n; ++i) {
        if (!isfinite(x[i])) return out;
        mean += x[i];
    }
    mean /= (double)n;

    double m2 = 0.0, m3 = 0.0, m4 = 0.0;
    size_t flat = 0U;
    for (size_t i = 0U; i < n; ++i) {
        double d = (double)x[i] - mean;
        double d2 = d * d;
        m2 += d2;
        m3 += d2 * d;
        m4 += d2 * d2;
        if (i > 0U && fabs(((double)x[i] - x[i - 1U]) * flat_scale) < 25.0)
            ++flat;
    }
    out.fsqi = (float)flat / (float)(n - 1U);
    m2 /= (double)n;
    m3 /= (double)n;
    m4 /= (double)n;
    if (!(m2 > 0.0)) return out;
    double g1 = m3 / pow(m2, 1.5);
    double g2 = m4 / (m2 * m2);
    out.ssqi = (float)(sqrt((double)n * (double)(n - 1U)) /
                       (double)(n - 2U) * g1);
    out.ksqi = (float)(((double)(n - 1U) /
                       ((double)(n - 2U) * (double)(n - 3U))) *
                       ((double)(n + 1U) * g2 - 3.0 * (double)(n - 1U)) + 3.0);

    /* SciPy-compatible periodogram band integrals; ratio-only scaling matters. */
    double p5_15 = 0.0, p5_40 = 0.0, p0_1 = 0.0, p0_40 = 0.0;
    if (periodogram_bands(x, n, fs, mean, workspace, &p5_15, &p5_40,
                          &p0_1, &p0_40) && p5_40 > 0.0 && p0_40 > 0.0) {
        out.psqi = (float)(p5_15 / p5_40);
        out.bassqi = (float)(1.0 - p0_1 / p0_40);
    }
    out.valid = isfinite(out.psqi) && isfinite(out.ssqi) && isfinite(out.ksqi) &&
                isfinite(out.fsqi) && isfinite(out.bassqi);
    return out;
}
