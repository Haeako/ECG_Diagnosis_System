#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "qrs_module.h"

int qrs_module_sliding(const float *x,size_t count,float *bpms,uint16_t *peaks,uint16_t *counts)
{
    if(!x||!bpms||!peaks||!counts||count<3600||count%1800)return 1;
    size_t bytes=ecg_qrs_workspace_bytes();void *workspace=malloc(bytes);
    if(!workspace)return 4;
    size_t window=0;
    for(size_t offset=0;offset+3600<=count;offset+=1800,++window){
        uint16_t *local=peaks+window*128;
        size_t n=ecg_qrs_detect(x+offset,3600,local,128,workspace,bytes);
        if(n>=128){free(workspace);return 2;}
        /* Same stable chronological ordering as the Python protocol; no dedup. */
        for(size_t i=1;i<n;++i){uint16_t v=local[i];size_t j=i;
            while(j&&local[j-1]>v){local[j]=local[j-1];--j;}local[j]=v;}
        double sum=0;unsigned intervals=0;
        for(size_t j=0;j<n;++j){
            if(local[j]>=3600){free(workspace);return 3;}
            if(j&&local[j]>=1800){sum+=local[j]-local[j-1];++intervals;}
        }
        counts[window]=(uint16_t)n;
        bpms[window]=intervals&&sum>0 ? (float)(21600.0*intervals/sum) : NAN;
    }
    free(workspace);return 0;
}
