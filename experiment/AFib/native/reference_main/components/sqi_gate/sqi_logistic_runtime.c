#include "sqi_logistic_runtime.h"

#include <math.h>
#include <string.h>

#define PI_F 3.14159265358979323846f
#define MAX_TEMPLATE_WIDTH 161U

static void sort_float(float *values, uint32_t count)
{
    for (uint32_t i = 1U; i < count; ++i) {
        float value = values[i];
        uint32_t j = i;
        while (j > 0U && values[j - 1U] > value) {
            values[j] = values[j - 1U];
            --j;
        }
        values[j] = value;
    }
}

static float median(float *values, uint32_t count)
{
    if (count == 0U) return NAN;
    sort_float(values, count);
    return (count & 1U) ? values[count / 2U]
                        : 0.5f * (values[count / 2U - 1U] + values[count / 2U]);
}

void sqi_logistic_reset(sqi_logistic_runtime_t *state)
{
    if (state != NULL) memset(state, 0, sizeof(*state));
}

bool sqi_logistic_push(sqi_logistic_runtime_t *state, float sample)
{
    if (state == NULL || state->sample_count >= SQI_LOGISTIC_MAX_SAMPLES) {
        if (state != NULL) state->overflow = true;
        return false;
    }
    state->samples[state->sample_count++] = sample;
    return true;
}

static bool append_peak(uint16_t *peaks, uint8_t *count, bool *overflow,
                        uint16_t sample_index)
{
    if (*count >= SQI_LOGISTIC_MAX_PEAKS) {
        *overflow = true;
        return false;
    }
    if (*count > 0U && sample_index <= peaks[*count - 1U]) return false;
    peaks[(*count)++] = sample_index;
    return true;
}

bool sqi_logistic_push_primary(sqi_logistic_runtime_t *state, uint16_t sample_index)
{
    return state != NULL && append_peak(state->primary, &state->primary_count,
                                        &state->overflow, sample_index);
}

bool sqi_logistic_push_secondary(sqi_logistic_runtime_t *state, uint16_t sample_index)
{
    return state != NULL && append_peak(state->secondary, &state->secondary_count,
                                        &state->overflow, sample_index);
}

static float bsqi(const sqi_logistic_runtime_t *state, uint32_t fs)
{
    uint32_t tolerance = (uint32_t)lroundf(0.05f * fs);
    uint8_t i = 0U, j = 0U, matches = 0U;
    while (i < state->primary_count && j < state->secondary_count) {
        int delta = (int)state->primary[i] - (int)state->secondary[j];
        if (delta < -(int)tolerance) ++i;
        else if (delta > (int)tolerance) ++j;
        else { ++matches; ++i; ++j; }
    }
    unsigned total = state->primary_count + state->secondary_count;
    return total ? 2.0f * matches / total : NAN;
}

static float spectral_entropy(const sqi_logistic_runtime_t *state, uint32_t fs,
                              float global_mean)
{
    uint32_t nperseg = 2U * fs;
    if (nperseg > state->sample_count) nperseg = state->sample_count;
    uint32_t step = nperseg / 2U;
    if (nperseg < 4U || step == 0U) return NAN;
    int first = (int)ceilf(0.5f * nperseg / fs);
    int last = (int)floorf(40.0f * nperseg / fs);
    int nyquist = (int)(nperseg / 2U);
    if (last > nyquist) last = nyquist;
    if (last < first) return NAN;
    float powers[161];
    uint32_t bins = (uint32_t)(last - first + 1);
    if (bins > 161U) return NAN;
    float total = 0.0f;
    for (int k = first; k <= last; ++k) {
        float omega = -2.0f * PI_F * k / nperseg;
        float cs = cosf(omega), sn = sinf(omega);
        float womega = 2.0f * PI_F / nperseg;
        float wcs = cosf(womega), wsn = sinf(womega);
        float power = 0.0f;
        for (uint32_t start = 0U; start + nperseg <= state->sample_count; start += step) {
            double segment_sum = 0.0;
            for (uint32_t n = 0U; n < nperseg; ++n) segment_sum += state->samples[start + n] - global_mean;
            float segment_mean = (float)(segment_sum / nperseg);
            float zr = 1.0f, zi = 0.0f, wr = 1.0f, wi = 0.0f, re = 0.0f, im = 0.0f;
            for (uint32_t n = 0U; n < nperseg; ++n) {
                float value = (state->samples[start + n] - global_mean - segment_mean) * (0.5f - 0.5f * wr);
                re += value * zr;
                im += value * zi;
                float next = zr * cs - zi * sn;
                zi = zr * sn + zi * cs;
                zr = next;
                next = wr * wcs - wi * wsn;
                wi = wr * wsn + wi * wcs;
                wr = next;
            }
            power += re * re + im * im;
        }
        powers[k - first] = power;
        total += power;
    }
    if (!(total > 0.0f) || bins < 2U) return NAN;
    float entropy = 0.0f;
    for (uint32_t i = 0U; i < bins; ++i) {
        float p = powers[i] / total;
        if (p > 0.0f) entropy -= p * logf(p);
    }
    return entropy / logf((float)bins);
}

