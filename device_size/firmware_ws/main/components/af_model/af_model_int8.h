#ifndef AF_MODEL_INT8_H
#define AF_MODEL_INT8_H

#include <stddef.h>
#include <stdint.h>

#define AF_MODEL_FEATURE_COUNT 5U
#define AF_MODEL_TREE_COUNT 80U
#define AF_MODEL_NODE_COUNT 17712U
#define AF_MODEL_SCORE_THRESHOLD 10240U

typedef struct {
    uint16_t score;
    uint16_t comparisons;
} af_model_result_t;

void af_model_quantize(const float features[AF_MODEL_FEATURE_COUNT], int8_t output[AF_MODEL_FEATURE_COUNT]);
af_model_result_t af_model_predict_int8(const int8_t features[AF_MODEL_FEATURE_COUNT]);
int af_model_predict(const float features[AF_MODEL_FEATURE_COUNT]);
size_t af_model_flash_bytes(void);
size_t af_model_working_ram_bytes(void);

#endif
