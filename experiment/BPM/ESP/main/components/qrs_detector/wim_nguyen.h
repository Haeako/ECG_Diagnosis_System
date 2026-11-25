#ifndef WIM_NGUYEN_H
#define WIM_NGUYEN_H

#include <stddef.h>
#include <stdint.h>

#define WIM_NGUYEN_FS 360U
#define WIM_NGUYEN_MAX_SAMPLES 3600U
#define WIM_NGUYEN_MAX_PEAKS 128U
#define WIM_NGUYEN_PAD 16U

typedef struct {
    float filtered[WIM_NGUYEN_MAX_SAMPLES];
    float template_signal[WIM_NGUYEN_MAX_SAMPLES];
    uint16_t candidate_index[WIM_NGUYEN_MAX_SAMPLES];
    uint8_t candidate_used[WIM_NGUYEN_MAX_SAMPLES];
} wim_nguyen_workspace_t;

/* Windowed, 360-Hz C port of the WIM rpeak_benchmarking Nguyen method.
 * Input uses the common AFE-calibrated mV domain; output preserves the
 * upstream detector's template-filter peak locations (no R realignment). */
size_t wim_nguyen_detect_360(const float *samples, size_t count,
                             uint16_t *peaks, size_t peak_capacity,
                             wim_nguyen_workspace_t *workspace);

#endif
