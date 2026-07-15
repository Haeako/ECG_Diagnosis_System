#include "ecg_pipeline.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "components/ble/ecg_ble.h"
#include "components/os/semaphore.h"
#include "components/ring_buffer/ring_buffer.h"
#include "drivers/adc/adc.h"
#include "drivers/sd_spi/sd_spi.h"
#include "drivers/timer/timer.h"
#include "ecg_signal.h"

#define ECG_SD_FILE_MAX_LEN     64
#define ECG_SD_SEGMENT_MS       10000
#define SD_BATCH_SAMPLES        16
#define SD_BATCH_BYTES          1024
#define SD_TASK_STACK_SIZE      6144
#define SD_RINGBUFFER_DROP_LIMIT 32

static const char *TAG_ECG = "ECG";

SemaphoreHandle_t adc_timer_semaphore_handle = NULL;
adc_oneshot_unit_handle_t adc_handle = NULL;
gptimer_handle_t adc_timer_handle = NULL;

static SemaphoreHandle_t sd_access_mutex = NULL;
static char ecg_sd_file[ECG_SD_FILE_MAX_LEN];
static uint32_t ecg_record_start_ms = 0;
static uint32_t ecg_sd_segment_start_ms = 0;
static uint32_t ecg_sd_segment_index = 0;
static char sd_batch[SD_BATCH_BYTES];

static volatile bool record_active = false;

static TaskHandle_t sd_task_handle = NULL;
static TaskHandle_t ecg_task_handle = NULL;
static ecg_pipeline_event_cb_t pipeline_event_cb = NULL;

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

static void ecg_debug_checkpoint(const char *name)
{
    bool heap_ok = heap_caps_check_integrity_all(true);

    ESP_LOGI(TAG_ECG,
             "DEBUG[%s]: heap free=%lu, minimum=%lu, integrity=%s",
             name,
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_minimum_free_heap_size(),
             heap_ok ? "OK" : "CORRUPT");
}

static void ecg_debug_task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        ESP_LOGI(TAG_ECG,
                 "DEBUG[stacks]: SD=%lu, ECG=%lu free stack units",
                 sd_task_handle != NULL
                     ? (unsigned long)uxTaskGetStackHighWaterMark(sd_task_handle)
                     : 0UL,
                 ecg_task_handle != NULL
                     ? (unsigned long)uxTaskGetStackHighWaterMark(ecg_task_handle)
                     : 0UL);
        ecg_debug_checkpoint("periodic");
        vTaskDelay(pdMS_TO_TICKS(5000));
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
    path_len = snprintf(ecg_sd_file,
                        sizeof(ecg_sd_file),
                        "/sdcard/ecg_%lu_%03lu.csv",
                        (unsigned long)ecg_record_start_ms,
                        (unsigned long)ecg_sd_segment_index);
    if (path_len <= 0 || path_len >= (int)sizeof(ecg_sd_file)) {
        ESP_LOGE(TAG_ECG, "SD file path too long");
        ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_SD_FAULT,
                                 ESP_ERR_INVALID_SIZE,
                                 "sd-file-path-too-long");
        return ESP_ERR_INVALID_SIZE;
    }

    ecg_sd_segment_index++;
    ESP_LOGI(TAG_ECG, "SD segment started: %s", ecg_sd_file);
    return sd_spi_write(
        ecg_sd_file,
        "timestamp_ms,raw_adc,baseline,corrected,filtered,is_peak,bpm\n",
        strlen("timestamp_ms,raw_adc,baseline,corrected,filtered,is_peak,bpm\n"));
}

esp_err_t ecg_pipeline_enter_recording(void)
{
    uint32_t now_ms;
    esp_err_t ret;

    ADC_Init();

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
    record_active = true;
    Timer_Start();
    ESP_LOGI(TAG_ECG, "Recording services started");
    return ESP_OK;
}

void ecg_pipeline_enter_idle(void)
{
    record_active = false;
    Timer_Stop();
    ADC_Deinit();
    ESP_LOGI(TAG_ECG, "Recording services stopped");
}

