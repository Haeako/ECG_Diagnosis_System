#ifndef FAST_QRS_PORT_H
#define FAST_QRS_PORT_H

#include <stddef.h>
#include <stdint.h>

#define FAST_QRS_MAX_SAMPLES 3600U
#define FAST_QRS_MAX_PEAKS 128U
#define FAST_QRS_PAD 15U
#define FAST_QRS_EXTENDED (FAST_QRS_MAX_SAMPLES + 2U * FAST_QRS_PAD)

typedef struct {
    float filtered[FAST_QRS_MAX_SAMPLES];
    float integrated[FAST_QRS_MAX_SAMPLES];
    uint16_t candidates[FAST_QRS_MAX_SAMPLES];
    float candidate_values[FAST_QRS_MAX_SAMPLES];
    uint16_t rr_buffer[10];
} fast_qrs_workspace_t;

/* Port of fast-qrs-detector 0.3.0 defaults at 360 Hz. */
size_t fast_qrs_detect_360(const float *signal, size_t count,
                           uint16_t *peaks, size_t peak_capacity,
                           fast_qrs_workspace_t *workspace);

#endif
