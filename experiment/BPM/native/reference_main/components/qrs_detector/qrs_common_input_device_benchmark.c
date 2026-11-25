#include "../sqi_gate/sqi_device_benchmark.h"
#include "qrs_common_test_vectors.h"
#include "pan_pyecg.h"
#include "elgendi_pyecg.h"
#include "wim_nguyen.h"
#include "wim_elgendi.h"
#include "wim_hamilton.h"
#include "wim_arteaga_falconi.h"
#include "wim_pantompkins.h"
#include "pan_tompkins_plus_plus_port.h"
#include "wim_xia.h"
#include "wim_shaik.h"
#include "wim_zhai.h"
#include "fast_qrs_port.h"
#include "gqrs_wfdb_port.h"
#include "../sqi_gate/sqi_xqrs.h"
#include "components/bpm/ExtractFeature.h"
#ifdef QRS_WIM_XIANG_TFLM_BENCHMARK
#include "wim_xiang_tflm.h"
#endif

#include "esp_cpu.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#define REPEATS 3U
#define WIM_XIA_REPEATS 1U
#define PEAK_CAPACITY 128U
#define CPU_MHZ ((double)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ)

static const char *TAG = "QRS_REAL_BENCH";
static uint32_t reported_repeats = REPEATS;
static float shared_input[QRS_COMMON_SAMPLES];
static float xqrs_input[SQI_XQRS_FS * 10U];
static uint16_t port_peaks[PEAK_CAPACITY];
static uint32_t cycle_samples[REPEATS];
static union {
    pan_pyecg_workspace_t pan;
    elgendi_pyecg_workspace_t elgendi;
    wim_nguyen_workspace_t nguyen;
    wim_elgendi_workspace_t wim_elgendi;
    wim_hamilton_workspace_t wim_hamilton;
    wim_af_workspace_t wim_arteaga_falconi;
    wim_pt_workspace_t wim_pantompkins;
    panpp_workspace_t panpp;
    wim_xia_workspace_t wim_xia;
    wim_shaik_workspace_t wim_shaik;
    wim_zhai_workspace_t wim_zhai;
    fast_qrs_workspace_t fast_qrs;
    gqrs_wfdb_workspace_t gqrs_wfdb;
    sqi_xqrs_workspace_t xqrs;
#ifdef QRS_WIM_XIANG_TFLM_BENCHMARK
    wim_xiang_workspace_t xiang;
#endif
} method_workspace;

static void log_result(const char *dataset, const char *record, const char *method,
                       const uint32_t *peaks, size_t count, uint32_t p50_cycles,
                       uint32_t max_cycles, size_t workspace_bytes,
                       uint32_t fs_hz, size_t sample_count)
{
    char peak_text[640];
    size_t used = 0U;
    peak_text[0] = '\0';
    for (size_t i = 0U; i < count && used < sizeof(peak_text); ++i) {
        const int written = snprintf(peak_text + used, sizeof(peak_text) - used,
                                     "%s%lu", i == 0U ? "" : ",",
                                     (unsigned long)peaks[i]);
        if (written < 0 || (size_t)written >= sizeof(peak_text) - used) break;
        used += (size_t)written;
    }
    ESP_LOGI(TAG, "RESULT dataset=%s record=%s method=%s fs=%lu samples=%u "
             "repeats=%u detections=%u p50_us=%.3f max_us=%.3f workspace_bytes=%u peaks=%s",
             dataset, record, method, (unsigned long)fs_hz, (unsigned)sample_count,
             (unsigned)reported_repeats, (unsigned)count,
             p50_cycles / CPU_MHZ, max_cycles / CPU_MHZ,
             (unsigned)workspace_bytes, peak_text);
}

static void sort_cycles(uint32_t values[REPEATS])
{
    for (size_t i = 1U; i < REPEATS; ++i) {
        uint32_t value = values[i];
        size_t j = i;
        while (j > 0U && values[j - 1U] > value) {
            values[j] = values[j - 1U];
            --j;
        }
        values[j] = value;
    }
}

