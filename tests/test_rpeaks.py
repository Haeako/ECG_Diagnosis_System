import math

import numpy as np

from ecg_benchmark.rpeaks import match_r_peaks, score_r_peaks


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
