import csv

import numpy as np
import pytest

from ecg_benchmark.quality import (
    clifford_rahman_statistical_sqi, decide, evaluate_peaks, evaluation_starts, input_quality, mix_noise,
    noise_partitions, ordered_matches, read_sd_segments, resample_noise,
    select_threshold, split_groups,
)
from ecg_benchmark.quality_reporting import summarize


def test_ordered_match_maximizes_count_without_crossing():
    # Nearest-first would pair 10->11 and then 12->9 (crossing).
    ri, ei = ordered_matches(np.array([10, 12]), np.array([9, 11]), 3)
    assert ri.tolist() == [0, 1]
    assert ei.tolist() == [0, 1]
    with pytest.raises(ValueError):
        ordered_matches(np.array([10, 10]), np.array([10]), 2)


@pytest.mark.parametrize("estimate,expected_pairs", [([100, 300, 400], 1), ([100, 200, 250, 300, 400], 2)])
def test_rr_never_bridges_missing_or_extra_beats(estimate, expected_pairs):
    result = evaluate_peaks(np.array([100, 200, 300, 400]), np.array(estimate), 1000, 10)
    assert result["rr_pair_count"] == expected_pairs
    assert result["rr_mae_ms"] == 0
    assert result["rr_target_ok"] is False


def test_rr_timing_and_empty_reference():
    result = evaluate_peaks(np.array([100, 200, 300]), np.array([101, 205, 306]), 1000, 10)
    assert result["rr_mae_ms"] == 2.5
    assert result["rr_target_ok"] is True
    empty = evaluate_peaks(np.array([]), np.array([]), 250)
    assert empty["rr_target_ok"] is None
    assert empty["f1"] is None


def test_mixture_snr_and_resampling_time():
    rng = np.random.default_rng(1)
    ecg, noise = rng.normal(size=3500)+30, rng.normal(size=3500)+10
    core = slice(500, 3000)
    mixed, _ = mix_noise(ecg, noise, 6, core)
    clean = ecg - np.mean(ecg[core])
    measured = 10*np.log10(np.mean(clean[core]**2)/np.mean((mixed-clean)[core]**2))
    assert measured == pytest.approx(6)
    impulse = np.zeros(3600)
    impulse[1800] = 1
    converted = resample_noise(impulse, 360, 250)
    assert len(converted) == 2500
    assert np.argmax(converted) == 1250
    with pytest.raises(ValueError):
        mix_noise(ecg, np.ones(3500), 6, core)


def test_split_and_nonoverlap():
    groups = ["a", "a", "b", "c", "d", "e"]
    assert split_groups(groups) == split_groups(list(reversed(groups)))
    assert set(split_groups(groups).values()) == {"development", "test"}
    partitions = noise_partitions(648000)
    assert partitions["development"][1] == partitions["test"][0]
    starts = evaluation_starts(225000, 250)
    assert len(starts) == 30
    assert starts[0] >= 500 and starts[-1] + 2500 + 500 <= 225000
    assert np.all(np.diff(starts) >= 2500)
    assert len(evaluation_starts(3000, 250)) == 0


@pytest.mark.parametrize("kind", ["constant", "nan", "inf", "short"])
def test_bad_inputs_do_not_become_good(kind):
    x = np.zeros(3500)
    core = slice(500, 3000)
    if kind == "nan":
        x[100] = np.nan
    elif kind == "inf":
        x[100] = np.inf
    elif kind == "short":
        core = slice(500, 2000)
    features, primary, _ = input_quality(x, 250, core)
    assert decide(features, .9)[0] == "insufficient_data"
    assert not len(primary)


def synthetic_ecg(inverted=False):
    fs = 250
    times = np.arange(3500)/fs
    x = sum(np.exp(-((times-t)/.018)**2) for t in np.arange(.5, 14, 1))
    return -x if inverted else x


