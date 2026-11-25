/* Host adapter for the unchanged production RR AF runtime and INT8 forest.
 * This file does not extract peaks or fit/calibrate any model.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "af_runtime.h"
#include "af_model_int8.h"

#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif

/* One output row per supplied peak: peak_ms, valid, new_window, raw,
 * confirmed, new_event, score, probability_x10000, event_id, window_id,
 * source_start_ms, source_end_ms, model_peak_index, rr_ms, rr_count.
 * model_peak_index is UINT32_MAX while no AF result is valid. The caller
 * maps this index to the completed SWT block's processing availability.
 */
API void server_rr_af_replay(const uint32_t *peaks_ms, uint32_t count,
                             uint32_t *rows, float *features,
                             int8_t *quantized)
{
    uint32_t last_peak = 0U, rr_count = 0U, model_peak_index = UINT32_MAX;
    af_runtime_reset();
    for (uint32_t i = 0U; i < count; ++i) {
        uint32_t peak = peaks_ms[i], rr = 0U;
        if (peak != 0U) {
            if (last_peak != 0U) {
                rr = peak - last_peak;
                if (rr < 250U || rr > 2500U) rr_count = 0U;
                else if (rr_count < AF_RUNTIME_RR_COUNT) ++rr_count;
            }
            last_peak = peak;
        }
        af_runtime_result_t result = af_runtime_push_peak(peak);
        if (result.new_window) model_peak_index = i;
        if (!result.valid) model_peak_index = UINT32_MAX;
        uint32_t row[15] = {peak, result.valid, result.new_window,
            result.raw_prediction, result.confirmed, result.new_event,
            result.score, result.probability_x10000, result.event_id,
            result.window_id, result.window_start_ms, result.window_end_ms,
            model_peak_index, rr, rr_count};
        memcpy(rows + 15U * i, row, sizeof(row));
        memset(features + AF_MODEL_FEATURE_COUNT * i, 0,
               AF_MODEL_FEATURE_COUNT * sizeof(float));
        memset(quantized + AF_MODEL_FEATURE_COUNT * i, 0,
               AF_MODEL_FEATURE_COUNT * sizeof(int8_t));
        if (result.new_window) {
            af_runtime_last_features(features + AF_MODEL_FEATURE_COUNT * i);
            af_model_quantize(features + AF_MODEL_FEATURE_COUNT * i,
                              quantized + AF_MODEL_FEATURE_COUNT * i);
        }
    }
}

/* Direct classifier calls use the production float32 quantizer, signed
 * integer tree comparisons and uint8 leaf scores. Runtime state is untouched.
 */
API void server_rr_af_predict_features(const float *features, uint32_t count,
                                       int8_t *quantized, uint16_t *scores,
                                       uint16_t *comparisons)
{
    for (uint32_t i = 0U; i < count; ++i) {
        af_model_quantize(features + AF_MODEL_FEATURE_COUNT * i,
                          quantized + AF_MODEL_FEATURE_COUNT * i);
        af_model_result_t result = af_model_predict_int8(
            quantized + AF_MODEL_FEATURE_COUNT * i);
        scores[i] = result.score;
        comparisons[i] = result.comparisons;
    }
}
