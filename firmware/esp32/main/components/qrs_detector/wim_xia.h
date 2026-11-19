#ifndef WIM_XIA_H
#define WIM_XIA_H

#include <stddef.h>
#include <stdint.h>

#define WIM_XIA_SAMPLES 3600U
#define WIM_XIA_MAX_PEAKS 128U
#define WIM_XIA_FILTER_PAD 21U
#define WIM_XIA_FILTER_LENGTH (WIM_XIA_SAMPLES + 2U * WIM_XIA_FILTER_PAD)
#define WIM_XIA_DETAIL_CAPACITY 3800U
#define WIM_XIA_MAX_CANDIDATES 1800U
#define WIM_XIA_LEVELS 10U

typedef struct {
    uint16_t index;
    uint16_t height;
} wim_xia_candidate_t;

typedef struct {
    union {
        double filter_work[WIM_XIA_FILTER_LENGTH];
        float approximation_b[WIM_XIA_SAMPLES];
    } scratch;
    float approximation_a[WIM_XIA_SAMPLES];
    float details[WIM_XIA_DETAIL_CAPACITY];
    uint16_t cumulative[WIM_XIA_SAMPLES];
    wim_xia_candidate_t candidates[WIM_XIA_MAX_CANDIDATES];
    uint16_t level_length[WIM_XIA_LEVELS + 1U];
    uint16_t detail_offset[WIM_XIA_LEVELS + 1U];
    size_t detail_count;
    size_t candidate_count;
} wim_xia_workspace_t;

/* Fixed 360-Hz, 10-second port of the WIM repository Xia detector. Input is
 * the calibrated mV signal used by the common-input benchmark. */
size_t wim_xia_detect_360(const uint16_t *adc_samples, size_t count,
                          uint16_t *peaks, size_t peak_capacity,
                          wim_xia_workspace_t *workspace);

#ifdef WIM_XIA_HOST_TEST
void wim_xia_debug_filter_360(const uint16_t *adc_samples, double *filtered,
                              wim_xia_workspace_t *workspace);
#endif

#endif
