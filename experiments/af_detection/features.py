"""Small, deterministic RR feature set suitable for embedded inference."""

from __future__ import annotations

import math

import numpy as np


FEATURE_NAMES = (
    "mean_rr_ms", "median_rr_ms", "sdnn_ms", "rmssd_ms", "pnn50",
    "mean_hr_bpm", "rr_min_ms", "rr_max_ms", "rr_range_ms", "cv_rr",
    "sd1_ms", "sd2_ms", "sd1_sd2", "turning_point_ratio", "shannon_entropy",
)


def _shannon_entropy(values: np.ndarray, bins: int = 10) -> float:
    counts, _ = np.histogram(values, bins=min(bins, max(2, int(np.sqrt(values.size)))))
    probabilities = counts[counts > 0] / counts.sum()
    return float(-np.sum(probabilities * np.log2(probabilities)))


def rr_features(rr_ms: np.ndarray) -> dict[str, float]:
    """Compute features from NN/RR intervals in milliseconds.

    At least eight physiologically plausible intervals are required. Invalid
    intervals should be removed by the dataset builder, not hidden here.
    """
    rr = np.asarray(rr_ms, dtype=np.float64)
    if rr.ndim != 1 or rr.size < 8 or not np.all(np.isfinite(rr)) or np.any(rr <= 0):
        raise ValueError("rr_ms must contain at least 8 finite positive intervals")

    differences = np.diff(rr)
    sdnn = float(np.std(rr, ddof=1))
    rmssd = float(np.sqrt(np.mean(differences**2)))
    sd1 = float(np.sqrt(0.5) * np.std(differences, ddof=1))
    sd2_squared = max(0.0, 2.0 * sdnn**2 - sd1**2)
    sd2 = math.sqrt(sd2_squared)
    turning_points = np.sum((rr[1:-1] - rr[:-2]) * (rr[2:] - rr[1:-1]) < 0)

    values = {
        "mean_rr_ms": float(np.mean(rr)),
        "median_rr_ms": float(np.median(rr)),
        "sdnn_ms": sdnn,
        "rmssd_ms": rmssd,
        "pnn50": float(np.mean(np.abs(differences) > 50.0)),
        "mean_hr_bpm": float(60000.0 / np.mean(rr)),
        "rr_min_ms": float(np.min(rr)),
        "rr_max_ms": float(np.max(rr)),
        "rr_range_ms": float(np.ptp(rr)),
        "cv_rr": float(sdnn / np.mean(rr)),
        "sd1_ms": sd1,
        "sd2_ms": sd2,
        "sd1_sd2": float(sd1 / sd2) if sd2 else 0.0,
        "turning_point_ratio": float(turning_points / (rr.size - 2)),
        "shannon_entropy": _shannon_entropy(rr),
    }
    return {name: values[name] for name in FEATURE_NAMES}
