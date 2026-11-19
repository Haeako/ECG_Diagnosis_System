/* Firmware-oriented AdaptivePTPortable: int16 input/history, float32 DSP. */
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef _WIN32
#define ADAPTIVE_PT_EXPORT __declspec(dllexport)
#else
#define ADAPTIVE_PT_EXPORT
#endif

#define SOS_COUNT 2
#define INTEGRATION_SAMPLES 40
#define REFRACTORY_SAMPLES 94
#define RELOCATION_SAMPLES 36
#define HISTORY_SAMPLES 40
#define WARMUP_SAMPLES 360
#define SETTLE_SAMPLES 90
#define THRESHOLD_FRACTION 0.35f

typedef struct {
    float envelope;
    float signal_level;
    float noise_level;
    int32_t event_index;
} adaptive_pt_int16_trace_t;

typedef struct {
    float zi[SOS_COUNT][2];
    float energy_ring[INTEGRATION_SAMPLES];
    int16_t raw_ring[HISTORY_SAMPLES];
    uint16_t energy_pos;
    uint16_t history_pos;
    float energy_sum;
    float previous_filtered;
    float env_2;
    float env_1;
    int32_t sample_index;
    float signal_level;
    float noise_level;
    float warmup_peak_max;
    float warmup_peak_mean;
    uint32_t warmup_peak_count;
    int32_t last_peak;
    uint8_t levels_initialized;
} adaptive_pt_int16_t;

/* Immutable coefficients remain in flash instead of every detector instance. */
static const float sos[SOS_COUNT][6] = {
    {0.02428136794066703f, 0.04856273588133406f,
     0.02428136794066703f, 1.0f,
     -1.5514877493163206f, 0.6754954803496371f},
    {1.0f, -2.0f, 1.0f, 1.0f,
     -1.8945312242304368f, 0.9037917690022873f},
};

static void detector_init(adaptive_pt_int16_t *detector)
{
    memset(detector, 0, sizeof(*detector));
    detector->sample_index = -1;
    detector->last_peak = -10 * REFRACTORY_SAMPLES;
}

static float biquad_push(adaptive_pt_int16_t *detector, float value)
{
    for (uint16_t section = 0; section < SOS_COUNT; ++section) {
        float *state = detector->zi[section];
        float output = sos[section][0] * value + state[0];
        state[0] = sos[section][1] * value
            - sos[section][4] * output + state[1];
        state[1] = sos[section][2] * value - sos[section][5] * output;
        value = output;
    }
    return value;
}

static int32_t relocate_peak(const adaptive_pt_int16_t *detector,
                             int32_t candidate)
{
    int32_t start = candidate > RELOCATION_SAMPLES
        ? candidate - RELOCATION_SAMPLES : 0;
    int32_t local_sum = 0;
    uint16_t local_count = 0;
    for (int32_t index = start; index <= candidate; ++index) {
        int32_t age = detector->sample_index - index;
        uint16_t slot = (uint16_t)((detector->history_pos + HISTORY_SAMPLES
            - 1U - (uint16_t)age) % HISTORY_SAMPLES);
        local_sum += detector->raw_ring[slot];
        ++local_count;
    }
    float local_mean = local_count ? (float)local_sum / local_count : 0.0f;
    int32_t best_index = candidate;
    float best_value = -1.0f;
    for (int32_t index = start; index <= candidate; ++index) {
        int32_t age = detector->sample_index - index;
        uint16_t slot = (uint16_t)((detector->history_pos + HISTORY_SAMPLES
            - 1U - (uint16_t)age) % HISTORY_SAMPLES);
        float magnitude = fabsf((float)detector->raw_ring[slot] - local_mean);
        if (magnitude > best_value) {
            best_value = magnitude;
            best_index = index;
        }
    }
    return best_index;
}

static int32_t detector_push(adaptive_pt_int16_t *detector, int16_t raw,
                             adaptive_pt_int16_trace_t *trace)
{
    detector->sample_index++;
    float filtered = biquad_push(detector, (float)raw);
    detector->raw_ring[detector->history_pos] = raw;
    detector->history_pos = (uint16_t)((detector->history_pos + 1U)
                                       % HISTORY_SAMPLES);

    float derivative = filtered - detector->previous_filtered;
    detector->previous_filtered = filtered;
    float energy = derivative * derivative;
    detector->energy_sum -= detector->energy_ring[detector->energy_pos];
    detector->energy_ring[detector->energy_pos] = energy;
    detector->energy_sum += energy;
    detector->energy_pos = (uint16_t)((detector->energy_pos + 1U)
                                      % INTEGRATION_SAMPLES);
    float envelope = detector->energy_sum / INTEGRATION_SAMPLES;

    int32_t event = -1;
    int32_t candidate_index = detector->sample_index - 1;
    float candidate = detector->env_1;
    int is_local_max = detector->env_1 > detector->env_2
        && detector->env_1 >= envelope;
    detector->env_2 = detector->env_1;
    detector->env_1 = envelope;

    if (is_local_max) {
        if (candidate_index < WARMUP_SAMPLES) {
            if (candidate_index >= SETTLE_SAMPLES) {
                detector->warmup_peak_count++;
                detector->warmup_peak_max = fmaxf(detector->warmup_peak_max,
                                                   candidate);
                detector->warmup_peak_mean +=
                    (candidate - detector->warmup_peak_mean)
                    / detector->warmup_peak_count;
            }
        } else {
            if (!detector->levels_initialized) {
                detector->signal_level = fmaxf(detector->warmup_peak_max,
                                                candidate);
                detector->noise_level = fminf(detector->warmup_peak_mean,
                                              0.5f * detector->signal_level);
                detector->levels_initialized = 1U;
            }
            float threshold = detector->noise_level + THRESHOLD_FRACTION
                * (detector->signal_level - detector->noise_level);
            int32_t relocated = relocate_peak(detector, candidate_index);
            int outside_refractory =
                relocated - detector->last_peak >= REFRACTORY_SAMPLES;
            if (candidate >= threshold && outside_refractory) {
                float learned = detector->signal_level > 0.0f
                    ? fminf(candidate, 2.0f * detector->signal_level) : candidate;
                detector->signal_level = 0.125f * learned
                    + 0.875f * detector->signal_level;
                detector->last_peak = relocated;
                event = relocated;
            } else if (outside_refractory) {
                detector->noise_level = 0.125f * candidate
                    + 0.875f * detector->noise_level;
            }
        }
    }

    if (trace != NULL) {
        trace->envelope = envelope;
        trace->signal_level = detector->signal_level;
        trace->noise_level = detector->noise_level;
        trace->event_index = event;
    }
    return event;
}

ADAPTIVE_PT_EXPORT uint32_t adaptive_pt_run_int16(
    const int16_t *samples, uint32_t sample_count, uint32_t *peaks,
    adaptive_pt_int16_trace_t *trace)
{
    if (samples == NULL || peaks == NULL) return 0;
    adaptive_pt_int16_t detector;
    detector_init(&detector);
    uint32_t peak_count = 0;
    for (uint32_t i = 0; i < sample_count; ++i) {
        adaptive_pt_int16_trace_t *row = trace == NULL ? NULL : &trace[i];
        int32_t event = detector_push(&detector, samples[i], row);
        if (event >= 0) peaks[peak_count++] = (uint32_t)event;
    }
    return peak_count;
}

ADAPTIVE_PT_EXPORT uint32_t adaptive_pt_int16_state_bytes(void)
{
    return (uint32_t)sizeof(adaptive_pt_int16_t);
}