def test_clifford_rahman_sqi_formulas_and_invalid_inputs():
    fs = 250
    t = np.arange(10 * fs) / fs
    x = np.sin(2 * np.pi * 10 * t)
    sqi = clifford_rahman_statistical_sqi(x, fs)
    assert sqi["clifford_p_sqi"] > .99
    assert sqi["clifford_bas_sqi"] > .99
    assert sqi["clifford_s_sqi"] == pytest.approx(0, abs=1e-12)
    assert sqi["clifford_k_sqi"] == pytest.approx(1.5, rel=1e-3)
    assert sqi["rahman_hos_sqi"] == pytest.approx(0, abs=1e-12)
    assert sqi["rahman_snr_sqi"] > 1
    assert all(v is None for v in clifford_rahman_statistical_sqi(np.zeros(100), fs).values())
    assert all(v is None for v in clifford_rahman_statistical_sqi(x, 80).values())


def test_polarity_and_core_cropping():
    a, p, _ = input_quality(synthetic_ecg(), 250, slice(500, 3000))
    b, q, _ = input_quality(synthetic_ecg(True), 250, slice(500, 3000))
    assert np.array_equal(p, q)
    assert len(p) == 10
    assert np.all((p >= 0) & (p < 2500))
    assert a["input_reason"] == b["input_reason"] == "ok"
    for name in ("baseline_power_ratio", "spectral_entropy", "signal_kurtosis",
                 "beat_count_disagreement", "peak_timing_disagreement_ms",
                 "qrs_template_correlation", "qrs_amplitude_cv", "rr_outlier_fraction",
                 "clifford_b_sqi", "clifford_p_sqi", "clifford_k_sqi",
                 "clifford_s_sqi", "clifford_bas_sqi", "rahman_hos_sqi",
                 "rahman_snr_sqi"):
        assert a[name] is not None
        assert np.isfinite(a[name])


def calibration_rows():
    return ([{"split": "development", "input_reason": "ok", "agreement": 1., "rr_target_ok": True} for _ in range(40)]
            + [{"split": "development", "input_reason": "ok", "agreement": .5, "rr_target_ok": False} for _ in range(40)])


def test_threshold_and_failed_calibration():
    rows = calibration_rows()
    threshold, _ = select_threshold(rows)
    assert threshold == 1
    assert decide(rows[-1], threshold)[0] == "unusable"
    for row in rows:
        row["agreement"] = 1.
    assert select_threshold(rows)[0] is None
    assert decide(rows[0], None) == ("insufficient_data", "no_calibrated_threshold")
    with pytest.raises(ValueError):
        select_threshold([{**rows[0], "split": "test"}])
    assert select_threshold(calibration_rows()[:10])[0] is None


def write_sd(path, ts, field="centered"):
    with path.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["timestamp_ms", field])
        writer.writerows(zip(ts, range(len(ts))))


def test_sd_gap_reset_and_reject_thinning(tmp_path):
    path = tmp_path / "sd.csv"
    timestamps = np.rint(np.arange(100)*1000/360)
    timestamps[50:] += 100
    write_sd(path, timestamps)
    segments, events = read_sd_segments(path)
    assert len(segments) == 2 and events[0]["reason"] == "sample_gap"
    assert len(segments[0][0]) == 50
    timestamps[50:] -= 1000
    write_sd(path, timestamps)
    assert read_sd_segments(path)[1][0]["reason"] == "timestamp_reset_or_duplicate"
    write_sd(path, np.arange(100)*40, "raw_adc")
    with pytest.raises(ValueError, match="cadence"):
        read_sd_segments(path)
    write_sd(path, np.arange(100)*3, "filtered")
    with pytest.raises(ValueError, match="full-rate"):
        read_sd_segments(path)


def test_cluster_bootstrap_and_denominators():
    row = {**evaluate_peaks(np.array([100, 200, 300]), np.array([100, 200, 300]), 1000),
           "status": "usable", "source_group": "a"}
    rows = [row, {**row, "source_group": "b", "status": "unusable"}]
    first = summarize(rows, 42, 100)
    assert first == summarize(rows, 42, 100)
    assert first["coverage"] == .5
    assert first["false_reject_among_good"] == .5
    assert first["source_groups"] == 2
    assert first["false_accept_among_bad"] is None
