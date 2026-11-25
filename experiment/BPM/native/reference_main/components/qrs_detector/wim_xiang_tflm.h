#ifndef WIM_XIANG_TFLM_H
#define WIM_XIANG_TFLM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float derivative[3600];
    float smoothed_derivative[3600];
    float salience[3600];
    float integral[3600];
} wim_xiang_workspace_t;

size_t wim_xiang_tflm_detect_360(const float *samples, size_t count,
                                 uint16_t *peaks, size_t peak_capacity,
                                 wim_xiang_workspace_t *workspace,
                                 size_t *arena_bytes);

#ifdef __cplusplus
}
#endif

#endif
