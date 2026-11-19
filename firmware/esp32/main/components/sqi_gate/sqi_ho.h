#ifndef SQI_HO_H
#define SQI_HO_H

#include <stddef.h>
#include <stdint.h>

/* Ho et al. / NeuroKit ICI kernel. Detector arrays are supplied by caller;
 * detectors must use the method's configured primary/secondary algorithms. */
int sqi_ho_ici_quality(const float *signal, size_t sample_count,
                       const uint32_t *primary_peaks, size_t primary_count,
                       const uint32_t *secondary_peaks, size_t secondary_count,
                       uint32_t sampling_rate, uint32_t tolerance_ms,
                       uint32_t *primary_scratch, size_t primary_capacity,
                       uint32_t *secondary_scratch, size_t secondary_capacity,
                       uint8_t *quality);

#endif
