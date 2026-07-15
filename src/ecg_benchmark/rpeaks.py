"""Pan-Tompkins-style R-peak detection and annotation-based scoring."""

from __future__ import annotations

import numpy as np


def match_r_peaks(
    reference: np.ndarray,
    estimate: np.ndarray,
    tolerance_samples: int,
) -> tuple[np.ndarray, np.ndarray]:
    """Greedily match each reference peak to at most one estimated peak."""

    ref = np.sort(np.asarray(reference, dtype=np.int64))
    est = np.sort(np.asarray(estimate, dtype=np.int64))
    if ref.ndim != 1 or est.ndim != 1:
        raise ValueError("R-peak arrays must be one-dimensional")
    if tolerance_samples < 0:
        raise ValueError("tolerance_samples must be non-negative")

    matched_ref: list[int] = []
    matched_est: list[int] = []
    used: set[int] = set()
    for ref_peak in ref:
        candidates = np.argsort(np.abs(est - ref_peak))
        for candidate in candidates:
            index = int(candidate)
            distance = abs(int(est[index]) - int(ref_peak))
            if distance > tolerance_samples:
                break
            if index not in used:
                used.add(index)
                matched_ref.append(int(ref_peak))
                matched_est.append(int(est[index]))
                break
    return np.asarray(matched_ref, dtype=np.int64), np.asarray(matched_est, dtype=np.int64)


def score_r_peaks(
    reference: np.ndarray,
    estimate: np.ndarray,
    fs: float,
    tolerance_ms: float = 150.0,
) -> dict[str, float]:
    """Score detected peaks against annotations, including missed/extra peaks."""

    ref = np.asarray(reference, dtype=np.int64)
    est = np.asarray(estimate, dtype=np.int64)
    tolerance = max(0, int(round(float(tolerance_ms) * float(fs) / 1000.0)))
    matched_ref, matched_est = match_r_peaks(ref, est, tolerance)

    tp = len(matched_ref)
    fn = len(ref) - tp
    fp = len(est) - tp
    precision = tp / (tp + fp) if tp + fp else 0.0
    recall = tp / (tp + fn) if tp + fn else 0.0
    f1 = 2.0 * precision * recall / (precision + recall) if precision + recall else 0.0
    timing_mae = (
        float(np.mean(np.abs(matched_ref - matched_est)) * 1000.0 / float(fs))
        if tp
        else float("nan")
    )
    rr_mae = (
        float(np.mean(np.abs(np.diff(matched_ref) - np.diff(matched_est))) * 1000.0 / float(fs))
        if tp >= 2
        else float("nan")
    )
    return {
        "RPeak_TP": float(tp),
        "RPeak_FP": float(fp),
        "RPeak_FN": float(fn),
        "RPeak_Precision": precision,
        "RPeak_Recall": recall,
        "RPeak_F1": f1,
        "RPeak_MissRate": fn / len(ref) if len(ref) else 0.0,
        "RPeak_FalseDiscoveryRate": fp / len(est) if len(est) else 0.0,
        "RPeak_TimingMAE_ms": timing_mae,
        "RR_MAE_ms": rr_mae,
    }


def detect_r_peaks(
    signal_in: np.ndarray,
    fs: float,
    min_distance_ms: float = 250.0,
    threshold_percentile: float = 90.0,
) -> np.ndarray:
    """Detect simple positive local maxima over the last axis.

    This is a pilot detector. Replace with Pan-Tompkins or trusted annotations
    for final experiments.
    """

    x = np.asarray(signal_in, dtype=np.float64)
    if x.ndim != 1:
        raise ValueError("detect_r_peaks expects a 1D signal")
    threshold = np.percentile(x, threshold_percentile)
    min_distance = max(1, int(round(float(min_distance_ms) * float(fs) / 1000.0)))
    candidates = np.where((x[1:-1] > x[:-2]) & (x[1:-1] >= x[2:]) & (x[1:-1] >= threshold))[0] + 1
    if candidates.size == 0:
        return np.array([], dtype=np.int64)

    selected: list[int] = []
    for idx in candidates:
        if not selected or idx - selected[-1] >= min_distance:
            selected.append(int(idx))
            continue
        if x[idx] > x[selected[-1]]:
            selected[-1] = int(idx)
    return np.asarray(selected, dtype=np.int64)


def detect_r_peaks_pan_tompkins(
    signal_in: np.ndarray,
    fs: float,
    refractory_ms: float = 250.0,
    integration_ms: float = 150.0,
    threshold_scale: float = 0.35,
) -> np.ndarray:
    """Detect R peaks with a lightweight Pan-Tompkins-style pipeline.

    This is intentionally compact for benchmark/downstream scoring:
    bandpass -> derivative -> squaring -> moving-window integration -> local
    maxima with adaptive robust threshold.
    """

    x = np.asarray(signal_in, dtype=np.float64)
    if x.ndim != 1:
        raise ValueError("detect_r_peaks_pan_tompkins expects a 1D signal")
    if x.size < 3:
        return np.array([], dtype=np.int64)

    try:
        from scipy import signal

        high = min(40.0, 0.45 * float(fs))
        if high <= 5.0:
            filtered = x - np.median(x)
        else:
            sos = signal.butter(2, [5.0, high], btype="bandpass", fs=float(fs), output="sos")
            filtered = signal.sosfiltfilt(sos, x)
    except Exception:
        filtered = x - np.median(x)

    derivative = np.diff(filtered, prepend=filtered[0])
    squared = derivative * derivative
    win = max(1, int(round(float(integration_ms) * float(fs) / 1000.0)))
    integrated = np.convolve(squared, np.ones(win, dtype=np.float64) / win, mode="same")

    baseline = np.median(integrated)
    spread = np.percentile(integrated, 95) - baseline
    threshold = baseline + float(threshold_scale) * max(spread, 1e-12)
    candidates = np.where(
        (integrated[1:-1] > integrated[:-2])
        & (integrated[1:-1] >= integrated[2:])
        & (integrated[1:-1] >= threshold)
    )[0] + 1
    if candidates.size == 0:
        return np.array([], dtype=np.int64)

    refractory = max(1, int(round(float(refractory_ms) * float(fs) / 1000.0)))
    search = max(1, int(round(0.15 * float(fs))))
    selected: list[int] = []
    for candidate in candidates:
        left = max(0, int(candidate) - search)
        right = min(x.size, int(candidate) + search + 1)
        peak = left + int(np.argmax(np.abs(filtered[left:right])))
        if not selected or peak - selected[-1] >= refractory:
            selected.append(peak)
            continue
        if integrated[peak] > integrated[selected[-1]]:
            selected[-1] = peak

    return np.asarray(selected, dtype=np.int64)


def match_peak_counts(reference: np.ndarray, estimate: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Truncate two peak arrays to a common count for pilot metric computation."""

    n = min(len(reference), len(estimate))
    return np.asarray(reference[:n]), np.asarray(estimate[:n])
