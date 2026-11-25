#include "hal/adc_types.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

//ADC Define
#define ADC_Unit                ADC_UNIT_1    
#define ADC_Pin                 4 //VP pin on the board
#define ADC_Channel             ADC_CHANNEL_0
#define ADC_BitWidth            ADC_BITWIDTH_12
#define ADC_VoltRange           ADC_ATTEN_DB_12

void ADC_Init(void);
void ADC_Deinit(void);

/* Initialize once before tasks; all channel access and handle teardown serialize. */
esp_err_t ADC_LockInit(void);
esp_err_t ADC_Read(int *raw);
/* Called only by ECG_ADC, after acquisition or while idle; never waits for the lock. */
esp_err_t ADC_ReadBattery(int *raw);
