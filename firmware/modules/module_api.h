#ifndef ECG_MODULE_API_H
#define ECG_MODULE_API_H
#include <stdint.h>
#if ECG_MODULE == 1
#include "components/bpm/ExtractFeature.h"
#define MODULE_NAME "bpm"
static void module_reset(void) { ecg_signal_init(0); }
static uint32_t module_push(int32_t value, int peak, uint32_t time_ms)
{
    (void)peak;
    ecg_record_t result = ecg_signal_process(value, time_ms);
    return result.bpm + result.is_peak;
}
#elif ECG_MODULE == 2
#include "components/sqi_gate/sqi_runtime.h"
#define MODULE_NAME "sqi"
static void module_reset(void) { sqi_runtime_reset(0); }
static uint32_t module_push(int32_t value, int peak, uint32_t time_ms)
{
    ecg_sqi_result_t result = sqi_runtime_push((int16_t)value, peak != 0, time_ms, time_ms);
    return result.score + result.window_id;
}
#elif ECG_MODULE == 3
#include "components/af_model/af_runtime.h"
#define MODULE_NAME "afib"
static void module_reset(void) { af_runtime_reset(); }
static uint32_t module_push(int32_t value, int peak, uint32_t time_ms)
{
    (void)peak; (void)time_ms;
    af_runtime_result_t result = af_runtime_push_peak((uint32_t)value);
    return result.probability_x10000 + result.window_id;
}
#else
#error "Select one ECG_MODULE: 1=BPM, 2=SQI, 3=AFib"
#endif
#endif
