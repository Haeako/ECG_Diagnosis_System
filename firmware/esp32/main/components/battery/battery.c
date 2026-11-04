#include "battery.h"
#include "sdkconfig.h"
#include "drivers/adc/adc.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"

#define BATTERY_POLL_MS 250U
#define BATTERY_FILTER_SAMPLES 8U
#define BATTERY_STALE_MS 10000U

static adc_cali_handle_t g_calibration;
static portMUX_TYPE g_snapshotLock = portMUX_INITIALIZER_UNLOCKED;
static BatteryReading g_reading;
static uint32_t g_lastPoll;
static uint32_t g_sumMv;
static unsigned g_count;
static unsigned g_lowCount;
static bool g_low;

/* eFuse calibration is mandatory unless a measured reference is configured. */
esp_err_t battery_init(void)
{
    if (g_calibration != NULL) {
        return ESP_OK;
    }
    adc_cali_line_fitting_efuse_val_t source;
    esp_err_t ret = adc_cali_scheme_line_fitting_check_efuse(&source);
    if (ret != ESP_OK) {
        return ret;
    }
    if (source == ADC_CALI_LINE_FITTING_EFUSE_VAL_DEFAULT_VREF
        && CONFIG_ECG_BATTERY_VREF_MV == 0) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const adc_cali_line_fitting_config_t config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
        .default_vref = CONFIG_ECG_BATTERY_VREF_MV,
    };
    return adc_cali_create_scheme_line_fitting(&config, &g_calibration);
}

/* Coarse voltage-only 1S estimate, NOT a measured cell curve or fuel gauge. */
static uint8_t battery_percent(int millivolts)
{
    const int emptyMv = CONFIG_ECG_BATTERY_EMPTY_MV;
    const int fullMv = CONFIG_ECG_BATTERY_FULL_MV;
    if (millivolts <= emptyMv) {
        return 0;
    }
    if (millivolts >= fullMv) {
        return 100;
    }
    return (uint8_t)(((millivolts - emptyMv) * 100 + (fullMv - emptyMv) / 2) / (fullMv - emptyMv));
}

void battery_poll(uint32_t nowMs)
{
    if ((uint32_t)(nowMs - g_lastPoll) < BATTERY_POLL_MS) {
        return;
    }
    if ((uint32_t)(nowMs - g_lastPoll) > BATTERY_STALE_MS) {
        g_sumMv = 0;
        g_count = 0;
        g_lowCount = 0;
        g_low = false;
    }
    g_lastPoll = nowMs;
    int raw = 0;
    int adcMv = 0;
    bool valid = g_calibration != NULL
        && ADC_ReadBattery(&raw) == ESP_OK && raw > 0 && raw < 4095
        && adc_cali_raw_to_voltage(g_calibration, raw, &adcMv) == ESP_OK;
    int batteryMv = 0;
    if (valid) {
        int64_t scaled = (int64_t)adcMv
            * (CONFIG_ECG_BATTERY_RTOP_OHM + CONFIG_ECG_BATTERY_RBOTTOM_OHM)
            * CONFIG_ECG_BATTERY_GAIN_PERMILLE;
        batteryMv = (int)(scaled / ((int64_t)CONFIG_ECG_BATTERY_RBOTTOM_OHM * 1000))
            + CONFIG_ECG_BATTERY_OFFSET_MV;
        valid = adcMv >= 100 && adcMv <= 2450 && batteryMv >= 2800 && batteryMv <= 4350;
    }
    if (!valid) {
        g_sumMv = 0;
        g_count = 0;
        g_lowCount = 0;
        g_low = false;
        portENTER_CRITICAL(&g_snapshotLock);
        g_reading = (BatteryReading){ .timestamp_ms = nowMs };
        portEXIT_CRITICAL(&g_snapshotLock);
        return;
    }
    g_sumMv += (uint32_t)batteryMv;
    if (++g_count < BATTERY_FILTER_SAMPLES) {
        return;
    }
    uint16_t average = (uint16_t)((g_sumMv + BATTERY_FILTER_SAMPLES / 2) / BATTERY_FILTER_SAMPLES);
    uint8_t percent = battery_percent(average);
    g_sumMv = 0;
    g_count = 0;
    /* Three filtered windows before warning, five percentage points hysteresis. */
    if (percent <= CONFIG_ECG_BATTERY_LOW_PERCENT) {
        if (g_lowCount < 3U) {
            ++g_lowCount;
        }
        if (g_lowCount >= 3U) {
            g_low = true;
        }
    } else {
        g_lowCount = 0;
        if (percent >= CONFIG_ECG_BATTERY_LOW_PERCENT + 5) {
            g_low = false;
        }
    }
    const BatteryReading reading = {
        .timestamp_ms = nowMs, .millivolts = average, .percent = percent,
        .valid = true, .low = g_low,
    };
    portENTER_CRITICAL(&g_snapshotLock);
    g_reading = reading;
    portEXIT_CRITICAL(&g_snapshotLock);
}

BatteryReading battery_snapshot(uint32_t nowMs)
{
    portENTER_CRITICAL(&g_snapshotLock);
    BatteryReading reading = g_reading;
    portEXIT_CRITICAL(&g_snapshotLock);
    if ((uint32_t)(nowMs - reading.timestamp_ms) > BATTERY_STALE_MS) {
        reading.valid = false;
        reading.low = false;
    }
    return reading;
}
