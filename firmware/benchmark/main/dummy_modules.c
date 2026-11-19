/* Independent modules consume a deterministic dummy fixture on logical time. */
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_cpu.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "components/bpm/ExtractFeature.h"
_Static_assert(BPM_UPDATE_MS==5000U && ECG_SAMPLE_RATE_HZ==360U,"Review dummy ring layout after production cadence changes");
#if ECG_TEST_BPM
#include "components/qrs_detector/fast_qrs_port.h"
static fast_qrs_workspace_t workspace;
static float bpm_samples[3600];
static uint16_t bpm_peaks[128];
static uint32_t bpm_windows;
static uint64_t bpm_cycles;
#endif
#if ECG_TEST_SQI
#include "components/sqi_gate/sqi_runtime.h"
static uint32_t sqi_windows;
static uint64_t sqi_cycles;
#endif
#if ECG_TEST_AF
#include "components/af_model/af_runtime.h"
static uint32_t af_windows;
static uint64_t af_cycles;
#endif

/* 288 samples/RR at360Hz =800ms =75BPM. Peaks are supplied independently
 * to SQI/AF so changing the tested BPM detector cannot change their input. */
static int16_t dummy_sample(uint32_t sample)
{
    int phase=(int)(sample%288U);
    int r=abs(phase-108),p=abs(phase-50),t=abs(phase-190);
    return (int16_t)((r<4 ? 1000-r*250 : 0)+(p<10 ? 60-p*6 : 0)+(t<20 ? 160-t*8 : 0));
}

void app_main(void)
{
    printf("DUMMY_BEGIN,bpm=%d,sqi=%d,af=%d,fs=360,seconds=%d,fixture=regular75bpm,clock=logical_accelerated,bpm_context_ms=10000,bpm_hop_ms=%u,sqi_window_ms=10000,sqi_hop_ms=1000,af_rr=30,af_stride_rr=15\n",
        ECG_TEST_BPM,ECG_TEST_SQI,ECG_TEST_AF,ECG_TEST_SECONDS,BPM_UPDATE_MS);
#if ECG_TEST_SQI
    sqi_runtime_reset(0);
#endif
#if ECG_TEST_AF
    af_runtime_reset();
#endif
    for(uint32_t i=0;i<ECG_TEST_SECONDS*360U;++i) {
        uint32_t now=(uint32_t)(((uint64_t)(i+1)*1000U)/360U);
        int16_t sample=dummy_sample(i);
        bool peak=i%288U==108U;
        (void)sample;(void)peak;
#if ECG_TEST_BPM
        bpm_samples[i%3600U]=sample/200.0f;
        if(i+1>=3600U && (i+1-3600U)%(BPM_UPDATE_MS*360U/1000U)==0) {
            /* At5s hops, rotate the ring in-place into chronological order. */
            uint32_t head=(i+1)%3600U;
            if(head) {
                for(unsigned j=0;j<1800;++j) {
                    float tmp=bpm_samples[j];bpm_samples[j]=bpm_samples[j+1800];bpm_samples[j+1800]=tmp;
                }
            }
            uint32_t begin=esp_cpu_get_cycle_count();
            memset(&workspace,0,sizeof(workspace));
            size_t n=fast_qrs_detect_360(bpm_samples,3600,bpm_peaks,128,&workspace);
            ESP_ERROR_CHECK(n<128 ? ESP_OK : ESP_ERR_INVALID_SIZE);
            double sum=0;unsigned intervals=0;
            for(size_t j=1;j<n;++j) {
                ESP_ERROR_CHECK(bpm_peaks[j]>bpm_peaks[j-1] ? ESP_OK : ESP_ERR_INVALID_STATE);
                if(bpm_peaks[j]>=1800) {sum+=bpm_peaks[j]-bpm_peaks[j-1];++intervals;}
            }
            float bpm=intervals ? (float)(21600.0*intervals/sum) : NAN;
            uint32_t cycles=esp_cpu_get_cycle_count()-begin;bpm_cycles+=cycles;++bpm_windows;
            printf("DUMMY_BPM,at_ms=%u,window=%u,peaks=%u,bpm=%.4f,cycles=%" PRIu32 "\n",
                (unsigned)now,(unsigned)bpm_windows,(unsigned)n,(double)bpm,cycles);
            /* Restore ring layout before the next sample is pushed. */
            if(head)for(unsigned j=0;j<1800;++j) {
                float tmp=bpm_samples[j];bpm_samples[j]=bpm_samples[j+1800];bpm_samples[j+1800]=tmp;
            }
        }
#endif
#if ECG_TEST_SQI
        uint32_t sqi_begin=esp_cpu_get_cycle_count();
        ecg_sqi_result_t quality=sqi_runtime_push(sample,peak,now,now);
        sqi_cycles+=(uint32_t)(esp_cpu_get_cycle_count()-sqi_begin);
        if(quality.window_id>sqi_windows) {
            sqi_windows=quality.window_id;
            printf("DUMMY_SQI,at_ms=%u,window=%u,status=%u,score=%u,finalize_us=%u\n",
                (unsigned)now,(unsigned)sqi_windows,(unsigned)quality.status,(unsigned)quality.score,(unsigned)quality.finalize_us);
        }
#endif
#if ECG_TEST_AF
        if(peak) {
            uint32_t af_begin=esp_cpu_get_cycle_count();
            af_runtime_result_t result=af_runtime_push_peak(now);
            af_cycles+=(uint32_t)(esp_cpu_get_cycle_count()-af_begin);
            if(result.new_window) {
                ++af_windows;
                printf("DUMMY_AF,at_ms=%u,window=%u,valid=%u,probability_x10000=%u,confirmed=%u\n",
                    (unsigned)now,(unsigned)result.window_id,(unsigned)result.valid,(unsigned)result.probability_x10000,(unsigned)result.confirmed);
            }
        }
#endif
        if((i+1)%360U==0)vTaskDelay(1);
    }
#if ECG_TEST_BPM
    ESP_ERROR_CHECK(bpm_windows>0 ? ESP_OK : ESP_ERR_INVALID_STATE);
    printf("DUMMY_COST,module=BPM,windows=%u,total_cycles=%" PRIu64 ",workspace_bytes=%u\n",(unsigned)bpm_windows,bpm_cycles,(unsigned)sizeof(workspace));
#endif
#if ECG_TEST_SQI
    ESP_ERROR_CHECK(sqi_windows>0 ? ESP_OK : ESP_ERR_INVALID_STATE);
    printf("DUMMY_COST,module=SQI,windows=%u,total_cycles=%" PRIu64 "\n",(unsigned)sqi_windows,sqi_cycles);
#endif
#if ECG_TEST_AF
    ESP_ERROR_CHECK(af_windows>0 ? ESP_OK : ESP_ERR_INVALID_STATE);
    printf("DUMMY_COST,module=AF,windows=%u,total_cycles=%" PRIu64 "\n",(unsigned)af_windows,af_cycles);
#endif
    printf("ECG_RESOURCE,profile=dummy-modules,cpu_mhz=%d,internal_heap_free_bytes=%u,internal_heap_min_bytes=%u,main_stack_min_free_bytes=%u,scope=whole_benchmark_image\n",
        CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),(unsigned)uxTaskGetStackHighWaterMark(NULL));
    printf("ECG_BENCHMARK_END\n");
}
