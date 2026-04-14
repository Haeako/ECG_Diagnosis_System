#include "semaphore.h"

void Semaphore_Init(SemaphoreHandle_t *sem){
   *sem = xSemaphoreCreateBinary();
}