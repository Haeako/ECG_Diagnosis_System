"""Execute the production battery filter with deterministic ADC/calibration stubs."""
from pathlib import Path
import shutil
import subprocess

import pytest


def test_production_battery_filter(tmp_path):
    compiler = shutil.which('gcc') or ('C:/msys64/ucrt64/bin/gcc.exe' if Path('C:/msys64/ucrt64/bin/gcc.exe').exists() else None)
    if not compiler:
        pytest.skip('Host C compiler unavailable')
    headers = {
        'esp_err.h': '#include <stddef.h>\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_ERR_NOT_SUPPORTED 1\n',
        'drivers/adc/adc.h': '#define ADC_UNIT_1 1\n#define ADC_ATTEN_DB_12 12\n#define ADC_BITWIDTH_12 12\nint ADC_ReadBattery(int *raw);\n',
        'esp_adc/adc_cali.h': 'typedef void *adc_cali_handle_t;\nint adc_cali_raw_to_voltage(adc_cali_handle_t handle,int raw,int *mv);\n',
        'esp_adc/adc_cali_scheme.h': '''typedef int adc_cali_line_fitting_efuse_val_t;
#define ADC_CALI_LINE_FITTING_EFUSE_VAL_DEFAULT_VREF 2
typedef struct {int unit_id,atten,bitwidth,default_vref;} adc_cali_line_fitting_config_t;
int adc_cali_scheme_line_fitting_check_efuse(int *source);
int adc_cali_create_scheme_line_fitting(const adc_cali_line_fitting_config_t *config,void **handle);
''',
        'freertos/FreeRTOS.h': '''typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
''',
        'sdkconfig.h': '\n'.join(f'#define CONFIG_ECG_BATTERY_{key} {value}' for key, value in {
            'VREF_MV': 0, 'RTOP_OHM': 100000, 'RBOTTOM_OHM': 100000,
            'GAIN_PERMILLE': 1000, 'OFFSET_MV': 0, 'EMPTY_MV': 3300,
            'FULL_MV': 4200, 'LOW_PERCENT': 15}.items()),
    }
    for name, content in headers.items():
        path = tmp_path / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding='utf-8')
    source = Path(__file__).resolve().parents[1] / 'firmware_ws/main/components/battery/battery.c'
    harness = tmp_path / 'battery_test.c'
    harness.write_text('#include <assert.h>\n#include "' + source.as_posix() + '"\n' + r'''
static int reference = 2, rawValue = 2000, voltage = 1875, failRead, failCalibration, reads;
static uint32_t now;
int ADC_ReadBattery(int *raw) { ++reads; *raw=rawValue; return failRead; }
int adc_cali_scheme_line_fitting_check_efuse(int *source) {*source=reference;return 0;}
int adc_cali_create_scheme_line_fitting(const adc_cali_line_fitting_config_t *config,void **handle) {
    assert(config->atten==12 && config->unit_id==1); *handle=(void*)1;return 0;
}
int adc_cali_raw_to_voltage(void *handle,int raw,int *mv) {(void)handle;(void)raw;*mv=voltage;return failCalibration;}
static BatteryReading samples(unsigned count,int mv) {
    voltage=mv;
    for(unsigned i=0;i<count;++i) {now+=250;battery_poll(now);}
    return battery_snapshot(now);
}
int main(void) {
    assert(!battery_snapshot(0).valid);
    assert(battery_init()==ESP_ERR_NOT_SUPPORTED);
    assert(!samples(8,1875).valid && reads==0);
    reference=0;assert(battery_init()==0);assert(battery_init()==0);
    assert(!samples(7,1875).valid);
    BatteryReading r=samples(1,1875);assert(r.valid && r.millivolts==3750 && r.percent==50 && !r.low);
    int previousReads=reads;battery_poll(now+249);assert(reads==previousReads);
    r=samples(8,2100);assert(r.percent==100);
    r=samples(8,2150);assert(r.percent==100);
    r=samples(8,1600);assert(r.percent==0 && !r.low);
    r=samples(8,1700);assert(!r.low);r=samples(8,1700);assert(r.low);
    r=samples(8,1725);assert(r.low); /* 17%: hysteresis retains warning */
    r=samples(8,1700);assert(r.low); /* returning below threshold must not clear */
    r=samples(8,1800);assert(!r.low);
    failRead=1;assert(!samples(1,1875).valid);failRead=0;
    assert(!samples(7,1875).valid);assert(samples(1,1875).valid);
    failCalibration=1;assert(!samples(1,1875).valid);failCalibration=0;
    rawValue=0;assert(!samples(1,1875).valid);rawValue=4095;assert(!samples(1,1875).valid);rawValue=2000;
    assert(!samples(1,2200).valid);assert(!samples(1,1300).valid);
    assert(samples(8,1875).valid);
    assert(!battery_snapshot(now+10001).valid);
    samples(4,1700);now+=10001;assert(!samples(1,2100).valid);
    r=samples(7,2100);assert(r.valid && r.percent==100); /* no old samples mixed in */
    now=UINT32_MAX-1000;g_lastPoll=now;g_count=0;g_sumMv=0;
    r=samples(8,1875);assert(r.valid && r.percent==50); /* tick wrap */
    return 0;
}
''', encoding='utf-8')
    binary = tmp_path / 'battery_test.exe'
    subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror', '-I', str(tmp_path), str(harness), '-o', str(binary)], check=True, capture_output=True, text=True)
    subprocess.run([str(binary)], check=True, capture_output=True, text=True)


