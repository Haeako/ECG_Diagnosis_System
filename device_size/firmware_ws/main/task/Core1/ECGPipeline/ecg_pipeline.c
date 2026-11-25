#include "ecg_pipeline.h"
#include "components/battery/battery.h"

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
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "components/metrics/pipeline_metrics.h"
#include "components/ring_buffer/ring_buffer.h"
#include "components/sqi_gate/sqi_runtime.h"
#include "drivers/adc/adc.h"
#include "drivers/sd_spi/sd_spi.h"
#include "drivers/timer/timer.h"
#include "ExtractFeature.h"
#include "components/processing/ecg_processor.h"
#include "components/processing/ecg_capture.h"
#include "task/Core0/BLETransmit/ble_transmit.h"

#define ECG_SD_FILE_MAX_LEN     64
#define ECG_SD_SEGMENT_MS       60000
#define SD_BATCH_SAMPLES        32
#define SD_BATCH_BYTES          8192
#define SD_TASK_STACK_SIZE      6144
#define SD_TASK_CORE            0
#define SD_RINGBUFFER_DROP_LIMIT 32
#define SD_DRAIN_TIMEOUT_MS      3000U

void ecg_pipeline_set_event_callback(ecg_pipeline_event_cb_t callback);
static void ecg_pipeline_raise_event(ecg_pipeline_event_t event, esp_err_t error, const char *detail);
static const char *TAG_ECG = "ECG";

QueueHandle_t adc_release_queue_handle = NULL;
adc_oneshot_unit_handle_t adc_handle = NULL;

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
static char sd_event_batch[4096];

static volatile bool record_active = false;
static bool ble_available;
static volatile bool sd_stop_requested = false;
static TaskHandle_t sd_task_handle = NULL;
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

    if (record_active) return ESP_ERR_INVALID_STATE;
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

    ret = ble_available ? ble_transmit_begin_session(5000) : ESP_OK;
    if (ret != ESP_OK) { sd_spi_close(); ADC_Deinit(); return ret; }
    ecg_processor_reset(now_ms);
    record_active = true;
    ret = ecg_capture_start();
    if (ret != ESP_OK) {
        record_active = false;
        sd_spi_close();
        ADC_Deinit();
    }
    return ret;
}

esp_err_t ecg_pipeline_stop_record(void)
{
    bool was_recording = record_active;
    esp_err_t capture_stop=ecg_capture_stop(SD_DRAIN_TIMEOUT_MS);
    if(capture_stop!=ESP_OK) return capture_stop;
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
    if (was_recording && ble_available) {
        esp_err_t status = ble_transmit_flush(5000);
        if (status != ESP_OK) {
            ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_BLE_FAULT, status, "ble-flush-timeout");
        }
    }
    if (was_recording) {
        ecg_capture_stats_t stats = ecg_capture_stats();
        pipeline_deadlines_t deadlines = pipeline_metrics_deadlines();
        ESP_LOGW(TAG_ECG, "STOP acquired=%llu processed=%llu raw_drops=%llu adc_errors=%llu timer_skips=%lu",
            (unsigned long long)stats.acquired, (unsigned long long)stats.processed,
            (unsigned long long)stats.raw_drops, (unsigned long long)stats.adc_errors,
            (unsigned long)Timer_SkippedReleases());
        ESP_LOGW(TAG_ECG, "STOP release_overruns=%llu sd_drops=%llu ble_drops=%llu dsp_max_us=%lu window_misses=%llu",
            (unsigned long long)deadlines.notify_overruns, (unsigned long long)deadlines.sd_drops,
            (unsigned long long)deadlines.ble_drops, (unsigned long)stats.dsp_compute_max_us,
            (unsigned long long)stats.window_misses);
    }
    record_active = false;
    pipeline_metrics_stop_session();
    return ESP_OK;
}

