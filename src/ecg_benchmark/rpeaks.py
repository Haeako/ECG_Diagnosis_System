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

    # Chronological two-pointer matching maximises the number of pairs for
    # sorted event streams and cannot let a later reference steal an estimate.
    matched_ref: list[int] = []
    matched_est: list[int] = []
    i = j = 0
    while i < ref.size and j < est.size:
        delta = int(est[j]) - int(ref[i])
        if delta < -tolerance_samples:
            j += 1
        elif delta > tolerance_samples:
            i += 1
        else:
            matched_ref.append(int(ref[i]))
            matched_est.append(int(est[j]))
            i += 1
            j += 1
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


def detect_r_peaks_adaptive(
    signal_in: np.ndarray,
    fs: float,
    refractory_ms: float = 240.0,
    integration_ms: float = 120.0,
    threshold_scale: float = 0.28,
) -> np.ndarray:
    """Noise-robust Pan-Tompkins variant with local adaptive thresholds.

    A zero-phase QRS bandpass and derivative-energy envelope are retained from
    Pan-Tompkins.  The threshold follows a 4-second rolling median/MAD, making
    the detector less sensitive to amplitude drift and short noise bursts.
    Peaks are finally relocated to the strongest signed QRS extremum.
    """

    x = np.asarray(signal_in, dtype=np.float64)
    if x.ndim != 1:
        raise ValueError("detect_r_peaks_adaptive expects a 1D signal")
    if x.size < 3 or fs <= 0:
        return np.array([], dtype=np.int64)

    from scipy import ndimage, signal

    high = min(25.0, 0.45 * float(fs))
    if high <= 5.0:
        filtered = x - np.median(x)
    else:
        sos = signal.butter(3, [5.0, high], btype="bandpass", fs=float(fs), output="sos")
        padlen = min(x.size - 1, max(0, 3 * (2 * len(sos) + 1)))
        filtered = signal.sosfiltfilt(sos, x, padlen=padlen) if padlen else x - np.median(x)

    derivative = np.gradient(filtered)
    win = max(1, int(round(integration_ms * fs / 1000.0)))
    envelope = ndimage.uniform_filter1d(derivative * derivative, size=win, mode="nearest")
    local_size = max(win * 2 + 1, int(round(4.0 * fs)) | 1)
    baseline = ndimage.median_filter(envelope, size=local_size, mode="nearest")
    deviation = ndimage.median_filter(np.abs(envelope - baseline), size=local_size, mode="nearest")
    global_span = max(float(np.percentile(envelope, 95) - np.median(envelope)), 1e-12)
    threshold = baseline + threshold_scale * np.maximum(6.0 * deviation, global_span)

    refractory = max(1, int(round(refractory_ms * fs / 1000.0)))
    candidates, _ = signal.find_peaks(envelope, height=threshold, distance=refractory)
    search = max(1, int(round(0.12 * fs)))
    selected: list[int] = []
    for candidate in candidates:
        left = max(0, int(candidate) - search)
        right = min(x.size, int(candidate) + search + 1)
        peak = left + int(np.argmax(np.abs(filtered[left:right])))
        if not selected or peak - selected[-1] >= refractory:
            selected.append(peak)
        elif abs(filtered[peak]) > abs(filtered[selected[-1]]):
            selected[-1] = peak
    return np.asarray(selected, dtype=np.int64)


def detect_r_peaks_adaptive_v2(signal_in: np.ndarray, fs: float) -> np.ndarray:
    """Frozen development-only v2 configuration; never tuned on locked test.

    Selected on the pre-declared 12-record QTDB development split using clean,
    BW, MA and EM conditions. See outputs/adaptive_pt_v2_dev/frozen_config.json.
    """

    return detect_r_peaks_adaptive(
        signal_in, fs, refractory_ms=260.0,
        integration_ms=90.0, threshold_scale=0.38,
    )


