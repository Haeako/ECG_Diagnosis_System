#ifndef ECG_QRS_MODULE_H
#define ECG_QRS_MODULE_H
#include <stddef.h>
#include <stdint.h>
/* 360 Hz, calibrated mV after common AFE. Caller owns aligned workspace.
 * Returns SIZE_MAX on invalid input/capacity. No heap allocation or I/O.
 * Context resets for each 3600-sample call; BPM scheduling belongs to caller. */
size_t ecg_qrs_workspace_bytes(void);
size_t ecg_qrs_detect(const float *mv, size_t count, uint16_t *peaks,
                      size_t capacity, void *workspace, size_t workspace_bytes);
#endif
