# Tomas Teijeiro Construe host adapter

This module runs the beat interpretation stage from the supplied
`external/tomas-teijeiro-220.tar.gz` before the project's current AF classifier.
The extracted upstream source is read only. The adapter and raw audit outputs
are kept in their experiment folders. Original `cinc17.py` is not executed:
its four-class XGBoost/LSTM stacker and record-name `VLCS` overrides are not
part of this experiment.

The two evaluated paths are:

```text
Current firmware C QRS -> Construe -> current C AF runtime and INT8 model
WFDB GQRS             -> Construe -> same C AF runtime and INT8 model
```

Both paths are also evaluated without Construe. No classifier is retrained.
Construe receives the ECG, input peaks, and auxiliary GQRS proposals at
threshold 0.1. Reference beat/rhythm annotations are withheld from inference.
They are used only for selecting the fixed evaluation snippets and scoring.

Run from the repository root in PowerShell:

```powershell
$env:PYTHONDONTWRITEBYTECODE='1'
$py='.\.venv\Scripts\python.exe'
& $py experiment\src\host_bench\tomas_construe\validate_compat.py
& $py experiment\src\host_bench\tomas_construe\sanity_recovery.py
& $py experiment\src\host_bench\tomas_construe\run_benchmark.py prepare --mitdb
& $py experiment\src\host_bench\tomas_construe\run_benchmark.py run --workers 2
& $py experiment\src\host_bench\tomas_construe\run_benchmark.py summarize
& $py experiment\src\host_bench\tomas_construe\make_report.py --write experiment\report\qrs_rr_bpm\REPORT.md
```

Completed branches are cached; failed branches are retried. To change the
protocol, use a fresh `--output` directory for all three commands. A worker
uses a separate process because Construe maintains global state. The default
search settings reproduce the archive's K=12, pruning schedule and 95-second
limit. The limit is checked between inference steps and can be exceeded by
one long step. Search outcomes `complete`, `exhausted` and `time_limit` remain
explicit in each raw result.

The adapter resamples to the submission's 300 Hz and 1000 digital units/mV.
It preserves signal polarity and maps the selected single channel to the
internal MLII signal slot; it omits the submission's learned lead inversion
stage. Python WFDB GQRS replaces the external WFDB command. The compatibility
loader restores legacy language/dependency interfaces, uses the bundled C DTW
implementation, and evaluates the bundled P-wave SVM parameters. The numeric
compatibility checks are component checks, not full original Python 2 parity.

AF screening uses the first 31 peaks within the shared central time interval,
requires all 30 RR in 250..2500 ms, then executes `af_runtime.c` feature
extraction and `af_model_int8.c`. It evaluates raw AF decisions, before temporal
3-of-4 confirmation. Python float predictions/features are diagnostic only.

The main QRS/RR/BPM report is
`experiment/report/qrs_rr_bpm/REPORT.md`. Raw outputs, protocol, hashes, and
audits are under `experiment/report/host_bench/tomas_construe/artifacts/`.
All timing here is host timing, not ESP32 timing.