static float rr_outlier_fraction(const sqi_logistic_runtime_t *state, uint32_t fs)
{
    if (state->primary_count < 2U) return NAN;
    uint32_t count = state->primary_count - 1U;
    float rr[SQI_LOGISTIC_MAX_PEAKS - 1U], copy[SQI_LOGISTIC_MAX_PEAKS - 1U];
    for (uint32_t i = 0U; i < count; ++i) {
        rr[i] = (state->primary[i + 1U] - state->primary[i]) * 1000.0f / fs;
        copy[i] = rr[i];
    }
    float med = median(copy, count);
    uint32_t outliers = 0U;
    for (uint32_t i = 0U; i < count; ++i) {
        outliers += rr[i] < 300.0f || rr[i] > 2000.0f || rr[i] < 0.5f * med || rr[i] > 1.8f * med;
    }
    return (float)outliers / count;
}

static float template_correlation(const sqi_logistic_runtime_t *state, uint32_t fs)
{
    uint32_t before = (uint32_t)lroundf(0.12f * fs);
    uint32_t after = (uint32_t)lroundf(0.20f * fs);
    uint32_t width = before + after + 1U;
    if (width > MAX_TEMPLATE_WIDTH) return NAN;
    uint8_t valid[SQI_LOGISTIC_MAX_PEAKS], count = 0U;
    float means[SQI_LOGISTIC_MAX_PEAKS], norms[SQI_LOGISTIC_MAX_PEAKS];
    for (uint8_t p = 0U; p < state->primary_count; ++p) {
        uint32_t peak = state->primary[p];
        if (peak < before || peak + after >= state->sample_count) continue;
        double sum = 0.0;
        for (uint32_t j = peak - before; j <= peak + after; ++j) sum += state->samples[j];
        float mean = (float)(sum / width);
        double squared = 0.0;
        for (uint32_t j = peak - before; j <= peak + after; ++j) {
            double d = state->samples[j] - mean;
            squared += d * d;
        }
        float norm = sqrtf((float)squared);
        if (norm > 0.0f) { valid[count] = p; means[count] = mean; norms[count] = norm; ++count; }
    }
    if (count < 3U) return NAN;
    float templ[MAX_TEMPLATE_WIDTH], column[SQI_LOGISTIC_MAX_PEAKS];
    for (uint32_t offset = 0U; offset < width; ++offset) {
        for (uint8_t b = 0U; b < count; ++b) {
            uint32_t start = state->primary[valid[b]] - before;
            column[b] = (state->samples[start + offset] - means[b]) / norms[b];
        }
        templ[offset] = median(column, count);
    }
    double template_squared = 0.0;
    for (uint32_t j = 0U; j < width; ++j) template_squared += templ[j] * templ[j];
    float template_norm = sqrtf((float)template_squared);
    if (!(template_norm > 0.0f)) return NAN;
    double total = 0.0;
    for (uint8_t b = 0U; b < count; ++b) {
        uint32_t start = state->primary[valid[b]] - before;
        double dot = 0.0;
        for (uint32_t j = 0U; j < width; ++j) {
            dot += ((state->samples[start + j] - means[b]) / norms[b]) * (templ[j] / template_norm);
        }
        total += dot;
    }
    return (float)(total / count);
}

sqi_logistic_result_t sqi_logistic_finalize(const sqi_logistic_runtime_t *state,
                                            uint32_t fs)
{
    sqi_logistic_result_t out = {{NAN, NAN, NAN, NAN, NAN}, NAN, SQI_GATE_INSUFFICIENT};
    if (state == NULL || state->overflow || state->sample_count < 2U || fs == 0U) return out;
    double sum = 0.0, squared = 0.0, diff_squared = 0.0;
    for (uint32_t i = 0U; i < state->sample_count; ++i) sum += state->samples[i];
    float mean = (float)(sum / state->sample_count);
    for (uint32_t i = 0U; i < state->sample_count; ++i) {
        double d = state->samples[i] - mean;
        squared += d * d;
        if (i > 0U) { double delta = state->samples[i] - state->samples[i - 1U]; diff_squared += delta * delta; }
    }
    float rms = sqrtf((float)(squared / state->sample_count));
    out.features[0] = bsqi(state, fs);
    out.features[1] = spectral_entropy(state, fs, mean);
    out.features[2] = rms > 0.0f ? sqrtf((float)(diff_squared / (state->sample_count - 1U))) / rms : NAN;
    out.features[3] = template_correlation(state, fs);
    out.features[4] = rr_outlier_fraction(state, fs);
    out.probability = sqi_gate_probability(out.features);
    out.decision = sqi_gate_predict(out.features);
    return out;
}

size_t sqi_logistic_static_bytes(void)
{
    return sizeof(sqi_logistic_runtime_t);
}
