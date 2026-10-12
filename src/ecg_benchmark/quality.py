"""Input-only SQIs and independent R/RR evaluation for the EMG experiment.

This module intentionally does not change legacy CleanGuard metrics or the
firmware detector. SQIs never consume reference annotations or mixture SNR.
"""
from __future__ import annotations

import csv
from fractions import Fraction
from pathlib import Path

import numpy as np
from scipy import signal
from scipy.integrate import trapezoid
from wfdb import processing

from .rpeaks import detect_r_peaks_pan_tompkins

PROTOCOL = "ecg-quality-emg-v1"
BEAT_SYMBOLS = frozenset("NLRBAaJSVrFejE/fQ?n")


def clifford_rahman_statistical_sqi(ecg: np.ndarray, fs: float) -> dict[str, float | None]:
    """Compute low-cost ECG SQIs used by Clifford (2012)/Rahman (2022).

    The returned values are *indices*, not quality probabilities.  In
    particular, Rahman et al. showed that fixed thresholds for these indices
    do not transfer reliably between datasets.  ``sqi_snr`` follows their
    equation (variance of x divided by variance of abs(x)); it is a linear
    ratio despite the historical name and must not be labelled dB.

    ``clifford_bas_sqi`` is the fraction of 0--40 Hz power outside the
    baseline band (0--1 Hz).  ``clifford_p_sqi``/``rahman_p_sqi`` is the
    relative QRS-band power, 5--15 Hz divided by 0--45 Hz power.
    """
    x = np.asarray(ecg, dtype=float)
    names = (
        "clifford_p_sqi", "clifford_k_sqi", "clifford_s_sqi",
        "clifford_bas_sqi", "rahman_hos_sqi", "rahman_snr_sqi",
    )
    empty = {name: None for name in names}
    if (x.ndim != 1 or len(x) < 4 or not np.all(np.isfinite(x))
            or not np.isfinite(fs) or fs <= 90):
        return empty
    centered = x - np.mean(x)
    sigma = float(np.sqrt(np.mean(centered ** 2)))
    if sigma <= np.finfo(float).eps:
        return empty
    standardized = centered / sigma
    skewness = float(np.mean(standardized ** 3))
    kurtosis = float(np.mean(standardized ** 4))  # Pearson, as in the papers.
    frequencies, power = signal.welch(
        centered, fs=fs, nperseg=min(len(centered), max(16, round(2 * fs)))
    )

    def band_power(lo: float, hi: float) -> float:
        mask = (frequencies >= lo) & (frequencies <= hi)
        return float(trapezoid(power[mask], frequencies[mask])) if np.count_nonzero(mask) >= 2 else 0.0

    total_45 = band_power(0, 45)
    total_40 = band_power(0, 40)
    p_sqi = band_power(5, 15) / total_45 if total_45 > 0 else None
    bas_sqi = 1.0 - band_power(0, 1) / total_40 if total_40 > 0 else None
    abs_variance = float(np.var(np.abs(centered)))
    snr_sqi = float(np.var(centered) / abs_variance) if abs_variance > np.finfo(float).eps else None
    return {
        "clifford_p_sqi": p_sqi,
        "clifford_k_sqi": kurtosis,
        "clifford_s_sqi": skewness,
        "clifford_bas_sqi": bas_sqi,
        "rahman_hos_sqi": abs(skewness) * kurtosis / 5.0,
        "rahman_snr_sqi": snr_sqi,
    }


def peak_indices(values: np.ndarray) -> np.ndarray:
    values = np.asarray(values)
    if values.ndim != 1 or not np.all(np.isfinite(values)):
        raise ValueError("Peaks must be a finite one-dimensional array")
    if np.any(values < 0) or np.any(values != np.floor(values)):
        raise ValueError("Peak locations must be nonnegative integer samples")
    result = values.astype(np.int64)
    if np.any(np.diff(result) <= 0):
        raise ValueError("Peaks must be strictly increasing")
    return result


def ordered_matches(reference: np.ndarray, estimate: np.ndarray, tolerance: int) -> tuple[np.ndarray, np.ndarray]:
    """Maximize ordered one-to-one matches; minimize total error on ties.

    Return indices into both input arrays so adjacent-RR scoring can detect
    skipped or extra beats. Dynamic programming avoids crossing greedy pairs.
    """
    ref, est = peak_indices(reference), peak_indices(estimate)
    if tolerance < 0:
        raise ValueError("Tolerance must be nonnegative")
    counts = np.zeros((len(ref) + 1, len(est) + 1), dtype=int)
    costs = np.zeros_like(counts, dtype=np.int64)
    action = np.zeros_like(counts, dtype=np.uint8)
    for i in range(1, len(ref) + 1):
        for j in range(1, len(est) + 1):
            options = [(counts[i-1, j], -costs[i-1, j], 1),
                       (counts[i, j-1], -costs[i, j-1], 2)]
            distance = abs(int(ref[i-1]) - int(est[j-1]))
            if distance <= tolerance:
                options.append((counts[i-1, j-1] + 1, -costs[i-1, j-1] - distance, 3))
            count, neg_cost, step = max(options)
            counts[i, j], costs[i, j], action[i, j] = count, -neg_cost, step
    i, j = len(ref), len(est)
    pairs = []
    while i and j:
        step = action[i, j]
        if step == 3:
            pairs.append((i-1, j-1))
            i, j = i-1, j-1
        elif step == 1:
            i -= 1
        else:
            j -= 1
    pairs.reverse()
    return (np.asarray([p[0] for p in pairs], dtype=int),
            np.asarray([p[1] for p in pairs], dtype=int))