esp_err_t ecg_pipeline_recover_from_event(ecg_pipeline_event_t event)
{
    esp_err_t ret = ESP_OK;

    ESP_LOGW(TAG_ECG, "Recovering pipeline from event=%d", event);

    ret=ecg_pipeline_stop_record();
    if(ret!=ESP_OK) return ret;

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
        /* STOP has drained the buffer; retain the handle used by the writer. */
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

/* Analysis windows produce retrospective events, independently of waveform
 * samples. Both transports keep the original peak timestamps. */
static void analysis_event_sink(const ecg_record_t *event, void *context)
{
    (void)context;
    ble_transmit_enqueue(event);
    if (xRingbufferSend(sd_buf_handle, event, sizeof(*event), 0) != pdTRUE) {
        pipeline_metrics_sd_drop();
    }
}

static void acquisition_idle(void)
{
    battery_poll(ecg_now_ms());
}

static void acquisition_fault(esp_err_t error)
{
    ecg_pipeline_raise_event(error==ESP_ERR_NO_MEM ? ECG_PIPELINE_EVENT_BUFFER_FAULT
        : ECG_PIPELINE_EVENT_ADC_FAULT,error,"adc-acquisition-or-raw-queue");
}


static void processed_sample(const ecg_record_t *sample, void *context)
{
    (void)context;
    static uint32_t session, consecutive_drops;
    if(session!=ecg_record_start_ms) {session=ecg_record_start_ms;consecutive_drops=0;}
    ble_transmit_enqueue(sample);
    if (xRingbufferSend(sd_buf_handle,sample,sizeof(*sample),0)!=pdTRUE) {
        pipeline_metrics_sd_drop();
        if(++consecutive_drops>=SD_RINGBUFFER_DROP_LIMIT) {
            consecutive_drops=0;
            ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_BUFFER_FAULT,
                ESP_ERR_NO_MEM,"sd-ringbuffer-drop-limit");
        }
    } else {
        consecutive_drops=0;
    }
    size_t available=xRingbufferGetCurFreeSize(sd_buf_handle);
    pipeline_metrics_sd_queue(2U*16384U-available,2U*16384U);
}

