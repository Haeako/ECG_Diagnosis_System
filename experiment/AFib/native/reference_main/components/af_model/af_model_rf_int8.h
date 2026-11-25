#ifndef AF_MODEL_RF_INT8_H
#define AF_MODEL_RF_INT8_H
#include <stddef.h>
#include <stdint.h>
typedef struct { uint16_t score; uint16_t comparisons; } af_rf_result_t;
void af_rf_quantize(const float features[5], int8_t output[5]);
af_rf_result_t af_rf_predict_int8(const int8_t features[5]);
af_rf_result_t af_rf_predict(const float features[5]);
int af_rf_predict_label(const float features[5]);
size_t af_rf_flash_bytes(void);
size_t af_rf_working_ram_bytes(void);
#endif
