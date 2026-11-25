#ifndef ECG_TIMER_H
#define ECG_TIMER_H

#include <stdbool.h>
#include "esp_log.h"
#include "driver/gptimer.h"

#define ECG_SAMP_FREQ 360
#define RESOLUTION_HZ 1000000

typedef struct { uint64_t scheduled_us,isr_us; uint32_t index; } timer_release_t;
uint64_t Timer_EpochUs(void);
uint32_t Timer_SkippedReleases(void);
void Timer_Init(void);
void Timer_Start(void);
void Timer_Stop(void);
uint32_t Timer_PeriodUs(void);

#endif
