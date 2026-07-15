import numpy as np

from ecg_benchmark.metrics import clean_guard, reconstruction_metrics


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
