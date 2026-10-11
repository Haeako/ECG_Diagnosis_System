# Experiment map

## Thesis baseline: required

- `af_detection/`: AF dataset, grouped feature selection, model training,
  firmware-RR evaluation, INT8 C export, and ESP32 profiling.
- `download_ecg_quality_data.py`: verified QTDB and NSTDB download.
- `ecg_quality_emg.py`: signal-quality calibration, reject analysis, and SD
  recording inspection.
- `firmware_qtdb_benchmark.py`: replay QTDB through the production C R-peak
  detector.
- `firmware_qtdb_host.c`: host adapter required by the firmware replay.

## Verification: keep

- `audit_qtdb_mlii_coverage.py`: verifies hashes, lead selection, annotation
  extent, and reference-beat accounting for the firmware result.

## Optional research experiment

- `qtdb_cleanguard_rpeaks.py`: denoising and clean-signal-damage benchmark.
  It is not required to train or deploy the AF model, but remains the main
  CleanGuard experiment described by the repository root README.
