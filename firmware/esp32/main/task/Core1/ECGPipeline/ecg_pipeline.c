#include "ecg_pipeline.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "components/esp_ble/ecg_ble.h"
#include "components/af_model/af_runtime.h"
#include "components/os/semaphore.h"
#include "components/metrics/pipeline_metrics.h"
#include "components/ring_buffer/ring_buffer.h"
#ifndef SQI_COST_DISABLE
#include "components/sqi_gate/sqi_runtime.h"
#endif
#include "drivers/adc/adc.h"
#include "drivers/sd_spi/sd_spi.h"
#include "drivers/timer/timer.h"
#include "ExtractFeature.h"
#include "task/Core0/BLETransmit/ble_transmit.h"

#define ECG_SD_FILE_MAX_LEN     64
#define ECG_SD_SEGMENT_MS       60000
#define SD_BATCH_SAMPLES        32
#define SD_BATCH_BYTES          6144
#define SD_TASK_STACK_SIZE      6144
#define SD_TASK_CORE            0
#define ECG_PROCESSING_CORE     1
#define SD_RINGBUFFER_DROP_LIMIT 32
#define SD_DRAIN_TIMEOUT_MS      3000U
#define RPEAK_ALIGN_DELAY_MS    1000U
#define RPEAK_PENDING_SAMPLES   512U

void ecg_pipeline_set_event_callback(ecg_pipeline_event_cb_t callback);
static void ecg_pipeline_raise_event(ecg_pipeline_event_t event, esp_err_t error, const char *detail);
static const char *TAG_ECG = "ECG";

SemaphoreHandle_t adc_timer_semaphore_handle = NULL;
adc_oneshot_unit_handle_t adc_handle = NULL;
gptimer_handle_t adc_timer_handle = NULL;

static SemaphoreHandle_t sd_access_mutex = NULL;
static SemaphoreHandle_t sd_drained_semaphore = NULL;
static char     ecg_sd_file[ECG_SD_FILE_MAX_LEN];
static char     ecg_event_file[ECG_SD_FILE_MAX_LEN];
static uint32_t ecg_record_start_ms     = 0;
static uint32_t ecg_session_id          = 0;
static uint32_t ecg_sd_segment_start_ms = 0;
static uint32_t ecg_sd_segment_index    = 0;
static bool     sd_single_file_fallback = false;

static char         sd_batch[SD_BATCH_BYTES];
static ecg_record_t rpeak_pending[RPEAK_PENDING_SAMPLES];

static volatile bool record_active = false;
static volatile bool sd_stop_requested = false;
static TaskHandle_t sd_task_handle = NULL;
static TaskHandle_t ecg_task_handle = NULL;
static ecg_pipeline_event_cb_t pipeline_event_cb = NULL;

static size_t append_event_row(char *buffer, size_t capacity, size_t used,
                               const char *event_type,
                               const ecg_record_t *sample)
{
    int length = snprintf(buffer + used, capacity - used,
        "%s%lu,%s,%u,%u,%u,%u,%u,%u,%lu,%u,%u\n",
        sd_single_file_fallback ? "EVENT," : "",
        (unsigned long)sample->timestamp_ms, event_type,
        sample->sqi_status, sample->sqi_score, sample->sqi_rule_mask,
        sample->af_probability_x10000, sample->af_score,
        sample->af_confirmed, (unsigned long)sample->af_window_id,
        sample->af_event_id, sample->bpm);
    if (length <= 0 || length >= (int)(capacity - used)) return used;
    return used + (size_t)length;
}

void ecg_pipeline_set_event_callback(ecg_pipeline_event_cb_t callback)
{
    pipeline_event_cb = callback;
}

static void ecg_pipeline_raise_event(ecg_pipeline_event_t event,
                                     esp_err_t error,
                                     const char *detail)
{
    ESP_LOGW(TAG_ECG,
             "Pipeline event: event=%d error=%s detail=%s",
             event,
             esp_err_to_name(error),
             detail != NULL ? detail : "-");

    if (pipeline_event_cb != NULL) {
        pipeline_event_cb(event, error, detail);
    }
}

