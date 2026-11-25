"""Exercise the production timer callback with deterministic peripheral stubs."""
from pathlib import Path
import shutil
import subprocess

import pytest


def test_timer_release_schedule_and_restart(tmp_path):
    compiler = shutil.which('gcc') or ('C:/msys64/ucrt64/bin/gcc.exe' if Path('C:/msys64/ucrt64/bin/gcc.exe').exists() else None)
    if not compiler:
        pytest.skip('Host C compiler unavailable')
    headers = {
        'esp_log.h': '''#include <assert.h>
#define ESP_OK 0
#define ESP_ERROR_CHECK(expr) assert((expr)==ESP_OK)
''',
        'freertos/FreeRTOS.h': '''#include <stddef.h>
typedef int BaseType_t;
#define pdFALSE 0
#define pdTRUE 1
''',
        'freertos/queue.h': '''typedef void *QueueHandle_t;
int xQueueSendFromISR(QueueHandle_t,const void *,int *);
''',
        'esp_timer.h': '#include <stdint.h>\nint64_t esp_timer_get_time(void);\n',
        'driver/gptimer.h': '''#include <stdbool.h>
#include <stdint.h>
typedef int esp_err_t;
typedef void *gptimer_handle_t;
typedef struct {uint64_t count_value;} gptimer_alarm_event_data_t;
typedef struct {int clk_src,direction;uint32_t resolution_hz;} gptimer_config_t;
typedef struct {uint64_t alarm_count,reload_count;struct {bool auto_reload_on_alarm;} flags;} gptimer_alarm_config_t;
typedef struct {bool (*on_alarm)(gptimer_handle_t,const gptimer_alarm_event_data_t *,void *);} gptimer_event_callbacks_t;
#define GPTIMER_CLK_SRC_DEFAULT 0
#define GPTIMER_COUNT_UP 0
int gptimer_new_timer(const gptimer_config_t *,gptimer_handle_t *);
int gptimer_get_resolution(gptimer_handle_t,uint32_t *);
int gptimer_set_alarm_action(gptimer_handle_t,const gptimer_alarm_config_t *);
int gptimer_register_event_callbacks(gptimer_handle_t,const gptimer_event_callbacks_t *,void *);
int gptimer_enable(gptimer_handle_t);
int gptimer_set_raw_count(gptimer_handle_t,uint64_t);
int gptimer_get_raw_count(gptimer_handle_t,uint64_t *);
int gptimer_start(gptimer_handle_t);
int gptimer_stop(gptimer_handle_t);
''',
    }
    for name, content in headers.items():
        path = tmp_path / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    root = Path(__file__).resolve().parents[1] / 'firmware_ws/main'
    harness = tmp_path / 'timer_test.c'
    harness.write_text('''#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#define PIPELINE_METRICS_H
void pipeline_metrics_set_period_us(uint32_t);
void pipeline_metrics_timer_isr(uint64_t,bool);
''' + '#include "' + (root / 'drivers/timer/timer.c').as_posix() + '"\n' + r'''
QueueHandle_t adc_release_queue_handle=(void *)2;
static gptimer_event_callbacks_t callbacks;
static uint64_t ticks,alarm,now=5000000;
static bool enabled,started,full;
static unsigned created,releases,overruns;
static timer_release_t last;
int64_t esp_timer_get_time(void) {return now;}
void pipeline_metrics_set_period_us(uint32_t p) {assert(p==2778);}
void pipeline_metrics_timer_isr(uint64_t us,bool sent) {assert(us==now);releases++;if(!sent)overruns++;}
int gptimer_new_timer(const gptimer_config_t *c,void **h) {assert(c->resolution_hz==1000000);created++;*h=(void *)1;return 0;}
int gptimer_get_resolution(void *h,uint32_t *r) {assert(h);*r=1000000;return 0;}
int gptimer_set_alarm_action(void *h,const gptimer_alarm_config_t *a) {assert(h && !a->flags.auto_reload_on_alarm);alarm=a->alarm_count;return 0;}
int gptimer_register_event_callbacks(void *h,const gptimer_event_callbacks_t *c,void *ctx) {assert(h && !enabled && !ctx);callbacks=*c;return 0;}
int gptimer_enable(void *h) {assert(h && callbacks.on_alarm);enabled=true;return 0;}
int gptimer_set_raw_count(void *h,uint64_t n) {assert(h && !started);ticks=n;return 0;}
int gptimer_get_raw_count(void *h,uint64_t *n) {assert(h);*n=ticks;return 0;}
int gptimer_start(void *h) {assert(h && enabled && !started && alarm>ticks);started=true;return 0;}
int gptimer_stop(void *h) {assert(h && started);started=false;return 0;}
int xQueueSendFromISR(void *q,const void *r,int *woken) {
    assert(q==adc_release_queue_handle);last=*(const timer_release_t *)r;
    *woken=full?0:1;return !full;
}
static bool fire(uint64_t late) {
    assert(started);ticks=alarm+late;now=Timer_EpochUs()+ticks;
    gptimer_alarm_event_data_t event={.count_value=ticks};
    return callbacks.on_alarm((void *)1,&event,NULL);
}
int main(void) {
    Timer_Init();Timer_Init();assert(created==1);
    Timer_Start();Timer_Start();uint64_t epoch=Timer_EpochUs();
    for(unsigned i=0;i<360;i++) {
        assert(fire(0));assert(last.index==i);
        assert(last.scheduled_us==epoch+((uint64_t)(i+1)*1000000)/360);
        assert(alarm>ticks);
    }
    assert(last.scheduled_us==epoch+1000000 && !Timer_SkippedReleases());
    full=true;assert(!fire(0));assert(overruns==1 && releases==361);full=false;
    assert(fire(10000));assert(Timer_SkippedReleases()==3 && alarm>ticks);
    uint32_t previous=last.index;assert(fire(0));assert(last.index==previous+4);
    Timer_Stop();Timer_Stop();now+=1000000;Timer_Start();
    assert(!Timer_SkippedReleases());assert(fire(0));assert(last.index==0);
    assert(last.scheduled_us==Timer_EpochUs()+2777 && Timer_PeriodUs()==2778);
    Timer_Stop();return 0;
}
''')
    binary = tmp_path / 'timer_test.exe'
    subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror', '-I', str(tmp_path), '-I', str(root), str(harness), '-o', str(binary)], check=True, capture_output=True, text=True)
    subprocess.run([str(binary)], check=True, capture_output=True, text=True)
