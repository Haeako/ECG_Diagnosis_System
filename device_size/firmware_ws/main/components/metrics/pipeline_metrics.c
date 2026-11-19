#include "pipeline_metrics.h"
#include <inttypes.h>
#include <limits.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"

#define METRICS_WINDOW_MS 10000U
#define EXPECTED_PERIOD_US 2770U
static const char *TAG = "PERF";
static const uint32_t bounds[] = {10,25,50,100,250,500,1000,1500,2000,2778,
                                  5000,10000,20000,50000,100000,UINT32_MAX};
#define HIST_BINS (sizeof(bounds)/sizeof(bounds[0]))
typedef struct { uint32_t bins[HIST_BINS], count, max; } hist_t;
typedef struct {
    uint64_t timer_ticks, processed, notify_overruns, sd_drops, ble_drops;
    uint64_t sd_write_errors, sd_bytes;
    int32_t jitter_min_ticks, jitter_max_ticks;
    uint32_t sd_queue_peak, ble_queue_peak;
    hist_t jitter_abs_ticks, dispatch_us, pipeline_us, sd_write_us;
} window_t;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static window_t window;
static TaskHandle_t handles[METRICS_TASK_COUNT];
static volatile bool active;
static volatile uint64_t last_isr_us;
static uint64_t previous_isr_us;

static void hist_add(hist_t *h, uint32_t value) {
    h->count++; if (value > h->max) h->max = value;
    for (size_t i=0; i<HIST_BINS; ++i) if (value <= bounds[i]) { h->bins[i]++; break; }
}
static uint32_t percentile(const hist_t *h, uint32_t p) {
    if (!h->count) return 0;
    uint32_t target=(h->count*p+99)/100, total=0;
    for (size_t i=0; i<HIST_BINS; ++i) { total+=h->bins[i]; if(total>=target) return bounds[i]; }
    return h->max;
}
void pipeline_metrics_timer_isr(uint64_t count, bool sent) {
    if (!active) return;
    uint64_t now=(uint64_t)esp_timer_get_time();
    portENTER_CRITICAL_ISR(&lock);
    window.timer_ticks++; if (!sent) window.notify_overruns++;
    if (previous_isr_us) {
        int32_t j=(int32_t)(now-previous_isr_us)-(int32_t)EXPECTED_PERIOD_US;
        if(j<window.jitter_min_ticks) window.jitter_min_ticks=j;
        if(j>window.jitter_max_ticks) window.jitter_max_ticks=j;
        hist_add(&window.jitter_abs_ticks,j<0?(uint32_t)-j:(uint32_t)j);
    }
    (void)count;
    previous_isr_us=now; last_isr_us=now;
    portEXIT_CRITICAL_ISR(&lock);
}
uint64_t pipeline_metrics_last_isr_us(void) { return last_isr_us; }
void pipeline_metrics_sample_begin(uint64_t start, uint64_t isr) {
    if (!active) return;
    uint32_t d=start>=isr?(uint32_t)(start-isr):0;
    portENTER_CRITICAL(&lock); window.processed++; hist_add(&window.dispatch_us,d); portEXIT_CRITICAL(&lock);
}
void pipeline_metrics_sample_end(uint32_t us) {
    if (!active) return;
    portENTER_CRITICAL(&lock); hist_add(&window.pipeline_us,us); portEXIT_CRITICAL(&lock);
}
void pipeline_metrics_sd_write(uint32_t us,size_t bytes,bool ok) {
    if (!active) return;
    portENTER_CRITICAL(&lock); hist_add(&window.sd_write_us,us);
    window.sd_bytes+=bytes; if(!ok) window.sd_write_errors++; portEXIT_CRITICAL(&lock);
}
void pipeline_metrics_sd_queue(size_t used,size_t cap) {
    if (!active||!cap)return;
    uint32_t p=(uint32_t)((used*1000U)/cap);
    portENTER_CRITICAL(&lock); if(p>window.sd_queue_peak)window.sd_queue_peak=p; portEXIT_CRITICAL(&lock);
}
void pipeline_metrics_ble_queue(UBaseType_t used,UBaseType_t cap) {
    if (!active||!cap)return;
    uint32_t p=(uint32_t)(((uint64_t)used*1000U)/cap);
    portENTER_CRITICAL(&lock); if(p>window.ble_queue_peak)window.ble_queue_peak=p; portEXIT_CRITICAL(&lock);
}
void pipeline_metrics_sd_drop(void){portENTER_CRITICAL(&lock);if(active)window.sd_drops++;portEXIT_CRITICAL(&lock);}
void pipeline_metrics_ble_drop(void){portENTER_CRITICAL(&lock);if(active)window.ble_drops++;portEXIT_CRITICAL(&lock);}
void pipeline_metrics_register_task(metrics_task_id_t id,TaskHandle_t h){if(id<METRICS_TASK_COUNT)handles[id]=h;}
void pipeline_metrics_start_session(void) {
    portENTER_CRITICAL(&lock); memset(&window,0,sizeof(window));
    window.jitter_min_ticks=INT32_MAX; window.jitter_max_ticks=INT32_MIN;
    previous_isr_us=0; last_isr_us=0; active=true; portEXIT_CRITICAL(&lock);
}
void pipeline_metrics_stop_session(void){active=false;}

