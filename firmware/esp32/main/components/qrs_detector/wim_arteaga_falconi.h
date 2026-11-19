#ifndef WIM_ARTEAGA_FALCONI_H
#define WIM_ARTEAGA_FALCONI_H

#include <stddef.h>
#include <stdint.h>

#define WIM_AF_MAX_SAMPLES 3600U
#define WIM_AF_MAX_PEAKS 128U
#define WIM_AF_PAD 36U

typedef struct {
    float work_a[WIM_AF_MAX_SAMPLES + 2U * WIM_AF_PAD];
    float work_b[WIM_AF_MAX_SAMPLES + 2U * WIM_AF_PAD];
    float filtered[WIM_AF_MAX_SAMPLES];
    float inverted[WIM_AF_MAX_SAMPLES];
    uint16_t order[WIM_AF_MAX_SAMPLES];
    uint8_t peak_mask[WIM_AF_MAX_SAMPLES];
} wim_af_workspace_t;

/* WIM rpeak_benchmarking Arteaga-Falconi defaults, fixed to the common
 * 360-Hz / 10-second benchmark window and calibrated-mV input. */
size_t wim_af_detect_360(const float *samples, size_t count,
                         uint16_t *peaks, size_t peak_capacity,
                         wim_af_workspace_t *workspace);

#endif
