#ifndef AF_MODEL_ESP32_BENCHMARK_H
#define AF_MODEL_ESP32_BENCHMARK_H

#include <stdint.h>
#include "af_model_int8.h"

void af_model_esp32_benchmark(const int8_t features[AF_MODEL_FEATURE_COUNT], uint32_t iterations);

#endif
