#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "replay_inputs.h"
#include "replay_dispatch.h"

static float input[3600];
static uint16_t peaks[128];

void app_main(void)
{
    printf("REPLAY_BEGIN,cases=66,methods_max=12,cpu_mhz=%d\n",CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    unsigned methods=0;
    for(unsigned method=0;method<12;++method) {
        size_t bytes=workspace_size(method);
        if(!bytes)continue;
        void *workspace=heap_caps_malloc(bytes,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
        if(!workspace){
            printf("REPLAY_SKIP,method=%u,reason=contiguous_workspace_allocation,bytes=%u,largest_block=%u,heap_free=%u\n",method,(unsigned)bytes,(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            continue;
        }
        printf("REPLAY_MODULE,method=%u,name=%s,workspace=%u,heap_free=%u\n",method,method_name(method),(unsigned)bytes,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        for(unsigned c=0;c<66;++c) {
            for(unsigned i=0;i<3600;++i)input[i]=((float)replay_adc[c][i]-2048.0f)/200.0f;
            memset(workspace,0,bytes);
            int64_t start=esp_timer_get_time();
            size_t n=detect(method,input,peaks,workspace);
            int64_t us=esp_timer_get_time()-start;
            if(n>=128){printf("REPLAY_ERROR,method=%u,case=%u,reason=peak_capacity\n",method,c);free(workspace);return;}
            printf("REPLAY_RESULT,%u,%u,%" PRId64 ",%u",method,c,us,(unsigned)n);
            for(size_t i=0;i<n;++i)printf(",%u",(unsigned)peaks[i]);
            printf("\n");
            vTaskDelay(1);
        }
        free(workspace);++methods;
    }
    printf("ECG_RESOURCE,profile=bpm-c-module-replay,methods=%u,internal_heap_min_bytes=%u,stack_high_water_bytes=%u\n",methods,(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),(unsigned)uxTaskGetStackHighWaterMark(NULL));
    printf("ECG_BENCHMARK_END\n");
}
