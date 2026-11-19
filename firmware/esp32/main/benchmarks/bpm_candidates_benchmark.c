/* Frozen selected C kernels, exact EDB timing-screen inputs, no peripherals. */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "esp_cpu.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bpm_candidates_vectors.h"
#include "components/bpm/ExtractFeature.h"
#include "components/qrs_detector/pan_pyecg.h"
#include "components/qrs_detector/elgendi_pyecg.h"
#include "components/qrs_detector/fast_qrs_port.h"
#include "components/qrs_detector/pan_tompkins_plus_plus_port.h"
#include "components/qrs_detector/wim_nguyen.h"
#include "components/qrs_detector/wim_elgendi.h"
#include "components/qrs_detector/wim_pantompkins.h"
#include "components/qrs_detector/wim_hamilton.h"
#include "components/qrs_detector/wim_arteaga_falconi.h"
#include "components/qrs_detector/wim_shaik.h"
#include "components/qrs_detector/wim_zhai.h"

#define REPEATS 5U
#define CAPACITY 128U
static float input[BPM_SAMPLES];
static uint16_t short_peaks[CAPACITY];
static uint32_t peaks[CAPACITY];
static uint32_t first_peaks[CAPACITY];
static union {
    pan_pyecg_workspace_t pan;
    elgendi_pyecg_workspace_t elgendi;
    fast_qrs_workspace_t fast;
    panpp_workspace_t panpp;
    wim_nguyen_workspace_t nguyen;
    wim_elgendi_workspace_t wim_elgendi;
    wim_pt_workspace_t pt;
    wim_hamilton_workspace_t hamilton;
    wim_af_workspace_t af;
    wim_shaik_workspace_t shaik;
    wim_zhai_workspace_t zhai;
} workspace;

static size_t run_method(unsigned method, unsigned c, size_t *bytes)
{
    size_t n = 0;
    memset(&workspace,0,sizeof(workspace));
    switch(method) {
    case 0:
        /* State lives in the runtime kernel, already counted in image DRAM. */
        *bytes = 0;
        ecg_signal_init(0);
        for (uint32_t i=0;i<BPM_SAMPLES;++i) {
            uint32_t t = (uint32_t)(((uint64_t)i*1000U+180U)/360U);
            ecg_record_t r = ecg_signal_process(bpm_adc[c][i],t);
            if (r.is_peak) {
                ESP_ERROR_CHECK(n<CAPACITY ? ESP_OK : ESP_ERR_INVALID_SIZE);
                peaks[n++] = r.r_peak_timestamp_ms;
            }
        }
        return n;
    case 1: *bytes=sizeof(workspace.pan); n=pan_pyecg_detect_360(input,BPM_SAMPLES,short_peaks,CAPACITY,&workspace.pan); break;
    case 2: *bytes=sizeof(workspace.elgendi); n=elgendi_pyecg_detect_360(input,BPM_SAMPLES,short_peaks,CAPACITY,&workspace.elgendi); break;
    case 3: *bytes=sizeof(workspace.fast); n=fast_qrs_detect_360(input,BPM_SAMPLES,short_peaks,CAPACITY,&workspace.fast); break;
    case 4: *bytes=sizeof(workspace.panpp); n=panpp_detect_360(input,BPM_SAMPLES,short_peaks,CAPACITY,&workspace.panpp); break;
    case 5: *bytes=sizeof(workspace.nguyen); n=wim_nguyen_detect_360(input,BPM_SAMPLES,short_peaks,CAPACITY,&workspace.nguyen); break;
    case 6: *bytes=sizeof(workspace.wim_elgendi); n=wim_elgendi_detect_360(input,BPM_SAMPLES,short_peaks,CAPACITY,&workspace.wim_elgendi); break;
    case 7: *bytes=sizeof(workspace.pt); n=wim_pt_detect_360(input,BPM_SAMPLES,short_peaks,CAPACITY,&workspace.pt); break;
    case 8: *bytes=sizeof(workspace.hamilton); n=wim_hamilton_detect_360(input,BPM_SAMPLES,short_peaks,CAPACITY,&workspace.hamilton); break;
    case 9: *bytes=sizeof(workspace.af); n=wim_af_detect_360(input,BPM_SAMPLES,short_peaks,CAPACITY,&workspace.af); break;
    case 10: *bytes=sizeof(workspace.shaik); n=wim_shaik_detect_360(input,BPM_SAMPLES,short_peaks,CAPACITY,&workspace.shaik); break;
    case 11: *bytes=sizeof(workspace.zhai); n=wim_zhai_detect_360(input,BPM_SAMPLES,short_peaks,CAPACITY,&workspace.zhai); break;
    default: ESP_ERROR_CHECK(ESP_ERR_INVALID_ARG);
    }
    ESP_ERROR_CHECK(n<CAPACITY ? ESP_OK : ESP_ERR_INVALID_SIZE);
    for (size_t i=0;i<n;++i) {
        ESP_ERROR_CHECK(short_peaks[i]<BPM_SAMPLES ? ESP_OK : ESP_ERR_INVALID_SIZE);
        peaks[i]=short_peaks[i];
    }
    return n;
}

void ecg_bpm_candidates_run(void)
{
    printf("BPM_BEGIN,cases=%u,methods=%u,samples=%u,repeats=%u,warmups=1,cpu_mhz=%d\n",
           BPM_CASES,BPM_METHODS,BPM_SAMPLES,REPEATS,CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
    for (unsigned c=0;c<BPM_CASES;++c) {
        for (unsigned i=0;i<BPM_SAMPLES;++i)
            input[i]=((float)bpm_adc[c][i]-2048.0f)/200.0f;
        for (unsigned method=0;method<BPM_METHODS;++method) {
            uint32_t cycles[REPEATS];
            size_t bytes=0;
            (void)run_method(method,c,&bytes);
            size_t first_count=0;
            for (unsigned repeat=0;repeat<REPEATS;++repeat) {
                uint32_t begin=esp_cpu_get_cycle_count();
                size_t count=run_method(method,c,&bytes);
                cycles[repeat]=esp_cpu_get_cycle_count()-begin;
                if (repeat==0) {
                    first_count=count;
                    memcpy(first_peaks,peaks,count*sizeof(peaks[0]));
                } else {
                    ESP_ERROR_CHECK(count==first_count && memcmp(first_peaks,peaks,count*sizeof(peaks[0]))==0 ? ESP_OK : ESP_ERR_INVALID_STATE);
                }
            }
            printf("BPM_RESULT,case=%u,method=%s,unit=%s,count=%u,workspace_bytes=%u,cycles=",
                   c,bpm_method_names[method],method==0 ? "ms" : "samples",(unsigned)first_count,(unsigned)bytes);
            for (unsigned r=0;r<REPEATS;++r) printf("%s%" PRIu32,r ? ":" : "",cycles[r]);
            printf(",peaks=");
            for (size_t i=0;i<first_count;++i) printf("%s%" PRIu32,i ? ":" : "",first_peaks[i]);
            printf("\n");
            vTaskDelay(1);
        }
    }
    printf("BPM_END\n");
}
