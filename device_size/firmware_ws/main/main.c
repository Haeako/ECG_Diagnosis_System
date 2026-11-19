#include "esp_log.h"
#include "task/Core0/StateMachine/StateMachine.h"

void app_main(void)
{
    if (state_machine_start() != ESP_OK) {
        ESP_LOGE("ECG_MAIN", "Application startup failed");
    }
}
