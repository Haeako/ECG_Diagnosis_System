#ifndef AF_RUNTIME_H
#define AF_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

#define AF_RUNTIME_RR_COUNT 30U

typedef struct {
    bool valid;
    bool new_window;
    bool raw_prediction;
    bool confirmed;
    bool new_event;
    uint16_t score;
    uint16_t probability_x10000;
    uint16_t event_id;
    uint32_t window_id;
    uint32_t window_start_ms;
    uint32_t window_end_ms;
} af_runtime_result_t;

void af_runtime_reset(void);
af_runtime_result_t af_runtime_push_peak(uint32_t peak_timestamp_ms);
af_runtime_result_t af_runtime_latest(void);
void af_runtime_last_features(float output[5]);

#endif
