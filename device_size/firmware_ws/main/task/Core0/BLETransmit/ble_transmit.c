#include "ble_transmit.h"

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "components/esp_ble/ecg_ble.h"
#include "esp_log.h"
#include "components/metrics/pipeline_metrics.h"

#define BLE_TX_QUEUE_LENGTH       128U
#define BLE_TX_TASK_STACK_SIZE    3072U
#define BLE_TX_TASK_PRIORITY      4U
#define BLE_TX_CORE               0

static QueueHandle_t ble_tx_queue;
static SemaphoreHandle_t flushed;
static TaskHandle_t ble_tx_task_handle;

static void ble_transmit_task(void *pv_parameters)
{
 (void)pv_parameters;
 for(;;) {
  ecg_record_t sample;
  if(xQueueReceive(ble_tx_queue,&sample,pdMS_TO_TICKS(250))==pdTRUE) {
   if(sample.analysis_event==255) {
    ecg_ble_flush();
    ecg_ble_stats_t stats = ecg_ble_stats();
    ESP_LOGW("BLE_TX", "STOP packets=%lu samples=%lu failures=%lu not_ready=%lu",
        (unsigned long)stats.packets, (unsigned long)stats.samples,
        (unsigned long)stats.failures, (unsigned long)stats.not_ready);
    xSemaphoreGive(flushed);
   }
   else if(sample.analysis_event==254){ecg_ble_begin_session();xSemaphoreGive(flushed);}
   else ecg_ble_notify(&sample);
  }
  ecg_ble_notify_battery();
 }
}
esp_err_t ble_transmit_start(void)
{
    if (ble_tx_queue != NULL) {
        return ESP_OK;
    }

    flushed = xSemaphoreCreateBinary();
    ble_tx_queue = xQueueCreate(BLE_TX_QUEUE_LENGTH, sizeof(ecg_record_t));
    if (ble_tx_queue == NULL || flushed == NULL) {
        if (ble_tx_queue) vQueueDelete(ble_tx_queue);
        if (flushed) vSemaphoreDelete(flushed);
        ble_tx_queue = NULL;
        flushed = NULL;
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreatePinnedToCore(ble_transmit_task, "BLE_TX", BLE_TX_TASK_STACK_SIZE,
                                NULL, BLE_TX_TASK_PRIORITY, &ble_tx_task_handle,
                                BLE_TX_CORE) != pdPASS) {
        vQueueDelete(ble_tx_queue);
        vSemaphoreDelete(flushed);
        flushed = NULL;
        ble_tx_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    pipeline_metrics_register_task(METRICS_TASK_BLE, ble_tx_task_handle);

    return ESP_OK;
}

void ble_transmit_enqueue(const ecg_record_t *sample)
{
    if (sample == NULL || ble_tx_queue == NULL) {
        return;
    }

    if (xQueueSend(ble_tx_queue, sample, 0) != pdTRUE) {
        pipeline_metrics_ble_drop();
    }
    pipeline_metrics_ble_queue(uxQueueMessagesWaiting(ble_tx_queue),
                               BLE_TX_QUEUE_LENGTH);
}

esp_err_t ble_transmit_flush(uint32_t timeout_ms)
{
    ecg_record_t marker = {.analysis_event=255};
    if (!ble_tx_queue || !flushed) return ESP_ERR_INVALID_STATE;
    (void)xSemaphoreTake(flushed,0);
    if (xQueueSend(ble_tx_queue,&marker,pdMS_TO_TICKS(timeout_ms))!=pdTRUE ||
        xSemaphoreTake(flushed,pdMS_TO_TICKS(timeout_ms))!=pdTRUE) return ESP_ERR_TIMEOUT;
    return ESP_OK;
}

esp_err_t ble_transmit_begin_session(uint32_t timeout_ms)
{
    ecg_record_t marker = {.analysis_event=254};
    if (!ble_tx_queue || !flushed) return ESP_ERR_INVALID_STATE;
    (void)xSemaphoreTake(flushed,0);
    if (xQueueSend(ble_tx_queue,&marker,pdMS_TO_TICKS(timeout_ms))!=pdTRUE ||
        xSemaphoreTake(flushed,pdMS_TO_TICKS(timeout_ms))!=pdTRUE) return ESP_ERR_TIMEOUT;
    return ESP_OK;
}
