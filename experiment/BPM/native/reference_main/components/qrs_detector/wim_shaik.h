#ifndef WIM_SHAIK_H
#define WIM_SHAIK_H

#include <stddef.h>
#include <stdint.h>

#define WIM_SHAIK_MAX_SAMPLES 3600U
#define WIM_SHAIK_MAX_PEAKS 40U
#define WIM_SHAIK_STFT_FRAMES 361U
#define WIM_SHAIK_MAX_CANDIDATES ((WIM_SHAIK_MAX_SAMPLES + 1U) / 2U)

typedef struct {
    float signal1[WIM_SHAIK_MAX_SAMPLES];
    float signal2[WIM_SHAIK_MAX_SAMPLES + 1U];
    float threshold1[WIM_SHAIK_MAX_SAMPLES + 1U];
    float threshold2[WIM_SHAIK_MAX_SAMPLES];
    float scratch[WIM_SHAIK_MAX_SAMPLES];
    float frame1[WIM_SHAIK_STFT_FRAMES];
    float frame2[WIM_SHAIK_STFT_FRAMES];
    uint16_t candidates[WIM_SHAIK_MAX_CANDIDATES];
    uint16_t selected[WIM_SHAIK_MAX_PEAKS];
} wim_shaik_workspace_t;

/* WIM Shaik STFT detector, fixed to the upstream defaults at 360 Hz. */
size_t wim_shaik_detect_360(const float *samples, size_t count,
                            uint16_t *peaks, size_t peak_capacity,
                            wim_shaik_workspace_t *workspace);

#endif
