# BPM/QRS benchmark: QTDB + NSTDB

## Protocol

`experiments/benchmark_bpm_qrs.py` compares the existing compact
Pan–Tompkins detector with `adaptive_pt`, a locally thresholded variant. QTDB
lead 0 supplies ECG and reference beat annotations. NSTDB `ma` supplies muscle
artifact, mixed independently at 12, 6, 0, and -6 dB; clean ECG is also tested.
The default run uses 12 records, four deterministic 30-second windows per
record, and a 150 ms one-to-one event tolerance.

Scores below are micro-averaged from 1,772 annotated beats per condition.
Latency is wall-clock host latency for one 30-second window (three repeats,
median per window). RAM is peak Python allocation measured by `tracemalloc`,
not ESP32 memory or whole-process RSS.

| Detector | Condition | Precision | Recall | F1 | BPM MAE | Median latency | Peak RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| Pan–Tompkins | clean | 0.9983 | 0.9944 | 0.9963 | 0.106 bpm | 6.44 ms | 308 KiB |
| Adaptive PT | clean | 1.0000 | 0.9921 | 0.9960 | 0.082 bpm | 6.76 ms | 457 KiB |
| Pan–Tompkins | ma 6 dB | 0.9601 | 0.9769 | 0.9684 | 1.262 bpm | 8.45 ms | 305 KiB |
| Adaptive PT | ma 6 dB | 0.9854 | 0.9910 | 0.9882 | 0.163 bpm | 6.81 ms | 458 KiB |
| Pan–Tompkins | ma 0 dB | 0.8400 | 0.8708 | 0.8551 | 7.699 bpm | 12.12 ms | 307 KiB |
| Adaptive PT | ma 0 dB | 0.9234 | 0.9729 | 0.9475 | 0.673 bpm | 6.83 ms | 459 KiB |
| Pan–Tompkins | ma -6 dB | 0.6565 | 0.5717 | 0.6112 | 22.600 bpm | 13.35 ms | 305 KiB |
| Adaptive PT | ma -6 dB | 0.7968 | 0.8454 | 0.8204 | 6.081 bpm | 6.93 ms | 458 KiB |

The adaptive detector is the recommended default: it preserves clean F1 to
within 0.0003 while improving F1 by 0.092 at 0 dB and 0.209 at -6 dB. Its
30-second latency remains below 8 ms at p95 in every condition. It uses roughly
150 KiB more traced host memory because rolling robust statistics retain extra
arrays. An embedded implementation should replace the zero-phase/full-window
filters with stateful causal filters and must be profiled separately on target.

## Reproduce

```powershell
.\.venv\Scripts\python.exe experiments/benchmark_bpm_qrs.py `
  --qtdb-dir data/qtdb --nstdb-dir data/nstdb `
  --output outputs/bpm_qrs_benchmark
```

Machine-readable results are in `outputs/bpm_qrs_benchmark/summary.csv` and
`summary.json`; window-level results are in `per_window.csv`.

## Interpretation limits

- NSTDB is used as additive muscle artifact, not as a source of beat labels.
- Only the locally available `ma` record is exercised; baseline-wander and
  electrode-motion records can be added with `--noise-record` when downloaded.
- These are deterministic sampled windows, not a patient-wise confidence
  interval or a clinical validation.
- `tracemalloc` and desktop timings do not predict ESP32 heap/cycle use.
