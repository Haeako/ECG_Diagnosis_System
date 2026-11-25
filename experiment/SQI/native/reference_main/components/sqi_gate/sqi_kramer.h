#ifndef SQI_KRAMER_H
#define SQI_KRAMER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SQI_KRAMER_FLAT_WINDOW 100U

typedef struct {
    float recent[SQI_KRAMER_FLAT_WINDOW];
    uint32_t sample_count;
    uint8_t recent_count;
    uint8_t recent_head;
    bool stationary_bad;
} sqi_kramer_state_t;

typedef struct {
    float score;
    bool accept;
    bool stationary_bad;
    bool heart_rate_bad;
    bool snr_bad;
} sqi_kramer_result_t;

void sqi_kramer_reset(sqi_kramer_state_t *state);
void sqi_kramer_push(sqi_kramer_state_t *state, float sample);
sqi_kramer_result_t sqi_kramer_finalize(const sqi_kramer_state_t *state,
                                        uint16_t filtered_peak_count,
                                        bool snr_bad);
size_t sqi_kramer_static_bytes(void);

#endif
