#ifndef ECG_PROCESSOR_H
#define ECG_PROCESSOR_H
#include "task/Core1/ECGPipeline/ExFeature/ExtractFeature.h"

typedef void (*ecg_peak_sink_t)(const ecg_record_t *event, void *context);
typedef struct {
    uint32_t samples, qrs_windows, peaks, sqi_windows, af_windows;
} ecg_processor_stats_t;
const ecg_processor_stats_t *ecg_processor_stats(void);
ecg_record_t ecg_processor_push_timed(int raw,uint32_t index,uint64_t scheduled_us,uint64_t adc_us,ecg_peak_sink_t sink,void *context);
void ecg_processor_reset(uint32_t now_ms);
#endif
