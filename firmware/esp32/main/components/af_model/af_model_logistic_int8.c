#include "af_model_logistic_int8.h"
#include <math.h>
static const float input_scales[5] = {0.00392156862745f, 0.0082471894009f, 1.44120819956f, 0.0125224240556f, 3.13725490196f};
static const int8_t input_zero_points[5] = {-128, -128, -128, -128, -128};
static const int16_t weights_q15[5] = {30844, 11909, 10376, -32767, -21242};
static const float weight_scale = 1.38169819034e-06f;
static const float intercept = -0.424145536255f;
static void quantize(const float *x, int8_t *q) {
  for (int i=0;i<5;i++) {
    long v=lrintf(x[i]/input_scales[i]+input_zero_points[i]);
    if(v < -128) { v=-128; }
    if(v > 127) { v=127; }
    q[i]=(int8_t)v;
  }
}
float af_lr_predict_probability(const float features[5]) {
  int8_t q[5]; quantize(features,q); int32_t acc=0;
  for(int i=0;i<5;i++) acc += ((int32_t)q[i]-(int32_t)input_zero_points[i])*(int32_t)weights_q15[i];
  float z=(float)acc*weight_scale+intercept;
  if(z>=0.0f) { float e=expf(-z); return 1.0f/(1.0f+e); }
  float e=expf(z); return e/(1.0f+e);
}
int af_lr_predict(const float features[5]) { return af_lr_predict_probability(features)>=0.5f; }
size_t af_lr_flash_bytes(void) { return sizeof(input_scales)+sizeof(input_zero_points)+sizeof(weights_q15)+sizeof(weight_scale)+sizeof(intercept); }
size_t af_lr_working_ram_bytes(void) { return 5U*sizeof(int8_t)+sizeof(int32_t)+2U*sizeof(float); }
