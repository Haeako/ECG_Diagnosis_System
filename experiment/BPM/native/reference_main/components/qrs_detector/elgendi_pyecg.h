#ifndef ELGENDI_PYECG_H
#define ELGENDI_PYECG_H

#include <stddef.h>
#include <stdint.h>

#define ELGENDI_PYECG_FS 360U
#define ELGENDI_PYECG_MAX_SAMPLES 3600U
#define ELGENDI_PYECG_MAX_PEAKS 64U

typedef struct {
    float filter_state[4];
    float filtered[ELGENDI_PYECG_MAX_SAMPLES];
    float mwa_ring[216];
    size_t workspace_bytes;
} elgendi_pyecg_workspace_t;

/* Batch/window C port of ecgdetectors.Detectors.two_average_detector, fs=360 Hz.
 * Input preprocessing and output timing follow the Python benchmark: no R-peak
 * alignment correction is applied. */
size_t elgendi_pyecg_detect_360(const float *samples, size_t count,
                               uint16_t *peaks, size_t peak_capacity,
                               elgendi_pyecg_workspace_t *workspace);

#endif