static void run_project_pan(const qrs_common_record_t *record)
{
    size_t count = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        count = pan_pyecg_detect_360(shared_input, QRS_COMMON_SAMPLES,
                                     port_peaks, PEAK_CAPACITY, &method_workspace.pan);
        cycle_samples[repeat] = esp_cpu_get_cycle_count() - before;
    }
    sort_cycles(cycle_samples);
    uint32_t peaks[PEAK_CAPACITY];
    for (size_t i = 0U; i < count; ++i) peaks[i] = port_peaks[i];
    log_result(record->dataset, record->record, "ProjectPan_pyecg_C", peaks, count,
               cycle_samples[1], cycle_samples[2], sizeof(method_workspace.pan),
               360U, QRS_COMMON_SAMPLES);
}

static void run_project_elgendi(const qrs_common_record_t *record)
{
    size_t count = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        count = elgendi_pyecg_detect_360(shared_input, QRS_COMMON_SAMPLES,
                                         port_peaks, PEAK_CAPACITY, &method_workspace.elgendi);
        cycle_samples[repeat] = esp_cpu_get_cycle_count() - before;
    }
    sort_cycles(cycle_samples);
    uint32_t peaks[PEAK_CAPACITY];
    for (size_t i = 0U; i < count; ++i) peaks[i] = port_peaks[i];
    log_result(record->dataset, record->record, "ProjectElgendi_pyecg_C", peaks, count,
               cycle_samples[1], cycle_samples[2], sizeof(method_workspace.elgendi),
               360U, QRS_COMMON_SAMPLES);
}

static void run_wim_nguyen(const qrs_common_record_t *record)
{
    size_t count = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        count = wim_nguyen_detect_360(shared_input, QRS_COMMON_SAMPLES,
                                      port_peaks, PEAK_CAPACITY,
                                      &method_workspace.nguyen);
        cycle_samples[repeat] = esp_cpu_get_cycle_count() - before;
    }
    sort_cycles(cycle_samples);
    uint32_t peaks[PEAK_CAPACITY];
    for (size_t i = 0U; i < count; ++i) peaks[i] = port_peaks[i];
    log_result(record->dataset, record->record, "WIM_Nguyen_C", peaks, count,
               cycle_samples[1], cycle_samples[2], sizeof(method_workspace.nguyen),
               360U, QRS_COMMON_SAMPLES);
}

static void run_wim_elgendi(const qrs_common_record_t *record)
{
    size_t count = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        count = wim_elgendi_detect_360(shared_input, QRS_COMMON_SAMPLES,
                                       port_peaks, PEAK_CAPACITY,
                                       &method_workspace.wim_elgendi);
        cycle_samples[repeat] = esp_cpu_get_cycle_count() - before;
    }
    sort_cycles(cycle_samples);
    uint32_t peaks[PEAK_CAPACITY];
    for (size_t i = 0U; i < count; ++i) peaks[i] = port_peaks[i];
    log_result(record->dataset, record->record, "WIM_Elgendi_C", peaks, count,
               cycle_samples[1], cycle_samples[2], sizeof(method_workspace.wim_elgendi),
               360U, QRS_COMMON_SAMPLES);
}

static void run_wim_hamilton(const qrs_common_record_t *record)
{
    size_t count = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        count = wim_hamilton_detect_360(shared_input, QRS_COMMON_SAMPLES,
                                        port_peaks, PEAK_CAPACITY,
                                        &method_workspace.wim_hamilton);
        cycle_samples[repeat] = esp_cpu_get_cycle_count() - before;
    }
    sort_cycles(cycle_samples);
    uint32_t peaks[PEAK_CAPACITY];
    for (size_t i = 0U; i < count; ++i) peaks[i] = port_peaks[i];
    log_result(record->dataset, record->record, "WIM_Hamilton_C", peaks, count,
               cycle_samples[1], cycle_samples[2], sizeof(method_workspace.wim_hamilton),
               360U, QRS_COMMON_SAMPLES);
}