static void sd_write_task(void *pvParameters)
{
    (void)pvParameters;
    uint32_t last_logged_sqi_window = UINT32_MAX;
    uint8_t last_logged_sqi_status = ECG_SQI_INSUFFICIENT;
    bool last_logged_af_confirmed = false;
    uint32_t logged_record_start_ms = 0;
    for (;;) {
        if (logged_record_start_ms != ecg_record_start_ms) {
            logged_record_start_ms = ecg_record_start_ms;
            last_logged_sqi_window = UINT32_MAX;
            last_logged_sqi_status = ECG_SQI_INSUFFICIENT;
            last_logged_af_confirmed = false;
        }
        size_t batch_len = 0, event_batch_len = 0;
        bool received_sample = false;
        for (unsigned i = 0; i < SD_BATCH_SAMPLES; ++i) {
            size_t item_size = 0;
            ecg_record_t *sample = xRingbufferReceive(sd_buf_handle, &item_size,
                i == 0 ? pdMS_TO_TICKS(250) : 0);
            if (!sample) break;
            received_sample = true;
            if (item_size == sizeof(*sample)) {
                if (sample->analysis_event) {
                    event_batch_len = append_event_row(sd_event_batch,
                        sizeof(sd_event_batch), event_batch_len, "R_PEAK", sample);
                } else {
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
                                    sample->bpm,
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
                pipeline_metrics_sd_drop();
            }
            if (line_len > 0 && line_len < (int)(sizeof(sd_batch) - batch_len)) batch_len += line_len;

                }
            if (sample->sqi_status != ECG_SQI_INSUFFICIENT
                && sample->sqi_window_id != last_logged_sqi_window) {
                if (sample->sqi_status == ECG_SQI_UNUSABLE) {
                    event_batch_len = append_event_row(sd_event_batch,
                        sizeof(sd_event_batch), event_batch_len, "BAD_SQI", sample);
                } else if (last_logged_sqi_status == ECG_SQI_UNUSABLE) {
                    event_batch_len = append_event_row(sd_event_batch,
                        sizeof(sd_event_batch), event_batch_len, "SQI_RECOVERED", sample);
                }
                last_logged_sqi_window = sample->sqi_window_id;
                last_logged_sqi_status = sample->sqi_status;
            }
            if (sample->af_new_event) {
                event_batch_len = append_event_row(sd_event_batch,
                    sizeof(sd_event_batch), event_batch_len, "AF_SUSPECTED", sample);
            }
            if (sample->af_new_window) {
                if (last_logged_af_confirmed && !sample->af_confirmed) {
                    event_batch_len = append_event_row(sd_event_batch,
                        sizeof(sd_event_batch), event_batch_len, "AF_ENDED", sample);
                }
                last_logged_af_confirmed = sample->af_confirmed != 0U;
            }
            }
            vRingbufferReturnItem(sd_buf_handle, sample);
        }
        if (batch_len || event_batch_len) {
            xSemaphoreTake(sd_access_mutex, portMAX_DELAY);
            if (ecg_sd_file[0] != '\0') {
                uint32_t now_ms = ecg_now_ms();
                esp_err_t status = ESP_OK;
                if (!sd_single_file_fallback
                    && now_ms - ecg_sd_segment_start_ms >= ECG_SD_SEGMENT_MS) {
                    status = ecg_start_sd_segment(now_ms);
                }
                int64_t started = esp_timer_get_time();
                if (status == ESP_OK && batch_len)
                    status = sd_spi_write(ecg_sd_file, sd_batch, batch_len);
                if (status == ESP_OK && event_batch_len && ecg_event_file[0])
                    status = sd_spi_write(ecg_event_file, sd_event_batch, event_batch_len);
                pipeline_metrics_sd_write((uint32_t)(esp_timer_get_time() - started),
                    batch_len + event_batch_len, status == ESP_OK);
                if (status != ESP_OK) {
                    ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_SD_FAULT,
                                             status, "sd-write-failed");
                }
            }
            xSemaphoreGive(sd_access_mutex);
        }
        if (sd_stop_requested && !received_sample) {
            sd_stop_requested = false;
            xSemaphoreGive(sd_drained_semaphore);
        }
    }
}

esp_err_t ecg_pipeline_init(void)
{
    esp_err_t adcLockStatus = ADC_LockInit();
    if (adcLockStatus != ESP_OK) {
        return adcLockStatus;
    }
    esp_err_t batteryStatus = battery_init();
    if (batteryStatus != ESP_OK) {
        ESP_LOGW(TAG_ECG, "Battery calibration unavailable (%s)", esp_err_to_name(batteryStatus));
    }
    pipeline_metrics_init();
    adc_release_queue_handle = xQueueCreate(1, sizeof(timer_release_t));
    if (adc_release_queue_handle == NULL) {
        ESP_LOGE(TAG_ECG, "ADC release queue allocation failed");
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
    ecg_processor_reset(ecg_now_ms());
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
    ecg_capture_config_t capture_config={
        .sample=processed_sample, .peak=analysis_event_sink, .idle=acquisition_idle,
        .fault=acquisition_fault,
    };
    esp_err_t capture_status=ecg_capture_init(&capture_config);
    if(capture_status!=ESP_OK) return capture_status;
    esp_err_t ble_status = ecg_ble_init();
    if (ble_status == ESP_OK) {
        ble_status = ble_transmit_start();
    }
    ble_available = ble_status == ESP_OK;
    if (ble_status != ESP_OK) {
        ESP_LOGE(TAG_ECG, "BLE stage 1 failed (%s); SD pipeline remains active", esp_err_to_name(ble_status));
        ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_BLE_FAULT, ble_status, "ble-init-failed");
    }
    return ESP_OK;
}
 