static uint32_t ecg_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static esp_err_t ecg_start_sd_segment(uint32_t now_ms)
{
    int path_len;

    if (ecg_record_start_ms == 0) {
        ecg_record_start_ms = now_ms;
    }

    ecg_sd_segment_start_ms = now_ms;
    if (sd_single_file_fallback) {
        path_len = snprintf(ecg_sd_file, sizeof(ecg_sd_file),
                            "/sdcard/test.txt");
    } else if (ecg_sd_segment_index > 0xFFFU) {
        ESP_LOGE(TAG_ECG, "SD segment index exhausted");
        return ESP_ERR_INVALID_SIZE;
    } else {
        path_len = snprintf(ecg_sd_file,
                            sizeof(ecg_sd_file),
                            "/sdcard/D%04lX%03lX.CSV",
                            (unsigned long)(ecg_session_id & 0xFFFFU),
                            (unsigned long)ecg_sd_segment_index);
    }
    if (path_len <= 0 || path_len >= (int)sizeof(ecg_sd_file)) {
        ESP_LOGE(TAG_ECG, "SD file path too long");
        ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_SD_FAULT,
                                 ESP_ERR_INVALID_SIZE,
                                 "sd-file-path-too-long");
        return ESP_ERR_INVALID_SIZE;
    }

    ecg_sd_segment_index++;
    const char *header = sd_single_file_fallback
        ? "#SAMPLE,timestamp_ms,raw_adc,centered,notch_49_51hz,bandpass_5_18hz,ma_qrs_100ms,threshold,is_r_peak,bpm,sqi_score,sqi_status,sqi_rule_mask,sqi_hr_bpm,sqi_max_rr_ms,sqi_rr_ratio_x1000,sqi_template_corr_x1000,sqi_window_id,sqi_finalize_us,af_valid,af_probability_x10000,af_score,af_raw_prediction,af_confirmed,af_window_id,af_event_id,af_new_window,af_new_event\n"
        : "timestamp_ms,raw_adc,centered,notch_49_51hz,bandpass_5_18hz,ma_qrs_100ms,threshold,is_r_peak,bpm,sqi_score,sqi_status,sqi_rule_mask,sqi_hr_bpm,sqi_max_rr_ms,sqi_rr_ratio_x1000,sqi_template_corr_x1000,sqi_window_id,sqi_finalize_us,af_valid,af_probability_x10000,af_score,af_raw_prediction,af_confirmed,af_window_id,af_event_id,af_new_window,af_new_event\n";
    return sd_spi_write(ecg_sd_file, header, strlen(header));
}

esp_err_t ecg_pipeline_start_recording(void)
{
    uint32_t now_ms;
    esp_err_t ret;

    ADC_Init();
    sd_stop_requested = false;
    if (sd_drained_semaphore != NULL) {
        (void)xSemaphoreTake(sd_drained_semaphore, 0);
    }

    xSemaphoreTake(sd_access_mutex, portMAX_DELAY);
    ret = sd_spi_init();
    xSemaphoreGive(sd_access_mutex);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_ECG,
                 "SD initialization failed (%s)",
                 esp_err_to_name(ret));
        ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_SD_FAULT,
                                 ret,
                                 "sd-init-failed");
        ADC_Deinit();
        return ret;
    }

    now_ms = ecg_now_ms();
    ecg_record_start_ms = now_ms;
    ecg_sd_segment_start_ms = now_ms;
    ecg_sd_segment_index = 0;
    sd_single_file_fallback = false;
    struct stat event_stat;
    bool session_available = false;
    int event_path_len = 0;
    for (unsigned attempt = 0; attempt < 16U; ++attempt) {
        ecg_session_id = esp_random() & 0xFFFFU;
        event_path_len = snprintf(ecg_event_file, sizeof(ecg_event_file),
                                  "/sdcard/E%04lX.CSV",
                                  (unsigned long)ecg_session_id);
        if (event_path_len > 0
            && event_path_len < (int)sizeof(ecg_event_file)
            && stat(ecg_event_file, &event_stat) != 0) {
            session_available = true;
            break;
        }
    }
    if (!session_available
        || sd_spi_try_write(ecg_event_file,
            "timestamp_ms,event_type,sqi_status,sqi_score,sqi_rule_mask,af_probability_x10000,af_score,af_confirmed,af_window_id,af_event_id,bpm\n",
            strlen("timestamp_ms,event_type,sqi_status,sqi_score,sqi_rule_mask,af_probability_x10000,af_score,af_confirmed,af_window_id,af_event_id,bpm\n")) != ESP_OK) {
        sd_single_file_fallback = true;
        snprintf(ecg_event_file, sizeof(ecg_event_file), "/sdcard/test.txt");
        char fallback_header[256];
        int fallback_len = snprintf(fallback_header, sizeof(fallback_header),
            "\n#SESSION,%04lX,%lu\n#EVENT,timestamp_ms,event_type,sqi_status,sqi_score,sqi_rule_mask,af_probability_x10000,af_score,af_confirmed,af_window_id,af_event_id,bpm\n",
            (unsigned long)(ecg_session_id & 0xFFFFU),
            (unsigned long)ecg_record_start_ms);
        if (fallback_len <= 0 || fallback_len >= (int)sizeof(fallback_header)
            || sd_spi_write(ecg_event_file, fallback_header,
                            (size_t)fallback_len) != ESP_OK) {
            ESP_LOGE(TAG_ECG, "Event journal fallback initialization failed");
            sd_spi_close();
            ADC_Deinit();
            return ESP_FAIL;
        }
        ESP_LOGW(TAG_ECG, "SD directory full; using test.txt mixed-log fallback");
    }
    if (ecg_start_sd_segment(now_ms) != ESP_OK) {
        ESP_LOGE(TAG_ECG, "SD header write failed");
        ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_SD_FAULT,
                                 ESP_FAIL,
                                 "sd-header-write-failed");
        sd_spi_close();
        ADC_Deinit();
        return ESP_FAIL;
    }

    ecg_signal_reset(now_ms);
    af_runtime_reset();