static void run_wim_arteaga_falconi(const qrs_common_record_t *record)
{
    size_t count = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        count = wim_af_detect_360(shared_input, QRS_COMMON_SAMPLES,
                                  port_peaks, PEAK_CAPACITY,
                                  &method_workspace.wim_arteaga_falconi);
        cycle_samples[repeat] = esp_cpu_get_cycle_count() - before;
    }
    sort_cycles(cycle_samples);
    uint32_t peaks[PEAK_CAPACITY];
    for (size_t i = 0U; i < count; ++i) peaks[i] = port_peaks[i];
    log_result(record->dataset, record->record, "WIM_ArteagaFalconi_C", peaks, count,
               cycle_samples[1], cycle_samples[2],
               sizeof(method_workspace.wim_arteaga_falconi), 360U, QRS_COMMON_SAMPLES);
}

static void run_wim_pantompkins(const qrs_common_record_t *record)
{
    size_t count = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        count = wim_pt_detect_360(shared_input, QRS_COMMON_SAMPLES,
                                  port_peaks, PEAK_CAPACITY,
                                  &method_workspace.wim_pantompkins);
        cycle_samples[repeat] = esp_cpu_get_cycle_count() - before;
    }
    sort_cycles(cycle_samples);
    uint32_t peaks[PEAK_CAPACITY];
    for (size_t i = 0U; i < count; ++i) peaks[i] = port_peaks[i];
    log_result(record->dataset, record->record, "WIM_PanTompkins_C", peaks, count,
               cycle_samples[1], cycle_samples[2],
               sizeof(method_workspace.wim_pantompkins), 360U, QRS_COMMON_SAMPLES);
}

static void run_pan_tompkins_plus_plus(const qrs_common_record_t *record)
{
    size_t count = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        count = panpp_detect_360(shared_input, QRS_COMMON_SAMPLES,
                                 port_peaks, PEAK_CAPACITY,
                                 &method_workspace.panpp);
        cycle_samples[repeat] = esp_cpu_get_cycle_count() - before;
    }
    sort_cycles(cycle_samples);
    uint32_t peaks[PEAK_CAPACITY];
    for (size_t i = 0U; i < count; ++i) peaks[i] = port_peaks[i];
    log_result(record->dataset, record->record, "PanTompkinsPlusPlus_C",
               peaks, count, cycle_samples[1], cycle_samples[2],
               sizeof(method_workspace.panpp), 360U, QRS_COMMON_SAMPLES);
}

static void run_wim_shaik(const qrs_common_record_t *record)
{
    size_t count = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        count = wim_shaik_detect_360(shared_input, QRS_COMMON_SAMPLES,
                                     port_peaks, PEAK_CAPACITY,
                                     &method_workspace.wim_shaik);
        cycle_samples[repeat] = esp_cpu_get_cycle_count() - before;
    }
    sort_cycles(cycle_samples);
    uint32_t peaks[PEAK_CAPACITY];
    for (size_t i = 0U; i < count; ++i) peaks[i] = port_peaks[i];
    log_result(record->dataset, record->record, "WIM_Shaik_C", peaks, count,
               cycle_samples[1], cycle_samples[2],
               sizeof(method_workspace.wim_shaik), 360U, QRS_COMMON_SAMPLES);
}

static void run_wim_zhai(const qrs_common_record_t *record)
{
    size_t count = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        count = wim_zhai_detect_360(shared_input, QRS_COMMON_SAMPLES,
                                    port_peaks, PEAK_CAPACITY,
                                    &method_workspace.wim_zhai);
        cycle_samples[repeat] = esp_cpu_get_cycle_count() - before;
    }
    sort_cycles(cycle_samples);
    uint32_t peaks[PEAK_CAPACITY];
    for (size_t i = 0U; i < count; ++i) peaks[i] = port_peaks[i];
    log_result(record->dataset, record->record, "WIM_Zhai_C", peaks, count,
               cycle_samples[1], cycle_samples[2],
               sizeof(method_workspace.wim_zhai), 360U, QRS_COMMON_SAMPLES);
}

