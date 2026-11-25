#ifndef WIM_XU_H
#define WIM_XU_H

#include <stddef.h>
#include <stdint.h>

#define WIM_XU_MAX_SAMPLES 3600U
#define WIM_XU_MAX_PEAKS 128U
#define WIM_XU_PAD 6U
#define WIM_XU_HILBERT_HALF 1800U

typedef struct {
    double input[3600];
    double filtered[WIM_XU_MAX_SAMPLES];
    double scratch[WIM_XU_MAX_SAMPLES + 2U * WIM_XU_PAD];
    double fft_imag[WIM_XU_MAX_SAMPLES];
} wim_xu_workspace_t;

/* WIM Xu defaults at 360 Hz; includes the length-3600 discrete Hilbert
 * transform without an external FFT/runtime dependency. */
size_t wim_xu_detect_360(const float *input_samples, size_t count,
                         uint16_t *peaks, size_t peak_capacity,
                         wim_xu_workspace_t *workspace);

#endif
