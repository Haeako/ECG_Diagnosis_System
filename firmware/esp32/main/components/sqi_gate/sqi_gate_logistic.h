#ifndef SQI_GATE_LOGISTIC_H
#define SQI_GATE_LOGISTIC_H
#include <stddef.h>
#define SQI_GATE_FEATURE_COUNT 5U
typedef enum { SQI_GATE_UNUSABLE=0, SQI_GATE_USABLE=1, SQI_GATE_INSUFFICIENT=2 } sqi_gate_decision_t;
float sqi_gate_probability(const float features[SQI_GATE_FEATURE_COUNT]);
sqi_gate_decision_t sqi_gate_predict(const float features[SQI_GATE_FEATURE_COUNT]);
size_t sqi_gate_flash_bytes(void);
size_t sqi_gate_working_ram_bytes(void);
#endif
