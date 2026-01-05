#include "timer.h"
#include "os/semaphore.h"

extern char *ECG_Sensor;
extern SemaphoreHandle_t adc_timer_semaphore_handle;
extern gptimer_handle_t adc_timer_handle;

bool IRAM_ATTR Reading_ECG_Sensor(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(adc_timer_semaphore_handle, &xHigherPriorityTaskWoken);
    return (xHigherPriorityTaskWoken == pdTRUE);
}

void Timer_Init(void){
    gptimer_config_t timer_config = {
    .clk_src = GPTIMER_CLK_SRC_DEFAULT, // Select the default clock source
    .direction = GPTIMER_COUNT_UP,      // Counting direction is up
    .resolution_hz = Timer_Frequency,   // Resolution is 10 kHz, i.e., 1 tick equals 0.1 millisecond
};
// Create a timer instance
ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &adc_timer_handle));
// Setting count value
ESP_ERROR_CHECK(gptimer_set_raw_count(adc_timer_handle, 0));
//Setting timer in alarm mode
gptimer_alarm_config_t adc_timer_alarm_cfg = {
    .alarm_count = (Timer_Frequency / (ECG_Sampling_Frequency * 10))  ,
    .reload_count = 0,
    .flags.auto_reload_on_alarm = true,
};
ESP_ERROR_CHECK(gptimer_set_alarm_action(adc_timer_handle, &adc_timer_alarm_cfg));
// Configure alarm action callback function
gptimer_event_callbacks_t adc_timer_cbs = {
    .on_alarm = Reading_ECG_Sensor,
};
ESP_ERROR_CHECK(gptimer_register_event_callbacks(adc_timer_handle, &adc_timer_cbs, NULL));
// Enable the timer
ESP_ERROR_CHECK(gptimer_enable(adc_timer_handle));
// Start the timer
ESP_ERROR_CHECK(gptimer_start(adc_timer_handle));
ESP_LOGI(ECG_Sensor, "ECG timer started at %d Hz", ECG_Sampling_Frequency);
}