#include "ecg_capture.h"
#include <string.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "drivers/adc/adc.h"
#include "drivers/timer/timer.h"
#include "components/metrics/pipeline_metrics.h"

extern QueueHandle_t adc_release_queue_handle;
typedef struct {
    uint64_t released_us,adc_us;
    uint32_t isr_delay_us,adc_duration_us;
    uint32_t timestamp_ms;
    int raw;
    uint32_t sample_index;
    bool end;
} captured_sample_t;
static QueueHandle_t samples_queue;
static SemaphoreHandle_t acquisition_lock, drained;
static TaskHandle_t adc_task_handle, dsp_task_handle;
static portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;
static ecg_capture_config_t config;
static ecg_capture_stats_t stats;
static bool running, stopped = true;
static bool drain_pending;

static void acquire(void *unused)
{
    (void)unused;
    for (;;) {
        timer_release_t release;
        if (xQueueReceive(adc_release_queue_handle, &release, pdMS_TO_TICKS(250)) != pdTRUE) {
            if (config.idle) config.idle();
            continue;
        }
        xSemaphoreTake(acquisition_lock, portMAX_DELAY);
        if (!running || release.scheduled_us < Timer_EpochUs()) { xSemaphoreGive(acquisition_lock); continue; }
        uint64_t start = esp_timer_get_time();
        pipeline_metrics_sample_begin(start, release.scheduled_us);
        captured_sample_t item = {.released_us=release.scheduled_us,.adc_us=start,.timestamp_ms=(uint32_t)(start/1000U),.sample_index=release.index,.isr_delay_us=release.isr_us>=release.scheduled_us?(uint32_t)(release.isr_us-release.scheduled_us):0};
        item.adc_us=esp_timer_get_time();item.timestamp_ms=(uint32_t)(item.adc_us/1000U);
        esp_err_t status = ADC_Read(&item.raw);
        item.adc_duration_us=(uint32_t)(esp_timer_get_time()-item.adc_us);
        bool sent = status == ESP_OK && xQueueSend(samples_queue, &item, 0) == pdTRUE;
        uint32_t depth = uxQueueMessagesWaiting(samples_queue);
        portENTER_CRITICAL(&stats_lock);
        stats.acquired++;
        if(status==ESP_OK) {
            stats.input_hash=(stats.input_hash^(uint32_t)item.raw)*16777619U;
            stats.input_hash=(stats.input_hash^item.timestamp_ms)*16777619U;
        }
        if (status != ESP_OK) stats.adc_errors++;
        else if (!sent) stats.raw_drops++;
        if (depth > stats.queue_peak) stats.queue_peak = depth;
        portEXIT_CRITICAL(&stats_lock);
        pipeline_metrics_sample_end((uint32_t)(esp_timer_get_time()-start));
        xSemaphoreGive(acquisition_lock);
        if(config.fault && (status!=ESP_OK || !sent))
            config.fault(status!=ESP_OK ? status : ESP_ERR_NO_MEM);
        /* Battery uses the same ADC owner and stays outside the acquisition
         * deadline. Skip it when the next release has little slack. */
        if (config.idle && Timer_PeriodUs() > 2000U && esp_timer_get_time()-release.scheduled_us < 1000)
            config.idle();
    }
}

static void process(void *unused)
{
    (void)unused;
    for (;;) {
        captured_sample_t item;
        if (xQueueReceive(samples_queue, &item, portMAX_DELAY) != pdTRUE) continue;
        if (item.end) { xSemaphoreGive(drained); continue; }
        uint32_t before = ecg_processor_stats()->qrs_windows;
        uint64_t start = esp_timer_get_time();
        ecg_record_t sample = ecg_processor_push_timed(item.raw,item.sample_index,item.released_us,item.adc_us,config.peak,config.context);
        sample.sample_time_us=item.adc_us;sample.scheduled_us=item.released_us;
        sample.isr_delay_us=item.isr_delay_us;sample.adc_duration_us=item.adc_duration_us;
        sample.sample_index = item.sample_index;
        if (config.sample) config.sample(&sample, config.context);
        uint64_t finish = esp_timer_get_time();
        uint32_t execution = (uint32_t)(finish-start);
        uint32_t age = (uint32_t)(finish-item.released_us);
        bool window = ecg_processor_stats()->qrs_windows != before;
        portENTER_CRITICAL(&stats_lock);
        stats.processed++;
        stats.output_hash=(stats.output_hash^(uint32_t)item.raw)*16777619U;
        stats.output_hash=(stats.output_hash^item.timestamp_ms)*16777619U;
        if (execution > stats.dsp_compute_max_us) stats.dsp_compute_max_us = execution;
        if (age > stats.sample_age_max_us) stats.sample_age_max_us = age;
        if (window) {
            if (age > stats.window_response_max_us) stats.window_response_max_us = age;
            if (age > stats.window_deadline_us) stats.window_misses++;
        }
        portEXIT_CRITICAL(&stats_lock);
    }
}

