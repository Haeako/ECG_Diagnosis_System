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


def _windowed_errors(
    reference: np.ndarray,
    estimate: np.ndarray,
    window: int,
    hop: int,
) -> tuple[np.ndarray, np.ndarray]:
    ref = _as_float_array(reference)
    est = _as_float_array(estimate)
    if ref.shape != est.shape:
        raise ValueError("reference and estimate must have the same shape")
    window = min(window, ref.shape[-1])
    starts = list(range(0, max(1, ref.shape[-1] - window + 1), hop))
    final = ref.shape[-1] - window
    if starts[-1] != final:
        starts.append(final)
    local_rmse, local_prd = [], []
    for start in starts:
        stop = start + window
        error = ref[..., start:stop] - est[..., start:stop]
        local_rmse.append(np.sqrt(np.mean(np.square(error), axis=-1)))
        local_prd.append(100.0 * np.sqrt(
            _energy(error) / (_energy(ref[..., start:stop]) + EPS)
        ))
    return np.stack(local_rmse, axis=-1), np.stack(local_prd, axis=-1)


def _shift_corrected_metrics(
    reference: np.ndarray,
    estimate: np.ndarray,
    max_shift_samples: int,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    ref = _as_float_array(reference)
    est = _as_float_array(estimate)
    leading_shape = ref.shape[:-1]
    ref_rows = ref.reshape(-1, ref.shape[-1])
    est_rows = est.reshape(-1, est.shape[-1])
    best_rmse = np.full(len(ref_rows), np.inf)
    best_prd = np.full(len(ref_rows), np.inf)
    best_shift = np.zeros(len(ref_rows), dtype=np.int64)
    for shift in range(-max_shift_samples, max_shift_samples + 1):
        if shift < 0:
            aligned_ref, aligned_est = ref_rows[:, -shift:], est_rows[:, :shift]
        elif shift > 0:
            aligned_ref, aligned_est = ref_rows[:, :-shift], est_rows[:, shift:]
        else:
            aligned_ref, aligned_est = ref_rows, est_rows
        error = aligned_ref - aligned_est
        values = np.sqrt(np.mean(np.square(error), axis=-1))
        improved = values < best_rmse
        best_rmse[improved] = values[improved]
        best_prd[improved] = 100.0 * np.sqrt(
            _energy(error[improved]) / (_energy(aligned_ref[improved]) + EPS)
        )
        best_shift[improved] = shift
    return (
        best_rmse.reshape(leading_shape),
        best_prd.reshape(leading_shape),
        best_shift.reshape(leading_shape),
    )


def clean_guard_protocol(
    clean: np.ndarray,
    clean_output: np.ndarray,
    fs: float,
    local_window_sec: float = 1.0,
    local_hop_sec: float = 0.5,
    boundary_sec: float = 1.0,
    max_shift_ms: float = 20.0,
    reduction: str = "mean",
) -> dict[str, float | np.ndarray]:
    """CleanGuard+ protocol for global, local, boundary, and delay damage.

    Shift-corrected values are diagnostic only. Primary metrics preserve exact
    sample alignment and deliberately avoid dynamic time warping.
    """

    ref = _as_float_array(clean)
    out = _as_float_array(clean_output)
    if ref.shape != out.shape:
        raise ValueError("clean and clean_output must have the same shape")
    if ref.shape[-1] < 2:
        raise ValueError("CleanGuard+ requires at least two samples")
    if fs <= 0:
        raise ValueError("fs must be positive")

    metrics = clean_guard(ref, out, reduction=reduction)
    window = max(2, int(round(local_window_sec * fs)))
    hop = max(1, int(round(local_hop_sec * fs)))
    local_rmse, local_prd = _windowed_errors(ref, out, window, hop)
    metrics["CleanGuard_WorstLocalRMSE"] = _reduce(np.max(local_rmse, axis=-1), reduction)
    metrics["CleanGuard_MaxLocalPRD"] = _reduce(np.max(local_prd, axis=-1), reduction)

    boundary = min(ref.shape[-1] // 2, max(1, int(round(boundary_sec * fs))))
    boundary_ref = np.concatenate((ref[..., :boundary], ref[..., -boundary:]), axis=-1)
    boundary_out = np.concatenate((out[..., :boundary], out[..., -boundary:]), axis=-1)
    metrics["CleanGuard_BoundaryRMSE"] = rmse(boundary_ref, boundary_out, reduction=reduction)

    max_shift = max(0, int(round(max_shift_ms * fs / 1000.0)))
    corrected_rmse, corrected_prd, shifts = _shift_corrected_metrics(ref, out, max_shift)
    metrics["CleanGuard_ShiftCorrectedRMSE"] = _reduce(corrected_rmse, reduction)
    metrics["CleanGuard_ShiftCorrectedPRD"] = _reduce(corrected_prd, reduction)
    metrics["CleanGuard_AbsShiftMs"] = _reduce(np.abs(shifts) * 1000.0 / fs, reduction)
    return metrics
