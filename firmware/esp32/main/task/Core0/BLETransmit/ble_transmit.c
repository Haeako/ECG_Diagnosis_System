#include "ble_transmit.h"

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "components/esp_ble/ecg_ble.h"
#include "components/metrics/pipeline_metrics.h"

#define BLE_TX_QUEUE_LENGTH       32U
#define BLE_TX_NOTIFY_INTERVAL_MS 10U
#define BLE_TX_TASK_STACK_SIZE    3072U
#define BLE_TX_TASK_PRIORITY      4U
#define BLE_TX_CORE               0

static QueueHandle_t ble_tx_queue;
static uint32_t last_enqueued_ms;
static TaskHandle_t ble_tx_task_handle;

static void ble_transmit_task(void *pv_parameters)
{
    (void)pv_parameters;

    for (;;) {
        ecg_record_t sample;
        if (xQueueReceive(ble_tx_queue, &sample, portMAX_DELAY) == pdTRUE) {
            ecg_ble_notify(&sample);
        }
    }
}

esp_err_t ble_transmit_start(void)
{
    if (ble_tx_queue != NULL) {
        return ESP_OK;
    }

    ble_tx_queue = xQueueCreate(BLE_TX_QUEUE_LENGTH, sizeof(ecg_record_t));
    if (ble_tx_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreatePinnedToCore(ble_transmit_task, "BLE_TX", BLE_TX_TASK_STACK_SIZE,
                                NULL, BLE_TX_TASK_PRIORITY, &ble_tx_task_handle,
                                BLE_TX_CORE) != pdPASS) {
        vQueueDelete(ble_tx_queue);
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

    if (!sample->is_peak
        && (sample->timestamp_ms - last_enqueued_ms) < BLE_TX_NOTIFY_INTERVAL_MS) {
        return;
    }
    last_enqueued_ms = sample->timestamp_ms;

    if (xQueueSend(ble_tx_queue, sample, 0) != pdTRUE) {
        pipeline_metrics_ble_drop();
        if (!sample->is_peak) return;
        ecg_record_t discarded;
        (void)xQueueReceive(ble_tx_queue, &discarded, 0);
        (void)xQueueSend(ble_tx_queue, sample, 0);
    }
    pipeline_metrics_ble_queue(uxQueueMessagesWaiting(ble_tx_queue),
                               BLE_TX_QUEUE_LENGTH);
}
