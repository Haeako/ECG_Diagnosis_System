#include "sqi_device_benchmark.h"
#include "sqi_kramer.h"
#include "esp_cpu.h"
#include "esp_log.h"
#include <inttypes.h>

#define N 5000U
#define R 201U
#define MHZ ((double)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)
static sqi_kramer_state_t state;
static uint32_t push_cycles[N], final_cycles[R];
static void sort(uint32_t *a, uint32_t n) { for(uint32_t i=1;i<n;i++){uint32_t v=a[i],j=i;while(j&&a[j-1]>v){a[j]=a[j-1];j--;}a[j]=v;} }
void sqi_device_benchmark_run(void) {
  volatile float sink=0; sqi_kramer_reset(&state);
  for(uint32_t i=0;i<N;i++){float x=(float)(i%101U);uint32_t t=esp_cpu_get_cycle_count();sqi_kramer_push(&state,x);push_cycles[i]=esp_cpu_get_cycle_count()-t;}
  for(uint32_t i=0;i<R;i++){uint32_t t=esp_cpu_get_cycle_count();sqi_kramer_result_t r=sqi_kramer_finalize(&state,10U,false);final_cycles[i]=esp_cpu_get_cycle_count()-t;sink+=r.score;}
  sort(push_cycles,N);sort(final_cycles,R);
  ESP_LOGI("SQI_BENCH","RESULT method=Kramer-rules samples=%u push_p50_us=%.3f push_p99_us=%.3f finalize_p50_us=%.3f finalize_p99_us=%.3f state_bytes=%u static_bytes=%u sink=%.1f",N,push_cycles[N/2]/MHZ,push_cycles[(99U*(N-1U))/100U]/MHZ,final_cycles[R/2]/MHZ,final_cycles[(99U*(R-1U))/100U]/MHZ,(unsigned)sizeof(state),(unsigned)sqi_kramer_static_bytes(),(double)sink);
}
