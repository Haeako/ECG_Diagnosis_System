#ifndef WIM_PANTOMPKINS_H
#define WIM_PANTOMPKINS_H

#include <stddef.h>
#include <stdint.h>

#define WIM_PT_MAX_SAMPLES 3600U
#define WIM_PT_MAX_PEAKS 128U
#define WIM_PT_PAD 40U
#define WIM_PT_MAX_CANDIDATES ((WIM_PT_MAX_SAMPLES + 1U) / 2U)

typedef struct {
    float work_a[WIM_PT_MAX_SAMPLES + 2U * WIM_PT_PAD];
    float work_b[WIM_PT_MAX_SAMPLES + 2U * WIM_PT_PAD];
    float energy[WIM_PT_MAX_SAMPLES];
    uint16_t candidates[WIM_PT_MAX_CANDIDATES];
    uint16_t selected[WIM_PT_MAX_PEAKS];
    uint8_t blocked[WIM_PT_MAX_SAMPLES];
} wim_pt_workspace_t;

/* WIM rpeak_benchmarking Pan-Tompkins defaults, fixed to its 360-Hz
 * implementation and common 10-second AFE-calibrated input. */
size_t wim_pt_detect_360(const float *samples, size_t count,
                         uint16_t *peaks, size_t peak_capacity,
                         wim_pt_workspace_t *workspace);

#endif