#ifndef SQI_COST_DISABLE
    sqi_runtime_reset(now_ms);
#endif
    pipeline_metrics_start_session();
    record_active = true;
    Timer_Start();
    return ESP_OK;
}

esp_err_t ecg_pipeline_stop_record(void)
{
    bool was_recording = record_active;
    record_active = false;
    Timer_Stop();
    ADC_Deinit();

    if (was_recording && sd_task_handle != NULL) {
        sd_stop_requested = true;
        if (xSemaphoreTake(sd_drained_semaphore,
                           pdMS_TO_TICKS(SD_DRAIN_TIMEOUT_MS)) != pdTRUE) {
            ESP_LOGE(TAG_ECG, "Timed out draining SD writer");
            ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_SD_FAULT,
                                     ESP_ERR_TIMEOUT,
                                     "sd-drain-timeout");
            return ESP_ERR_TIMEOUT;
        }
    }

    if (sd_access_mutex != NULL) {
        xSemaphoreTake(sd_access_mutex, portMAX_DELAY);
        sd_spi_close();
        xSemaphoreGive(sd_access_mutex);
    }
    pipeline_metrics_stop_session();
    return ESP_OK;
}

esp_err_t ecg_pipeline_recover_from_event(ecg_pipeline_event_t event)
{
    esp_err_t ret = ESP_OK;

    ESP_LOGW(TAG_ECG, "Recovering pipeline from event=%d", event);

    record_active = false;
    Timer_Stop();

    switch (event) {
    case ECG_PIPELINE_EVENT_SD_FAULT:
        if (sd_access_mutex != NULL) {
            xSemaphoreTake(sd_access_mutex, portMAX_DELAY);
            sd_spi_close();
            xSemaphoreGive(sd_access_mutex);
        }
        break;

    case ECG_PIPELINE_EVENT_ADC_FAULT:
        ADC_Deinit();
        ADC_Init();
        ADC_Deinit();
        break;

    case ECG_PIPELINE_EVENT_BUFFER_FAULT:
        /*
         * Let sd_write_task leave its short receive/write cycle before replacing
         * the global ring buffer handle. Then suspend it briefly so the handle
         * cannot be used while the buffer is deleted and recreated.
         */
        vTaskDelay(pdMS_TO_TICKS(600));
        if (sd_task_handle != NULL) {
            vTaskSuspend(sd_task_handle);
        }
        if (!ring_buffer_reset()) {
            ret = ESP_ERR_NO_MEM;
        }
        if (sd_task_handle != NULL) {
            vTaskResume(sd_task_handle);
        }
        break;

    case ECG_PIPELINE_EVENT_TASK_FAULT:
        ESP_LOGW(TAG_ECG,
                 "Task fault recovery requires system-level restart or manual retry");
        ret = ESP_FAIL;
        break;

    case ECG_PIPELINE_EVENT_BLE_FAULT:
        ESP_LOGW(TAG_ECG, "BLE fault is non-critical for SD recording pipeline");
        break;

    default:
        ret = ESP_ERR_INVALID_ARG;
        break;
    }

    return ret;
}

