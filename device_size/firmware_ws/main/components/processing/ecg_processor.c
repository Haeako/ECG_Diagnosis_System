#include "ecg_processor.h"
#include "components/af_model/af_runtime.h"
#include "components/sqi_gate/sqi_runtime.h"
#include <string.h>
#include "sample_clock.h"

static ecg_sqi_result_t quality;
static uint32_t last_peak_ms;
static ecg_processor_stats_t stats;
static bool timed;
static uint32_t current_index,last_index,contiguous;
static uint64_t clock_epoch;

const ecg_processor_stats_t *ecg_processor_stats(void) { return &stats; }

static void results(ecg_record_t *r, af_runtime_result_t a)
{
    r->af_valid = a.valid;
    r->af_probability_x10000 = a.probability_x10000;
    r->af_score = a.score;
    r->af_event_id = a.event_id;
    r->af_window_id = a.window_id;
    r->af_raw_prediction = a.raw_prediction;
    r->af_confirmed = a.confirmed;
    r->af_new_window = a.new_window;
    r->af_new_event = a.new_event;
    r->sqi_score = quality.score;
    r->sqi_status = quality.status;
    r->sqi_rule_mask = quality.rule_mask;
    r->sqi_heart_rate_bpm = quality.heart_rate_bpm;
    r->sqi_max_rr_ms = quality.max_rr_ms;
    r->sqi_rr_ratio_x1000 = quality.rr_ratio_x1000;
    r->sqi_template_correlation_x1000 = quality.template_correlation_x1000;
    r->sqi_window_id = quality.window_id;
    r->sqi_finalize_us = quality.finalize_us;
}

void ecg_processor_reset(uint32_t now_ms)
{
    timed=false;contiguous=0;
    ecg_signal_init(now_ms);
    af_runtime_reset();
    sqi_runtime_reset(now_ms);
    memset(&quality, 0, sizeof(quality));
    quality.score = ECG_SQI_INSUFFICIENT_SCORE;
    quality.status = ECG_SQI_INSUFFICIENT;
    quality.template_correlation_x1000 = -1000;
    last_peak_ms = 0;
    memset(&stats, 0, sizeof(stats));
}

static ecg_record_t ecg_processor_push(int raw_adc, uint32_t now_ms,
                              ecg_peak_sink_t sink, void *context)
{
    ecg_record_t r = ecg_signal_process(raw_adc, now_ms);
    stats.samples++;
    const ecg_signal_window_t *w = ecg_signal_completed_window();
    if (w != NULL) {
        stats.qrs_windows++;
        uint32_t first=current_index-(ECG_WINDOW_SAMPLES-1U);
        uint32_t window_start_ms=(uint32_t)((clock_epoch+ecg_sample_us((uint64_t)first+1U,360U))/1000U);
        /* Same non-overlapping 10s SQI cadence; all R peaks of that window
         * are available before evaluating its Orphanidou/template rules. */
        if ((w->sequence & 1U) != 0U) {
            quality = sqi_runtime_evaluate_window(w->samples, w->peaks,
                w->peak_count, window_start_ms, (w->sequence + 1U) / 2U);
            stats.sqi_windows++;
        }
        for (size_t i = 0; i < w->peak_count; ++i) {
            if(contiguous<ECG_WINDOW_SAMPLES)break;
            if (w->peaks[i] < ECG_HOP_SAMPLES) continue;
            uint64_t peak_us=clock_epoch+ecg_sample_us((uint64_t)first+w->peaks[i]+1U,360U);
            uint32_t timestamp=(uint32_t)((peak_us+500U)/1000U);
            if (last_peak_ms && (int32_t)(timestamp - last_peak_ms) <= 0) continue;
            ecg_record_t event = r;
            event.analysis_event = 1;
            event.is_peak = 1;
            event.timestamp_ms = timestamp;
            event.r_peak_time_us=peak_us;event.sample_time_us=peak_us;event.scheduled_us=peak_us;
            event.sample_index=first+w->peaks[i];
            event.r_peak_timestamp_ms = timestamp;
            uint32_t rr = last_peak_ms ? timestamp - last_peak_ms : 0;
            event.rr_interval_ms = rr <= UINT16_MAX ? (uint16_t)rr : 0;
            event.instant_bpm = rr ? (uint16_t)(60000U / rr) : 0;
            last_peak_ms = timestamp;
            af_runtime_result_t a = af_runtime_push_peak(timestamp);
            stats.peaks++;
            if (a.new_window) stats.af_windows++;
            results(&event, a);
            if (sink) sink(&event, context);
        }
    }
    results(&r, af_runtime_latest());
    return r;
}

ecg_record_t ecg_processor_push_timed(int raw,uint32_t index,uint64_t scheduled_us,uint64_t adc_us,ecg_peak_sink_t sink,void *context)
{
 if(!timed||index!=last_index+1U)contiguous=1;else contiguous++;
 timed=true;last_index=index;current_index=index;
 clock_epoch=scheduled_us-ecg_sample_us((uint64_t)index+1U,360U);
 return ecg_processor_push(raw,(uint32_t)(adc_us/1000U),sink,context);
}
