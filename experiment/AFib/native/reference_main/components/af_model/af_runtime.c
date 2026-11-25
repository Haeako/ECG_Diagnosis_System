#include "af_runtime.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "af_model_int8.h"

#define AF_MIN_RR_MS 250U
#define AF_MAX_RR_MS 2500U
#define AF_STRIDE_RR 15U
#define AF_CONFIRM_HISTORY 4U
#define AF_CONFIRM_REQUIRED 3U

typedef struct {
    uint16_t rr[AF_RUNTIME_RR_COUNT];
    uint32_t rr_end_ms[AF_RUNTIME_RR_COUNT];
    uint8_t count;
    uint8_t head;
    uint8_t since_inference;
    uint32_t last_peak_ms;
    uint8_t decisions;
    uint8_t decision_count;
    uint16_t next_event_id;
    float last_features[AF_MODEL_FEATURE_COUNT];
    af_runtime_result_t latest;
} af_runtime_state_t;

static af_runtime_state_t state;

static void sort_float(float *values, size_t count)
{
    for (size_t i = 1; i < count; ++i) {
        float value = values[i];
        size_t j = i;
        while (j > 0 && values[j - 1] > value) {
            values[j] = values[j - 1];
            --j;
        }
        values[j] = value;
    }
}

static float sample_variance(const float *values, size_t count)
{
    if (count < 2U) return 0.0f;
    float mean = 0.0f;
    for (size_t i = 0; i < count; ++i) mean += values[i];
    mean /= (float)count;
    float sum = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        float delta = values[i] - mean;
        sum += delta * delta;
    }
    return sum / (float)(count - 1U);
}

static float shannon_entropy_5_bins(const float *rr)
{
    float minimum = rr[0], maximum = rr[0];
    for (size_t i = 1; i < AF_RUNTIME_RR_COUNT; ++i) {
        if (rr[i] < minimum) minimum = rr[i];
        if (rr[i] > maximum) maximum = rr[i];
    }
    if (maximum <= minimum) return 0.0f;
    uint8_t bins[5] = {0};
    float width = (maximum - minimum) / 5.0f;
    for (size_t i = 0; i < AF_RUNTIME_RR_COUNT; ++i) {
        int bin = (int)((rr[i] - minimum) / width);
        if (bin < 0) bin = 0;
        if (bin > 4) bin = 4;
        bins[bin]++;
    }
    float entropy = 0.0f;
    for (size_t i = 0; i < 5U; ++i) {
        if (bins[i] == 0U) continue;
        float probability = (float)bins[i] / (float)AF_RUNTIME_RR_COUNT;
        entropy -= probability * (logf(probability) / logf(2.0f));
    }
    return entropy;
}

static void collect_rr(float *rr, uint32_t *end_ms)
{
    for (size_t i = 0; i < AF_RUNTIME_RR_COUNT; ++i) {
        size_t slot = (state.head + i) % AF_RUNTIME_RR_COUNT;
        rr[i] = (float)state.rr[slot];
        end_ms[i] = state.rr_end_ms[slot];
    }
}

