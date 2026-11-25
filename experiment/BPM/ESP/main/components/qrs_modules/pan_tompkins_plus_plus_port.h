#ifndef PAN_TOMPKINS_PLUS_PLUS_PORT_H
#define PAN_TOMPKINS_PLUS_PLUS_PORT_H

#include <stddef.h>
#include <stdint.h>

#define PANPP_MAX_SAMPLES 3600U
#define PANPP_MAX_PEAKS 128U
#define PANPP_PAD 27U
#define PANPP_INTEGRATED_SAMPLES (PANPP_MAX_SAMPLES + 54U - 1U)
#define PANPP_MAX_CANDIDATES ((PANPP_INTEGRATED_SAMPLES + 1U) / 2U)
#define PANPP_MAX_BEATS 256U
#define PANPP_BLOCKED_BYTES ((PANPP_INTEGRATED_SAMPLES + 7U) / 8U)

typedef struct {
    /* Lifetimes do not overlap: input -> derivative -> integrated. */
    union {
        double input[PANPP_MAX_SAMPLES];
        double derivative[PANPP_MAX_SAMPLES];
        double integrated[PANPP_INTEGRATED_SAMPLES];
    };
    /* Filter scratch is dead before smoothing begins. */
    union {
        double smooth[PANPP_MAX_SAMPLES];
    };
    double filtered[PANPP_MAX_SAMPLES];
    uint16_t candidates[PANPP_MAX_CANDIDATES];
    uint8_t blocked[PANPP_BLOCKED_BYTES];
    uint16_t qrs_integrated[PANPP_MAX_BEATS];
    uint16_t qrs_raw[PANPP_MAX_BEATS];
    size_t candidate_count;
    size_t beat_count;
    size_t raw_count;
} panpp_workspace_t;

/* Fixed 360-Hz, 10-second-compatible port of Pan-Tompkins++.
 * Input is the common AFE-domain calibrated ECG in mV. */
size_t panpp_detect_360(const float *input_samples, size_t count,
                        uint16_t *peaks, size_t peak_capacity,
                        panpp_workspace_t *workspace);

#endif
