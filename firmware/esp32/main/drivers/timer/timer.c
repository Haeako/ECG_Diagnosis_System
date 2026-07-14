#include "timer.h"
#include "../../components/os/semaphore.h"

extern char *ECG_Sensor;
extern SemaphoreHandle_t adc_timer_semaphore_handle;
extern gptimer_handle_t adc_timer_handle;

static bool adc_timer_running = false;

bool IRAM_ATTR Reading_ECG_Sensor(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(adc_timer_semaphore_handle, &xHigherPriorityTaskWoken);
    return (xHigherPriorityTaskWoken == pdTRUE);
}

/**
 * @brief Initialize timer
 *
 * This function configures timer
 *
 * @param void 
 *
 * @return
 *      - None
 */
void Timer_Init(void) 
{
    //timer + alarm configuation
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &adc_timer_handle));
    ESP_ERROR_CHECK(gptimer_set_raw_count(adc_timer_handle, 0));

    uint32_t actual_resolution_hz = 0;
    ESP_ERROR_CHECK(
        gptimer_get_resolution(adc_timer_handle, &actual_resolution_hz));

    gptimer_alarm_config_t adc_timer_alarm_cfg = {
        .alarm_count = actual_resolution_hz / ECG_SAMP_FREQ,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };

    ESP_ERROR_CHECK(gptimer_set_alarm_action(adc_timer_handle, &adc_timer_alarm_cfg));
  
    gptimer_event_callbacks_t adc_timer_cbs = {.on_alarm = Reading_ECG_Sensor};
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(adc_timer_handle, &adc_timer_cbs, NULL));
    // Enable timer
    ESP_ERROR_CHECK(gptimer_enable(adc_timer_handle));
    ESP_LOGI(ECG_Sensor,
             "ECG timer configured at %d Hz (resolution: %lu Hz)",
             ECG_SAMP_FREQ, (unsigned long)actual_resolution_hz);
}

void Timer_Start(void)
{
    if (adc_timer_handle == NULL || adc_timer_running) {
        return;
    }

    ESP_ERROR_CHECK(gptimer_set_raw_count(adc_timer_handle, 0));
    ESP_ERROR_CHECK(gptimer_start(adc_timer_handle));
    adc_timer_running = true;
    ESP_LOGI(ECG_Sensor, "ECG timer started");
}

void Timer_Stop(void)
{
    if (adc_timer_handle == NULL || !adc_timer_running) {
        return;
    }

    ESP_ERROR_CHECK(gptimer_stop(adc_timer_handle));
    adc_timer_running = false;
    ESP_LOGI(ECG_Sensor, "ECG timer stopped");
}
