#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "fast_qrs_port.h"

static fast_qrs_workspace_t workspace;
/* Mirrors dummy_modules.c: reset detector on 10s context, emit every 5s,
 * use adjacent RR whose right endpoint lies in the latest 5s. */
int fast_qrs_sliding(const float *x, size_t count, float *bpms,
                     uint16_t *peaks, uint16_t *counts)
{
    if(!x || !bpms || !peaks || !counts || count<3600 || count%1800) return 1;
    size_t window=0;
    for(size_t offset=0;offset+3600<=count;offset+=1800,++window) {
        uint16_t *local=peaks+window*128;
        memset(&workspace,0,sizeof(workspace));
        size_t n=fast_qrs_detect_360(x+offset,3600,local,128,&workspace);
        if(n>=128) return 2;
        double sum=0;unsigned intervals=0;
        for(size_t j=0;j<n;++j) {
            if(local[j]>=3600 || (j && local[j]<=local[j-1])) return 3;
            if(j && local[j]>=1800) {sum+=local[j]-local[j-1];++intervals;}
        }
        counts[window]=(uint16_t)n;
        bpms[window]=intervals ? (float)(21600.0*intervals/sum) : NAN;
    }
    return 0;
}
