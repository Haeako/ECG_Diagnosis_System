/* Current production computational kernels, linked without peripheral tasks. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "components/bpm/ExtractFeature.h"
#include "components/af_model/af_runtime.h"
#include "components/sqi_gate/sqi_runtime.h"
#include "replay_vectors.h"

extern const uint8_t replay_adc_start[] asm("_binary_replay_adc_start");
extern const uint8_t replay_adc_end[] asm("_binary_replay_adc_end");
#define CPU_MHZ ((double)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)
#define EVENT_CAPACITY 512u
#define BLOCK_SAMPLES 3600u
static uint32_t sample_cycles[BLOCK_SAMPLES];
static uint32_t af_cycles[EVENT_CAPACITY], sqi_cycles[EVENT_CAPACITY];
static uint32_t sqi_finalize_us[EVENT_CAPACITY];

static int compare_u32(const void *a, const void *b)
{
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

static uint32_t quantile(const uint32_t *v, uint32_t n, uint32_t p)
{
    return n ? v[(n * p + 99u) / 100u - 1u] : 0u;
}

static void report_times(const char *record, const char *scope,
                         uint32_t *v, uint32_t n, double divisor)
{
    if (!n) {
        printf("PIPE_REPLAY_STAT,%s,%s,0,0,0,0,0,0,0\n", record, scope);
        return;
    }
    uint64_t sum = 0;
    for (uint32_t i = 0; i < n; ++i) sum += v[i];
    qsort(v, n, sizeof(v[0]), compare_u32);
    printf("PIPE_REPLAY_STAT,%s,%s,%"PRIu32",%.4f,%.4f,%.4f,%.4f,%.4f,%.4f\n",
           record, scope, n, sum / divisor, sum / divisor / n,
           quantile(v,n,50) / divisor, quantile(v,n,95) / divisor,
           quantile(v,n,99) / divisor, v[n-1] / divisor);
}

static uint32_t mix(uint32_t hash, uint32_t value)
{
    return (hash ^ value) * 16777619u;
}

/* Preserve exact cycle counts while reusing a 14.4 KiB buffer. UART emission
 * and sorting happen after the block's last measured kernel call. Each line
 * holds at most32 RLE pairs, keeping serial readline timeouts manageable. */
static void report_histogram(const char *record, uint32_t block)
{
    qsort(sample_cycles, BLOCK_SAMPLES, sizeof(sample_cycles[0]), compare_u32);
    uint32_t pairs=0u;
    for (uint32_t i=0u; i<BLOCK_SAMPLES;) {
        uint32_t j=i+1u;
        while (j<BLOCK_SAMPLES && sample_cycles[j]==sample_cycles[i]) ++j;
        if (pairs==0u) printf("PIPE_REPLAY_HIST,%s,%"PRIu32,record,block);
        printf(",%"PRIu32":%"PRIu32,sample_cycles[i],j-i);
        ++pairs;
        if (pairs==32u) { printf("\n"); pairs=0u; }
        i=j;
    }
    if (pairs) printf("\n");
}

