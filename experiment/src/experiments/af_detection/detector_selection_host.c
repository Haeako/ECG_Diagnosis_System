/* Experiment-only adapter: unchanged production and candidate C detectors. */
#include <stdint.h>
#include "components/bpm/ExtractFeature.h"
#include "fast_qrs_port.h"
#include "wim_nguyen.h"
#include "wim_elgendi.h"
#include "sqi_xqrs.h"
#ifdef _WIN32
#define API __declspec(dllexport)
#else
#define API
#endif

static union {
    fast_qrs_workspace_t fast;
    wim_nguyen_workspace_t nguyen;
    wim_elgendi_workspace_t elgendi;
    sqi_xqrs_workspace_t xqrs;
} work;

/* The production notch output is the SAME calibrated input for all candidate
 * algorithms. Production keeps its continuous detector state for the record. */
API int prepare_record(const int32_t *adc, unsigned n, int16_t *notched,
                       uint32_t *peaks, unsigned capacity, uint32_t *counts)
{
    unsigned found=0;
    ecg_signal_init(0);
    for (unsigned i=0; i<n; i++) {
        uint32_t now=(uint32_t)(((uint64_t)i*1000+180)/360);
        ecg_record_t r=ecg_signal_process(adc[i],now);
        notched[i]=r.notch_49_51hz;
        if (r.is_peak) {
            if (found>=capacity) return -1;
            if (found && r.r_peak_timestamp_ms<=peaks[2*(found-1)+1]) return -2;
            peaks[2*found]=now; peaks[2*found+1]=r.r_peak_timestamp_ms;
            found++;
        }
    }
    counts[0]=found;
    return 0;
}

/* Method IDs fixed before analysis: FastQRS, WIM Nguyen, WIM Elgendi,
 * fixed-250-Hz XQRS C. Blocks are disjoint 10 s; no future backfill. */
API int detect_blocks(const int16_t *input, unsigned n, unsigned method,
                      uint32_t *peaks, unsigned capacity, uint32_t *counts)
{
    float block[3600], resampled[2500];
    uint16_t found[128];
    unsigned total=0, blocks=0, tail=0;
    for (unsigned start=0; start<n; start+=3600) {
        unsigned count=n-start; if (count>3600) count=3600;
        if (count<32) { tail=count; break; }
        for (unsigned i=0; i<count; i++) block[i]=(float)input[start+i]/200.0f;
        size_t nf=0;
        if (method==0) nf=fast_qrs_detect_360(block,count,found,128,&work.fast);
        else if (method==1) nf=wim_nguyen_detect_360(block,count,found,128,&work.nguyen);
        else if (method==2) nf=wim_elgendi_detect_360(block,count,found,128,&work.elgendi);
        else if (method==3) {
            unsigned nn=(unsigned)(((uint64_t)count*250)/360);
            for (unsigned i=0; i<nn; i++) {
                unsigned scaled=i*360, index=scaled/250, rem=scaled%250;
                unsigned next=index+1<count?index+1:index;
                float fraction=(float)rem/250.0f;
                resampled[i]=block[index]*(1.0f-fraction)+block[next]*fraction;
            }
            nf=sqi_xqrs_detect_250(resampled,nn,found,128,&work.xqrs);
        } else return -4;
        if (nf>=128) return -1;
        blocks++;
        uint32_t available=(uint32_t)(((uint64_t)(start+count)*1000+359)/360);
        for (size_t j=0; j<nf; j++) {
            if (total>=capacity) return -2;
            uint32_t peak=method==3
                ? (uint32_t)(((uint64_t)start*1000+180)/360)+(uint32_t)found[j]*4
                : (uint32_t)(((uint64_t)(start+found[j])*1000+180)/360);
            if ((total && peak<=peaks[2*(total-1)+1]) || peak>available) return -3;
            peaks[2*total]=available; peaks[2*total+1]=peak; total++;
        }
    }
    counts[0]=total; counts[1]=blocks; counts[2]=tail;
    return 0;
}