static void ecg_processing_task(void *pvParameters)
{
    int raw = 0;
    uint32_t ringbuffer_drop_count = 0;
    uint32_t heartbeat_start_ms = 0U;
    uint32_t heartbeat_samples = 0U;
    uint32_t heartbeat_record_start_ms = 0U;

    while (1) {
        if (xSemaphoreTake(adc_timer_semaphore_handle, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!record_active) {
            continue;
        }
        uint64_t pipeline_start_us = (uint64_t)esp_timer_get_time();
        pipeline_metrics_sample_begin(pipeline_start_us,
                                      pipeline_metrics_last_isr_us());

        if (heartbeat_record_start_ms != ecg_record_start_ms) {
            heartbeat_record_start_ms = ecg_record_start_ms;
            heartbeat_start_ms = 0U;
            heartbeat_samples = 0U;
            ringbuffer_drop_count = 0U;
        }

        esp_err_t adc_ret = adc_oneshot_read(adc_handle, ADC_Channel, &raw);
        if (adc_ret != ESP_OK) {
            ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_ADC_FAULT,
                                     adc_ret,
                                     "adc-read-failed");
            continue;
        }

        ecg_record_t sample = ecg_signal_process(raw, ecg_now_ms());
        heartbeat_samples++;
        af_runtime_result_t af = sample.is_peak != 0U
            ? af_runtime_push_peak(sample.r_peak_timestamp_ms)
            : af_runtime_latest();
        sample.af_probability_x10000 = af.probability_x10000;
        sample.af_score = af.score;
        sample.af_event_id = af.event_id;
        sample.af_window_id = af.window_id;
        sample.af_valid = af.valid ? 1U : 0U;
        sample.af_raw_prediction = af.raw_prediction ? 1U : 0U;
        sample.af_confirmed = af.confirmed ? 1U : 0U;
        sample.af_new_window = af.new_window ? 1U : 0U;
        sample.af_new_event = af.new_event ? 1U : 0U;
#ifndef SQI_COST_DISABLE
        ecg_sqi_result_t sqi = sqi_runtime_push(sample.corrected,
                                                sample.is_peak != 0U,
                                                sample.r_peak_timestamp_ms,
                                                sample.timestamp_ms);
        sample.sqi_score = sqi.score;
        sample.sqi_status = (uint8_t)sqi.status;
        sample.sqi_rule_mask = sqi.rule_mask;
        sample.sqi_heart_rate_bpm = sqi.heart_rate_bpm;
        sample.sqi_max_rr_ms = sqi.max_rr_ms;
        sample.sqi_rr_ratio_x1000 = sqi.rr_ratio_x1000;
        sample.sqi_template_correlation_x1000 =
            sqi.template_correlation_x1000;
        sample.sqi_window_id = sqi.window_id;
        sample.sqi_finalize_us = sqi.finalize_us;
#else
        sample.sqi_score = 255U;
        sample.sqi_status = 2U;
#endif
        ble_transmit_enqueue(&sample);

        if (xRingbufferSend(sd_buf_handle, &sample, sizeof(sample), 0) != pdTRUE) {
            pipeline_metrics_sd_drop();
            ringbuffer_drop_count++;
            ESP_LOGW(TAG_ECG, "SD ring buffer full, sample dropped");
            if (ringbuffer_drop_count >= SD_RINGBUFFER_DROP_LIMIT) {
                ringbuffer_drop_count = 0;
                ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_BUFFER_FAULT,
                                         ESP_ERR_NO_MEM,
                                         "sd-ringbuffer-drop-limit");
            }
        } else {
            ringbuffer_drop_count = 0;
        }
        size_t sd_free = xRingbufferGetCurFreeSize(sd_buf_handle);
        pipeline_metrics_sd_queue(2U * 16384U - sd_free, 2U * 16384U);
        pipeline_metrics_sample_end((uint32_t)
            ((uint64_t)esp_timer_get_time() - pipeline_start_us));

        if (heartbeat_start_ms == 0U) {
            heartbeat_start_ms = sample.timestamp_ms;
            heartbeat_samples = 0U;
        } else if ((sample.timestamp_ms - heartbeat_start_ms) >= 10000U) {
            uint32_t elapsed_ms = sample.timestamp_ms - heartbeat_start_ms;
            uint32_t rate_x10 = elapsed_ms > 0U
                ? (heartbeat_samples * 10000U) / elapsed_ms : 0U;
            ESP_LOGW(TAG_ECG,
                "HEALTH rate=%lu.%luHz raw=%u corrected=%d bpm=%u "
                "sqi=%u/%u window=%lu af=%u p=%u window=%lu heap=%lu",
                (unsigned long)(rate_x10 / 10U),
                (unsigned long)(rate_x10 % 10U),
                sample.raw_adc, sample.corrected, sample.bpm,
                sample.sqi_status, sample.sqi_score,
                (unsigned long)sample.sqi_window_id,
                sample.af_confirmed, sample.af_probability_x10000,
                (unsigned long)sample.af_window_id,
                (unsigned long)esp_get_free_heap_size());
            heartbeat_start_ms = sample.timestamp_ms;
            heartbeat_samples = 0U;
        }
    }
}

