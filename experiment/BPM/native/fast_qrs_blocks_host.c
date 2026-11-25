#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "fast_qrs_port.h"

/* Same 10s reset and mean-RR formula as the isolated ESP benchmark. */
static fast_qrs_workspace_t workspace;
int fast_qrs_blocks(const float *x, size_t count, uint32_t *peaks,
                    size_t capacity, size_t *written, float *bpms)
{
    if(!x || !peaks || !written || !bpms || count%3600) return 1;
    *written=0;
    for(size_t offset=0;offset<count;offset+=3600) {
        uint16_t local[128];
        memset(&workspace,0,sizeof(workspace));
        size_t n=fast_qrs_detect_360(x+offset,3600,local,128,&workspace);
        if(n>=128 || *written+n>capacity) return 2;
        double sum=0;
        for(size_t i=0;i<n;++i) {
            if(local[i]>=3600 || (i && local[i]<=local[i-1])) return 3;
            peaks[(*written)++]=(uint32_t)(offset+local[i]);
            if(i)sum+=local[i]-local[i-1];
        }
        bpms[offset/3600]=n>=2 ? (float)(60.0*360.0*(n-1)/sum) : NAN;
    }
    return 0;
}
