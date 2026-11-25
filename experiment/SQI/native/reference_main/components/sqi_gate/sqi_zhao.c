#include "sqi_zhao.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979323846f

void sqi_zhao_reset(sqi_zhao_state_t *state)
{
    if (state != NULL) memset(state, 0, sizeof(*state));
}

bool sqi_zhao_push(sqi_zhao_state_t *state, float sample)
{
    if (state == NULL || state->sample_count >= SQI_ZHAO_MAX_SAMPLES) {
        if (state != NULL) state->overflow = true;
        return false;
    }
    state->samples[state->sample_count++] = sample;
    return true;
}

bool sqi_zhao_push_peak(sqi_zhao_state_t *state, uint32_t timestamp_ms)
{
    if (state == NULL || state->peak_count >= SQI_ZHAO_MAX_PEAKS) {
        if (state != NULL) state->overflow = true;
        return false;
    }
    if (state->peak_count > 0U && timestamp_ms <= state->peak_ms[state->peak_count - 1U]) {
        return false;
    }
    state->peak_ms[state->peak_count++] = timestamp_ms;
    return true;
}

static float bandpower(const sqi_zhao_state_t *state, uint32_t fs,
                       float low_hz, float high_hz, float mean)
{
    uint32_t nperseg = state->sample_count / 2U;
    uint32_t nfft = nperseg * 2U;
    uint32_t step = nperseg / 2U;
    if (nperseg < 4U || step == 0U || fs == 0U) return NAN;
    float minimum = 4.0f * fs / state->sample_count;
    float lower = low_hz > minimum ? low_hz : minimum;
    int first = (int)ceilf(lower * nfft / fs);
    int last = (int)ceilf(high_hz * nfft / fs) - 1;
    int nyquist = (int)(nfft / 2U);
    if (last > nyquist) last = nyquist;
    if (first < 0 || last < first) return NAN;

    float integral = 0.0f;
    for (int k = first; k <= last; ++k) {
        float omega = -2.0f * PI_F * k / nfft;
        float cs = cosf(omega), sn = sinf(omega);
        float window_omega = 2.0f * PI_F / nperseg;
        float window_cs = cosf(window_omega), window_sn = sinf(window_omega);
        float bin_power = 0.0f;
        for (uint32_t start = 0U; start + nperseg <= state->sample_count; start += step) {
            float zr = 1.0f, zi = 0.0f, re = 0.0f, im = 0.0f;
            float window_zr = 1.0f, window_zi = 0.0f;
            for (uint32_t n = 0U; n < nperseg; ++n) {
                float window = 0.5f - 0.5f * window_zr;
                float value = (state->samples[start + n] - mean) * window;
                re += value * zr;
                im += value * zi;
                float next_r = zr * cs - zi * sn;
                zi = zr * sn + zi * cs;
                zr = next_r;
                float next_window_r = window_zr * window_cs - window_zi * window_sn;
                window_zi = window_zr * window_sn + window_zi * window_cs;
                window_zr = next_window_r;
            }
            bin_power += re * re + im * im;
        }
        float trapezoid_weight = (k == first || k == last) ? 0.5f : 1.0f;
        integral += trapezoid_weight * bin_power;
    }
    return integral > 0.0f ? integral : NAN;
}

sqi_zhao_result_t sqi_zhao_finalize(const sqi_zhao_state_t *state,
                                    uint32_t sample_rate_hz)
{
    sqi_zhao_result_t out = {NAN, NAN, NAN, NAN, SQI_ZHAO_UNACCEPTABLE};
    if (state == NULL || state->overflow || state->sample_count < 8U || sample_rate_hz == 0U) return out;
    double sum = 0.0;
    for (uint32_t i = 0U; i < state->sample_count; ++i) sum += state->samples[i];
    float mean = (float)(sum / state->sample_count);
    double m2 = 0.0, m4 = 0.0;
    for (uint32_t i = 0U; i < state->sample_count; ++i) {
        double d = (double)state->samples[i] - mean;
        double d2 = d * d;
        m2 += d2;
        m4 += d2 * d2;
    }
    m2 /= state->sample_count;
    m4 /= state->sample_count;
    out.kurtosis = m2 > 0.0 ? (float)(m4 / (m2 * m2)) : NAN;
    float p_5_15 = bandpower(state, sample_rate_hz, 5.0f, 15.0f, mean);
    float p_5_40 = bandpower(state, sample_rate_hz, 5.0f, 40.0f, mean);
    float p_0_1 = bandpower(state, sample_rate_hz, 0.0f, 1.0f, mean);
    float p_0_40 = bandpower(state, sample_rate_hz, 0.0f, 40.0f, mean);
    out.power_ratio = p_5_15 / p_5_40;
    out.baseline_sqi = 1.0f - p_0_1 / p_0_40;

    float rate = 1.0f;
    if (state->peak_count > 1U) {
        uint32_t minimum_rr = UINT32_MAX;
        for (uint8_t i = 1U; i < state->peak_count; ++i) {
            uint32_t rr = state->peak_ms[i] - state->peak_ms[i - 1U];
            if (rr < minimum_rr) minimum_rr = rr;
        }
        if (minimum_rr > 0U) rate = 60000.0f / minimum_rr;
    }
    float l1 = rate < 130.0f ? 0.5f : 0.4f;
    float l2 = rate < 130.0f ? 0.8f : 0.7f;
    float l3 = rate < 130.0f ? 0.4f : 0.3f;
    int pc = (out.power_ratio > l1 && out.power_ratio < l2) ? 2
           : ((out.power_ratio > l3 && out.power_ratio < l1) ? 1 : 0);
    int kc = out.kurtosis > 5.0f ? 2 : 0;
    int bc = out.baseline_sqi >= 0.95f ? 2 : (out.baseline_sqi < 0.9f ? 0 : 1);
    int optimal = (pc == 2) + (kc == 2) + (bc == 2);
    int suspicious = (pc == 1) + (kc == 1) + (bc == 1);
    int bad = (pc == 0) + (kc == 0) + (bc == 0);
    if (bad >= 2 || (bad == 1 && suspicious == 2)) {
        out.label = SQI_ZHAO_UNACCEPTABLE;
        out.score = 0.0f;
    } else if (optimal >= 2 && bad == 0) {
        out.label = SQI_ZHAO_EXCELLENT;
        out.score = 1.0f;
    } else {
        out.label = SQI_ZHAO_BARELY_ACCEPTABLE;
        out.score = 0.5f;
    }
    return out;
}

size_t sqi_zhao_static_bytes(void)
{
    return sizeof(sqi_zhao_state_t);
}
