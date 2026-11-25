#include "sqi_ho.h"

#include <string.h>

static size_t select_distinct_cycles(const float *signal,
                                  const uint32_t *input, size_t input_count,
                                  uint32_t tolerance,
                                  uint32_t *output, size_t output_capacity)
{
    if (input_count == 0U) {
        return 0U;
    }
    if (input_count > output_capacity) {
        return SIZE_MAX;
    }

    size_t kept = 1U;
    output[0] = input[0];
    for (size_t i = 1U; i < input_count; ++i) {
        const uint32_t current = input[i];
        const uint32_t previous = output[kept - 1U];
        if (current > previous && current - previous > tolerance) {
            output[kept++] = current;
        } else if (current >= previous &&
                   current - previous <= tolerance &&
                   signal[current] > signal[previous]) {
            output[kept - 1U] = current;
        }
    }
    return kept;
}

static int peak_matches(uint32_t target, const uint32_t *peaks,
                        size_t count, uint32_t tolerance)
{
    for (size_t i = 0U; i < count; ++i) {
        const uint32_t peak = peaks[i];
        const uint32_t difference = target >= peak ? target - peak : peak - target;
        if (difference <= tolerance) {
            return 1;
        }
    }
    return 0;
}

int sqi_ho_ici_quality(const float *signal, size_t sample_count,
                       const uint32_t *primary_peaks, size_t primary_count,
                       const uint32_t *secondary_peaks, size_t secondary_count,
                       uint32_t sampling_rate, uint32_t tolerance_ms,
                       uint32_t *primary_scratch, size_t primary_capacity,
                       uint32_t *secondary_scratch, size_t secondary_capacity,
                       uint8_t *quality)
{
    if (signal == NULL || quality == NULL || sampling_rate == 0U ||
        (primary_count > 0U && (primary_peaks == NULL || primary_scratch == NULL)) ||
        (secondary_count > 0U && (secondary_peaks == NULL || secondary_scratch == NULL))) {
        return 0;
    }
    memset(quality, 0, sample_count * sizeof(*quality));

    for (size_t i = 0U; i < primary_count; ++i) {
        if (primary_peaks[i] >= sample_count ||
            (i > 0U && primary_peaks[i] < primary_peaks[i - 1U])) {
            return 0;
        }
    }
    for (size_t i = 0U; i < secondary_count; ++i) {
        if (secondary_peaks[i] >= sample_count ||
            (i > 0U && secondary_peaks[i] < secondary_peaks[i - 1U])) {
            return 0;
        }
    }

    const uint64_t tolerance_wide = (uint64_t)tolerance_ms * sampling_rate / 1000U;
    if (tolerance_wide > UINT32_MAX) {
        return 0;
    }
    const uint32_t tolerance = (uint32_t)tolerance_wide;
    const size_t primary_filtered_count = select_distinct_cycles(
        signal, primary_peaks, primary_count, tolerance,
        primary_scratch, primary_capacity);
    const size_t secondary_filtered_count = select_distinct_cycles(
        signal, secondary_peaks, secondary_count, tolerance,
        secondary_scratch, secondary_capacity);
    if (primary_filtered_count == SIZE_MAX || secondary_filtered_count == SIZE_MAX) {
        return 0;
    }

    for (size_t i = 0U; i + 1U < primary_filtered_count; ++i) {
        const uint32_t start = primary_scratch[i];
        const uint32_t end = primary_scratch[i + 1U];
        const int match_start = peak_matches(start, secondary_scratch,
                                             secondary_filtered_count, tolerance);
        const int match_end = peak_matches(end, secondary_scratch,
                                           secondary_filtered_count, tolerance);
        int extra_cycle = 0;
        if ((uint64_t)start + tolerance < end &&
            (uint64_t)start + 2U * tolerance < end) {
            const uint64_t lower = (uint64_t)start + tolerance;
            const uint64_t upper = (uint64_t)end - tolerance;
            for (size_t j = 0U; j < secondary_filtered_count; ++j) {
                if ((uint64_t)secondary_scratch[j] > lower &&
                    (uint64_t)secondary_scratch[j] < upper) {
                    extra_cycle = 1;
                    break;
                }
            }
        }

        if (match_start && match_end && !extra_cycle) {
            memset(&quality[start], 1, (size_t)(end - start));
            if (i == 0U) {
                memset(quality, 1, start);
            }
            if (i + 2U == primary_filtered_count) {
                memset(&quality[end], 1, sample_count - end);
            }
        }
    }
    return 1;
}