static void run_wim_xia(const qrs_common_record_t *record)
{
    size_t count = 0U;
    for (uint32_t repeat = 0U; repeat < WIM_XIA_REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        count = wim_xia_detect_360(qrs_common_adc[record - qrs_common_records], QRS_COMMON_SAMPLES,
                                   port_peaks, PEAK_CAPACITY,
                                   &method_workspace.wim_xia);
        cycle_samples[repeat] = esp_cpu_get_cycle_count() - before;
    }
    uint32_t peaks[PEAK_CAPACITY];
    for (size_t i = 0U; i < count; ++i) peaks[i] = port_peaks[i];
    reported_repeats = WIM_XIA_REPEATS;
    log_result(record->dataset, record->record, "WIM_Xia_C",
               peaks, count, cycle_samples[0], cycle_samples[0],
               sizeof(method_workspace.wim_xia), 360U, QRS_COMMON_SAMPLES);
    reported_repeats = REPEATS;
}

static void run_fast_qrs(const qrs_common_record_t *record)
{
    size_t count = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        count = fast_qrs_detect_360(shared_input, QRS_COMMON_SAMPLES,
                                    port_peaks, PEAK_CAPACITY,
                                    &method_workspace.fast_qrs);
        cycle_samples[repeat] = esp_cpu_get_cycle_count() - before;
    }
    sort_cycles(cycle_samples);
    uint32_t peaks[PEAK_CAPACITY];
    for (size_t i = 0U; i < count; ++i) peaks[i] = port_peaks[i];
    log_result(record->dataset, record->record, "FastQRS_C", peaks, count,
               cycle_samples[1], cycle_samples[2], sizeof(method_workspace.fast_qrs),
               360U, QRS_COMMON_SAMPLES);
}

static void run_gqrs_wfdb(const qrs_common_record_t *record)
{
    size_t count = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        count = gqrs_wfdb_detect_360(shared_input, QRS_COMMON_SAMPLES,
                                     port_peaks, PEAK_CAPACITY,
                                     &method_workspace.gqrs_wfdb);
        cycle_samples[repeat] = esp_cpu_get_cycle_count() - before;
    }
    sort_cycles(cycle_samples);
    uint32_t peaks[PEAK_CAPACITY];
    for (size_t i = 0U; i < count; ++i) peaks[i] = port_peaks[i];
    log_result(record->dataset, record->record, "WFDB_GQRS_C_24bit",
               peaks, count, cycle_samples[1], cycle_samples[2],
               sizeof(method_workspace.gqrs_wfdb), 360U, QRS_COMMON_SAMPLES);
}

static void run_wfdb_xqrs_port(const qrs_common_record_t *record)
{
    /* Match the XQRS C port's required 250 Hz domain from the shared 360 Hz
       AFE waveform using linear interpolation over the same 10-second window. */
    for (uint32_t i = 0U; i < SQI_XQRS_FS * 10U; ++i) {
        const uint32_t scaled = i * 360U;
        const uint32_t source_index = scaled / SQI_XQRS_FS;
        const uint32_t remainder = scaled % SQI_XQRS_FS;
        const float fraction = (float)remainder / (float)SQI_XQRS_FS;
        xqrs_input[i] = shared_input[source_index] * (1.0f - fraction) +
                        shared_input[source_index + 1U] * fraction;
    }

    size_t count = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        count = sqi_xqrs_detect_250(xqrs_input, SQI_XQRS_FS * 10U,
                                    port_peaks, PEAK_CAPACITY, &method_workspace.xqrs);
        cycle_samples[repeat] = esp_cpu_get_cycle_count() - before;
    }
    sort_cycles(cycle_samples);
    uint32_t peaks[PEAK_CAPACITY];
    for (size_t i = 0U; i < count; ++i) peaks[i] = port_peaks[i];
    log_result(record->dataset, record->record, "WFDB_XQRS_C_250Hz", peaks, count,
               cycle_samples[1], cycle_samples[2], sizeof(method_workspace.xqrs),
               SQI_XQRS_FS, SQI_XQRS_FS * 10U);
}

