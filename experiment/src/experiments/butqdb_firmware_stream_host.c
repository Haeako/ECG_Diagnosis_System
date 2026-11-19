/* Stateful observer for the unchanged production QRS and SQI sources. */
#include <stdint.h>
#include <string.h>
#include "components/bpm/ExtractFeature.h"
#include "components/sqi_gate/sqi_runtime.h"

#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif

static uint64_t sample_index;
static uint32_t last_window;

static void save_quality(int32_t *out, uint32_t now, ecg_sqi_result_t q)
{
    int32_t row[12] = {(int32_t)(now - 10000U), (int32_t)now,
        (int32_t)now, q.status, q.score, (int32_t)q.window_id,
        q.rule_mask, q.heart_rate_bpm, q.max_rr_ms, q.rr_ratio_x1000,
        q.template_correlation_x1000, 0};
    memcpy(out, row, sizeof(row));
}

API void butqdb_stream_reset(void)
{
    sample_index = 0U;
    last_window = 0U;
    ecg_signal_init(0U);
    sqi_runtime_reset(0U);
}

/* Quality rows: start_ms,end_ms,availability_ms,status,score,window_id,
 * rule_mask,HR,maxRR,RRratio_x1000,templateCC_x1000,EOF_flush.
 * Peak rows: processing_ms,aligned_peak_ms. */
API int butqdb_stream_step(const int32_t *adc, uint32_t n,
                          int32_t *quality, uint32_t quality_capacity,
                          uint32_t *peaks, uint32_t peak_capacity,
                          uint32_t *counts)
{
    uint32_t nq = 0U, np = 0U;
    for (uint32_t i = 0U; i < n; ++i, ++sample_index) {
        uint32_t now = (uint32_t)((sample_index * 1000U + 180U) / 360U);
        ecg_record_t r = ecg_signal_process(adc[i], now);
        ecg_sqi_result_t q = sqi_runtime_push(r.corrected, r.is_peak != 0U,
                                             r.r_peak_timestamp_ms, now);
        if (r.is_peak) {
            if (np >= peak_capacity) return -1;
            peaks[2U*np] = now;
            peaks[2U*np+1U] = r.r_peak_timestamp_ms;
            ++np;
        }
        if (q.window_id != last_window) {
            if (nq >= quality_capacity) return -2;
            save_quality(quality + 12U*nq, now, q);
            last_window = q.window_id;
            ++nq;
        }
    }
    counts[0] = nq;
    counts[1] = np;
    return 0;
}

/* Native evaluation precedes append. The unused argument is appended only
 * after the final real window has been evaluated. No fake QRS sample runs. */
API int butqdb_stream_eof(int32_t *quality)
{
    if (sample_index % 360U != 0U || sample_index < 3600U) return 0;
    uint32_t now = (uint32_t)(sample_index * 1000U / 360U);
    ecg_sqi_result_t q = sqi_runtime_push(0, 0, 0U, now);
    if (q.window_id == last_window) return 0;
    save_quality(quality, now, q);
    quality[11] = 1;
    last_window = q.window_id;
    return 1;
}
