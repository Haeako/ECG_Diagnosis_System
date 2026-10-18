#include <math.h>
#include <stdio.h>
#include "sqi_gate_logistic.h"
#include "sqi_gate_golden_vectors.h"

int main(void) {
    float max_error=0.0f;
    for (size_t i=0;i<SQI_GATE_GOLDEN_COUNT;i++) {
        float got=sqi_gate_probability(SQI_GATE_GOLDEN[i].x);
        float error=fabsf(got-SQI_GATE_GOLDEN[i].probability);
        if (error>max_error) max_error=error;
        if (error>2e-6f || (int)sqi_gate_predict(SQI_GATE_GOLDEN[i].x)!=SQI_GATE_GOLDEN[i].decision) {
            fprintf(stderr,"FAIL vector=%zu got=%.9g expected=%.9g error=%.9g\n",i,got,SQI_GATE_GOLDEN[i].probability,error);
            return 1;
        }
    }
    printf("PASS vectors=%u max_abs_probability_error=%.9g parameter_bytes=%zu working_ram_bytes=%zu\n",
           SQI_GATE_GOLDEN_COUNT,max_error,sqi_gate_flash_bytes(),sqi_gate_working_ram_bytes());
    return 0;
}