def evaluate_peaks(reference: np.ndarray, estimate: np.ndarray, fs: float,
                   tolerance_ms: float = 50.0) -> dict:
    ref, est = peak_indices(reference), peak_indices(estimate)
    if not np.isfinite(fs) or fs <= 0 or tolerance_ms < 0:
        raise ValueError("Invalid sampling frequency or tolerance")
    ri, ei = ordered_matches(ref, est, int(round(tolerance_ms * fs / 1000)))
    tp, fp, fn = len(ri), len(est) - len(ei), len(ref) - len(ri)
    adjacent = (np.diff(ri) == 1) & (np.diff(ei) == 1)
    errors = np.abs(np.diff(ref[ri])[adjacent] - np.diff(est[ei])[adjacent]) * 1000 / fs
    rr_mae = float(np.mean(errors)) if len(errors) else None
    timing = float(np.mean(np.abs(ref[ri] - est[ei])) * 1000 / fs) if tp else None
    adequate = len(ref) >= 3
    return {
        "reference_count": len(ref), "tp": tp, "fp": fp, "fn": fn,
        "precision": tp / (tp + fp) if tp + fp else None,
        "recall": tp / (tp + fn) if tp + fn else None,
        "f1": 2 * tp / (2 * tp + fp + fn) if 2 * tp + fp + fn else None,
        "timing_mae_ms": timing, "rr_mae_ms": rr_mae,
        "rr_pair_count": len(errors), "rr_error_sum_ms": float(sum(errors)),
        "timing_error_sum_ms": float(np.sum(np.abs(ref[ri] - est[ei])) * 1000 / fs),
        "reference_evaluable": adequate,
        "rr_target_ok": bool(fp == 0 and fn == 0 and len(errors) >= 2 and rr_mae <= 20)
        if adequate else None,
    }


def resample_noise(noise: np.ndarray, source_fs: float, target_fs: float) -> np.ndarray:
    if source_fs <= 0 or target_fs <= 0:
        raise ValueError("Sampling frequencies must be positive")
    ratio = Fraction(float(target_fs) / float(source_fs)).limit_denominator(10000)
    return signal.resample_poly(noise, ratio.numerator, ratio.denominator)


def mix_noise(ecg: np.ndarray, noise: np.ndarray, snr_db: float, core: slice) -> tuple[np.ndarray, float]:
    ecg, noise = np.asarray(ecg, dtype=float), np.asarray(noise, dtype=float)
    if ecg.shape != noise.shape or not np.all(np.isfinite(ecg)) or not np.all(np.isfinite(noise)):
        raise ValueError("Mixture inputs must have equal shape and finite values")
    x = ecg - np.mean(ecg[core])
    n = noise - np.mean(noise[core])
    px, pn = np.mean(x[core] ** 2), np.mean(n[core] ** 2)
    if px <= 0 or pn <= 0 or not np.isfinite(snr_db):
        raise ValueError("Mixture requires positive signal/noise power and finite SNR")
    scale = float(np.sqrt(px / pn / (10 ** (snr_db / 10))))
    return x + scale * n, scale


def evaluation_starts(length: int, fs: float, count: int = 30,
                      window_sec: float = 10, context_sec: float = 2) -> np.ndarray:
    size, pad = round(window_sec * fs), round(context_sec * fs)
    if count <= 0 or size <= 0 or pad < 0:
        raise ValueError("Invalid window parameters")
    possible = np.arange(pad, length - pad - size + 1, size, dtype=int)
    if len(possible) <= count:
        return possible
    return possible[np.linspace(0, len(possible)-1, count, dtype=int)]


def split_groups(groups: list[str], seed: int = 42) -> dict[str, str]:
    unique = sorted(set(groups))
    if len(unique) < 2:
        raise ValueError("At least two source groups are required for development/test")
    shuffled = np.random.default_rng(seed).permutation(unique)
    boundary = min(len(unique)-1, max(1, int(np.floor(0.6 * len(unique)))))
    return {str(g): "development" if i < boundary else "test" for i, g in enumerate(shuffled)}


