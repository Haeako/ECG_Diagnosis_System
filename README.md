# CleanGuard ECG Benchmark and Edge Pipeline

This repository contains the submission-oriented code for two connected parts:

1. a reproducible ECG denoising benchmark centered on clean-signal damage and
   annotation-based R-peak preservation; and
2. an ESP32 pipeline for acquisition, filtering, R-peak/RR extraction, SD
   recording, and BLE transport.

The paper experiment is the primary reproducible artifact. The firmware is kept
separate because it has a different toolchain and validation protocol.

## Repository layout

```text
experiments/                 runnable paper experiments
src/ecg_benchmark/           metrics, baselines, and R-peak evaluation
tests/                       deterministic unit tests
firmware/esp32/              ESP-IDF application
docs/RESEARCH_SCOPE.md       claim, metric protocol, and limitations
docs/legacy_python/          archived acquisition/simulation utilities
data/                        local datasets (ignored)
outputs/                     generated scorecards (ignored)
```

## Main experiment

The final scorecard evaluates each denoiser on the same continuous QTDB
windows with NSTDB noise:

```text
noisy input  -> denoiser -> reconstruction metrics
clean input  -> denoiser -> CleanGuard damage
denoised ECG -> Pan-Tompkins -> R-peak metrics against QTDB annotations
```

Reported metrics are:

- utility: SNR, SNR improvement, PRD, RMSE, derivative RMSE, cosine similarity;
- clean passthrough: CleanGuard PRD/RMSE/derivative RMSE/cosine similarity;
- downstream: R-peak TP/FP/FN, precision, recall, F1, miss rate, false discovery
  rate, timing MAE, and RR-interval MAE. These are reported separately for
  `D(noisy)` (`NoisyDenoised_*`) and `D(clean)` (`CleanPass_*`).

CleanGuard is reported as a metric vector. It is not collapsed into one score
without a pre-registered normalization and weight sensitivity analysis.

## Environment

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e '.[test]'
```

Keep PhysioNet datasets outside Git. The experiment expects extracted QTDB and
NSTDB directories.

## Run

```bash
python experiments/qtdb_cleanguard_rpeaks.py \
  --qtdb-dir /path/to/qt-database-1.0.0 \
  --nstdb-dir /path/to/mit-bih-noise-stress-test-database-1.0.0 \
  --output outputs/qtdb_cleanguard_rpeaks
```

For a quick local smoke run, add `--records sel123 --windows-per-record 1`.
Every run writes per-window results, an aggregated CSV/JSON, and the exact CLI
configuration. Do not compare methods from different generated window sets.

## Test

```bash
python -m pytest
```

## Firmware

The ESP-IDF project is under `firmware/esp32`:

```bash
cd firmware/esp32
idf.py set-target esp32
idf.py build
```

Hardware measurements and paper experiments are intentionally not mixed into
one command. See `firmware/esp32/README.md` for the firmware data path.
