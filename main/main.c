/*Code is written for ESP32-DevKitC-32U*/

//Inlcude
#include <stdio.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "os/semaphore.h"

//#include "network/wifi/wifi_sta.h"

#include "drivers/adc/adc.h"
#include "drivers/timer/timer.h"

#include "filter/HPfilter.h"
#include "filter/LPfilter.h"
#include "filter/SBfilter.h"
#include "filter/Kalman.h"

#include <math.h>

//Global variables
const char                *ECG_Sensor = "AD8232"; //Tag for writing log
const char                *Wifi_STA   = "Wifi Station"; //Tag for writing log
SemaphoreHandle_t adc_timer_semaphore_handle = NULL; //semphore to synchronize adc and timer
adc_oneshot_unit_handle_t adc_handle = NULL;   // main handle ADC 
gptimer_handle_t adc_timer_handle = NULL; //trigger timer for ADC
HPfilterType HPfilter;
SBfilterType SBfilter;
LPfilterType LPfilter;
//Define

//Typedef

//Functions Prototype
int16_t filtering(int data);
//Tasks
void ECG_Processing_Task(void *pvParameters){
    static int raw = 0;
    while (1){
        if (xSemaphoreTake(adc_timer_semaphore_handle, portMAX_DELAY) == pdTRUE)
        {
            //Read ecg value
            ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_Channel, &raw));
            int16_t filtered_ecg = filtering(raw - 2048);
            printf("%d,%d\n", raw - 2048, filtered_ecg);
        }
    }
}

void app_main(void)
{
    // //Initialize Wifi
    // esp_err_t ret = nvs_flash_init();
    // if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    //   ESP_ERROR_CHECK(nvs_flash_erase());
    //   ret = nvs_flash_init();
    // }
    // ESP_ERROR_CHECK(ret);
    // if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL) {
    //     /* If you only want to open more logs in the wifi module, you need to make the max level greater than the default level,
    //      * and call esp_log_level_set() before esp_wifi_init() to improve the log level of the wifi module. */
    //     esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
    // }

    // ESP_LOGI(Wifi_STA, "ESP_WIFI_MODE_STA");
    //wifi_init_sta();
    //Peripherals initialization
    ADC_Init();
    //Digital signal processing filter
    HPfilter_init(&HPfilter);
    SBfilter_init(&SBfilter);
    LPfilter_init(&LPfilter);
    KalmanFilter_init(1.0f, 1.0f, 0.001f);
    //freeRTOS
    Semaphore_Init(&adc_timer_semaphore_handle);
    //Timer initializtion
    Timer_Init();
    //Create Task
    //Core 1 Tasks
    xTaskCreatePinnedToCore(
        ECG_Processing_Task,
        "ECG_Processing_Task",
        4096,    // stack size
        NULL,    // parameters
        2,       // priority
        NULL,    // handle
        1);      // core ID
}

//Functions Definition
int16_t filtering(int data){
    float nor_data = (float)data;
    //0.5Hz order 4 HPF
    HPfilter_writeInput(&HPfilter, nor_data);
    float HPF_Output = HPfilter_readOutput(&HPfilter);
    //40-50Hz order 4 SBF
    SBfilter_writeInput(&SBfilter, HPF_Output);
    float SBF_Output = SBfilter_readOutput(&SBfilter);
    //40HZ order 4 LPF
    LPfilter_writeInput(&LPfilter, SBF_Output);
    float LPF_Output = LPfilter_readOutput(&LPfilter);

    int16_t filtered_data = (int16_t)KFupdateEstimate(LPF_Output);

    return filtered_data;
}