void ecg_pipeline_enter_init(void)
{
    record_active = false;
    Timer_Stop();
    ADC_Deinit();

    if (sd_access_mutex != NULL) {
        xSemaphoreTake(sd_access_mutex, portMAX_DELAY);
        sd_spi_close();
        xSemaphoreGive(sd_access_mutex);
    }

    ESP_LOGI(TAG_ECG, "Pipeline reset to INIT");
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

    while (1) {
        if (xSemaphoreTake(adc_timer_semaphore_handle, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!record_active) {
            continue;
        }

        esp_err_t adc_ret = adc_oneshot_read(adc_handle, ADC_Channel, &raw);
        if (adc_ret != ESP_OK) {
            ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_ADC_FAULT,
                                     adc_ret,
                                     "adc-read-failed");
            continue;
        }

        ecg_record_t sample = ecg_signal_process(raw, ecg_now_ms());
        if (xRingbufferSend(sd_buf_handle, &sample, sizeof(sample), 0) != pdTRUE) {
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
    }
}

static void sd_write_task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        size_t batch_len = 0;

        for (int i = 0; i < SD_BATCH_SAMPLES; i++) {
            size_t item_size = 0;
            TickType_t wait = i == 0 ? pdMS_TO_TICKS(500) : 0;
            ecg_record_t *sample = (ecg_record_t *)xRingbufferReceive(
                sd_buf_handle, &item_size, wait);
            if (sample == NULL) {
                break;
            }

            if (record_active && item_size == sizeof(*sample)) {
                int line_len = snprintf(sd_batch + batch_len,
                                        sizeof(sd_batch) - batch_len,
                                        "%lu,%u,%d,%d,%d,%u,%u\n",
                                        (unsigned long)sample->timestamp_ms,
                                        sample->raw_adc,
                                        sample->baseline,
                                        sample->corrected,
                                        sample->filtered,
                                        sample->is_peak,
                                        sample->bpm);
                if (line_len > 0
                    && line_len < (int)(sizeof(sd_batch) - batch_len)) {
                    batch_len += line_len;
                } else {
                    ESP_LOGW(TAG_ECG, "SD batch full, flushing early");
                }
            }
            vRingbufferReturnItem(sd_buf_handle, sample);
        }

        if (batch_len > 0) {
            xSemaphoreTake(sd_access_mutex, portMAX_DELAY);
            if (record_active) {
                uint32_t now_ms = ecg_now_ms();

                if ((now_ms - ecg_sd_segment_start_ms) >= ECG_SD_SEGMENT_MS
                    && ecg_start_sd_segment(now_ms) != ESP_OK) {
                    ESP_LOGW(TAG_ECG, "SD segment header write failed");
                    ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_SD_FAULT,
                                             ESP_FAIL,
                                             "sd-segment-header-write-failed");
                } else if (sd_spi_write(ecg_sd_file, sd_batch, batch_len) != ESP_OK) {
                    ESP_LOGW(TAG_ECG, "SD write failed");
                    ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_SD_FAULT,
                                             ESP_FAIL,
                                             "sd-write-failed");
                }
            }
            xSemaphoreGive(sd_access_mutex);
        }
    }
}

esp_err_t ecg_pipeline_init(void)
{
    Semaphore_Init(&adc_timer_semaphore_handle);
    if (adc_timer_semaphore_handle == NULL) {
        ESP_LOGE(TAG_ECG, "ADC timer semaphore allocation failed");
        return ESP_ERR_NO_MEM;
    }
    ecg_debug_checkpoint("semaphore-created");

    if (!ring_buffer_init()) {
        ESP_LOGE(TAG_ECG, "Ring buffer allocation failed");
        return ESP_ERR_NO_MEM;
    }
    ecg_debug_checkpoint("ring-buffer-created");

    sd_access_mutex = xSemaphoreCreateMutex();
    if (sd_access_mutex == NULL) {
        ESP_LOGE(TAG_ECG, "SD mutex allocation failed");
        return ESP_ERR_NO_MEM;
    }
    ecg_debug_checkpoint("sd-mutex-created");

    ecg_signal_init(ecg_now_ms());
    Timer_Init();
    ecg_debug_checkpoint("timer-created");
    return ESP_OK;
}

void ecg_pipeline_start(void)
{
    if (xTaskCreatePinnedToCore(sd_write_task, "SD_Task", SD_TASK_STACK_SIZE, NULL, 4,
                                &sd_task_handle, 0) != pdPASS) {
        ESP_LOGE(TAG_ECG, "SD task creation failed");
        ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_TASK_FAULT,
                                 ESP_ERR_NO_MEM,
                                 "sd-task-create-failed");
        return;
    }
    ecg_debug_checkpoint("sd-task-created");

    if (xTaskCreatePinnedToCore(ecg_processing_task, "ECG_Task", 4096, NULL, 5,
                                &ecg_task_handle, 1) != pdPASS) {
        ESP_LOGE(TAG_ECG, "ECG task creation failed");
        ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_TASK_FAULT,
                                 ESP_ERR_NO_MEM,
                                 "ecg-task-create-failed");
        return;
    }
    ecg_debug_checkpoint("ecg-task-created");

    if (xTaskCreate(ecg_debug_task, "ECG_Debug", 3072, NULL, 1, NULL)
        != pdPASS) {
        ESP_LOGE(TAG_ECG, "Debug task creation failed");
        ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_TASK_FAULT,
                                 ESP_ERR_NO_MEM,
                                 "debug-task-create-failed");
    }
    ecg_debug_checkpoint("before-ble-stage1");
    esp_err_t ble_status = ecg_ble_init();
    ecg_debug_checkpoint("after-ble-stage1");
    if (ble_status == ESP_OK) {
        ESP_LOGI(TAG_ECG,
                 "NETWORK: BLE stage 1 enabled (advertising/GATT only)");
    } else {
        ESP_LOGE(TAG_ECG,
                 "BLE stage 1 failed (%s); SD pipeline remains active",
                 esp_err_to_name(ble_status));
        ecg_pipeline_raise_event(ECG_PIPELINE_EVENT_BLE_FAULT,
                                 ble_status,
                                 "ble-init-failed");
    }
}
 
