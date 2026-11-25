#include "sqi_template_matching.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define SQI_MIN_BEATS             3U
#define SQI_MAX_BEAT_SAMPLES      1080U

static float beat_template[SQI_MAX_BEAT_SAMPLES];

static bool beat_bounds(uint32_t peak_ms,
                        uint32_t window_start_ms,
                        uint32_t sample_rate_hz,
                        uint32_t beat_samples,
                        uint16_t sample_count,
                        uint32_t *start)
{
    uint32_t relative_ms = peak_ms - window_start_ms;
    uint32_t peak_index = (relative_ms * sample_rate_hz + 500U) / 1000U;
    uint32_t left_samples = beat_samples / 2U;
    uint32_t right_samples = beat_samples - left_samples;
    if (peak_index < left_samples
        || peak_index + right_samples > sample_count) {
        return false;
    }
    *start = peak_index - left_samples;
    return true;
}

float sqi_template_average_correlation(const int16_t *samples,
                                       uint16_t sample_count,
                                       uint32_t sample_rate_hz,
                                       const uint32_t *peak_ms,
                                       uint8_t peak_count,
                                       uint32_t window_start_ms,
                                       uint32_t median_rr_ms)
{
    if (samples == NULL || peak_ms == NULL || sample_rate_hz == 0U) return NAN;

    uint32_t beat_samples = (median_rr_ms * sample_rate_hz + 500U) / 1000U;
    if (beat_samples < 2U || beat_samples > SQI_MAX_BEAT_SAMPLES) return NAN;
    memset(beat_template, 0, beat_samples * sizeof(beat_template[0]));

    uint8_t valid_beats = 0U;
    for (uint8_t beat = 0U; beat < peak_count; ++beat) {
        uint32_t start;
        if (!beat_bounds(peak_ms[beat], window_start_ms, sample_rate_hz,
                         beat_samples, sample_count, &start)) {
            continue;
        }
        for (uint32_t i = 0U; i < beat_samples; ++i) {
            beat_template[i] += (float)samples[start + i];
        }
        ++valid_beats;
    }
    if (valid_beats < SQI_MIN_BEATS) return NAN;

    for (uint32_t i = 0U; i < beat_samples; ++i) {
        beat_template[i] /= (float)valid_beats;
    }

    float correlation_sum = 0.0f;
    uint8_t correlation_count = 0U;
    for (uint8_t beat = 0U; beat < peak_count; ++beat) {
        uint32_t start;
        if (!beat_bounds(peak_ms[beat], window_start_ms, sample_rate_hz,
                         beat_samples, sample_count, &start)) {
            continue;
        }

        double beat_sum = 0.0;
        double template_sum = 0.0;
        for (uint32_t i = 0U; i < beat_samples; ++i) {
            beat_sum += (double)samples[start + i];
            template_sum += (double)beat_template[i];
        }
        double beat_mean = beat_sum / (double)beat_samples;
        double template_mean = template_sum / (double)beat_samples;
        double covariance = 0.0;
        double beat_energy = 0.0;
        double template_energy = 0.0;
        for (uint32_t i = 0U; i < beat_samples; ++i) {
            double x = (double)samples[start + i] - beat_mean;
            double y = (double)beat_template[i] - template_mean;
            covariance += x * y;
            beat_energy += x * x;
            template_energy += y * y;
        }
        double denominator = sqrt(beat_energy * template_energy);
        if (denominator <= 0.0) return NAN;
        correlation_sum += (float)(covariance / denominator);
        ++correlation_count;
    }
    if (correlation_count < SQI_MIN_BEATS) return NAN;
    return correlation_sum / (float)correlation_count;
}