static void metrics_task(void *arg) {
    (void)arg;
    ESP_LOGW(TAG,"PERF_CSV_HEADER,uptime_ms,timer_ticks,processed,notify_overruns,jitter_min_us,jitter_max_us,jitter_p99_abs_us,dispatch_p99_us,pipeline_p95_us,pipeline_p99_us,pipeline_max_us,sd_p95_us,sd_p99_us,sd_max_us,sd_bytes,sd_errors,sd_drops,ble_drops,sd_queue_peak_pct,ble_queue_peak_pct,stack_state_B,stack_ecg_B,stack_sd_B,stack_ble_B,cpu0_pct,cpu1_pct");
    int64_t prev_us=esp_timer_get_time();
    uint32_t prev_idle[2]={ulTaskGetIdleRunTimeCounterForCore(0),ulTaskGetIdleRunTimeCounterForCore(1)};
    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(METRICS_WINDOW_MS));
        int64_t now=esp_timer_get_time(); uint32_t elapsed=(uint32_t)(now-prev_us); prev_us=now;
        uint32_t idle[2]={ulTaskGetIdleRunTimeCounterForCore(0),ulTaskGetIdleRunTimeCounterForCore(1)},cpu[2];
        for(int c=0;c<2;c++){uint32_t d=idle[c]-prev_idle[c];prev_idle[c]=idle[c];
            cpu[c]=elapsed&&d<elapsed?1000U-(uint32_t)(((uint64_t)d*1000U)/elapsed):0;}
        window_t s; portENTER_CRITICAL(&lock);s=window;memset(&window,0,sizeof(window));
        window.jitter_min_ticks=INT32_MAX;window.jitter_max_ticks=INT32_MIN;portEXIT_CRITICAL(&lock);
        if(!active&&!s.timer_ticks)continue;
        uint32_t st[METRICS_TASK_COUNT]={0};for(int i=0;i<METRICS_TASK_COUNT;i++)if(handles[i])st[i]=uxTaskGetStackHighWaterMark(handles[i]);
        int32_t jmin=s.jitter_min_ticks==INT32_MAX?0:s.jitter_min_ticks;
        int32_t jmax=s.jitter_max_ticks==INT32_MIN?0:s.jitter_max_ticks;
        ESP_LOGW(TAG,"PERF_CSV,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%"PRId32",%"PRId32",%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%"PRIu64",%"PRIu64",%"PRIu64",%"PRIu64",%lu.%lu,%lu.%lu,%lu,%lu,%lu,%lu,%lu.%lu,%lu.%lu",
            (uint64_t)(now/1000),s.timer_ticks,s.processed,s.notify_overruns,jmin,jmax,
            (unsigned long)percentile(&s.jitter_abs_ticks,99),(unsigned long)percentile(&s.dispatch_us,99),
            (unsigned long)percentile(&s.pipeline_us,95),(unsigned long)percentile(&s.pipeline_us,99),(unsigned long)s.pipeline_us.max,
            (unsigned long)percentile(&s.sd_write_us,95),(unsigned long)percentile(&s.sd_write_us,99),(unsigned long)s.sd_write_us.max,
            s.sd_bytes,s.sd_write_errors,s.sd_drops,s.ble_drops,
            (unsigned long)(s.sd_queue_peak/10),(unsigned long)(s.sd_queue_peak%10),
            (unsigned long)(s.ble_queue_peak/10),(unsigned long)(s.ble_queue_peak%10),
            (unsigned long)st[0],(unsigned long)st[1],(unsigned long)st[2],(unsigned long)st[3],
            (unsigned long)(cpu[0]/10),(unsigned long)(cpu[0]%10),(unsigned long)(cpu[1]/10),(unsigned long)(cpu[1]%10));
    }
}
void pipeline_metrics_init(void) {
    memset(&window,0,sizeof(window));window.jitter_min_ticks=INT32_MAX;window.jitter_max_ticks=INT32_MIN;
    xTaskCreatePinnedToCore(metrics_task,"Metrics",4096,NULL,1,NULL,0);
}
