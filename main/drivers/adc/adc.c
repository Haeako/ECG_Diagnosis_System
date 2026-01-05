#include "adc.h"

extern char *ECG_Sensor;
extern adc_oneshot_unit_handle_t adc_handle;

void ADC_Init()
{
    //Handle init
    adc_oneshot_unit_init_cfg_t adc_handle_cfg = {
        .unit_id = ADC_Unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
        .clk_src = ADC_DIGI_CLK_SRC_DEFAULT
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_handle_cfg, &adc_handle));
    ESP_LOGI(ECG_Sensor, "ADC handle created");

    //ADC configure
   adc_oneshot_chan_cfg_t adc_cfg = {
       .atten = ADC_VoltRange,
       .bitwidth = ADC_BitWidth
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_Channel, &adc_cfg));
    ESP_LOGI(ECG_Sensor, "ADC configured");
}