# Lightweight and learned QRS detector benchmark

## Candidates

Three upstream implementations were cloned under `external/` and executed
through adapters in `experiments/benchmark_bpm_qrs.py`:

- [Fast-QRS detector](https://github.com/Aura-healthcare/Fast-QRS-detector),
  a CPU-only 2024 detector (EPL-2.0);
- [Pan-Tompkins++](https://github.com/Niaz-Imtiaz/Pan-Tompkins-Plus-Plus),
  the implementation accompanying the IEEE BIBM 2022 workshop paper;
- [ecg2rr](https://github.com/HealthSciTech/ecg2rr), a bidirectional LSTM
  detector with bundled pretrained weights (MIT license).

The ecg2rr HDF5 checkpoint predates Keras 3. The adapter reconstructs its two
64-unit bidirectional LSTM layers and loads the original weights; the upstream
clone is not modified. Repositories that classify already-segmented beats were
excluded because they do not solve R-peak detection.

## Protocol

The same protocol as `BPM_QRS_BENCHMARK.md` was used: 12 QTDB records, four
deterministic 30-second windows per record, QTDB reference annotations, NSTDB
muscle artifact at 12, 6, 0 and -6 dB, plus clean ECG. Each condition contains
1,772 annotated beats. Scores are event-level micro averages with a 150 ms
one-to-one tolerance. Latency is the host median for a 30-second window over
three measured runs after warm-up.

`RAM` below is peak Python allocation from `tracemalloc`. It excludes native
TensorFlow/scipy runtime memory and must not be interpreted as total RSS or
embedded RAM. The model artifact itself is 1.65 MB for ecg2rr.

## Results

| Detector | Clean F1 | 6 dB F1 | 0 dB F1 | -6 dB F1 | -6 dB BPM MAE | Median latency* | Peak traced RAM* |
|---|---:|---:|---:|---:|---:|---:|---:|
| Pan-Tompkins | **0.9963** | 0.9684 | 0.8551 | 0.6112 | 22.60 bpm | 6.6–13.8 ms | 309 KiB |
| Adaptive PT | 0.9960 | 0.9882 | 0.9475 | 0.8204 | 6.08 bpm | **6.9–7.1 ms** | 457 KiB |
| Fast-QRS | 0.9960 | **0.9885** | 0.9494 | 0.8489 | 12.07 bpm | 20.6–20.9 ms | **253 KiB** |
| Pan-Tompkins++ | 0.9226 | 0.9154 | 0.8850 | 0.7943 | 7.96 bpm | 63.4–85.3 ms | 382 KiB |
| ecg2rr BiLSTM | 0.9898 | 0.9779 | **0.9801** | **0.9685** | **0.38 bpm** | 551–553 ms | 2,926 KiB |

\* Range across noise conditions; host Python measurements, not ESP32.

## Recommendation

- **Default embedded/real-time choice:** Adaptive PT. It has the best
  F1/latency balance and stable latency as noise increases.
- **CPU-only lightweight alternative:** Fast-QRS. It slightly improves event
  F1 in heavy noise, but extra peaks make its BPM estimate less reliable.
- **Maximum robustness on a host:** ecg2rr LSTM. It dominates at 0 and -6 dB
  and keeps BPM MAE below 0.4 bpm at -6 dB, at approximately 79 times the
  Adaptive PT latency plus TensorFlow runtime overhead.
- **Do not select Pan-Tompkins++ unchanged for this dataset:** its high recall
  comes with many false positives and lower F1, including on clean QTDB.

This does not establish global SOTA: the benchmark is a controlled local
comparison on deterministic QTDB windows with one NSTDB noise type. The learned
model may also benefit from training-domain similarity. Patient-wise external
validation and electrode-motion/baseline-wander noise remain necessary.

## Reproduce

```powershell
.\.venv\Scripts\python.exe -m pip install -e ".[sota]"
.\.venv\Scripts\python.exe experiments/benchmark_bpm_qrs.py `
  --external-root external `
  --output outputs/bpm_qrs_sota_benchmark
```

Detailed outputs are saved as `per_window.csv`, `summary.csv`, `summary.json`,
and `run_config.json` in the selected output directory.