def test_adc_owner_lifecycle(tmp_path):
    compiler = shutil.which('gcc') or ('C:/msys64/ucrt64/bin/gcc.exe' if Path('C:/msys64/ucrt64/bin/gcc.exe').exists() else None)
    if not compiler:
        pytest.skip('Host C compiler unavailable')
    headers = {
        'hal/adc_types.h': '''#include <stdbool.h>
#include <stddef.h>
#define ADC_UNIT_1 1
#define ADC_CHANNEL_0 0
#define ADC_CHANNEL_4 4
#define ADC_ATTEN_DB_12 12
#define ADC_BITWIDTH_12 12
#define ADC_ULP_MODE_DISABLE 0
#define ADC_DIGI_CLK_SRC_DEFAULT 0
''',
        'esp_adc/adc_oneshot.h': '''typedef int esp_err_t;
typedef void *adc_oneshot_unit_handle_t;
typedef struct {int unit_id,ulp_mode,clk_src;} adc_oneshot_unit_init_cfg_t;
typedef struct {int atten,bitwidth;} adc_oneshot_chan_cfg_t;
int adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t*,void**);
int adc_oneshot_config_channel(void*,int,const adc_oneshot_chan_cfg_t*);
int adc_oneshot_del_unit(void*);
int adc_oneshot_read(void*,int,int*);
''',
        'esp_log.h': '''#include <assert.h>
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_TIMEOUT 3
#define ESP_ERROR_CHECK(expr) assert((expr)==0)
''',
        'freertos/FreeRTOS.h': '#define pdTRUE 1\n#define portMAX_DELAY 0xffffffffU\n',
        'freertos/semphr.h': '''typedef void *SemaphoreHandle_t;
void *xSemaphoreCreateMutex(void);
int xSemaphoreTake(void*,unsigned);
int xSemaphoreGive(void*);
''',
    }
    for name, content in headers.items():
        path = tmp_path / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding='utf-8')
    source = Path(__file__).resolve().parents[1] / 'firmware_ws/main/drivers/adc/adc.c'
    harness = tmp_path / 'adc_test.c'
    harness.write_text('#include "'+source.as_posix()+'"\n'+r'''
adc_oneshot_unit_handle_t adc_handle;
static int locked, busy, creates, deletes, reads, failBattery;
void *xSemaphoreCreateMutex(void) {return (void*)1;}
int xSemaphoreTake(void *lock,unsigned wait) {
    assert(lock);if(busy){assert(wait==0);return 0;}assert(!locked);locked=1;return 1;
}
int xSemaphoreGive(void *lock) {assert(lock && locked);locked=0;return 1;}
int adc_oneshot_new_unit(const adc_oneshot_unit_init_cfg_t *cfg,void **handle) {
    assert(locked && cfg->unit_id==1 && !*handle);++creates;*handle=(void*)2;return 0;
}
int adc_oneshot_config_channel(void *handle,int channel,const adc_oneshot_chan_cfg_t *cfg) {
    assert(locked && handle && cfg->atten==12 && cfg->bitwidth==12);
    assert(channel==0 || channel==4);return channel==4?failBattery:0;
}
int adc_oneshot_del_unit(void *handle) {assert(locked && handle);++deletes;return 0;}
int adc_oneshot_read(void *handle,int channel,int *raw) {
    assert(locked && handle);++reads;*raw=channel==0?123:456;return 0;
}
int main(void) {
    int raw;assert(ADC_ReadBattery(&raw)==ESP_ERR_INVALID_STATE);
    assert(ADC_LockInit()==0);assert(ADC_Read(&raw)==ESP_ERR_INVALID_STATE);
    assert(ADC_ReadBattery(&raw)==0 && raw==456 && creates==1 && deletes==1 && !adc_handle);
    ADC_Init();assert(creates==2 && adc_handle);
    assert(ADC_Read(&raw)==0 && raw==123);
    assert(ADC_ReadBattery(&raw)==0 && raw==456 && creates==2 && deletes==1);
    busy=1;int oldReads=reads;assert(ADC_ReadBattery(&raw)==ESP_ERR_TIMEOUT && reads==oldReads);busy=0;
    failBattery=7;assert(ADC_ReadBattery(&raw)==7 && adc_handle);
    assert(ADC_Read(&raw)==0 && raw==123); /* optional channel failure does not disable ECG */
    ADC_Deinit();assert(!adc_handle && deletes==2);
    assert(ADC_ReadBattery(&raw)==7 && !adc_handle && creates==3 && deletes==3);
    failBattery=0;ADC_Init();assert(ADC_Read(&raw)==0);ADC_Deinit();ADC_Deinit();
    assert(creates==deletes && !locked);
    return 0;
}
''', encoding='utf-8')
    binary = tmp_path / 'adc_test.exe'
    subprocess.run([compiler, '-std=c99', '-Wall', '-Wextra', '-Werror', '-I', str(tmp_path), str(harness), '-o', str(binary)], check=True, capture_output=True, text=True)
    subprocess.run([str(binary)], check=True, capture_output=True, text=True)
