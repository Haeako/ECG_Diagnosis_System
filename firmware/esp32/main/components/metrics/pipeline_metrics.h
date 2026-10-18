#ifndef PIPELINE_METRICS_H
#define PIPELINE_METRICS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
typedef enum { METRICS_TASK_STATE, METRICS_TASK_ECG, METRICS_TASK_SD,
               METRICS_TASK_BLE, METRICS_TASK_COUNT } metrics_task_id_t;
void pipeline_metrics_init(void);
void pipeline_metrics_start_session(void);
void pipeline_metrics_stop_session(void);
void pipeline_metrics_register_task(metrics_task_id_t id, TaskHandle_t handle);
void pipeline_metrics_timer_isr(uint64_t timer_count, bool notification_sent);
uint64_t pipeline_metrics_last_isr_us(void);
void pipeline_metrics_sample_begin(uint64_t task_start_us, uint64_t isr_us);
void pipeline_metrics_sample_end(uint32_t elapsed_us);
void pipeline_metrics_sd_write(uint32_t elapsed_us, size_t bytes, bool success);
void pipeline_metrics_sd_queue(size_t used_bytes, size_t capacity_bytes);
void pipeline_metrics_ble_queue(UBaseType_t waiting, UBaseType_t capacity);
void pipeline_metrics_sd_drop(void);
void pipeline_metrics_ble_drop(void);
#endif
