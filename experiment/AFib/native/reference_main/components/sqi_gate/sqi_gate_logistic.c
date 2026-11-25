#include "sqi_gate_logistic.h"
#include <math.h>
static const float median[5] = {1.0f, 0.809385478f, 0.279931337f, 0.981418252f, 0.0f};
static const float weight[5] = {6.72329807f, -3.87026501f, 15.0100489f, 1.69090247f, -34.6090965f};
static const float bias = -8.29388046f;
static const float threshold = 0.949167609f;
float sqi_gate_probability(const float f[5]) {
    float z=bias;
    if (!f) return NAN;
    for (unsigned i=0;i<5;i++) { float x=isfinite(f[i])?f[i]:median[i]; z += weight[i]*x; }
    if (z >= 0.0f) { float e=expf(-z); return 1.0f/(1.0f+e); }
    float e=expf(z); return e/(1.0f+e);
}
sqi_gate_decision_t sqi_gate_predict(const float f[5]) {
    float p=sqi_gate_probability(f); return isfinite(p) && p>=threshold ? SQI_GATE_USABLE : SQI_GATE_UNUSABLE;
}
size_t sqi_gate_flash_bytes(void) { return sizeof(median)+sizeof(weight)+sizeof(bias)+sizeof(threshold); }
size_t sqi_gate_working_ram_bytes(void) { return sizeof(float)*2U+sizeof(unsigned); }
