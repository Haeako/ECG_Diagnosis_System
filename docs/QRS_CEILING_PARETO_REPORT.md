# QRS accuracy ceiling and embedded Pareto benchmark

Run date: 2026-10-07

## Protocol

- Same ECG window, channel, annotations, one-to-one scorer, and tolerance for every detector.
- Two deterministic 60 s windows per record (start and end), two timed repeats after warm-up.
- No detector-specific delay correction or parameter tuning after observing the results.
- QTDB: 82 records with usable `atr` beat annotations, channel 0, 164 windows.
- NSTDB: 12 ECG/noise mixtures (records 118 and 119; 24, 18, 12, 6, 0, and -6 dB), channel 0, 24 windows. Pure `bw`, `em`, and `ma` noise records are excluded because they have no beat annotations.
- QTDB is 250 Hz. The production firmware path is explicitly resampled to its fixed 360 Hz input and detections are mapped back to the original timeline.
- Latency is host CPU wall time per 60 s window. It is not ESP32 latency.

Runner: `experiments/benchmark_qrs_ceiling.py`

## QTDB results at +/-50 ms

| Detector | Precision | Recall | F1 | BPM MAE | Median latency (ms/60 s) |
|---|---:|---:|---:|---:|---:|
| SWT + SepConv + ASPP | 0.9978 | 0.9990 | **0.9984** | 0.134 | 110.12 |
| WFDB GQRS | 0.9628 | 0.9574 | 0.9601 | 0.181 | 96.45 |
| WFDB XQRS | 0.8978 | 0.9048 | 0.9013 | 0.555 | 52.11 |
| Adaptive PT v1 | 0.8994 | 0.8954 | **0.8974** | 0.179 | **5.88** |
| Adaptive PT v2 frozen | 0.8994 | 0.8916 | 0.8955 | 0.204 | 5.95 |
| Local Pan-Tompkins | 0.8775 | 0.8711 | 0.8743 | 0.208 | 3.99 |
| Current firmware | 0.8530 | 0.8600 | **0.8564** | 0.836 | **3.80** |
| Fast-QRS | 0.8444 | 0.8446 | 0.8445 | 0.186 | 9.42 |

The strict score is dominated by localization for most lightweight methods. At +/-100 ms, Adaptive PT v1 reaches 0.9971 and the current firmware reaches 0.9823, whereas the ceiling is 0.9993. The SWT model is already at 0.9956 with only +/-25 ms.

## NSTDB aggregate at +/-50 ms

| Detector | Precision | Recall | F1 | BPM MAE | Median latency (ms/60 s) |
|---|---:|---:|---:|---:|---:|
| SWT + SepConv + ASPP | 0.9653 | 0.9714 | **0.9683** | 0.192 | 113.89 |
| Fast-QRS | 0.9050 | 0.9662 | 0.9346 | 4.218 | 11.37 |
| Local Pan-Tompkins | 0.9190 | 0.9388 | 0.9288 | 0.852 | 4.96 |
| Adaptive PT v2 frozen | 0.8783 | 0.8998 | 0.8889 | 1.582 | 7.85 |
| Adaptive PT v1 | 0.8703 | 0.8998 | 0.8848 | 1.548 | 7.64 |
| WFDB XQRS | 0.8424 | 0.9126 | 0.8761 | 4.881 | 88.91 |
| Current firmware | 0.8459 | 0.8800 | **0.8626** | 3.854 | **2.82** |
| WFDB GQRS | 0.7707 | 0.9365 | 0.8456 | 17.568 | 137.28 |

The aggregate contains one clean start window and one noisy end window per record. The noisy window alone is the meaningful stress result:

| Detector | -6 dB | 0 dB | 6 dB | 12 dB | 18 dB | 24 dB |
|---|---:|---:|---:|---:|---:|---:|
| SWT + SepConv + ASPP | **0.6892** | **0.9635** | **0.9933** | **0.9966** | 0.9966 | 0.9933 |
| Adaptive PT v2 frozen | 0.5215 | 0.8961 | 0.9797 | 0.9730 | 0.9730 | 0.9730 |
| Local Pan-Tompkins | 0.5032 | 0.9196 | 0.9799 | 0.9664 | 0.9732 | 0.9732 |
| Fast-QRS | 0.5283 | 0.8434 | 0.9834 | **1.0000** | **1.0000** | **1.0000** |
| Current firmware | **0.3253** | **0.6892** | 0.9216 | 0.9797 | **1.0000** | **1.0000** |

## Architecture and resource interpretation

The ceiling checkpoint is the authors' published PyTorch model, not a reimplementation. It has 693,016 trainable parameters and a 2,903,303 byte checkpoint. Its pipeline includes DWT baseline removal, 40 Hz low-pass filtering, level-4 SWT, a derivative channel, a residual separable-convolution encoder, ASPP, and a decoder/localizer. It is an offline/non-causal accuracy ceiling and is not a realistic always-on ESP32 detector.

The current firmware is the opposite end of the Pareto curve: causal filters, two moving averages, adaptive thresholds, and raw-history relocation. It is roughly 30--40 times faster than the ceiling on the host and has no multi-megabyte model, but loses strict localization and collapses earlier under severe noise.

## Decision

1. Keep SWT-SepConv as the accuracy ceiling, not as the deployment candidate.
2. Improve raw R relocation first. QTDB shows that much of the apparent F1 gap disappears when tolerance increases from 50 to 100 ms; this can be improved with a very small history buffer rather than a CNN.
3. Add a signal-quality gate around the 0--6 dB failure region. Clean/high-SNR segments stay on the current low-cost path; uncertain segments invoke a stronger detector or suppress unreliable RR/AF decisions.
4. Use Adaptive PT v2 or Fast-QRS as the fallback candidate. Adaptive PT v2 is stronger at 0 dB; Fast-QRS is strongest from 6 dB upward but has worse BPM error in the aggregate.
5. Do not select the final QRS detector from QRS F1 alone. Repeat the AF model with annotation-derived peaks, ceiling peaks, and firmware peaks, then report the AF F1/AUROC loss and RR-error distribution.

## Reproduction

```powershell
.\.venv\Scripts\python.exe experiments\benchmark_qrs_ceiling.py `
  --database data\qtdb --output outputs\qtdb_qrs_ceiling `
  --windows 2 --window-sec 60 --repeats 2 --channel first

.\.venv\Scripts\python.exe experiments\benchmark_qrs_ceiling.py `
  --database data\nstdb --output outputs\nstdb_qrs_ceiling `
  --windows 2 --window-sec 60 --repeats 2 --channel first
```

Raw results and manifests are in `outputs/qtdb_qrs_ceiling/` and `outputs/nstdb_qrs_ceiling/`.
