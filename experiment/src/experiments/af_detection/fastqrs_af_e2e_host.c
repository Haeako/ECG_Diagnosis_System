/* Experiment adapter. Production detector/model sources are unchanged. */
#include <stdint.h>
#include <string.h>
#include "fast_qrs_port.h"
#include "af_runtime.h"
#include "af_model_int8.h"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif

static fast_qrs_workspace_t workspace;

/* Each peak: acquisition availability time, physiological peak time (ms).
 * FastQRS uses forward/backward filtering, so ALL block peaks become available
 * only when that block has been acquired. No retrospective output backfill. */
API int fast_extract(const int32_t *adc, unsigned n, uint32_t *out,
                     unsigned capacity, uint32_t *counts)
{
    float input[FAST_QRS_MAX_SAMPLES];
    uint16_t found[FAST_QRS_MAX_PEAKS];
    unsigned total = 0, blocks = 0, short_tail = 0;
    uint32_t previous = 0;
    for (unsigned start = 0; start < n; start += FAST_QRS_MAX_SAMPLES) {
        unsigned count = n - start;
        if (count > FAST_QRS_MAX_SAMPLES) count = FAST_QRS_MAX_SAMPLES;
        if (count < 32) { short_tail = count; break; }
        for (unsigned i = 0; i < count; i++)
            input[i] = ((float)adc[start + i] - 2048.0f) / 200.0f;
        size_t nf = fast_qrs_detect_360(input, count, found,
                                      FAST_QRS_MAX_PEAKS, &workspace);
        if (nf >= FAST_QRS_MAX_PEAKS) return -1; /* No silent truncation. */
        blocks++;
        uint32_t available = (uint32_t)(((uint64_t)(start + count) * 1000 + 359) / 360);
        for (size_t j = 0; j < nf; j++) {
            if (total >= capacity) return -2;
            uint32_t peak = (uint32_t)(((uint64_t)(start + found[j]) * 1000 + 180) / 360);
            if ((total && peak <= previous) || peak > available) return -3;
            out[2 * total] = available;
            out[2 * total + 1] = peak;
            previous = peak;
            total++;
        }
    }
    counts[0] = total; counts[1] = blocks; counts[2] = short_tail;
    return 0;
}

/* Rows: available, peak, valid, new, raw, confirmed, score, probability_x10000,
 * window_id, window_start, window_end. AF state resets only once per record;
 * the production runtime itself handles invalid RR and confirmation resets. */
API void replay_peaks(const uint32_t *peaks, unsigned n, uint32_t *rows, float *features)
{
    af_runtime_reset();
    for (unsigned i = 0; i < n; i++) {
        af_runtime_result_t a = af_runtime_push_peak(peaks[2 * i + 1]);
        uint32_t r[11] = {peaks[2*i], peaks[2*i+1], a.valid, a.new_window,
                         a.raw_prediction, a.confirmed, a.score, a.probability_x10000,
                         a.window_id, a.window_start_ms, a.window_end_ms};
        memcpy(rows + 11*i, r, sizeof(r));
        memset(features + 5*i, 0, 5*sizeof(float));
        if (a.new_window) af_runtime_last_features(features + 5*i);
    }
}

API void predict_features(const float *features, unsigned n, uint16_t *scores)
{
    for (unsigned i = 0; i < n; i++) {
        int8_t q[AF_MODEL_FEATURE_COUNT];
        af_model_quantize(features + 5*i, q);
        scores[i] = af_model_predict_int8(q).score;
    }
}
