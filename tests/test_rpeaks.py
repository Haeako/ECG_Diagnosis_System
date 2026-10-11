import math

import numpy as np

from ecg_benchmark.rpeaks import (
    AdaptivePTPortable,
    detect_r_peaks_adaptive_portable,
    detect_r_peaks_adaptive_v2,
    estimate_bpm,
    match_r_peaks,
    score_r_peaks,
)


def test_matching_is_one_to_one() -> None:
    matched_ref, matched_est = match_r_peaks(np.array([100, 200]), np.array([99, 101, 203]), 5)
    assert matched_ref.tolist() == [100, 200]
    assert matched_est.tolist() == [99, 203]


def test_peak_score_exposes_missed_and_extra_peaks() -> None:
    score = score_r_peaks(np.array([100, 200, 300]), np.array([101, 205, 450]), fs=1000, tolerance_ms=10)
    assert score["RPeak_TP"] == 2
    assert score["RPeak_FP"] == 1
    assert score["RPeak_FN"] == 1
    assert math.isclose(score["RPeak_Precision"], 2 / 3)
    assert math.isclose(score["RPeak_Recall"], 2 / 3)
    assert math.isclose(score["RPeak_F1"], 2 / 3)
    assert math.isclose(score["RPeak_TimingMAE_ms"], 3.0)
    assert math.isclose(score["RR_MAE_ms"], 4.0)


def test_matching_does_not_steal_an_earlier_valid_estimate() -> None:
    matched_ref, matched_est = match_r_peaks(np.array([100, 110]), np.array([106, 116]), 6)
    assert matched_ref.tolist() == [100, 110]
    assert matched_est.tolist() == [106, 116]


def test_bpm_uses_robust_median_rr() -> None:
    assert math.isclose(estimate_bpm(np.array([0, 250, 500, 760]), 250), 60.0)


def test_frozen_v2_detects_synthetic_qrs_train() -> None:
    fs = 250
    x = np.zeros(fs * 10)
    expected = np.arange(fs, fs * 10, fs)
    for peak in expected:
        x[peak - 2:peak + 3] = [0.1, 0.6, 1.2, 0.6, 0.1]
    detected = detect_r_peaks_adaptive_v2(x, fs)
    assert len(match_r_peaks(expected, detected, 20)[0]) == len(expected)


def test_portable_detector_batch_and_stream_are_identical() -> None:
    fs = 250
    x = np.zeros(fs * 10)
    for peak in np.arange(fs, fs * 10, fs):
        x[peak - 2:peak + 3] = [0.1, 0.6, 1.2, 0.6, 0.1]
    batch = detect_r_peaks_adaptive_portable(x, fs)
    detector = AdaptivePTPortable(fs)
    stream = [event for sample in x if (event := detector.push(sample)) is not None]
    assert batch.tolist() == stream
    assert len(batch) >= 8


def test_portable_detector_is_chunk_boundary_independent() -> None:
    fs = 360
    x = np.zeros(fs * 8)
    for peak in np.arange(fs, fs * 8, fs):
        x[peak - 2:peak + 3] = [0.2, 0.8, 1.4, 0.8, 0.2]
    expected = detect_r_peaks_adaptive_portable(x, fs)
    detector = AdaptivePTPortable(fs)
    actual = []
    for chunk in np.array_split(x, 13):
        actual.extend(event for value in chunk if (event := detector.push(value)) is not None)
    assert actual == expected.tolist()
