#include "driver/gptimer_types.h"
#include "driver/gptimer.h"
#include "esp_log.h"

#define ECG_Sampling_Frequency  500
#define Timer_Frequency         10000

void Timer_Init();
bool Reading_ECG_Sensor(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx);