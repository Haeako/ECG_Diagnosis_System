#ifndef SQI_XQRS_H
#define SQI_XQRS_H

#include <stddef.h>
#include <stdint.h>

/* The frozen comparison protocol uses 10 s QTDB cores with 2 s context on
 * either side: 3,500 samples at 250 Hz. */
#define SQI_XQRS_FS 250U
#define SQI_XQRS_MAX_SAMPLES 3500U
#define SQI_XQRS_MAX_PEAKS 512U
#define SQI_XQRS_MAX_CANDIDATES 512U

typedef struct {
    float filtered[SQI_XQRS_MAX_SAMPLES];
    float integrated[SQI_XQRS_MAX_SAMPLES];
    float work_a[SQI_XQRS_MAX_SAMPLES + 150U];
    float work_b[SQI_XQRS_MAX_SAMPLES + 150U];
    uint16_t candidates[SQI_XQRS_MAX_CANDIDATES];
    uint16_t filtered_candidates[SQI_XQRS_MAX_CANDIDATES];
} sqi_xqrs_workspace_t;

/* Batch port of WFDB XQRS for the protocol's fixed 250 Hz domain. The
 * returned indices are relative to samples[0]. Returns 0 for invalid/flat
 * input. */
size_t sqi_xqrs_detect_250(const float *samples, size_t count,
                           uint16_t *peaks, size_t peak_capacity,
                           sqi_xqrs_workspace_t *workspace);
size_t sqi_xqrs_static_bytes(void);

#endif