class AdaptivePTPortable:
    """Causal, bounded-state Adaptive Pan--Tompkins detector.

    The implementation intentionally mirrors an embedded implementation: SOS
    biquads use direct-form-II-transposed state, integration uses a ring buffer,
    and candidate classification only sees current/past samples.  A short
    history is retained to move the delayed energy maximum back to the QRS
    extremum.  The emitted event is delayed, but its sample index is corrected.
    """

    def __init__(
        self,
        fs: float,
        refractory_ms: float = 260.0,
        integration_ms: float = 110.0,
        relocation_ms: float = 100.0,
        warmup_ms: float = 1000.0,
        threshold_fraction: float = 0.35,
    ) -> None:
        if fs <= 0:
            raise ValueError("fs must be positive")
        from scipy import signal

        self.fs = float(fs)
        high = min(25.0, 0.45 * self.fs)
        if high <= 5.0:
            raise ValueError("sampling rate is too low for the 5-25 Hz QRS band")
        self.sos = signal.butter(2, [5.0, high], btype="bandpass", fs=self.fs, output="sos")
        self.zi = np.zeros((len(self.sos), 2), dtype=np.float64)
        self.integration_samples = max(1, int(round(integration_ms * self.fs / 1000.0)))
        self.refractory_samples = max(1, int(round(refractory_ms * self.fs / 1000.0)))
        self.relocation_samples = max(1, int(round(relocation_ms * self.fs / 1000.0)))
        self.warmup_samples = max(1, int(round(warmup_ms * self.fs / 1000.0)))
        self.settle_samples = max(1, int(round(250.0 * self.fs / 1000.0)))
        self.threshold_fraction = float(threshold_fraction)
        self.energy_ring = np.zeros(self.integration_samples, dtype=np.float64)
        self.history_size = self.relocation_samples + 4
        self.raw_ring = np.zeros(self.history_size, dtype=np.float64)
        self.energy_pos = self.history_pos = 0
        self.energy_sum = 0.0
        self.previous_filtered = 0.0
        self.env_2 = self.env_1 = 0.0
        self.sample_index = -1
        self.signal_level = 0.0
        self.noise_level = 0.0
        self.levels_initialized = False
        self.warmup_peak_max = 0.0
        self.warmup_peak_mean = 0.0
        self.warmup_peak_count = 0
        self.last_peak = -10 * self.refractory_samples

    def _biquad_push(self, value: float) -> float:
        for section, state in zip(self.sos, self.zi):
            b0, b1, b2, _a0, a1, a2 = section
            output = b0 * value + state[0]
            state[0] = b1 * value - a1 * output + state[1]
            state[1] = b2 * value - a2 * output
            value = output
        return float(value)

    def _relocate(self, candidate: int) -> int:
        start = max(0, candidate - self.relocation_samples)
        best_index = candidate
        best_value = -1.0
        local_sum = 0.0
        local_count = 0
        # Every requested sample is still present because history_size is
        # relocation_samples + a small local-maximum lookahead allowance.
        for index in range(start, candidate + 1):
            age = self.sample_index - index
            slot = (self.history_pos - 1 - age) % self.history_size
            local_sum += float(self.raw_ring[slot])
            local_count += 1
        local_mean = local_sum / local_count if local_count else 0.0
        for index in range(start, candidate + 1):
            age = self.sample_index - index
            slot = (self.history_pos - 1 - age) % self.history_size
            value = float(self.raw_ring[slot])
            magnitude = abs(value - local_mean)
            if magnitude > best_value:
                best_value = magnitude
                best_index = index
        return best_index

    def push(self, sample: float) -> int | None:
        """Consume one sample and return a corrected R index when accepted."""

        self.sample_index += 1
        raw = float(sample)
        filtered = self._biquad_push(raw)
        self.raw_ring[self.history_pos] = raw
        self.history_pos = (self.history_pos + 1) % self.history_size

        derivative = filtered - self.previous_filtered
        self.previous_filtered = filtered
        energy = derivative * derivative
        self.energy_sum -= self.energy_ring[self.energy_pos]
        self.energy_ring[self.energy_pos] = energy
        self.energy_sum += energy
        self.energy_pos = (self.energy_pos + 1) % self.integration_samples
        envelope = self.energy_sum / self.integration_samples

        event: int | None = None
        candidate_index = self.sample_index - 1
        candidate = self.env_1
        is_local_max = self.env_1 > self.env_2 and self.env_1 >= envelope
        self.env_2, self.env_1 = self.env_1, envelope
        if not is_local_max:
            return None

        if candidate_index < self.warmup_samples:
            if candidate_index < self.settle_samples:
                return None
            self.warmup_peak_count += 1
            self.warmup_peak_max = max(self.warmup_peak_max, candidate)
            self.warmup_peak_mean += (
                candidate - self.warmup_peak_mean
            ) / self.warmup_peak_count
            return None

        if not self.levels_initialized:
            self.signal_level = max(self.warmup_peak_max, candidate)
            self.noise_level = min(self.warmup_peak_mean, 0.5 * self.signal_level)
            self.levels_initialized = True
        threshold = self.noise_level + self.threshold_fraction * (
            self.signal_level - self.noise_level
        )
        relocated = self._relocate(candidate_index)
        outside_refractory = relocated - self.last_peak >= self.refractory_samples
        if candidate >= threshold and outside_refractory:
            learned = min(candidate, 2.0 * self.signal_level) if self.signal_level > 0 else candidate
            self.signal_level = 0.125 * learned + 0.875 * self.signal_level
            self.last_peak = relocated
            event = relocated
        elif outside_refractory:
            self.noise_level = 0.125 * candidate + 0.875 * self.noise_level
        return event


def detect_r_peaks_adaptive_portable(signal_in: np.ndarray, fs: float) -> np.ndarray:
    """Run the causal bounded-state Adaptive PT implementation as a batch."""

    x = np.asarray(signal_in, dtype=np.float64)
    if x.ndim != 1:
        raise ValueError("detect_r_peaks_adaptive_portable expects a 1D signal")
    if x.size < 3:
        return np.array([], dtype=np.int64)
    detector = AdaptivePTPortable(fs)
    peaks = [event for sample in x if (event := detector.push(float(sample))) is not None]
    return np.asarray(peaks, dtype=np.int64)


def estimate_bpm(peaks: np.ndarray, fs: float) -> float:
    """Estimate robust heart rate from the median RR interval."""

    values = np.asarray(peaks, dtype=np.int64)
    if values.ndim != 1 or values.size < 2 or fs <= 0:
        return float("nan")
    rr = np.diff(values) / float(fs)
    rr = rr[(rr >= 0.25) & (rr <= 3.0)]
    return float(60.0 / np.median(rr)) if rr.size else float("nan")


def match_peak_counts(reference: np.ndarray, estimate: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Truncate two peak arrays to a common count for pilot metric computation."""

    n = min(len(reference), len(estimate))
    return np.asarray(reference[:n]), np.asarray(estimate[:n])
