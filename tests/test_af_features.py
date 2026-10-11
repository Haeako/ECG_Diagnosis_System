import numpy as np
import pytest

from experiments.af_detection.features import FEATURE_NAMES, rr_features


def test_constant_rr_has_expected_features():
    features = rr_features(np.full(30, 1000.0))
    assert tuple(features) == FEATURE_NAMES
    assert features["mean_hr_bpm"] == pytest.approx(60.0)
    assert features["sdnn_ms"] == pytest.approx(0.0)
    assert features["rmssd_ms"] == pytest.approx(0.0)
    assert features["pnn50"] == pytest.approx(0.0)


def test_irregular_rr_increases_variability():
    regular = rr_features(np.full(30, 800.0))
    irregular = rr_features(np.resize([500.0, 1100.0, 650.0, 1300.0], 30))
    assert irregular["sdnn_ms"] > regular["sdnn_ms"]
    assert irregular["rmssd_ms"] > regular["rmssd_ms"]
    assert irregular["pnn50"] > regular["pnn50"]


def test_rr_validation():
    with pytest.raises(ValueError):
        rr_features(np.array([1000.0] * 7))
