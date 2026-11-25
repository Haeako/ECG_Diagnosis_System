#ifndef WIM_ZHAI_H
#define WIM_ZHAI_H

#include <stddef.h>
#include <stdint.h>

#define WIM_ZHAI_MAX_SAMPLES 3600U
#define WIM_ZHAI_MAX_PEAKS 128U
#define WIM_ZHAI_PAD 16U
#define WIM_ZHAI_TEMPLATE 158U

typedef struct {
    float filtered[WIM_ZHAI_MAX_SAMPLES];
    float scratch[WIM_ZHAI_MAX_SAMPLES];
    float template_signal[WIM_ZHAI_TEMPLATE];
    uint16_t selected[WIM_ZHAI_MAX_PEAKS];
} wim_zhai_workspace_t;

/* WIM Zhai defaults at 360 Hz, operating on a 10-second AFE input. */
size_t wim_zhai_detect_360(const float *samples, size_t count,
                           uint16_t *peaks, size_t peak_capacity,
                           wim_zhai_workspace_t *workspace);

#endif
