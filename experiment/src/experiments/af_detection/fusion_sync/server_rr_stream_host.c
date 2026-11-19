/* Incremental benchmark adapter; production AF runtime/model stay unchanged. */
#include <stdint.h>
#include <string.h>
#include "af_runtime.h"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
API void latency_rr_reset(void) { af_runtime_reset(); }
API void latency_rr_step(const uint32_t *peaks, uint32_t count,
                         uint32_t *row, float *features)
{
    for (uint32_t i = 0; i < count; ++i) af_runtime_push_peak(peaks[i]);
    af_runtime_result_t r = af_runtime_latest();
    uint32_t values[10] = {r.valid, r.raw_prediction, r.confirmed, r.score,
        r.probability_x10000, r.window_id, r.window_start_ms, r.window_end_ms,
        r.event_id, r.new_event};
    memcpy(row, values, sizeof(values));
    memset(features, 0, 5 * sizeof(float));
    if (r.valid) af_runtime_last_features(features);
}
