#ifndef ECG_CAPTURE_H
#define ECG_CAPTURE_H
#include "esp_err.h"
#include "ecg_processor.h"

#define ECG_CAPTURE_QUEUE_SAMPLES 256U
typedef struct {
    void (*sample)(const ecg_record_t *, void *);
    ecg_peak_sink_t peak;
    void (*idle)(void);
    void (*fault)(esp_err_t error);
    void *context;
} ecg_capture_config_t;
typedef struct {
    uint64_t acquired, processed, raw_drops, adc_errors, window_misses;
    uint32_t queue_peak, sample_age_max_us, dsp_compute_max_us;
    uint32_t window_response_max_us, window_deadline_us;
    uint32_t adc_stack_free, dsp_stack_free;
    uint32_t input_hash, output_hash;
} ecg_capture_stats_t;
/* Init once, after release queue/timer creation. All queue slots are owned,
 * copied records; the ADC never waits for the DSP or overwrites old data. */
esp_err_t ecg_capture_init(const ecg_capture_config_t *config);
/* Call only after a successful stop/drain and processor reset. */
esp_err_t ecg_capture_start(void);
/* Stops acquisition first, then FIFO-drains DSP through a sentinel. */
esp_err_t ecg_capture_stop(uint32_t timeout_ms);
ecg_capture_stats_t ecg_capture_stats(void);
#endif
