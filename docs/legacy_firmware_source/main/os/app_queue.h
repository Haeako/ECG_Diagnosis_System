#ifndef ECG_APP_QUEUE_H
#define ECG_APP_QUEUE_H

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

void Queue_Init(QueueHandle_t *queue, uint32_t queue_length, uint32_t item_size);

#endif
