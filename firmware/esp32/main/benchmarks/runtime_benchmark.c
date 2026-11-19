/* Synthetic inputs exercise only the selected runtime, without other modules. */
#include <stdio.h>
#include <stdint.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if ECG_MODULE == 1
#include "components/bpm/ExtractFeature.h"
#else
#include "components/af_model/af_runtime.h"
#endif

void ecg_runtime_benchmark_run(void)
{
    uint32_t checksum = 0;
    int64_t elapsed = 0;
#if ECG_MODULE == 1
    ecg_signal_init(0);
    const char *name = "bpm";
#else
    af_runtime_reset();
    const char *name = "afib";
#endif
    for (uint32_t block = 0; block < 100; ++block) {
        int64_t start = esp_timer_get_time();
        for (uint32_t j = 0; j < 360; ++j) {
            uint32_t i = block * 360 + j;
#if ECG_MODULE == 1
            unsigned phase = i % 288;
            int adc = 2048 + (phase == 108 ? 1000 : (phase == 107 || phase == 109 ? -250 : 0));
            ecg_record_t r = ecg_signal_process(adc, (i * 1000 + 180) / 360);
            checksum += r.bpm + r.is_peak;
#else
            af_runtime_result_t r = af_runtime_push_peak((i + 1) * 800);
            checksum += r.probability_x10000 + r.window_id;
#endif
        }
        elapsed += esp_timer_get_time() - start;
        vTaskDelay(1); /* Outside timing; feed the idle watchdog. */
    }
    printf("MODULE_PROFILE,module=%s,platform=esp32,input=synthetic,calls=36000,total_us=%lld,us_per_call=%.6f,checksum=%lu,END\n",
           name, (long long)elapsed, elapsed / 36000.0, (unsigned long)checksum);
}
