#include "esp_log.h"

#include "task/Core0/StateMachine/StateMachine.h"
#ifdef SQI_BENCHMARK_ENABLE
#include "components/sqi_gate/sqi_device_benchmark.h"
#endif
#ifdef SQI_COST_ANCHOR_ENABLE
#include "components/sqi_gate/sqi_cost_anchor.h"
#endif

static const char *TAG = "ECG_MAIN";

void app_main(void)
{
#ifdef SQI_BENCHMARK_ENABLE
    sqi_device_benchmark_run();
    return;
#endif
#ifdef SQI_COST_ANCHOR_ENABLE
    sqi_cost_anchor_run();
#endif
    if (state_machine_start() != ESP_OK) {
        ESP_LOGE(TAG, "Application startup failed");
    }
}
