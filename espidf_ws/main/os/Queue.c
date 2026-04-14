#include "Queue.h"

void Queue_Init(QueueHandle_t *queue, uint32_t QueueLength, uint32_t ItemSize){
    *queue = xQueueCreate(QueueLength, ItemSize);
}