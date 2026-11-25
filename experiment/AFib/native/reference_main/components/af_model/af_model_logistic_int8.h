#ifndef AF_MODEL_LOGISTIC_INT8_H
#define AF_MODEL_LOGISTIC_INT8_H
#include <stddef.h>
#include <stdint.h>
float af_lr_predict_probability(const float features[5]);
int af_lr_predict(const float features[5]);
size_t af_lr_flash_bytes(void);
size_t af_lr_working_ram_bytes(void);
#endif
