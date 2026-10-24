# CleanGuard ECG Benchmark and Edge Pipeline

This repository contains the submission-oriented code for two connected parts:

1. a reproducible ECG denoising benchmark centered on clean-signal damage and
   annotation-based R-peak preservation; and
2. an ESP32 pipeline for acquisition, filtering, R-peak/RR extraction, SD
   recording, and BLE transport.

The paper experiment is the primary reproducible artifact. The firmware is kept
separate because it has a different toolchain and validation protocol.

## ECG Live Monitor V1

The local React dashboard in `web/` connects through the existing Python BLE
Gateway, with live corrected ECG, device telemetry, START/STOP, SQLite sessions,
replay/export, diagnostics, themes and a clearly labeled deterministic Demo.
See [Windows setup](docs/LOCAL_SETUP.md), [acceptance evidence](docs/ACCEPTANCE_MATRIX.md)
and [current implementation state](docs/PROJECT_STATE.md).
Software/mock verification passes; actual ESP32 acceptance remains blocked until
a matching physical device is available. The static frontend requires a separate
running Python Gateway for real BLE data.

## Repository layout

```text
experiments/                 runnable paper experiments
src/ecg_benchmark/           metrics, baselines, and R-peak evaluation
tests/                       deterministic unit tests
firmware/esp32/              ESP-IDF application
web/                        local ECG dashboard (React + TypeScript + Vite)
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
- local clean damage (CleanGuard+): worst 1-second RMSE, maximum local PRD,
  first/last-second boundary RMSE, and an auxiliary shift-corrected PRD with a
  maximum lag of 20 ms;
- downstream: R-peak TP/FP/FN, precision, recall, F1, miss rate, false discovery
  rate, timing MAE, and RR-interval MAE. These are reported separately for
  `D(noisy)` (`NoisyDenoised_*`) and `D(clean)` (`CleanPass_*`).

CleanGuard is reported as a metric vector. CleanGuard+ prevents a short local
or overlap-add boundary artifact from being hidden by a whole-window average.
Shift correction is diagnostic only; primary scores preserve exact sample
alignment and do not use DTW. The vector is not collapsed into one score
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

## BPM and QRS benchmark

The standalone detector benchmark compares conventional Pan–Tompkins with a
noise-robust adaptive variant on QTDB ECG mixed with NSTDB muscle artifact. It
reports micro precision/recall/F1, BPM MAE, host latency, real-time factor, and
peak traced RAM:

```powershell
.\.venv\Scripts\python.exe experiments/benchmark_bpm_qrs.py `
  --qtdb-dir data/qtdb --nstdb-dir data/nstdb `
  --output outputs/bpm_qrs_benchmark
```

See [the benchmark protocol and measured results](docs/BPM_QRS_BENCHMARK.md).

Optional cloned Fast-QRS, Pan-Tompkins++, and ecg2rr LSTM detectors can be
included with `--external-root external`. Install their runtime dependencies
with `pip install -e ".[sota]"`. See the
[lightweight/SOTA comparison](docs/BPM_QRS_SOTA_BENCHMARK.md).

The current Adaptive PT detector was also tested against nine algorithms used
by `ecg-tools/benchmark-qrs-detectors`. See the
[classic-suite comparison](docs/BPM_QRS_CLASSIC_SUITE.md).

A leakage-controlled three-stage stress test covers all NSTDB noise sources,
device faults, compound failures, four event tolerances, and 70 locked QTDB
records. See the [noise-coverage report](docs/NOISE_COVERAGE_3STAGE_REPORT.md).

## ECG quality under muscle noise

The separate Python pilot evaluates input-only quality indices against actual
R/RR errors using QTDB source-reference `.atr` annotations and NSTDB `ma`.
It does not change the legacy denoising scorecard or ESP32 firmware.

```powershell
python experiments/download_ecg_quality_data.py
python experiments/ecg_quality_emg.py benchmark
python experiments/ecg_quality_emg.py sd-csv --csv path/to/full_rate_sd.csv
```

Use a new `--output` directory for each run. A failed quality calibration is
reported explicitly, never repaired using test labels. SD mode reports SQIs
without claiming reference accuracy or transferring QTDB thresholds to AD8232.
See [the Vietnamese research report](docs/ECG_SIGNAL_QUALITY_REPORT.md).

Before exporting a learned quality gate, run the grouped feasibility check. It
keeps a rejected model host-only and never writes it into the firmware tree:

```powershell
python experiments/quality_gate_feasibility.py `
  --windows outputs/ecg_quality_gate_v2/windows.csv `
  --output outputs/ecg_quality_gate_v2/deployment
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