static void run_record(const replay_record_t *record)
{
    const int16_t *adc = (const int16_t *)replay_adc_start + record->offset;
    uint32_t input_hash = 2166136261u, output_hash = 2166136261u;
    for (uint32_t i = 0; i < record->samples; ++i) input_hash = mix(input_hash, (uint16_t)adc[i]);
    if (input_hash != record->adc_fnv) {
        printf("PIPE_REPLAY_ERROR,adc_hash,%s,%"PRIu32",%"PRIu32"\n",record->record,input_hash,record->adc_fnv);
        abort();
    }
    ecg_signal_init(0u);
    af_runtime_reset();
    sqi_runtime_reset(0u);
    uint32_t n_af=0, n_sqi=0, peaks=0, af_valid=0, last_sqi=0, over_period=0;
    uint64_t block_sum=0;
    size_t heap_before=heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    for (uint32_t i = 0; i < record->samples; ++i) {
        uint32_t timestamp=(uint32_t)(((uint64_t)i*1000u+180u)/360u);
        uint32_t start=esp_cpu_get_cycle_count();
        ecg_record_t r=ecg_signal_process(adc[i], timestamp);
        uint32_t af_start=esp_cpu_get_cycle_count();
        af_runtime_result_t a=r.is_peak ? af_runtime_push_peak(r.r_peak_timestamp_ms) : af_runtime_latest();
        uint32_t af_elapsed=esp_cpu_get_cycle_count()-af_start;
        uint32_t sqi_start=esp_cpu_get_cycle_count();
        ecg_sqi_result_t q=sqi_runtime_push(r.corrected,r.is_peak!=0u,r.r_peak_timestamp_ms,timestamp);
        uint32_t sqi_elapsed=esp_cpu_get_cycle_count()-sqi_start;
        uint32_t elapsed=esp_cpu_get_cycle_count()-start;
        sample_cycles[i%BLOCK_SAMPLES]=elapsed;
        block_sum+=elapsed;
        if (elapsed / CPU_MHZ > 1000000.0 / 360.0) ++over_period;
        output_hash=mix(output_hash,(uint16_t)r.corrected);
        output_hash=mix(output_hash,r.bpm);
        if (r.is_peak) {
            ++peaks;
            if (a.valid) ++af_valid;
            printf("PIPE_REPLAY_PEAK,%s,%"PRIu32",%"PRIu32",%u,%u,%u,%u,%u,%u,%u\n",
                   record->record,timestamp,r.r_peak_timestamp_ms,a.valid,a.new_window,
                   a.score,a.probability_x10000,(unsigned)a.window_id,a.raw_prediction,a.confirmed);
        }
        if (a.new_window) {
            if (n_af >= EVENT_CAPACITY) abort();
            af_cycles[n_af++]=af_elapsed;
        }
        if (q.window_id!=last_sqi) {
            if (n_sqi >= EVENT_CAPACITY) abort();
            sqi_cycles[n_sqi]=sqi_elapsed;
            sqi_finalize_us[n_sqi++]=q.finalize_us;
            last_sqi=q.window_id;
            printf("PIPE_REPLAY_SQI,%s,%"PRIu32",%"PRIu32",%u,%u,%u,%u,%u,%u,%d\n",
                   record->record,timestamp,q.window_id,q.status,q.score,q.rule_mask,
                   q.heart_rate_bpm,q.max_rr_ms,q.rr_ratio_x1000,q.template_correlation_x1000);
        }
        if ((i+1u)%3600u==0u) {
            printf("PIPE_REPLAY_BLOCK,%s,%"PRIu32",%.4f,%u,%u,%u,%u\n",record->record,
                   (i+1u)/3600u,block_sum/CPU_MHZ,r.bpm,a.valid,a.score,q.status);
            report_histogram(record->record,(i+1u)/BLOCK_SAMPLES);
            block_sum=0;
            vTaskDelay(1); /* outside active timing; idle/WDT can run */
        }
    }
    report_times(record->record,"af_new_window_call",af_cycles,n_af,CPU_MHZ);
    report_times(record->record,"sqi_evaluation_call",sqi_cycles,n_sqi,CPU_MHZ);
    report_times(record->record,"sqi_internal_finalize",sqi_finalize_us,n_sqi,1.0);
    printf("PIPE_REPLAY_RECORD,%s,%"PRIu32",%"PRIu32",%"PRIu32",%"PRIu32",%"PRIu32",%"PRIu32",%"PRIu32",%"PRIu32",%u,%u,%u,%u\n",
           record->record,record->samples,peaks,n_af,af_valid,n_sqi,input_hash,output_hash,over_period,
           (unsigned)heap_before,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
           (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(1000));
    printf("PIPE_REPLAY_BEGIN,records=%u,fs=360,cpu_mhz=%d,vector_bytes=%u,vector_sha256=%s\n",
           REPLAY_RECORD_COUNT,CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
           (unsigned)(replay_adc_end-replay_adc_start),REPLAY_VECTOR_SHA256);
    printf("PIPE_REPLAY_SCOPE,active_DSP_only;unchanged_QRS_RR_AF_SQI;no_ADC_queue_BLE_SD\n");
    printf("PIPE_REPLAY_MEMORY,timing_arrays_bytes=%u,ecg_record_bytes=%u\n",
           (unsigned)(sizeof(sample_cycles)+sizeof(af_cycles)+sizeof(sqi_cycles)+sizeof(sqi_finalize_us)),
           (unsigned)sizeof(ecg_record_t));
    printf("PIPE_REPLAY_STAT_HEADER,record,scope,n,total_us,mean_us,p50_us,p95_us,p99_us,max_us\n");
    printf("PIPE_REPLAY_BLOCK_HEADER,record,block,total_compute_us,bpm,af_valid,af_score,sqi_status\n");
    printf("PIPE_REPLAY_RECORD_HEADER,record,samples,peaks,af_inferences,af_valid_peak_events,sqi_evaluations,adc_fnv32,corrected_bpm_fnv32,samples_over_nominal_period,heap_before_B,heap_after_B,heap_min_B,stack_high_water_B\n");
    for (uint32_t i=0; i<REPLAY_RECORD_COUNT; ++i) run_record(&replay_records[i]);
    printf("PIPE_REPLAY_END\n");
}
