# Experiment map

Runners are stored in `experiment/src/experiments/`; reusable package code is
in `experiment/src/ecg_benchmark/`. Run commands from the repository root.

## Thesis baseline: required

- `af_detection/`: AF dataset, grouped feature selection, model training,
  firmware-RR evaluation, INT8 C export, and ESP32 profiling.
- `download_ecg_quality_data.py` and `ecg_quality_emg.py`: verified QTDB/NSTDB
  download and signal-quality evaluation.
- `firmware_qtdb_benchmark.py`: replay QTDB through the production C R-peak
  detector; `firmware_qtdb_host.c` is its host adapter.

## Verification: keep

- `audit_qtdb_mlii_coverage.py`: verifies hashes, lead selection, annotation
  extent, and reference-beat accounting for the firmware result.

## Optional research experiment

- `qtdb_cleanguard_rpeaks.py`: denoising and clean-signal-damage benchmark.
  It remains the main CleanGuard experiment described by the repository README.
