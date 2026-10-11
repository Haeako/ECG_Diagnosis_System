# AF feature benchmark for ESP32

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
.\.venv\Scripts\python.exe experiments\af_detection\explain_af_shap.py
```

It generates exact TreeSHAP explanations and waveform/R-peak plots for 50
deterministically selected TP/TN/FP/FN windows without retraining. See
`docs/AF_XAI_50_REPORT.md` and `outputs/af_detection/xai_50/`.

## Data boundary

Training needs a WFDB rhythm database with beat and rhythm annotations, for
example the MIT-BIH Atrial Fibrillation Database (AFDB). Keep it under `data/`
or elsewhere outside Git. Windows crossing an AF/non-AF transition are dropped,
and the benchmark split is grouped by `record_id` to prevent adjacent windows
from the same recording leaking into train and test.

## Run

```powershell
.\.venv\Scripts\python.exe -m pip install -e ".[af,test]"

.\.venv\Scripts\python.exe experiments\af_detection\download_afdb_annotations.py

.\.venv\Scripts\python.exe experiments\af_detection\prepare_wfdb_dataset.py `
  --wfdb-dir data\afdb --rhythm-annotation atr --beat-annotation qrs `
  --output outputs\af_detection\features.csv

.\.venv\Scripts\python.exe experiments\af_detection\feature_selection.py `
  --dataset outputs\af_detection\features.csv

.\.venv\Scripts\python.exe experiments\af_detection\train_and_report.py `
  --dataset outputs\af_detection\features.csv

.\.venv\Scripts\python.exe experiments\af_detection\export_int8_c.py `
  --dataset outputs\af_detection\features.csv `
  --model outputs\af_detection\final_benchmark\models\extra_trees_selected.pkl `
  --experiment outputs\af_detection\final_benchmark\experiment.json `
  --output-dir firmware\esp32\main\components\af_model `
  --metrics outputs\af_detection\int8_deployment\metrics.json
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
- `plot_int8_deployment.py`: deployment result plot.
- `profile_esp32.py`: on-board log and ESP-IDF size report.

## ESP32 hardware profile

Enable `CONFIG_AF_MODEL_PROFILE_ON_BOOT`, build and flash the firmware, then
capture the serial output and ESP-IDF size summary:

```powershell
New-Item -ItemType Directory -Force outputs\af_detection\esp32_profile | Out-Null
idf.py menuconfig  # Component config -> AF baseline profiling
idf.py build flash
idf.py size | Tee-Object outputs\af_detection\esp32_profile\size.log
idf.py monitor | Tee-Object outputs\af_detection\esp32_profile\serial.log
.\.venv\Scripts\python.exe experiments\af_detection\profile_esp32.py `
  --serial-log outputs\af_detection\esp32_profile\serial.log `
  --size-log outputs\af_detection\esp32_profile\size.log
```

The boot benchmark emits one `AF_PROFILE_JSON` record with model-only p50,
p95 and maximum latency, comparisons, minimum free heap, stack high-water and
model bytes. Measure the full pipeline separately because this microbenchmark
does not include filtering, R-peak detection, feature extraction, SD, or BLE.

## Firmware-RR evaluation dataset

After the annotation-RR model and split are frozen, build a test-only dataset
from the production C detector:

```powershell
.\.venv\Scripts\python.exe experiments\af_detection\download_afdb_annotations.py `
  --experiment outputs\af_detection\final_benchmark\experiment.json `
  --split test
.\.venv\Scripts\python.exe experiments\af_detection\prepare_firmware_rr_dataset.py `
  --wfdb-dir data\afdb `
  --experiment outputs\af_detection\final_benchmark\experiment.json `
  --split test
```

The output is `outputs/af_detection/firmware_rr/features_firmware_rr_test.csv`.
Do not train, select features, or tune the decision threshold on this file. It
is reserved for end-to-end evaluation of the already frozen model.

Evaluate the frozen float model without retraining:

```powershell
.\.venv\Scripts\python.exe experiments\af_detection\evaluate_frozen_model.py `
  --dataset outputs\af_detection\firmware_rr\features_firmware_rr_test.csv `
  --model outputs\af_detection\final_benchmark\models\extra_trees_selected.pkl `
  --output outputs\af_detection\firmware_rr\frozen_model_metrics.json
```

Generate the matched-cohort float/INT8 screening report:

```powershell
.\.venv\Scripts\python.exe experiments\af_detection\report_firmware_rr_screening.py `
  --annotation-dataset outputs\af_detection\features.csv `
  --firmware-dataset outputs\af_detection\firmware_rr\features_firmware_rr_test.csv `
  --manifest outputs\af_detection\firmware_rr\manifest_test.json `
  --experiment outputs\af_detection\final_benchmark\experiment.json `
  --model outputs\af_detection\final_benchmark\models\extra_trees_selected.pkl `
  --output-dir outputs\af_detection\firmware_rr
```
