"""NumPy FIR baselines for ECG filtering."""

from __future__ import annotations

import numpy as np


def _as_float_array(x):
    return np.asarray(x, dtype=np.float64)


def _lowpass_kernel(cutoff_hz: float, fs: float, numtaps: int) -> np.ndarray:
    if numtaps % 2 == 0:
        raise ValueError("numtaps must be odd for symmetric FIR filtering")
    cutoff = float(cutoff_hz) / float(fs)
    n = np.arange(numtaps) - (numtaps - 1) / 2
    kernel = 2 * cutoff * np.sinc(2 * cutoff * n)
    kernel *= np.hamming(numtaps)
    return kernel / np.sum(kernel)


def _apply_fir(signal: np.ndarray, kernel: np.ndarray) -> np.ndarray:
    x = _as_float_array(signal)
    pad = len(kernel) // 2
    padded = np.pad(x, [(0, 0)] * (x.ndim - 1) + [(pad, pad)], mode="edge")
    return np.apply_along_axis(lambda row: np.convolve(row, kernel, mode="valid"), -1, padded)


def fir_highpass(signal: np.ndarray, fs: float, cutoff_hz: float = 0.5, numtaps: int = 101) -> np.ndarray:
    """High-pass FIR by spectral inversion of a low-pass kernel."""

    low = _lowpass_kernel(cutoff_hz, fs, numtaps)
    high = -low
    high[numtaps // 2] += 1.0
    return _apply_fir(signal, high)


def fir_bandpass(
    signal: np.ndarray,
    fs: float,
    low_hz: float = 0.5,
    high_hz: float = 40.0,
    numtaps: int = 101,
) -> np.ndarray:
    """Windowed-sinc FIR bandpass baseline."""

    low_kernel = _lowpass_kernel(low_hz, fs, numtaps)
    high_kernel = _lowpass_kernel(high_hz, fs, numtaps)
    band = high_kernel - low_kernel
    return _apply_fir(signal, band)
