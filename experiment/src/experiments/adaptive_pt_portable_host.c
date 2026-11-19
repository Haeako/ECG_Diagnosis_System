/* Host reference port of AdaptivePTPortable at the firmware rate (360 Hz).
 *
 * This file uses double precision deliberately: its job is algorithmic parity
 * with the Python reference before a separate float32 ESP32 optimization.
 */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FS_HZ 360
#define SOS_COUNT 2
#define INTEGRATION_SAMPLES 40
#define REFRACTORY_SAMPLES 94
#define RELOCATION_SAMPLES 36
#define HISTORY_SAMPLES 40
#define WARMUP_SAMPLES 360
#define SETTLE_SAMPLES 90
#define THRESHOLD_FRACTION 0.35

typedef struct {
    double filtered;
    double envelope;
    double threshold;
    double signal_level;
    double noise_level;
    int64_t event_index;
} adaptive_pt_trace_t;

typedef struct {
    double sos[SOS_COUNT][6];
    double zi[SOS_COUNT][2];
    double energy_ring[INTEGRATION_SAMPLES];
    double raw_ring[HISTORY_SAMPLES];
    size_t energy_pos;
    size_t history_pos;
    double energy_sum;
    double previous_filtered;
    double env_2;
    double env_1;
    int64_t sample_index;
    double signal_level;
    double noise_level;
    int levels_initialized;
    double warmup_peak_max;
    double warmup_peak_mean;
    uint32_t warmup_peak_count;
    int64_t last_peak;
} adaptive_pt_t;

static void adaptive_pt_init(adaptive_pt_t *detector)
{
    memset(detector, 0, sizeof(*detector));
    const double coefficients[SOS_COUNT][6] = {
        {0.02428136794066703, 0.04856273588133406,
         0.02428136794066703, 1.0,
         -1.5514877493163206, 0.6754954803496371},
        {1.0, -2.0, 1.0, 1.0,
         -1.8945312242304368, 0.9037917690022873},
    };
    memcpy(detector->sos, coefficients, sizeof(coefficients));
    detector->sample_index = -1;
    detector->last_peak = -10 * REFRACTORY_SAMPLES;
}

static double biquad_push(adaptive_pt_t *detector, double value)
{
    for (size_t section = 0; section < SOS_COUNT; ++section) {
        const double *coefficient = detector->sos[section];
        double *state = detector->zi[section];
        double output = coefficient[0] * value + state[0];
        state[0] = coefficient[1] * value
            - coefficient[4] * output + state[1];
        state[1] = coefficient[2] * value - coefficient[5] * output;
        value = output;
    }
    return value;
}

static int64_t relocate_peak(const adaptive_pt_t *detector, int64_t candidate)
{
    int64_t start = candidate > RELOCATION_SAMPLES
        ? candidate - RELOCATION_SAMPLES : 0;
    double local_sum = 0.0;
    size_t local_count = 0;
    for (int64_t index = start; index <= candidate; ++index) {
        int64_t age = detector->sample_index - index;
        size_t slot = (detector->history_pos + HISTORY_SAMPLES - 1
                       - (size_t)age) % HISTORY_SAMPLES;
        local_sum += detector->raw_ring[slot];
        ++local_count;
    }
    double local_mean = local_count ? local_sum / (double)local_count : 0.0;
    int64_t best_index = candidate;
    double best_value = -1.0;
    for (int64_t index = start; index <= candidate; ++index) {
        int64_t age = detector->sample_index - index;
        size_t slot = (detector->history_pos + HISTORY_SAMPLES - 1
                       - (size_t)age) % HISTORY_SAMPLES;
        double magnitude = fabs(detector->raw_ring[slot] - local_mean);
        if (magnitude > best_value) {
            best_value = magnitude;
            best_index = index;
        }
    }
    return best_index;
}

static int64_t adaptive_pt_push(adaptive_pt_t *detector, double raw,
                                adaptive_pt_trace_t *trace)
{
    detector->sample_index++;
    double filtered = biquad_push(detector, raw);
    detector->raw_ring[detector->history_pos] = raw;
    detector->history_pos = (detector->history_pos + 1) % HISTORY_SAMPLES;

    double derivative = filtered - detector->previous_filtered;
    detector->previous_filtered = filtered;
    double energy = derivative * derivative;
    detector->energy_sum -= detector->energy_ring[detector->energy_pos];
    detector->energy_ring[detector->energy_pos] = energy;
    detector->energy_sum += energy;
    detector->energy_pos = (detector->energy_pos + 1) % INTEGRATION_SAMPLES;
    double envelope = detector->energy_sum / INTEGRATION_SAMPLES;

    int64_t event = -1;
    int64_t candidate_index = detector->sample_index - 1;
    double candidate = detector->env_1;
    int is_local_max = detector->env_1 > detector->env_2
        && detector->env_1 >= envelope;
    detector->env_2 = detector->env_1;
    detector->env_1 = envelope;

    double threshold = detector->levels_initialized
        ? detector->noise_level + THRESHOLD_FRACTION
            * (detector->signal_level - detector->noise_level)
        : 0.0;
    if (is_local_max) {
        if (candidate_index < WARMUP_SAMPLES) {
            if (candidate_index >= SETTLE_SAMPLES) {
                detector->warmup_peak_count++;
                if (candidate > detector->warmup_peak_max) {
                    detector->warmup_peak_max = candidate;
                }
                detector->warmup_peak_mean +=
                    (candidate - detector->warmup_peak_mean)
                    / detector->warmup_peak_count;
            }
        } else {
            if (!detector->levels_initialized) {
                detector->signal_level = fmax(detector->warmup_peak_max,
                                               candidate);
                detector->noise_level = fmin(detector->warmup_peak_mean,
                                              0.5 * detector->signal_level);
                detector->levels_initialized = 1;
            }
            threshold = detector->noise_level + THRESHOLD_FRACTION
                * (detector->signal_level - detector->noise_level);
            int64_t relocated = relocate_peak(detector, candidate_index);
            int outside_refractory =
                relocated - detector->last_peak >= REFRACTORY_SAMPLES;
            if (candidate >= threshold && outside_refractory) {
                double learned = detector->signal_level > 0.0
                    ? fmin(candidate, 2.0 * detector->signal_level) : candidate;
                detector->signal_level = 0.125 * learned
                    + 0.875 * detector->signal_level;
                detector->last_peak = relocated;
                event = relocated;
            } else if (outside_refractory) {
                detector->noise_level = 0.125 * candidate
                    + 0.875 * detector->noise_level;
            }
        }
    }

    if (trace != NULL) {
        trace->filtered = filtered;
        trace->envelope = envelope;
        trace->threshold = threshold;
        trace->signal_level = detector->signal_level;
        trace->noise_level = detector->noise_level;
        trace->event_index = event;
    }
    return event;
}

__declspec(dllexport) uint32_t adaptive_pt_run(
    const double *samples, uint32_t sample_count, uint32_t *peaks,
    adaptive_pt_trace_t *trace)
{
    if (samples == NULL || peaks == NULL) return 0;
    adaptive_pt_t detector;
    adaptive_pt_init(&detector);
    uint32_t peak_count = 0;
    for (uint32_t i = 0; i < sample_count; ++i) {
        adaptive_pt_trace_t *row = trace == NULL ? NULL : &trace[i];
        int64_t event = adaptive_pt_push(&detector, samples[i], row);
        if (event >= 0) peaks[peak_count++] = (uint32_t)event;
    }
    return peak_count;
}

__declspec(dllexport) uint32_t adaptive_pt_state_bytes(void)
{
    return (uint32_t)sizeof(adaptive_pt_t);
}
