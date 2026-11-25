#ifndef SQI_TEMPLATE_MATCHING_H
#define SQI_TEMPLATE_MATCHING_H

#include <stdint.h>

/* Returns NAN when the window cannot produce at least three complete beats. */
float sqi_template_average_correlation(const int16_t *samples,
                                       uint16_t sample_count,
                                       uint32_t sample_rate_hz,
                                       const uint32_t *peak_ms,
                                       uint8_t peak_count,
                                       uint32_t window_start_ms,
                                       uint32_t median_rr_ms);

#endif