static void sd_write_task(void *pvParameters)
{
    (void)pvParameters;
    size_t pending_head = 0;
    size_t pending_count = 0;
    uint32_t last_logged_sqi_window = UINT32_MAX;
    uint8_t last_logged_sqi_status = ECG_SQI_INSUFFICIENT;
    bool last_logged_af_confirmed = false;
    uint32_t logged_record_start_ms = 0U;

    while (1) {
        if (logged_record_start_ms != ecg_record_start_ms) {
            logged_record_start_ms = ecg_record_start_ms;
            last_logged_sqi_window = UINT32_MAX;
            last_logged_sqi_status = ECG_SQI_INSUFFICIENT;
            last_logged_af_confirmed = false;
        }
        size_t batch_len = 0;
        char event_batch[1024];
        size_t event_batch_len = 0;
        bool received_sample = false;
        uint32_t newest_timestamp_ms = 0;

        for (int i = 0; i < SD_BATCH_SAMPLES; i++) {
            size_t item_size = 0;
            TickType_t wait = i == 0 && (record_active || pending_count == 0)
                ? pdMS_TO_TICKS(500) : 0;
            ecg_record_t *sample = (ecg_record_t *)xRingbufferReceive(
                sd_buf_handle, &item_size, wait);
            if (sample == NULL) {
                break;
            }

            if (item_size == sizeof(*sample)) {
                received_sample = true;
                newest_timestamp_ms = sample->timestamp_ms;

                if (sample->is_peak && sample->r_peak_timestamp_ms != 0U) {
                    size_t best_offset = pending_count;
                    uint32_t best_error = UINT32_MAX;
                    for (size_t offset = 0; offset < pending_count; ++offset) {
                        size_t slot = (pending_head + offset)
                            % RPEAK_PENDING_SAMPLES;
                        uint32_t timestamp = rpeak_pending[slot].timestamp_ms;
                        uint32_t error = timestamp > sample->r_peak_timestamp_ms
                            ? timestamp - sample->r_peak_timestamp_ms
                            : sample->r_peak_timestamp_ms - timestamp;
                        if (error < best_error) {
                            best_error = error;
                            best_offset = offset;
                        }
                    }
                    if (best_offset < pending_count) {
                        size_t slot = (pending_head + best_offset)
                            % RPEAK_PENDING_SAMPLES;
                        rpeak_pending[slot].is_peak = 1U;
                    } else {
                        ESP_LOGW(TAG_ECG,
                                 "Aligned R timestamp is outside pending window");
                    }
                }

                if (pending_count == RPEAK_PENDING_SAMPLES) {
                    ESP_LOGW(TAG_ECG, "R-peak alignment window full");
                    pending_head = (pending_head + 1U) % RPEAK_PENDING_SAMPLES;
                    pending_count--;
                }
                size_t tail = (pending_head + pending_count)
                    % RPEAK_PENDING_SAMPLES;
                rpeak_pending[tail] = *sample;
                rpeak_pending[tail].is_peak = 0U;
                pending_count++;
            }
            vRingbufferReturnItem(sd_buf_handle, sample);
        }

        for (int i = 0; i < SD_BATCH_SAMPLES && pending_count > 0; ++i) {
            ecg_record_t *sample = &rpeak_pending[pending_head];
            bool old_enough = received_sample
                && (newest_timestamp_ms - sample->timestamp_ms)
                    >= RPEAK_ALIGN_DELAY_MS;
            if (received_sample && !old_enough) break;

            int line_len = snprintf(sd_batch + batch_len,
                                    sizeof(sd_batch) - batch_len,
                                    "%s%lu,%u,%d,%d,%d,%.3f,%.3f,%u,%u,%u,%u,%u,%u,%u,%u,%d,%lu,%lu,%u,%u,%u,%u,%u,%lu,%u,%u,%u\n",
                                    sd_single_file_fallback ? "SAMPLE," : "",
                                    (unsigned long)sample->timestamp_ms,
                                    sample->raw_adc,
                                    sample->raw_centered,
                                    sample->notch_49_51hz,
                                    sample->filtered,
                                    (double)sample->ma_qrs_100ms,
                                    (double)sample->threshold,
                                    sample->is_peak,
                                    sample->instant_bpm,
                                    sample->sqi_score,
                                    sample->sqi_status,
                                    sample->sqi_rule_mask,
                                    sample->sqi_heart_rate_bpm,
                                    sample->sqi_max_rr_ms,
                                    sample->sqi_rr_ratio_x1000,
                                    sample->sqi_template_correlation_x1000,
                                    (unsigned long)sample->sqi_window_id,
                                    (unsigned long)sample->sqi_finalize_us,
                                    sample->af_valid,
                                    sample->af_probability_x10000,
                                    sample->af_score,
                                    sample->af_raw_prediction,
                                    sample->af_confirmed,
                                    (unsigned long)sample->af_window_id,
                                    sample->af_event_id,
                                    sample->af_new_window,
                                    sample->af_new_event);
            if (line_len <= 0
                || line_len >= (int)(sizeof(sd_batch) - batch_len)) {
                ESP_LOGW(TAG_ECG, "SD batch full, flushing early");
                break;
            }
            batch_len += line_len;

            if (sample->sqi_status != ECG_SQI_INSUFFICIENT
                && sample->sqi_window_id != last_logged_sqi_window) {
                if (sample->sqi_status == ECG_SQI_UNUSABLE) {
                    event_batch_len = append_event_row(event_batch,
                        sizeof(event_batch), event_batch_len, "BAD_SQI", sample);
                } else if (last_logged_sqi_status == ECG_SQI_UNUSABLE) {
                    event_batch_len = append_event_row(event_batch,
                        sizeof(event_batch), event_batch_len, "SQI_RECOVERED", sample);
                }
                last_logged_sqi_window = sample->sqi_window_id;
                last_logged_sqi_status = sample->sqi_status;
            }
            if (sample->af_new_event) {
                event_batch_len = append_event_row(event_batch,
                    sizeof(event_batch), event_batch_len, "AF_SUSPECTED", sample);
            }
            if (sample->af_new_window) {
                if (last_logged_af_confirmed && !sample->af_confirmed) {
                    event_batch_len = append_event_row(event_batch,
                        sizeof(event_batch), event_batch_len, "AF_ENDED", sample);
                }
                last_logged_af_confirmed = sample->af_confirmed != 0U;
            }
            pending_head = (pending_head + 1U) % RPEAK_PENDING_SAMPLES;
            pending_count--;
        }

        if (batch_len > 0) {
            xSemaphoreTake(sd_access_mutex, portMAX_DELAY);
            if (ecg_sd_file[0] != '\0') {
                uint32_t now_ms = ecg_now_ms();

                /* A full FAT root directory can reject creation of new 8.3
                 * files with EACCES.  In fallback mode both samples and
                 * events are intentionally appended to the existing
                 * test.txt file, so do not try to "rotate" it every minute.
                 * This keeps recording alive until the user stops it with
                 * the physical button. */
                if (!sd_single_file_fallback
                    && (now_ms - ecg_sd_segment_start_ms) >= ECG_SD_SEGMENT_MS
                    && ecg_start_sd_segment(now_ms) != ESP_OK) {
                    ESP_LOGW(TAG_ECG, "SD segment header write failed");
                    ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_SD_FAULT,
                                             ESP_FAIL,
                                             "sd-segment-header-write-failed");
                } else {
                    int64_t write_start_us = esp_timer_get_time();
                    esp_err_t write_status =
                        sd_spi_write(ecg_sd_file, sd_batch, batch_len);
                    pipeline_metrics_sd_write(
                        (uint32_t)(esp_timer_get_time() - write_start_us),
                        batch_len, write_status == ESP_OK);
                    if (write_status != ESP_OK) {
                    ESP_LOGW(TAG_ECG, "SD write failed");
                    ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_SD_FAULT,
                                             ESP_FAIL,
                                             "sd-write-failed");
                    }
                }
                if (event_batch_len > 0U && ecg_event_file[0] != '\0'
                    && sd_spi_write(ecg_event_file, event_batch,
                                    event_batch_len) != ESP_OK) {
                    ESP_LOGW(TAG_ECG, "Event journal write failed");
                    ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_SD_FAULT,
                                             ESP_FAIL,
                                             "event-journal-write-failed");
                }
            }
            xSemaphoreGive(sd_access_mutex);
        }

        /* Acknowledge STOP only after one complete pass observes no queued
         * sample and all delayed R-peak alignment records have been flushed. */
        if (sd_stop_requested && !received_sample && pending_count == 0U) {
            sd_stop_requested = false;
            xSemaphoreGive(sd_drained_semaphore);
        }
    }
}

