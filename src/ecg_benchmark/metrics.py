"""Reconstruction and clean-passthrough metrics for ECG denoising.

Array convention
----------------
Functions accept NumPy-like arrays with the time axis on the last dimension:

    (time,), (batch, time), or (batch, channels, time)

Metrics are computed per item over the last axis and then averaged by default.
Use ``reduction="none"`` to keep per-item values.
"""

from __future__ import annotations

import numpy as np


EPS = 1e-12


def _as_float_array(x: np.ndarray | list[float]) -> np.ndarray:
    return np.asarray(x, dtype=np.float64)


def _reduce(values: np.ndarray, reduction: str) -> float | np.ndarray:
    if reduction == "none":
        return values
    if reduction == "mean":
        return float(np.mean(values))
    if reduction == "median":
        return float(np.median(values))
    raise ValueError(f"Unsupported reduction: {reduction!r}")


def _energy(x: np.ndarray) -> np.ndarray:
    return np.sum(np.square(x), axis=-1)


def rmse(reference: np.ndarray, estimate: np.ndarray, reduction: str = "mean") -> float | np.ndarray:
    """Root mean squared error over the time axis."""

    ref = _as_float_array(reference)
    est = _as_float_array(estimate)
    values = np.sqrt(np.mean(np.square(ref - est), axis=-1))
    return _reduce(values, reduction)


def snr_db(reference: np.ndarray, estimate: np.ndarray, reduction: str = "mean") -> float | np.ndarray:
    """Signal-to-noise ratio in dB using ``reference - estimate`` as error."""

    ref = _as_float_array(reference)
    est = _as_float_array(estimate)
    signal = _energy(ref)
    noise = _energy(ref - est)
    values = 10.0 * np.log10((signal + EPS) / (noise + EPS))
    return _reduce(values, reduction)


def delta_snr_db(
    clean: np.ndarray,
    noisy: np.ndarray,
    denoised: np.ndarray,
    reduction: str = "mean",
) -> float | np.ndarray:
    """SNR improvement from noisy input to denoised output."""

    clean_arr = _as_float_array(clean)
    noisy_arr = _as_float_array(noisy)
    denoised_arr = _as_float_array(denoised)
    out = snr_db(clean_arr, denoised_arr, reduction="none")
    inp = snr_db(clean_arr, noisy_arr, reduction="none")
    return _reduce(out - inp, reduction)


def prd_percent(reference: np.ndarray, estimate: np.ndarray, reduction: str = "mean") -> float | np.ndarray:
    """Percentage root-mean-square difference."""

    ref = _as_float_array(reference)
    est = _as_float_array(estimate)
    values = 100.0 * np.sqrt(_energy(ref - est) / (_energy(ref) + EPS))
    return _reduce(values, reduction)


def cosine_similarity(
    reference: np.ndarray,
    estimate: np.ndarray,
    reduction: str = "mean",
) -> float | np.ndarray:
    """Cosine similarity over the time axis."""

    ref = _as_float_array(reference)
    est = _as_float_array(estimate)
    numerator = np.sum(ref * est, axis=-1)
    denominator = np.sqrt(_energy(ref) * _energy(est)) + EPS
    values = numerator / denominator
    return _reduce(values, reduction)


def derivative_rmse(
    reference: np.ndarray,
    estimate: np.ndarray,
    reduction: str = "mean",
) -> float | np.ndarray:
    """RMSE between first differences, used as lightweight morphology damage."""

    ref = np.diff(_as_float_array(reference), axis=-1)
    est = np.diff(_as_float_array(estimate), axis=-1)
    return rmse(ref, est, reduction=reduction)


def reconstruction_metrics(
    clean: np.ndarray,
    noisy: np.ndarray,
    denoised: np.ndarray,
    reduction: str = "mean",
) -> dict[str, float | np.ndarray]:
    """Classical denoising utility metrics."""

    return {
        "SNR": snr_db(clean, denoised, reduction=reduction),
        "Noisy_SNR": snr_db(clean, noisy, reduction=reduction),
        "SNR_Improve": delta_snr_db(clean, noisy, denoised, reduction=reduction),
        "PRD": prd_percent(clean, denoised, reduction=reduction),
        "RMSE": rmse(clean, denoised, reduction=reduction),
        "dRMSE": derivative_rmse(clean, denoised, reduction=reduction),
        "CosSim": cosine_similarity(clean, denoised, reduction=reduction),
    }


def clean_guard(
    clean: np.ndarray,
    clean_output: np.ndarray,
    reduction: str = "mean",
) -> dict[str, float | np.ndarray]:
    """Measure how much a denoiser changes already-clean ECG."""

    return {
        "CleanGuard_PRD": prd_percent(clean, clean_output, reduction=reduction),
        "CleanGuard_RMSE": rmse(clean, clean_output, reduction=reduction),
        "CleanGuard_dRMSE": derivative_rmse(clean, clean_output, reduction=reduction),
        "CleanGuard_CosSim": cosine_similarity(clean, clean_output, reduction=reduction),
    }