def noise_partitions(length: int) -> dict[str, tuple[int, int]]:
    boundary = int(length * 0.6)
    return {"development": (0, boundary), "test": (boundary, length)}


def input_quality(ecg: np.ndarray, fs: float, core: slice) -> tuple[dict, np.ndarray, np.ndarray]:
    x = np.asarray(ecg, dtype=float)
    empty = np.array([], dtype=int)
    result = {
        "agreement": None, "hf_power_ratio": None, "baseline_power_ratio": None,
        "normalized_diff_rms": None, "flatline_ratio": None, "signal_kurtosis": None,
        "spectral_entropy": None, "beat_count_disagreement": None,
        "peak_timing_disagreement_ms": None, "qrs_template_correlation": None,
        "qrs_amplitude_cv": None, "rr_outlier_fraction": None,
        "clifford_b_sqi": None, "clifford_p_sqi": None,
        "clifford_k_sqi": None, "clifford_s_sqi": None,
        "clifford_bas_sqi": None, "rahman_hos_sqi": None,
        "rahman_snr_sqi": None,
        "detected_beat_count": 0, "xqrs_beat_count": 0, "input_reason": "ok",
    }
    if fs <= 80 or not np.isfinite(fs):
        raise ValueError("SQI frequency bands require fs > 80 Hz")
    if x.ndim != 1 or core.start is None or core.stop is None or core.start < 0 or core.stop > len(x):
        raise ValueError("Invalid signal/core")
    if core.stop - core.start < round(10 * fs):
        result["input_reason"] = "short_window"
        return result, empty, empty
    if not np.all(np.isfinite(x)):
        result["input_reason"] = "nonfinite_signal"
        return result, empty, empty
    middle = x[core] - np.mean(x[core])
    rms = np.sqrt(np.mean(middle ** 2))
    if rms <= np.finfo(float).eps:
        result["input_reason"] = "constant_signal"
        return result, empty, empty
    result.update(clifford_rahman_statistical_sqi(middle, fs))
    frequencies, power = signal.welch(middle, fs=fs, nperseg=min(len(middle), round(2*fs)))
    def band(lo, hi):
        mask = (frequencies >= lo) & (frequencies <= hi)
        return float(trapezoid(power[mask], frequencies[mask]))
    denominator = band(5, 40)
    result["hf_power_ratio"] = band(20, 40) / denominator if denominator > 0 else None
    total_band_power = band(.5, 40)
    result["baseline_power_ratio"] = band(.5, 5) / total_band_power if total_band_power > 0 else None
    result["normalized_diff_rms"] = float(np.sqrt(np.mean(np.diff(middle)**2)) / rms)
    result["flatline_ratio"] = float(np.mean(np.abs(np.diff(middle)) <= 1e-3 * rms))
    standardized = middle / rms
    result["signal_kurtosis"] = float(np.mean(standardized ** 4))
    positive_power = power[(frequencies >= .5) & (frequencies <= 40)]
    if positive_power.sum() > 0:
        probabilities = positive_power / positive_power.sum()
        result["spectral_entropy"] = float(
            -np.sum(probabilities * np.log(probabilities + np.finfo(float).eps))
            / np.log(len(probabilities))
        )
    # Both algorithms see the same observed signal and context, no references.
    primary = detect_r_peaks_pan_tompkins(x, fs)
    secondary = processing.xqrs_detect(x, fs=fs, verbose=False)
    primary = primary[(primary >= core.start) & (primary < core.stop)] - core.start
    secondary = secondary[(secondary >= core.start) & (secondary < core.stop)] - core.start
    primary, secondary = peak_indices(primary), peak_indices(secondary)
    result.update(detected_beat_count=len(primary), xqrs_beat_count=len(secondary))
    primary_matches, secondary_matches = ordered_matches(primary, secondary, round(0.05 * fs))
    total = len(primary) + len(secondary)
    result["agreement"] = 2 * len(primary_matches) / total if total else None
    result["clifford_b_sqi"] = result["agreement"]
    result["beat_count_disagreement"] = abs(len(primary) - len(secondary)) / max(len(primary), len(secondary), 1)
    if len(primary_matches):
        result["peak_timing_disagreement_ms"] = float(
            np.mean(np.abs(primary[primary_matches] - secondary[secondary_matches])) * 1000 / fs
        )
    if len(primary) >= 2:
        rr = np.diff(primary) * 1000 / fs
        median_rr = np.median(rr)
        result["rr_outlier_fraction"] = float(
            np.mean((rr < 300) | (rr > 2000) | (rr < .5 * median_rr) | (rr > 1.8 * median_rr))
        )

    # Beat-template consistency and amplitude stability are morphology SQIs. They
    # use only the observed waveform and the production detector's peaks.
    before, after = round(.12 * fs), round(.20 * fs)
    templates = []
    amplitudes = []
    for peak in primary:
        absolute_peak = int(peak + core.start)
        if absolute_peak - before < core.start or absolute_peak + after >= core.stop:
            continue
        beat = x[absolute_peak-before:absolute_peak+after+1].astype(float)
        amplitudes.append(float(np.ptp(beat)))
        beat -= np.mean(beat)
        norm = np.linalg.norm(beat)
        if norm > np.finfo(float).eps:
            templates.append(beat / norm)
    if len(templates) >= 3:
        stacked = np.vstack(templates)
        template = np.median(stacked, axis=0)
        template_norm = np.linalg.norm(template)
        if template_norm > np.finfo(float).eps:
            result["qrs_template_correlation"] = float(
                np.mean(stacked @ (template / template_norm))
            )
    if amplitudes and np.mean(amplitudes) > np.finfo(float).eps:
        result["qrs_amplitude_cv"] = float(np.std(amplitudes) / np.mean(amplitudes))
    if len(primary) < 3:
        result["input_reason"] = "fewer_than_three_primary_beats"
    return result, primary, secondary


