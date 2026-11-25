#include "adc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern adc_oneshot_unit_handle_t adc_handle;
static SemaphoreHandle_t g_adcLock;

esp_err_t ADC_LockInit(void)
{
    if (g_adcLock == NULL) {
        g_adcLock = xSemaphoreCreateMutex();
    }
    return g_adcLock != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

/* Caller owns g_adcLock; preserve one ADC1 owner across START/STOP. */
static esp_err_t adc_open(void)
{
  if (adc_handle != NULL) {
    return ESP_OK;
  }

  // Handle init
  adc_oneshot_unit_init_cfg_t adc_handle_cfg = {
      .unit_id = ADC_Unit,
      .ulp_mode = ADC_ULP_MODE_DISABLE,
      .clk_src = ADC_DIGI_CLK_SRC_DEFAULT};

  esp_err_t ret = adc_oneshot_new_unit(&adc_handle_cfg, &adc_handle);
  if (ret != ESP_OK) {
    return ret;
  }

  // ADC configure
  adc_oneshot_chan_cfg_t adc_cfg = {.atten = ADC_VoltRange,
                                    .bitwidth = ADC_BitWidth};

  ret = adc_oneshot_config_channel(adc_handle, ADC_Channel, &adc_cfg);
  if (ret != ESP_OK) {
    (void)adc_oneshot_del_unit(adc_handle);
    adc_handle = NULL;
  }
  return ret;
}

void ADC_Init(void)
{
    ESP_ERROR_CHECK(ADC_LockInit());
    xSemaphoreTake(g_adcLock, portMAX_DELAY);
    esp_err_t ret = adc_open();
    xSemaphoreGive(g_adcLock);
    ESP_ERROR_CHECK(ret);
}

void ADC_Deinit(void)
{
  if (g_adcLock == NULL) {
    return;
  }
  xSemaphoreTake(g_adcLock, portMAX_DELAY);
  if (adc_handle != NULL) {
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc_handle));
    adc_handle = NULL;
  }
  xSemaphoreGive(g_adcLock);
}

esp_err_t ADC_Read(int *raw)
{
    if (g_adcLock == NULL || raw == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(g_adcLock, portMAX_DELAY);
    esp_err_t ret = adc_handle != NULL
        ? adc_oneshot_read(adc_handle, ADC_Channel, raw) : ESP_ERR_INVALID_STATE;
    xSemaphoreGive(g_adcLock);
    return ret;
}

esp_err_t ADC_ReadBattery(int *raw)
{
    if (raw == NULL || g_adcLock == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(g_adcLock, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    /* Idle has no ECG unit. A temporary handle cannot race START/STOP. */
    bool temporary = adc_handle == NULL;
    esp_err_t ret = adc_open();
    if (ret == ESP_OK) {
        const adc_oneshot_chan_cfg_t batteryConfig = {
            .atten = ADC_ATTEN_DB_12, .bitwidth = ADC_BITWIDTH_12,
        };
        ret = adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_4, &batteryConfig);
    }
    if (ret == ESP_OK) {
        ret = adc_oneshot_read(adc_handle, ADC_CHANNEL_4, raw);
    }
    if (temporary && adc_handle != NULL) {
        esp_err_t closeRet = adc_oneshot_del_unit(adc_handle);
        if (closeRet == ESP_OK) {
            adc_handle = NULL;
        } else {
            ret = closeRet;
        }
    }
    xSemaphoreGive(g_adcLock);
    return ret;
}