esp_err_t ecg_pipeline_init(void)
{
    pipeline_metrics_init();
    Semaphore_Init(&adc_timer_semaphore_handle);
    if (adc_timer_semaphore_handle == NULL) {
        ESP_LOGE(TAG_ECG, "ADC timer semaphore allocation failed");
        return ESP_ERR_NO_MEM;
    }
    if (!ring_buffer_init()) {
        return ESP_ERR_NO_MEM;
    }
    sd_access_mutex = xSemaphoreCreateMutex();
    if (sd_access_mutex == NULL) {
        ESP_LOGE(TAG_ECG, "SD mutex allocation failed");
        return ESP_ERR_NO_MEM;
    }
    sd_drained_semaphore = xSemaphoreCreateBinary();
    if (sd_drained_semaphore == NULL) {
        ESP_LOGE(TAG_ECG, "SD drain semaphore allocation failed");
        return ESP_ERR_NO_MEM;
    }
    ecg_signal_init(ecg_now_ms());
    af_runtime_reset();
#ifndef SQI_COST_DISABLE
    sqi_runtime_reset(ecg_now_ms());
#endif
    Timer_Init();
    return ESP_OK;
}

esp_err_t ecg_pipeline_start_tasks(void)
{
    if (xTaskCreatePinnedToCore(sd_write_task, "SD_Task", SD_TASK_STACK_SIZE, NULL, 4,
                                &sd_task_handle, SD_TASK_CORE) != pdPASS) {
        ESP_LOGE(TAG_ECG, "SD task creation failed");
        ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_TASK_FAULT,
                                 ESP_ERR_NO_MEM,
                                 "sd-task-create-failed");
        return ESP_ERR_NO_MEM;
    }
    pipeline_metrics_register_task(METRICS_TASK_SD, sd_task_handle);
    if (xTaskCreatePinnedToCore(ecg_processing_task, "ECG_Task", 4096, NULL, 5,
                                &ecg_task_handle, ECG_PROCESSING_CORE) != pdPASS) {
        ESP_LOGE(TAG_ECG, "ECG task creation failed");
        ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_TASK_FAULT, ESP_ERR_NO_MEM, "ecg-process-task-create-failed");
        return ESP_ERR_NO_MEM;
    }
    pipeline_metrics_register_task(METRICS_TASK_ECG, ecg_task_handle);
    esp_err_t ble_status = ecg_ble_init();
    if (ble_status == ESP_OK) {
        ble_status = ble_transmit_start();
    }
    if (ble_status != ESP_OK) {
        ESP_LOGE(TAG_ECG, "BLE stage 1 failed (%s); SD pipeline remains active", esp_err_to_name(ble_status));
        ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_BLE_FAULT, ble_status, "ble-init-failed");
    }
    return ESP_OK;
}
 