def decide(row: dict, threshold: float | None) -> tuple[str, str]:
    if row["input_reason"] != "ok":
        return "insufficient_data", row["input_reason"]
    if threshold is None:
        return "insufficient_data", "no_calibrated_threshold"
    if row["agreement"] is None:
        return "insufficient_data", "agreement_unavailable"
    return ("usable", "agreement_pass") if row["agreement"] >= threshold else ("unusable", "agreement_below_threshold")


def select_threshold(rows: list[dict], min_kept: int = 30, max_false_accept: float = 0.05) -> tuple[float | None, list[dict]]:
    """False acceptance constraint = bad among retained (selective risk).

    Unknown reference targets are excluded, never treated as good. Explicitly
    reject test rows to prevent accidental tuning on the held-out data.
    """
    if any(r.get("split") != "development" for r in rows):
        raise ValueError("Calibration accepts development rows only")
    evaluable = [r for r in rows if r.get("rr_target_ok") is not None]
    curve = []
    for value in np.arange(101) / 100:
        kept = [r for r in evaluable if decide(r, float(value))[0] == "usable"]
        bad = sum(not r["rr_target_ok"] for r in kept)
        risk = bad / len(kept) if kept else None
        curve.append({"threshold": float(value), "kept": len(kept), "bad_kept": bad,
                      "coverage": len(kept) / len(evaluable) if evaluable else 0.0,
                      "false_accept_among_kept": risk,
                      "eligible": len(kept) >= min_kept and risk is not None and risk <= max_false_accept})
    eligible = [c for c in curve if c["eligible"]]
    best = max(eligible, key=lambda c: (c["kept"], c["threshold"])) if eligible else None
    return (best["threshold"] if best else None), curve


def read_sd_segments(path: Path, fs: float = 360) -> tuple[list[tuple[np.ndarray, np.ndarray]], list[dict]]:
    """Read full-rate SD only. Split gaps/resets; reject thinned BLE streams."""
    if fs != 360:
        raise ValueError("This SD adapter targets the current 360 Hz firmware")
    with path.open(encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        fields = set(reader.fieldnames or [])
        if "timestamp_ms" not in fields or not ({"centered", "raw_adc"} & fields):
            raise ValueError("Need timestamp_ms and centered or raw_adc from full-rate SD")
        if "recv_time" in fields or "rr_interval_ms" in fields:
            raise ValueError("BLE payload is not a full-rate SD waveform")
        records = list(reader)
    if not records:
        raise ValueError("Empty SD CSV")
    try:
        ts = np.asarray([float(r["timestamp_ms"]) for r in records])
        x = np.asarray([float(r["centered"]) if "centered" in fields else float(r["raw_adc"])-2048 for r in records])
    except (ValueError, TypeError) as exc:
        raise ValueError("Invalid numeric SD field") from exc
    if not np.all(np.isfinite(ts)):
        raise ValueError("SD timestamps must be finite")
    dt = np.diff(ts)
    positive = dt[dt > 0]
    if len(positive) and not (0.75 * 1000/fs <= np.median(positive) <= 1.25 * 1000/fs):
        raise ValueError("Timestamp cadence does not match 360 Hz; possible thinned BLE data")
    cuts = np.flatnonzero((dt <= 0) | (dt > 2*1000/fs)) + 1
    bounds = np.r_[0, cuts, len(ts)]
    segments, events = [], []
    for start, end in zip(bounds[:-1], bounds[1:]):
        segments.append((ts[start:end], x[start:end]))
    for index in cuts:
        events.append({"row": int(index+2), "delta_ms": float(dt[index-1]),
                       "reason": "timestamp_reset_or_duplicate" if dt[index-1] <= 0 else "sample_gap"})
    return segments, events
