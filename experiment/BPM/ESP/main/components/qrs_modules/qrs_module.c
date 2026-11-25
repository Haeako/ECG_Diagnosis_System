#include "qrs_module.h"
#include ECG_QRS_HEADER
#include <string.h>

size_t ecg_qrs_workspace_bytes(void) { return sizeof(ECG_QRS_STATE); }
size_t ecg_qrs_detect(const float *mv, size_t count, uint16_t *peaks,
                      size_t capacity, void *workspace, size_t bytes)
{
    if (!mv || !peaks || !workspace || count != 3600U || capacity < 128U ||
        bytes < sizeof(ECG_QRS_STATE) ||
        (uintptr_t)workspace % _Alignof(ECG_QRS_STATE)) return SIZE_MAX;
    memset(workspace, 0, sizeof(ECG_QRS_STATE));
    size_t n = ECG_QRS_DETECT(mv, count, peaks, capacity, (ECG_QRS_STATE *)workspace);
    return n >= capacity ? SIZE_MAX : n;
}
