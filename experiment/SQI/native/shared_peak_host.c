#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include "components/sqi_gate/sqi_runtime.h"
#include "components/sqi_gate/sqi_zhao.h"

static sqi_zhao_state_t zhao;

int shared_peak_window(const float *signal, const int16_t *counts, size_t n,
                       const uint32_t *peaks, size_t np, double *out)
{
    if (!signal || !counts || !out || n != 3600 || (np && !peaks)) return 1;
    for (size_t j = 0; j < np; ++j) {
        if (peaks[j] >= 10000 || (j && peaks[j] <= peaks[j-1])) return 2;
    }
    sqi_zhao_reset(&zhao);
    sqi_runtime_reset(0);
    size_t cursor = 0;
    for (size_t i = 0; i < n; ++i) {
        if (!isfinite(signal[i]) || !sqi_zhao_push(&zhao, signal[i])) return 3;
        uint32_t now = (uint32_t)(i * 1000 / 360);
        int is_peak = cursor < np && peaks[cursor] <= now;
        uint32_t peak_ms = is_peak ? peaks[cursor++] : 0;
        if (is_peak) (void)sqi_zhao_push_peak(&zhao, peak_ms);
        (void)sqi_runtime_push(counts[i], is_peak, peak_ms, now);
        if (cursor < np && peaks[cursor] <= now) return 4;
    }
    if (cursor != np) return 5;
    ecg_sqi_result_t orphan = sqi_runtime_benchmark_finalize();
    sqi_zhao_result_t result = sqi_zhao_finalize(&zhao, 360);
    out[0] = orphan.score == ECG_SQI_INSUFFICIENT_SCORE ? NAN : orphan.score / 100.0;
    out[1] = orphan.status == ECG_SQI_USABLE;
    out[2] = orphan.rule_mask;
    out[3] = orphan.template_correlation_x1000;
    out[4] = result.score;
    out[5] = isfinite(result.score) && result.label != SQI_ZHAO_UNACCEPTABLE;
    out[6] = result.label;
    out[7] = zhao.overflow;
    out[8] = result.kurtosis;
    out[9] = result.power_ratio;
    out[10] = result.baseline_sqi;
    return 0;
}

size_t shared_zhao_state_bytes(void) { return sizeof(zhao); }
