# AF feature benchmark for ESP32

## Frozen branch latency on a fixed AFDB subset

`run_af_branch_latency.py` measures each of the seven frozen AF branches in a
fresh process, batch one, two CPU threads. Six records each contribute 120
consecutive ten-second windows starting at `floor(0.10 * complete_blocks)`;
selection reads headers only. Each RR route independently reruns SWT and the
incremental production C runtime with ninety seconds of prior context, then
calls only its own classifier. The unchanged C feature runtime also executes
its deployed INT8 forest; that side effect remains inside float-route timers.
Waveform routes time their own preprocessing and inference separately.

```powershell
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\run_af_branch_latency.py --threads 2 --output experiment\report\benchmark_runs\af_branch_latency_NEW
```

Add `--resume` to reuse completed routes under the same hash-locked protocol.
Shared AFE/ADC/notch acquisition, source-file reading, model loading, three
neural warmup calls, acquisition-window waiting and transport are outside the
branch timer. Prior RR context processing is saved and excluded from the 720
evaluation timings per route. Accuracy remains the separate full-AFDB result
on 18,470 common rows; the latency subset's RR coverage is 651/720 (90.42%).

The completed run saves predictions/timings, all RR peaks/features, loading
times, tensor bytes, process RSS, source/model/data hashes and integrated
incremental/one-pass C parity. Independent saved-output checks passed for 91
statistic cohorts and 54 unchanged files. No fitting or unit test suite runs.
See [latency results and scope](../../../../docs/ECGFOUNDER_AF_COMPARISON.md#latency-từng-nhánh-trên-cohort-afdb-riêng--05112026)
and [raw summary](../../../report/benchmark_runs/af_branch_latency_20261105/latency_summary.json).

## MLP fusion and complete server compute latency

`run_mlp_fusion_latency.py` trains one fixed MLP (16,8) on the original
SHDB-AF training subjects and compares it with the unchanged LR. Validation
locks the MLP threshold. It then executes both complete server inference and
fusion paths on all 1,026 test windows, batch one, with alternating order,
and measures an additional MLP + native RR/INT8 + exact SHAP path.

```powershell
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\run_mlp_fusion_latency.py --threads 2
```

Use a new `--output` for a fresh measurement. `--resume` reuses only completed
sessions from the same locked protocol; keep the same host/runtime/settings.
The timer covers active processing of the completed shared ECG buffer through
JSON serialization. Device telemetry is replayed as of the fixed +1s snapshot;
that policy wait is not slept. Acquisition, network, database and browser time
are outside the active-compute measurement. See [results and scheduling boundaries](../../../../docs/AF_FUSION_MLP_LATENCY.md).

## Frozen comparison: five RR classifiers and two waveform models

`run_af_model_comparison.py` evaluates all six selected AFDB ECG1 records
without fitting. ECGFounder retains the released 150-label head (AF index 5).
The five RR methods share continuous SWT peaks and the production C features.
Both waveform networks use batches of 32 with independent window preprocessing.

```powershell
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\run_af_model_comparison.py --threads 2 --output experiment\report\benchmark_runs\af_models_full_afdb_20261105_v2
```

Add `--resume` with the same output and thread count to reuse verified 128-block
chunks. Data, model, source and protocol hashes must match. A partial cache
transaction is recomputed; RR history is reconstructed from all ordered peaks.
Outputs include the complete 150 ECGFounder probabilities, peak/RR provenance,
per-block predictions and exclusions, common/own-valid metrics, and integrated
benchmark acceptance checks. No unit test suite is invoked.
See [the comparison report](../../../../docs/ECGFOUNDER_AF_COMPARISON.md).

## Run one selected dataset with per-window AF explanations

Use `run_selected_dataset_af_xai.py --dataset {afdb,ltafdb,shdb-af}` with
`--records ...` or `--all-records`. It runs the frozen server RR/INT8 branch
and writes feature values, signed SHAP, actual one-feature substitutions,
ECG/RR plots and a standalone HTML viewer. No model is fitted.
See [commands and server Python file map](../../../../docs/SERVER_AF_XAI_RUNNER.md).

```powershell
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\run_selected_dataset_af_xai.py --dataset shdb-af --records 002 --start-seconds 8610 --duration-seconds 180 --open-report
```

This experiment compares a linear sanity baseline (Logistic Regression),
Random Forest, Extra Trees, and a deliberately small XGBoost model on the same
RR/HRV feature vector. It is a screening experiment, not a clinical diagnostic
claim. `baseline_protocol.json` freezes the minimum comparison and profiling
contract used by the thesis.

The experiment seed is locked to `42` in `reproducibility.py`. Python, NumPy,
the record split, cross-validation, permutation importance, scikit-learn and
XGBoost all use that seed; model training runs with one worker.

The held-out explainability audit is produced with:

```powershell
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\explain_af_shap.py
```

It generates exact TreeSHAP explanations and waveform/R-peak plots for 50
deterministically selected TP/TN/FP/FN windows without retraining. See
`experiment/report/AF_XAI_50_REPORT.md` and `experiment/report/outputs/af_detection/xai_50/`.

## Data boundary

Training needs a WFDB rhythm database with beat and rhythm annotations, for
example the MIT-BIH Atrial Fibrillation Database (AFDB). Keep it under `datasets/`
or elsewhere outside Git. Windows crossing an AF/non-AF transition are dropped,
and the benchmark split is grouped by `record_id` to prevent adjacent windows
from the same recording leaking into train and test.

## Run

```powershell
.\.venv\Scripts\python.exe -m pip install -e ".[af,test]"

.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\download_afdb_annotations.py

.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\prepare_wfdb_dataset.py `
  --wfdb-dir datasets\afdb --rhythm-annotation atr --beat-annotation qrs `
  --output experiment\report\outputs\af_detection\features.csv

.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\feature_selection.py `
  --dataset experiment\report\outputs\af_detection\features.csv

.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\train_and_report.py `
  --dataset experiment\report\outputs\af_detection\features.csv

.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\export_int8_c.py `
  --dataset experiment\report\outputs\af_detection\features.csv `
  --model experiment\report\outputs\af_detection\final_benchmark\models\extra_trees_selected.pkl `
  --experiment experiment\report\outputs\af_detection\final_benchmark\experiment.json `
  --output-dir firmware\esp32\main\components\af_model `
  --metrics experiment\report\outputs\af_detection\int8_deployment\metrics.json
```

The feature CSV has 15 time-domain, Poincare, irregularity, and entropy
features. It intentionally avoids frequency-domain HRV because a 30-beat
window is too short for defensible spectral estimates. Before deployment,
select the model using validation records, freeze the threshold, export the
trees to C, and measure generated flash/RAM and latency on the actual ESP32.

`pickle_bytes` in `metrics.csv` is only a relative host artifact size. It must
not be reported as ESP32 flash consumption.

## Files to keep

- `baseline_protocol.json`: frozen evaluation contract.
- `reproducibility.py`: shared seed 42.
- `download_afdb_annotations.py`: AFDB annotation download.
- `prepare_wfdb_dataset.py` and `features.py`: RR window and feature dataset.
- `prepare_firmware_rr_dataset.py`: test features from production C R peaks.
- `evaluate_frozen_model.py`: evaluate a saved model without retraining.
- `feature_selection.py`: grouped feature selection used by training.
- `train_and_report.py`: the only model-training entry point.
- `export_int8_c.py`: Extra Trees INT8 C export and parity vectors.
- `af_model_host_benchmark.c`: generated-model host timing and parity check.
- [`plot_int8_deployment.py`](../../../../visualize/scripts/experiments/af_detection/plot_int8_deployment.py): deployment result plot.
- `profile_esp32.py`: on-board log and ESP-IDF size report.

## ESP32 hardware profile

Enable `CONFIG_AF_MODEL_PROFILE_ON_BOOT`, build and flash the firmware, then
capture the serial output and ESP-IDF size summary:

```powershell
New-Item -ItemType Directory -Force experiment\report\outputs\af_detection\esp32_profile | Out-Null
idf.py menuconfig  # Component config -> AF baseline profiling
idf.py build flash
idf.py size | Tee-Object experiment\report\outputs\af_detection\esp32_profile\size.log
idf.py monitor | Tee-Object experiment\report\outputs\af_detection\esp32_profile\serial.log
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\profile_esp32.py `
  --serial-log experiment\report\outputs\af_detection\esp32_profile\serial.log `
  --size-log experiment\report\outputs\af_detection\esp32_profile\size.log
```

The boot benchmark emits one `AF_PROFILE_JSON` record with model-only p50,
p95 and maximum latency, comparisons, minimum free heap, stack high-water and
model bytes. Measure the full pipeline separately because this microbenchmark
does not include filtering, R-peak detection, feature extraction, SD, or BLE.

## Firmware-RR evaluation dataset

After the annotation-RR model and split are frozen, build a test-only dataset
from the production C detector:

```powershell
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\download_afdb_annotations.py `
  --experiment experiment\report\outputs\af_detection\final_benchmark\experiment.json `
  --split test
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\prepare_firmware_rr_dataset.py `
  --wfdb-dir datasets\afdb `
  --experiment experiment\report\outputs\af_detection\final_benchmark\experiment.json `
  --split test
```

The output is `experiment/report/outputs/af_detection/firmware_rr/features_firmware_rr_test.csv`.
Do not train, select features, or tune the decision threshold on this file. It
is reserved for end-to-end evaluation of the already frozen model.

Evaluate the frozen float model without retraining:

```powershell
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\evaluate_frozen_model.py `
  --dataset experiment\report\outputs\af_detection\firmware_rr\features_firmware_rr_test.csv `
  --model experiment\report\outputs\af_detection\final_benchmark\models\extra_trees_selected.pkl `
  --output experiment\report\outputs\af_detection\firmware_rr\frozen_model_metrics.json
```

Generate the matched-cohort float/INT8 screening report:

```powershell
.\.venv\Scripts\python.exe experiment\src\experiments\af_detection\report_firmware_rr_screening.py `
  --annotation-dataset experiment\report\outputs\af_detection\features.csv `
  --firmware-dataset experiment\report\outputs\af_detection\firmware_rr\features_firmware_rr_test.csv `
  --manifest experiment\report\outputs\af_detection\firmware_rr\manifest_test.json `
  --experiment experiment\report\outputs\af_detection\final_benchmark\experiment.json `
  --model experiment\report\outputs\af_detection\final_benchmark\models\extra_trees_selected.pkl `
  --output-dir experiment\report\outputs\af_detection\firmware_rr
```
