import numpy as np

from ecg_benchmark.metrics import clean_guard, clean_guard_protocol, reconstruction_metrics


def test_identity_has_zero_cleanguard_damage() -> None:
    clean = np.array([[0.0, 1.0, -0.5, 0.25]])
    result = clean_guard(clean, clean.copy())
    assert result["CleanGuard_PRD"] == 0.0
    assert result["CleanGuard_RMSE"] == 0.0
    assert result["CleanGuard_dRMSE"] == 0.0


def test_reconstruction_reports_snr_improvement() -> None:
    clean = np.array([[0.0, 1.0, 0.0, -1.0]])
    noisy = clean + 0.5
    denoised = clean + 0.1
    result = reconstruction_metrics(clean, noisy, denoised)
    assert result["SNR_Improve"] > 0.0


def test_cleanguard_plus_identity_has_zero_damage() -> None:
    clean = np.sin(np.linspace(0, 8 * np.pi, 1000))[None, :]
    result = clean_guard_protocol(clean, clean.copy(), fs=250)
    for key, value in result.items():
        if key == "CleanGuard_CosSim":
            assert np.isclose(value, 1.0)
        else:
            assert np.isclose(value, 0.0), key


def test_cleanguard_plus_exposes_short_boundary_artifact() -> None:
    clean = np.sin(np.linspace(0, 8 * np.pi, 1000))[None, :]
    output = clean.copy()
    output[:, :50] += 2.0
    result = clean_guard_protocol(clean, output, fs=250)
    assert result["CleanGuard_WorstLocalRMSE"] > result["CleanGuard_RMSE"]
    assert result["CleanGuard_MaxLocalPRD"] > result["CleanGuard_PRD"]
    assert result["CleanGuard_BoundaryRMSE"] > result["CleanGuard_RMSE"]


def test_cleanguard_plus_shift_diagnostic_detects_small_delay() -> None:
    clean = np.zeros((1, 1000))
    clean[:, 200:205] = np.asarray([0.2, 0.8, 1.5, 0.8, 0.2])
    clean[:, 600:605] = np.asarray([0.2, 0.8, 1.5, 0.8, 0.2])
    delayed = np.roll(clean, 3, axis=-1)
    result = clean_guard_protocol(clean, delayed, fs=250, max_shift_ms=20)
    assert result["CleanGuard_ShiftCorrectedRMSE"] < result["CleanGuard_RMSE"]
    assert result["CleanGuard_ShiftCorrectedPRD"] < result["CleanGuard_PRD"]
    assert np.isclose(result["CleanGuard_AbsShiftMs"], 12.0)
