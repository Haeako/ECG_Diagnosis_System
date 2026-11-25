#include "timer.h"
#include "freertos/FreeRTOS.h"
#include "../../components/metrics/pipeline_metrics.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "components/processing/sample_clock.h"

extern QueueHandle_t adc_release_queue_handle;
static gptimer_handle_t adc_timer_handle;

static bool adc_timer_running = false;
static uint32_t timer_resolution_hz;
static const uint32_t period_us = (1000000U + ECG_SAMP_FREQ - 1U) / ECG_SAMP_FREQ;
static const uint32_t rate_hz = ECG_SAMP_FREQ;
static uint32_t skipped_releases;
static uint64_t next_ordinal=1,epoch_us;
uint64_t Timer_EpochUs(void){return epoch_us;}
uint32_t Timer_SkippedReleases(void){return skipped_releases;}

static bool Reading_ECG_Sensor(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    (void)user_ctx;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint64_t ordinal=next_ordinal;
    timer_release_t release={.scheduled_us=epoch_us+ecg_sample_us(ordinal,rate_hz),
        .isr_us=(uint64_t)esp_timer_get_time(),.index=(uint32_t)(ordinal-1)};
    uint64_t now_ticks=edata->count_value;
    (void)gptimer_get_raw_count(timer,&now_ticks);
    next_ordinal=ordinal+1;
    while(ecg_sample_ticks(next_ordinal,timer_resolution_hz,rate_hz)<=now_ticks+2U) {next_ordinal++;skipped_releases++;}
    gptimer_alarm_config_t next={.alarm_count=ecg_sample_ticks(next_ordinal,timer_resolution_hz,rate_hz)};
    if(gptimer_set_alarm_action(timer,&next)!=ESP_OK)skipped_releases++;
    BaseType_t sent=xQueueSendFromISR(adc_release_queue_handle,&release,&xHigherPriorityTaskWoken);
    pipeline_metrics_timer_isr(release.isr_us,sent==pdTRUE);
    return (xHigherPriorityTaskWoken == pdTRUE);
}

void Timer_Init(void)
{
    if (adc_timer_handle != NULL) return;
    const gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = RESOLUTION_HZ,
    };
    ESP_ERROR_CHECK(gptimer_new_timer(&config, &adc_timer_handle));
    ESP_ERROR_CHECK(gptimer_get_resolution(adc_timer_handle, &timer_resolution_hz));
    const gptimer_alarm_config_t alarm = {
        .alarm_count = ecg_sample_ticks(1, timer_resolution_hz, rate_hz),
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(adc_timer_handle, &alarm));
    pipeline_metrics_set_period_us(period_us);
    const gptimer_event_callbacks_t callbacks = {.on_alarm = Reading_ECG_Sensor};
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(adc_timer_handle, &callbacks, NULL));
    ESP_ERROR_CHECK(gptimer_enable(adc_timer_handle));
}


uint32_t Timer_PeriodUs(void) { return period_us; }

void Timer_Start(void)
{
    if (adc_timer_handle == NULL || adc_timer_running) {
        return;
    }

    ESP_ERROR_CHECK(gptimer_set_raw_count(adc_timer_handle, 0));
    next_ordinal=1;skipped_releases=0;
    gptimer_alarm_config_t first={.alarm_count=ecg_sample_ticks(1,timer_resolution_hz,rate_hz)};
    ESP_ERROR_CHECK(gptimer_set_alarm_action(adc_timer_handle,&first));
    uint64_t before=esp_timer_get_time();
    epoch_us=before;
    ESP_ERROR_CHECK(gptimer_start(adc_timer_handle));
    adc_timer_running = true;
}

void Timer_Stop(void)
{
    if (adc_timer_handle == NULL || !adc_timer_running) {
        return;
    }

    ESP_ERROR_CHECK(gptimer_stop(adc_timer_handle));
    adc_timer_running = false;
}
