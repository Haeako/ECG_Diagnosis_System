#ifndef WIM_ELGENDI_H
#define WIM_ELGENDI_H

#include <stddef.h>
#include <stdint.h>

#define WIM_ELGENDI_FS 360U
#define WIM_ELGENDI_MAX_SAMPLES 3600U
#define WIM_ELGENDI_MAX_PEAKS 128U
#define WIM_ELGENDI_PAD 24U

typedef struct {
    float work_a[WIM_ELGENDI_MAX_SAMPLES + 2U * WIM_ELGENDI_PAD];
    float work_b[WIM_ELGENDI_MAX_SAMPLES + 2U * WIM_ELGENDI_PAD];
    float filtered[WIM_ELGENDI_MAX_SAMPLES];
    float prefix[WIM_ELGENDI_MAX_SAMPLES + 1U];
    uint8_t peak_flags[WIM_ELGENDI_MAX_SAMPLES];
} wim_elgendi_workspace_t;

/* Windowed port of WIM rpeak_benchmarking/algorithms/elgendi.py at 360 Hz.
 * Input is the shared AFE-calibrated mV array; the returned positions follow
 * the upstream implementation's final absolute-amplitude correction. */
size_t wim_elgendi_detect_360(const float *samples, size_t count,
                              uint16_t *peaks, size_t peak_capacity,
                              wim_elgendi_workspace_t *workspace);

#endif
