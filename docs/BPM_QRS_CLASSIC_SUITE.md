# Adaptive PT versus benchmark-qrs-detectors suite

## Scope

The [ecg-tools benchmark repository](https://github.com/ecg-tools/benchmark-qrs-detectors)
was cloned to `external/benchmark-qrs-detectors`. Its `py-ecg-detectors` and
WFDB algorithms were adapted into the local QTDB + NSTDB runner. This avoids
comparing scores produced under different datasets or tolerance rules.

The run covers 12 QTDB records, 48 deterministic 30-second windows, 1,772
annotated beats per condition, NSTDB muscle artifact at 12, 6, 0 and -6 dB,
and clean ECG. Event matching uses a 150 ms one-to-one tolerance. Each detector
is warmed up once and timed once per window. RAM is Python allocation measured
by `tracemalloc`, not process RSS or embedded RAM.

## Results

| Detector | Clean F1 | 6 dB F1 | 0 dB F1 | -6 dB F1 | Mean F1 (5 conditions) | Mean latency / 30 s | Peak traced RAM |
|---|---:|---:|---:|---:|---:|---:|---:|
| **Adaptive PT** | **0.9960** | **0.9882** | **0.9475** | 0.8204 | **0.94963** | **7.1 ms** | 453 KiB |
| WFDB XQRS | 0.9949 | 0.9795 | 0.9404 | 0.8403 | 0.94944 | 303.3 ms | 307 KiB |
| Two-average | 0.9929 | 0.9569 | 0.9199 | **0.8454** | 0.93843 | 71.5 ms | 294 KiB |
| Hamilton | 0.9740 | 0.9431 | 0.9066 | 0.8104 | 0.92038 | 78.6 ms | 238 KiB |
| benchmark Pan-Tompkins | 0.9817 | 0.9573 | 0.8844 | 0.7785 | 0.91608 | 73.6 ms | 297 KiB |
| WFDB GQRS | 0.9929 | 0.9471 | 0.8668 | 0.7639 | 0.91188 | 707.6 ms | 1,202 KiB |
| SWT | 0.9822 | 0.9391 | 0.8496 | 0.7318 | 0.89499 | 79.5 ms | 770 KiB |
| Matched filter | 0.9774 | 0.9156 | 0.8450 | 0.7706 | 0.89355 | 72.3 ms | 232 KiB |
| Local Pan-Tompkins | 0.9963 | 0.9684 | 0.8551 | 0.6112 | 0.88473 | 10.2 ms | 302 KiB |
| Engzee | 0.9315 | 0.8867 | 0.8248 | 0.7038 | 0.85335 | 285.5 ms | 645 KiB |
| Christov | 0.9209 | 0.8847 | 0.7719 | 0.7009 | 0.84052 | 734.3 ms | 1,235 KiB |

The five-condition mean includes clean, 12, 6, 0, and -6 dB with equal
condition weight. It is a convenient summary, not a clinical utility score.

## Interpretation

Adaptive PT is the best overall F1/latency option in this experiment. Its mean
F1 is marginally above XQRS while its host latency is about 43 times lower.
XQRS and Two-average are stronger at the extreme -6 dB condition, but Adaptive
PT is better at clean, 12, 6, and 0 dB.

At -6 dB, Two-average reaches F1 0.8454 versus Adaptive PT 0.8204, but its BPM
MAE is 7.50 bpm versus 6.08 bpm and its latency is about ten times higher.
XQRS reaches F1 0.8403 but has 15.39 bpm BPM MAE and about 41 times the latency.
Thus the extra event recall does not translate into a better robust BPM result.

The original benchmark's BioSPPy, MNE, and HeartPy wrappers were not included.
They largely add alternative implementations of overlapping algorithm
families, require a much larger legacy dependency set, and are not attractive
for the intended lightweight deployment. Their adapters remain available in
the cloned upstream repository for a future exhaustive replication.

## Reproduce

```powershell
.\.venv\Scripts\python.exe -m pip install -e ".[sota]"
.\.venv\Scripts\python.exe experiments/benchmark_bpm_qrs.py `
  --classic-suite external/benchmark-qrs-detectors `
  --repeats 1 `
  --output outputs/bpm_qrs_classic_benchmark
```
