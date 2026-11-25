#ifndef PAN_PYECG_H
#define PAN_PYECG_H

#include <stddef.h>
#include <stdint.h>

#define PAN_PYECG_FS 360U
#define PAN_PYECG_MAX_SAMPLES 3600U
#define PAN_PYECG_MAX_CANDIDATES ((PAN_PYECG_MAX_SAMPLES + 1U) / 2U)

typedef struct {
    double input[3600];
    double mwa_ring[54];
    uint16_t candidate_index[PAN_PYECG_MAX_CANDIDATES];
    double candidate_value[PAN_PYECG_MAX_CANDIDATES];
    uint16_t signal_peak[PAN_PYECG_MAX_CANDIDATES + 1U];
    uint16_t signal_candidate[PAN_PYECG_MAX_CANDIDATES];
    uint16_t candidate_count;
    uint16_t signal_count;
    uint16_t signal_candidate_count;
    size_t workspace_bytes;
} pan_pyecg_workspace_t;

/* Batch/window port of ecgdetectors.Detectors.pan_tompkins_detector at 360 Hz.
 * Input is already preprocessed identically to the Python benchmark. Returned
 * indices retain the Python method's integrated-signal timing (no R alignment). */
size_t pan_pyecg_detect_360(const float *input_samples, size_t count,
                            uint16_t *peaks, size_t peak_capacity,
                            pan_pyecg_workspace_t *workspace);

#endif
