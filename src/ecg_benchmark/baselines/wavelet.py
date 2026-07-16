"""Wavelet-threshold ECG denoising baseline."""

from __future__ import annotations

import numpy as np


def _pywt_module():
    try:
        import pywt
    except ImportError as exc:
        raise ImportError("Wavelet baseline requires PyWavelets. Install pywt or skip this baseline.") from exc
    return pywt


def _denoise_1d(
    x: np.ndarray,
    wavelet: str,
    level: int | None,
    mode: str,
    threshold_scale: float,
    approximation_scale: float,
) -> np.ndarray:
    pywt = _pywt_module()
    coeffs = pywt.wavedec(x, wavelet=wavelet, mode="symmetric", level=level)
    sigma = np.median(np.abs(coeffs[-1])) / 0.6745
    threshold = threshold_scale * sigma * np.sqrt(2.0 * np.log(x.size))
    denoised_coeffs = [approximation_scale * coeffs[0]]
    if threshold <= 0 or not np.isfinite(threshold):
        denoised_coeffs.extend(coeffs[1:])
    else:
        denoised_coeffs.extend(pywt.threshold(c, threshold, mode=mode) for c in coeffs[1:])
    out = pywt.waverec(denoised_coeffs, wavelet=wavelet, mode="symmetric")
    return out[: x.size]


def wavelet_denoise(
    signal_in: np.ndarray,
    wavelet: str = "db4",
    level: int | None = None,
    mode: str = "soft",
    threshold_scale: float = 1.0,
    approximation_scale: float = 1.0,
) -> np.ndarray:
    """Apply wavelet thresholding over the last axis."""

    x = np.asarray(signal_in, dtype=np.float64)
    return np.apply_along_axis(
        lambda row: _denoise_1d(
            row, wavelet, level, mode, threshold_scale, approximation_scale
        ),
        -1,
        x,
    )


def _swt_denoise_1d(
    x: np.ndarray,
    wavelet: str,
    level: int,
    threshold_scale: float,
    mode: str,
) -> np.ndarray:
    pywt = _pywt_module()
    original_len = x.size
    block = 2 ** int(level)
    pad_len = (block - original_len % block) % block
    padded = np.pad(x, (0, pad_len), mode="edge") if pad_len else x
    coeffs = pywt.swt(padded, wavelet=wavelet, level=level)
    sigma = np.median(np.abs(coeffs[-1][1])) / 0.6745
    threshold = threshold_scale * sigma * np.sqrt(2.0 * np.log(padded.size))
    denoised_coeffs = []
    for approx, detail in coeffs:
        if threshold <= 0 or not np.isfinite(threshold):
            denoised_detail = detail
        else:
            denoised_detail = pywt.threshold(detail, threshold, mode=mode)
        denoised_coeffs.append((approx, denoised_detail))
    out = pywt.iswt(denoised_coeffs, wavelet=wavelet)
    return np.asarray(out[:original_len], dtype=np.float64)


def swt_denoise(
    signal_in: np.ndarray,
    wavelet: str = "rbio3.9",
    level: int = 5,
    threshold_scale: float = 0.5,
    mode: str = "soft",
) -> np.ndarray:
    """Stationary wavelet transform thresholding baseline.

    Defaults follow the Nature Scientific Reports 2025 paper's highlighted SWT
    configuration: rbio3.9, level 5, threshold scale 0.5.
    """

    x = np.asarray(signal_in, dtype=np.float64)
    return np.apply_along_axis(
        lambda row: _swt_denoise_1d(row, wavelet, level, threshold_scale, mode),
        -1,
        x,
    )
