/* Benchmark adapter only. No SQI calls, no firmware behavior changes. */
#include <stdint.h>
#include "task/Core1/ECGPipeline/ExFeature/ExtractFeature.h"
#include "components/af_model/af_runtime.h"
#include "components/af_model/af_model_int8.h"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif
/* events: end_ms,start_ms,score,probability,raw,confirmed,window_id,event_id */
API unsigned replay(const int32_t *adc, unsigned n, uint32_t *peaks,
                    uint32_t *bpm, uint32_t *events, uint32_t *counts, int af)
{
    unsigned np=0, nb=0, ne=0, next_bpm=5000;
    ecg_signal_init(0);
    af_runtime_reset();
    for (unsigned i=0;i<n;i++) {
        uint32_t t=(uint32_t)(((uint64_t)i*1000+180)/360);
        ecg_record_t r=ecg_signal_process(adc[i],t);
        if (t>=next_bpm) {
            bpm[2*nb]=t; bpm[2*nb+1]=r.bpm; nb++;
            next_bpm+=5000;
        }
        if (r.is_peak) {
            peaks[np++]=r.r_peak_timestamp_ms;
            if (af) {
                af_runtime_result_t a=af_runtime_push_peak(r.r_peak_timestamp_ms);
                if (a.new_window) {
                    uint32_t *e=events+8*ne++;
                    e[0]=a.window_end_ms; e[1]=a.window_start_ms;
                    e[2]=a.score; e[3]=a.probability_x10000;
                    e[4]=a.raw_prediction; e[5]=a.confirmed;
                    e[6]=a.window_id; e[7]=a.event_id;
                }
            }
        }
    }
    counts[0]=np; counts[1]=nb; counts[2]=ne;
    return np;
}
API void predict_many(const float *x,unsigned n,uint16_t *score) {
    for(unsigned i=0;i<n;i++) {
        int8_t q[AF_MODEL_FEATURE_COUNT];
        af_model_quantize(x+i*AF_MODEL_FEATURE_COUNT,q);
        score[i]=af_model_predict_int8(q).score;
    }
}
