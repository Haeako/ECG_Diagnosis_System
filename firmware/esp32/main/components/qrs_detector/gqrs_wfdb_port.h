#ifndef GQRS_WFDB_PORT_H
#define GQRS_WFDB_PORT_H

#include <stddef.h>
#include <stdint.h>

/*
 * Benchmark-only fixed-10-second port based on WFDB's Python GQRS, itself a
 * direct port of George Moody's gqrs.c (GPL-2.0-or-later). Do not link this
 * file into production firmware without reviewing the upstream license.
 * The 4096-sample ring is valid only for this benchmark's 3600-sample window.
 */

#define GQRS_MAX_SAMPLES 3600U
#define GQRS_RING_LENGTH 4096U /* sufficient for the fixed 10 s benchmark window */
#define GQRS_MAX_PEAKS 128U
#define GQRS_PEAK_RING 64U

typedef struct {
    int32_t time;
    int64_t amp;
    int16_t type;
    uint8_t next;
    uint8_t prev;
} gqrs_peak_t;

typedef struct {
    int64_t qfv[GQRS_RING_LENGTH];
    int64_t smv[GQRS_RING_LENGTH];
    gqrs_peak_t peak_ring[GQRS_PEAK_RING];
    const float *signal_mv;
    double adc_gain;
    int64_t adc_baseline;
    int64_t pthr, qthr, pthmin, qthmin;
    int64_t rrdev, rrinc, rrmean, rrmax, rrmin;
    int64_t rtmax, rtmin, rtmean, v1, v1norm;
    int64_t smt, smt0;
    int32_t dt, dt2, dt3, dt4, smdt;
    int32_t sample_count, t, tf, annot_time, last_peak, last_qrs;
    int32_t countdown, current_peak;
    int32_t state, sample_valid;
    size_t result_count;
} gqrs_wfdb_workspace_t;

/* Fixed-window benchmark port of WFDB GQRS, using WFDB's 24-bit ADC rule. */
size_t gqrs_wfdb_detect_360(const float *signal_mv, size_t count,
                            uint16_t *peaks, size_t peak_capacity,
                            gqrs_wfdb_workspace_t *workspace);

#endif
