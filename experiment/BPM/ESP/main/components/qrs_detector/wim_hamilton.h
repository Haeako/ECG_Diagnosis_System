#ifndef WIM_HAMILTON_H
#define WIM_HAMILTON_H

#include <stddef.h>
#include <stdint.h>

#define WIM_HAMILTON_FS 360U
#define WIM_HAMILTON_MAX_SAMPLES 3600U
#define WIM_HAMILTON_MAX_PEAKS 128U
#define WIM_HAMILTON_PAD 18U
#define WIM_HAMILTON_MAX_CANDIDATES ((WIM_HAMILTON_MAX_SAMPLES + 1U) / 2U)
#define WIM_HAMILTON_QRS_CAPACITY 64U

typedef struct {
    float work_a[WIM_HAMILTON_MAX_SAMPLES + 2U * WIM_HAMILTON_PAD];
    float filtered[WIM_HAMILTON_MAX_SAMPLES];
    uint16_t candidate_index[WIM_HAMILTON_MAX_CANDIDATES];
    float candidate_value[WIM_HAMILTON_MAX_CANDIDATES];
    uint8_t candidate_used[WIM_HAMILTON_MAX_CANDIDATES];
    float qrs_index[WIM_HAMILTON_QRS_CAPACITY];
} wim_hamilton_workspace_t;

/* 360-Hz C port of WIM rpeak_benchmarking Hamilton and its Sedghamiz
 * threshold function. Input is the common AFE-calibrated mV window. */
size_t wim_hamilton_detect_360(const float *samples, size_t count,
                               uint16_t *peaks, size_t peak_capacity,
                               wim_hamilton_workspace_t *workspace);

#endif
