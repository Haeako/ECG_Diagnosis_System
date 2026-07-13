#ifndef ECG_TIMER_H
#define ECG_TIMER_H

#include <stdbool.h>
#include "esp_log.h"
#include "driver/gptimer.h"

#define ECG_SAMP_FREQ 360
#define RESOLUTION_HZ 100000

void Timer_Init(void);
void Timer_Start(void);
void Timer_Stop(void);
bool Reading_ECG_Sensor(gptimer_handle_t timer,
                        const gptimer_alarm_event_data_t *edata,
                        void *user_ctx);

#endif
