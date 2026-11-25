#ifndef SQI_CLIFFORD_H
#define SQI_CLIFFORD_H

#include <stdbool.h>
#include <stddef.h>

#define SQI_CLIFFORD_MAX_SAMPLES 1250U
#define SQI_CLIFFORD_FFT_SIZE 4096U

typedef struct {
    float real[SQI_CLIFFORD_FFT_SIZE];
    float imag[SQI_CLIFFORD_FFT_SIZE];
    float kernel_real[SQI_CLIFFORD_FFT_SIZE];
    float kernel_imag[SQI_CLIFFORD_FFT_SIZE];
} sqi_clifford_workspace_t;

typedef struct {
    float psqi;
    float ssqi;
    float ksqi;
    float fsqi;
    float bassqi;
    bool valid;
} sqi_clifford_result_t;

/*
 * One-lead Clifford-style features at the supplied sampling rate.
 * Input is assumed to be in mV when flatline_scale_to_uv=1000.
 * Frequency ratios use a mean-removed rectangular-window periodogram,
 * matching the project's scipy.signal.periodogram implementation.
 */
sqi_clifford_result_t sqi_clifford_extract(const float *samples, size_t count,
                                            float sample_rate_hz,
                                            float flatline_scale_to_uv,
                                            sqi_clifford_workspace_t *workspace);

#endif