esp_err_t ecg_capture_init(const ecg_capture_config_t *options)
{
    if (samples_queue || !options) return ESP_ERR_INVALID_STATE;
    config = *options;
    samples_queue = xQueueCreate(ECG_CAPTURE_QUEUE_SAMPLES, sizeof(captured_sample_t));
    acquisition_lock = xSemaphoreCreateMutex();
    drained = xSemaphoreCreateBinary();
    if (!samples_queue || !acquisition_lock || !drained) goto failed;
    /* Same core: the high-priority ADC task preempts batch DSP immediately.
     * No cross-core access to processor/model state while recording. */
    if (xTaskCreatePinnedToCore(process,"ECG_DSP",4096,NULL,5,&dsp_task_handle,1)!=pdPASS)
        goto failed;
    if (xTaskCreatePinnedToCore(acquire,"ECG_ADC",3072,NULL,6,&adc_task_handle,1)!=pdPASS)
        goto failed;
    pipeline_metrics_register_task(METRICS_TASK_ECG, dsp_task_handle);
    return ESP_OK;

failed:
    if (dsp_task_handle) vTaskDelete(dsp_task_handle);
    if (samples_queue) vQueueDelete(samples_queue);
    if (acquisition_lock) vSemaphoreDelete(acquisition_lock);
    if (drained) vSemaphoreDelete(drained);
    dsp_task_handle = NULL;
    samples_queue = NULL;
    acquisition_lock = drained = NULL;
    return ESP_ERR_NO_MEM;
}

esp_err_t ecg_capture_start(void)
{
    if (!stopped || !samples_queue) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(acquisition_lock, portMAX_DELAY);
    xQueueReset(samples_queue);
    xQueueReset(adc_release_queue_handle);
    (void)xSemaphoreTake(drained,0);
    portENTER_CRITICAL(&stats_lock);
    memset(&stats,0,sizeof(stats));
    stats.input_hash=stats.output_hash=2166136261U;
    stats.window_deadline_us = ECG_HOP_SAMPLES * Timer_PeriodUs();
    portEXIT_CRITICAL(&stats_lock);
    drain_pending=false; stopped=false;
    pipeline_metrics_start_session();
    running=true; Timer_Start();
    xSemaphoreGive(acquisition_lock);
    return ESP_OK;
}

esp_err_t ecg_capture_stop(uint32_t timeout_ms)
{
    if (stopped) return ESP_OK;
    xSemaphoreTake(acquisition_lock,portMAX_DELAY);
    Timer_Stop(); running=false;
    xSemaphoreGive(acquisition_lock);
    captured_sample_t marker = {.end=true};
    if (!drain_pending) {
        if (xQueueSend(samples_queue,&marker,pdMS_TO_TICKS(timeout_ms))!=pdTRUE)
            return ESP_ERR_TIMEOUT;
        drain_pending=true;
    }
    if (xSemaphoreTake(drained,pdMS_TO_TICKS(timeout_ms))!=pdTRUE) return ESP_ERR_TIMEOUT;
    drain_pending=false;
    stopped=true;
    /* The owner stops metrics after draining its SD/BLE sinks as well. */
    return ESP_OK;
}

ecg_capture_stats_t ecg_capture_stats(void)
{
    portENTER_CRITICAL(&stats_lock);
    ecg_capture_stats_t result=stats;
    portEXIT_CRITICAL(&stats_lock);
    result.adc_stack_free=uxTaskGetStackHighWaterMark(adc_task_handle);
    result.dsp_stack_free=uxTaskGetStackHighWaterMark(dsp_task_handle);
    return result;
}