#ifdef QRS_WIM_XIANG_TFLM_BENCHMARK
static void run_wim_xiang(const qrs_common_record_t *record)
{
    size_t count = 0U;
    size_t arena_bytes = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        const uint32_t before = esp_cpu_get_cycle_count();
        count = wim_xiang_tflm_detect_360(shared_input, QRS_COMMON_SAMPLES,
                                           port_peaks, PEAK_CAPACITY,
                                           &method_workspace.xiang, &arena_bytes);
        cycle_samples[repeat] = esp_cpu_get_cycle_count() - before;
    }
    sort_cycles(cycle_samples);
    uint32_t peaks[PEAK_CAPACITY];
    for (size_t i = 0U; i < count; ++i) peaks[i] = port_peaks[i];
    log_result(record->dataset, record->record, "WIM_Xiang_TFLM_INT8", peaks, count,
               cycle_samples[1], cycle_samples[2],
               arena_bytes + sizeof(method_workspace.xiang),
               360U, QRS_COMMON_SAMPLES);
}
#endif

static void run_firmware_detector(const qrs_common_record_t *record)
{
    uint32_t peaks[PEAK_CAPACITY];
    size_t count = 0U;
    for (uint32_t repeat = 0U; repeat < REPEATS; ++repeat) {
        ecg_signal_init(0U);
        count = 0U;
        const uint32_t before = esp_cpu_get_cycle_count();
        for (uint32_t i = 0U; i < QRS_COMMON_SAMPLES; ++i) {
            const uint32_t now_ms = (uint32_t)(((uint64_t)i * 1000U + 180U) / 360U);
            ecg_record_t sample = ecg_signal_process(qrs_common_adc[record - qrs_common_records][i], now_ms);
            if (sample.is_peak != 0U && count < PEAK_CAPACITY) {
                peaks[count++] = sample.r_peak_timestamp_ms * 360U / 1000U;
            }
        }
        cycle_samples[repeat] = esp_cpu_get_cycle_count() - before;
    }
    sort_cycles(cycle_samples);
    log_result(record->dataset, record->record, "ProductionPanStyle_C", peaks, count,
               cycle_samples[1], cycle_samples[2], 0U,
               360U, QRS_COMMON_SAMPLES);
}

void sqi_device_benchmark_run(void)
{
    ESP_LOGI(TAG, "BEGIN records=%u samples=%u cpu_mhz=%.0f input=shared_AFE_ADC_counts_mV_scale",
             QRS_COMMON_RECORDS, QRS_COMMON_SAMPLES, CPU_MHZ);
    for (size_t r = 0U; r < QRS_COMMON_RECORDS; ++r) {
        for (uint32_t i = 0U; i < QRS_COMMON_SAMPLES; ++i) {
            shared_input[i] = ((float)qrs_common_adc[r][i] - 2048.0f) / 200.0f;
        }
        run_firmware_detector(&qrs_common_records[r]);
        run_project_pan(&qrs_common_records[r]);
        run_project_elgendi(&qrs_common_records[r]);
        run_wim_nguyen(&qrs_common_records[r]);
        run_wim_elgendi(&qrs_common_records[r]);
        run_wim_hamilton(&qrs_common_records[r]);
        run_wim_arteaga_falconi(&qrs_common_records[r]);
        run_wim_pantompkins(&qrs_common_records[r]);
        run_pan_tompkins_plus_plus(&qrs_common_records[r]);
        run_wim_shaik(&qrs_common_records[r]);
        run_wim_zhai(&qrs_common_records[r]);
        run_wim_xia(&qrs_common_records[r]);
        run_fast_qrs(&qrs_common_records[r]);
        run_gqrs_wfdb(&qrs_common_records[r]);
        run_wfdb_xqrs_port(&qrs_common_records[r]);
#ifdef QRS_WIM_XIANG_TFLM_BENCHMARK
        run_wim_xiang(&qrs_common_records[r]);
#endif
        /* pdMS_TO_TICKS(1) rounds to zero on a 100-Hz tick; yield for one
         * real scheduler tick so the benchmark does not starve IDLE0/WDT. */
        vTaskDelay(1U);
    }
    ESP_LOGI(TAG, "END");
}