static af_runtime_result_t infer_window(void)
{
    float rr[AF_RUNTIME_RR_COUNT];
    float differences[AF_RUNTIME_RR_COUNT - 1U];
    float sorted[AF_RUNTIME_RR_COUNT];
    uint32_t end_ms[AF_RUNTIME_RR_COUNT];
    collect_rr(rr, end_ms);
    memcpy(sorted, rr, sizeof(sorted));
    sort_float(sorted, AF_RUNTIME_RR_COUNT);

    unsigned over_50 = 0U;
    for (size_t i = 0; i < AF_RUNTIME_RR_COUNT - 1U; ++i) {
        differences[i] = rr[i + 1U] - rr[i];
        if (fabsf(differences[i]) > 50.0f) over_50++;
    }
    float sdnn2 = sample_variance(rr, AF_RUNTIME_RR_COUNT);
    float sd1 = sqrtf(0.5f * sample_variance(
        differences, AF_RUNTIME_RR_COUNT - 1U));
    float sd2 = sqrtf(fmaxf(0.0f, 2.0f * sdnn2 - sd1 * sd1));
    float features[AF_MODEL_FEATURE_COUNT] = {
        (float)over_50 / (float)(AF_RUNTIME_RR_COUNT - 1U),
        shannon_entropy_5_bins(rr),
        sd2,
        sd2 > 0.0f ? sd1 / sd2 : 0.0f,
        0.5f * (sorted[14] + sorted[15]),
    };
    memcpy(state.last_features, features, sizeof(features));
    int8_t quantized[AF_MODEL_FEATURE_COUNT];
    af_model_quantize(features, quantized);
    af_model_result_t prediction = af_model_predict_int8(quantized);

    bool was_confirmed = state.latest.confirmed;
    bool raw = prediction.score >= AF_MODEL_SCORE_THRESHOLD;
    state.decisions = (uint8_t)(((state.decisions << 1U) | (raw ? 1U : 0U))
        & ((1U << AF_CONFIRM_HISTORY) - 1U));
    if (state.decision_count < AF_CONFIRM_HISTORY) state.decision_count++;
    unsigned positives = 0U;
    for (unsigned i = 0; i < state.decision_count; ++i) {
        positives += (state.decisions >> i) & 1U;
    }
    bool confirmed = state.decision_count == AF_CONFIRM_HISTORY
        && positives >= AF_CONFIRM_REQUIRED;
    bool new_event = confirmed && !was_confirmed;
    if (new_event) state.next_event_id++;

    af_runtime_result_t result = {
        .valid = true,
        .new_window = true,
        .raw_prediction = raw,
        .confirmed = confirmed,
        .new_event = new_event,
        .score = prediction.score,
        .probability_x10000 = (uint16_t)(((uint32_t)prediction.score * 10000U)
            / (AF_MODEL_TREE_COUNT * 255U)),
        .event_id = state.next_event_id,
        .window_id = state.latest.window_id + 1U,
        .window_start_ms = end_ms[0] - (uint32_t)rr[0],
        .window_end_ms = end_ms[AF_RUNTIME_RR_COUNT - 1U],
    };
    state.latest = result;
    return result;
}

void af_runtime_reset(void)
{
    memset(&state, 0, sizeof(state));
}

af_runtime_result_t af_runtime_latest(void)
{
    af_runtime_result_t result = state.latest;
    result.new_window = false;
    result.new_event = false;
    return result;
}

void af_runtime_last_features(float output[AF_MODEL_FEATURE_COUNT])
{
    if (output != NULL) memcpy(output, state.last_features,
                               sizeof(state.last_features));
}

af_runtime_result_t af_runtime_push_peak(uint32_t peak_timestamp_ms)
{
    if (peak_timestamp_ms == 0U) return af_runtime_latest();
    if (state.last_peak_ms == 0U) {
        state.last_peak_ms = peak_timestamp_ms;
        return af_runtime_latest();
    }
    uint32_t rr = peak_timestamp_ms - state.last_peak_ms;
    state.last_peak_ms = peak_timestamp_ms;
    if (rr < AF_MIN_RR_MS || rr > AF_MAX_RR_MS) {
        state.count = 0U;
        state.head = 0U;
        state.since_inference = 0U;
        state.decisions = 0U;
        state.decision_count = 0U;
        state.latest.valid = false;
        state.latest.new_window = false;
        state.latest.raw_prediction = false;
        state.latest.confirmed = false;
        state.latest.new_event = false;
        state.latest.score = 0U;
        state.latest.probability_x10000 = 0U;
        return af_runtime_latest();
    }

    size_t slot;
    if (state.count < AF_RUNTIME_RR_COUNT) {
        slot = (state.head + state.count) % AF_RUNTIME_RR_COUNT;
        state.count++;
    } else {
        slot = state.head;
        state.head = (uint8_t)((state.head + 1U) % AF_RUNTIME_RR_COUNT);
    }
    state.rr[slot] = (uint16_t)rr;
    state.rr_end_ms[slot] = peak_timestamp_ms;
    state.since_inference++;
    if (state.count == AF_RUNTIME_RR_COUNT
        && (state.latest.window_id == 0U
            || state.since_inference >= AF_STRIDE_RR)) {
        state.since_inference = 0U;
        return infer_window();
    }
    return af_runtime_latest();
}
