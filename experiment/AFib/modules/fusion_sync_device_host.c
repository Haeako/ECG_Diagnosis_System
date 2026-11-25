/* Observe the current production C path without modifying firmware state.
 * The RR counter below is diagnostic only; af_runtime.c owns all decisions.
 */
#include <stdint.h>
#include <string.h>
#include "components/bpm/ExtractFeature.h"
#include "components/af_model/af_runtime.h"
#include "components/sqi_gate/sqi_runtime.h"

#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif

/* Peak columns: processing_ms, peak_ms, valid, new_window, raw, confirmed,
 * score, probability_x10000, window_id, start_ms, end_ms, rr_count,
 * event_id, new_event, last_inference_ms, rr_ms.
 * SQI columns: processing_ms, status, score, window_id, rule_mask,
 * heart_rate_bpm, max_rr_ms, rr_ratio_x1000, template_correlation_x1000.
 * Both outputs have explicit caller-supplied capacities.
 */
API int fusion_sync_replay(const int32_t *adc, uint32_t n,
                          int16_t *corrected, uint16_t *bpm,
                          uint32_t *peaks, float *features,
                          uint32_t peak_capacity, int32_t *sqi,
                          uint32_t sqi_capacity, uint32_t *counts)
{
    uint32_t np = 0U, nq = 0U, rr_count = 0U, last_peak = 0U;
    uint32_t last_inference = UINT32_MAX, last_sqi_window = UINT32_MAX;
    ecg_signal_init(0U);
    af_runtime_reset();
    sqi_runtime_reset(0U);
    for (uint32_t i = 0U; i < n; ++i) {
        uint32_t t = (uint32_t)(((uint64_t)i * 1000U + 180U) / 360U);
        ecg_record_t r = ecg_signal_process(adc[i], t);
        corrected[i] = r.corrected;
        bpm[i] = r.bpm;
        af_runtime_result_t a = r.is_peak
            ? af_runtime_push_peak(r.r_peak_timestamp_ms)
            : af_runtime_latest();
        ecg_sqi_result_t q = sqi_runtime_push(r.corrected, r.is_peak != 0U,
                                             r.r_peak_timestamp_ms, t);
        if (r.is_peak) {
            uint32_t rr = 0U;
            if (r.r_peak_timestamp_ms != 0U) {
                if (last_peak != 0U) {
                    rr = r.r_peak_timestamp_ms - last_peak;
                    if (rr < 250U || rr > 2500U) rr_count = 0U;
                    else if (rr_count < AF_RUNTIME_RR_COUNT) ++rr_count;
                }
                last_peak = r.r_peak_timestamp_ms;
            }
            if (a.new_window) last_inference = t;
            if (!a.valid) last_inference = UINT32_MAX;
            if (np >= peak_capacity) return -1;
            uint32_t row[16] = {t, r.r_peak_timestamp_ms, a.valid,
                a.new_window, a.raw_prediction, a.confirmed, a.score,
                a.probability_x10000, a.window_id, a.window_start_ms,
                a.window_end_ms, rr_count, a.event_id, a.new_event,
                last_inference, rr};
            memcpy(peaks + 16U * np, row, sizeof(row));
            memset(features + 5U * np, 0, 5U * sizeof(float));
            if (a.new_window) af_runtime_last_features(features + 5U * np);
            ++np;
        }
        if (q.window_id != last_sqi_window) {
            if (nq >= sqi_capacity) return -2;
            int32_t row[9] = {(int32_t)t, q.status, q.score,
                (int32_t)q.window_id, q.rule_mask, q.heart_rate_bpm,
                q.max_rr_ms, q.rr_ratio_x1000,
                q.template_correlation_x1000};
            memcpy(sqi + 9U * nq, row, sizeof(row));
            last_sqi_window = q.window_id;
            ++nq;
        }
    }
    counts[0] = np;
    counts[1] = nq;
    return 0;
}
