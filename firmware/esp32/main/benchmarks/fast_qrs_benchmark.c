/* Isolated Fast-QRS module on the frozen 21 EDB screen inputs. */
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_cpu.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bpm_candidates_vectors.h"
#include "components/qrs_detector/fast_qrs_port.h"

static fast_qrs_workspace_t workspace;
static float input[BPM_SAMPLES];
static uint16_t peaks[FAST_QRS_MAX_PEAKS], first[FAST_QRS_MAX_PEAKS];

static size_t run(float *bpm)
{
    memset(&workspace,0,sizeof(workspace));
    size_t count=fast_qrs_detect_360(input,BPM_SAMPLES,peaks,FAST_QRS_MAX_PEAKS,&workspace);
    ESP_ERROR_CHECK(count<FAST_QRS_MAX_PEAKS ? ESP_OK : ESP_ERR_INVALID_SIZE);
    double total_rr_samples=0;
    for(size_t i=0;i<count;++i) {
        ESP_ERROR_CHECK(peaks[i]<BPM_SAMPLES ? ESP_OK : ESP_ERR_INVALID_SIZE);
        if(i) {
            ESP_ERROR_CHECK(peaks[i]>peaks[i-1] ? ESP_OK : ESP_ERR_INVALID_STATE);
            total_rr_samples+=peaks[i]-peaks[i-1];
        }
    }
    *bpm=count>=2 ? (float)(60.0*360.0*(count-1)/total_rr_samples) : NAN;
    return count;
}

void app_main(void)
{
    printf("FAST_BEGIN,cases=%u,samples=%u,repeats=5,warmups=1,cpu_mhz=%d,workspace_bytes=%u,input_bytes=%u,peak_buffer_bytes=%u\n",
        BPM_CASES,BPM_SAMPLES,CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,(unsigned)sizeof(workspace),
        (unsigned)sizeof(input),(unsigned)(sizeof(peaks)+sizeof(first)));
    for(unsigned c=0;c<BPM_CASES;++c) {
        for(unsigned i=0;i<BPM_SAMPLES;++i)input[i]=((float)bpm_adc[c][i]-2048.0f)/200.0f;
        float bpm=NAN;size_t first_count=run(&bpm);float first_bpm=bpm;
        memcpy(first,peaks,first_count*sizeof(peaks[0]));
        uint32_t cycles[5];
        for(unsigned repeat=0;repeat<5;++repeat) {
            uint32_t begin=esp_cpu_get_cycle_count();
            size_t count=run(&bpm);
            cycles[repeat]=esp_cpu_get_cycle_count()-begin;
            ESP_ERROR_CHECK(count==first_count && memcmp(first,peaks,count*sizeof(peaks[0]))==0 ? ESP_OK : ESP_ERR_INVALID_STATE);
            ESP_ERROR_CHECK((isnan(bpm)&&isnan(first_bpm)) || bpm==first_bpm ? ESP_OK : ESP_ERR_INVALID_STATE);
        }
        printf("FAST_RESULT,case=%u,count=%u,bpm=%.9g,cycles=",c,(unsigned)first_count,(double)bpm);
        for(unsigned r=0;r<5;++r)printf("%s%" PRIu32,r ? ":" : "",cycles[r]);
        printf(",peaks=");
        for(size_t i=0;i<first_count;++i)printf("%s%u",i ? ":" : "",(unsigned)first[i]);
        printf("\n");vTaskDelay(1);
    }
    printf("ECG_RESOURCE,profile=%s,cpu_mhz=%d,internal_heap_free_bytes=%u,internal_heap_min_bytes=%u,main_stack_min_free_bytes=%u,scope=whole_benchmark_image\n",
        ECG_PROFILE_NAME,CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),(unsigned)uxTaskGetStackHighWaterMark(NULL));
    printf("ECG_BENCHMARK_END\n");
}
