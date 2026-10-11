# BPM/QRS architecture comparison and common-protocol benchmark

## Executive result

The production detector is not a novel-from-scratch QRS architecture. Its most
accurate description is a **causal hybrid of Elgendi/TERMA two event-related
moving averages and Pan–Tompkins/Hamilton adaptive signal/noise decision
logic**, followed by a project-specific raw-ECG R-location step.

On the same external MIT-BIH screening protocol, production firmware is the
fastest implementation (1.64 ms per 60 s) but is less accurate than the best
offline methods. `adaptive_pt_v1` gives the strongest current accuracy/latency
trade-off; Fast-QRS has the best strict 50 ms F1, while WFDB GQRS has the best
100/150 ms F1 at much higher latency.

## Current production architecture

`AD8232 0.5–40 Hz -> ADC 360 Hz -> 49–51 Hz notch -> causal 5–18 Hz biquads ->
square -> MA 100 ms and MA 600 ms -> candidate block >=60 ms -> adaptive
signal/noise threshold -> 300 ms refractory -> raw-history peak relocation
(-90/+15 ms) -> valid RR 250–2000 ms -> 5 s BPM`.

The adaptive levels use `0.125 * new + 0.875 * old`. A candidate used to learn
the signal level is capped at twice the previous signal level. Refractory
duplicates do not update the noise estimate. R relocation uses the largest
absolute deviation from a local raw-signal baseline, supporting inverted QRS.

## Closest published architectures

| Paper | Shared structure | Material differences | Similarity |
|---|---|---|---|
| Elgendi 2013, *Fast QRS Detection with an Optimized Knowledge-Based Method* | Bandpass, squaring, short QRS MA, long heartbeat MA, blocks of interest, duration rule, extremum in block | Typical 8–20 Hz and optimized ~97/611 ms windows; no Pan-style SPKI/NPKI layer as used here | **Closest front end** |
| Pan & Tompkins 1985 | QRS bandpass, nonlinear energy enhancement, integration, adaptive signal/noise levels, refractory | Original uses derivative, ~150 ms integration, dual thresholds, search-back and T-wave logic; current firmware uses two MAs and lacks search-back | **Closest decision ancestry** |
| Hamilton & Tompkins 1986 | Candidate/noise classification, adaptive levels, refractory and RR rules | Current firmware lacks the fuller missed-beat/rhythm rule set | Close decision logic |
| Christov 2004 | Causal real-time differentiated energy and noise-adaptive threshold | Christov combines three thresholds: slope, EMG integration and beat expectation; current firmware does not contain this M+F+R structure | Related, not a match |
| Bachi, Billeci & Varanini 2021 | Moving-average-based QRS enhancement, low complexity, adaptive decision using morphology/rhythm knowledge | Uses cascades/pool of derivative MA filters and richer rhythm/morphology logic | Related lightweight family |

Therefore the defensible novelty claim is the **embedded hybrid, inverted-QRS
handling, capped adaptation, raw relocation, end-to-end AF integration and
deployment validation**, not invention of Two-MA or adaptive QRS thresholding.

Key sources:

- Pan J, Tompkins WJ. DOI `10.1109/TBME.1985.325532`.
- Hamilton PS, Tompkins WJ. DOI `10.1109/TBME.1986.325695`.
- Elgendi M. DOI `10.1371/journal.pone.0073557`.
- Christov II. DOI `10.1186/1475-925X-3-28`.
- Bachi L, Billeci L, Varanini M. DOI `10.3390/app11156995`.

## One common protocol

- Dataset: all 48 MIT-BIH Arrhythmia records bundled by the upstream
  Pan–Tompkins++ repository.
- Signal: lead 0; no detector-specific parameter tuning.
- Sampling: native 360 Hz. The frozen ecg2rr model alone requires a fixed
  360->250 Hz adapter. Firmware alone requires its fixed AFE/ADC adapter:
  causal 0.5 Hz coupling, 200 counts/mV, center 2048, 12-bit clipping.
- Windows: two deterministic 60 s windows per record (record start/end), 96
  windows and 7,400 annotated beats.
- Event matching: chronological one-to-one at 25, 50, 100 and 150 ms.
- BPM: median valid RR, same implementation for every detector.
- Timing: warm-up then one untraced host run per window. This is screening
  timing, not ESP32 cycles.
- Memory: a separate `tracemalloc` call on the first window of each record.
  It excludes native allocations and is not used as ESP32 RAM.

Paper-reported scores are deliberately not mixed into the result table. A
paper without runnable code in this workspace is architecture-only.

## Results at strict 50 ms

| Detector | Precision | Recall | F1 | BPM MAE | Median latency / 60 s |
|---|---:|---:|---:|---:|---:|
| Fast-QRS | 0.9874 | **0.9878** | **0.9876** | 0.445 bpm | 7.56 ms |
| Adaptive PT v1 | 0.9900 | 0.9722 | **0.9810** | 0.342 bpm | 4.73 ms |
| Adaptive PT v2 frozen | 0.9914 | 0.9684 | 0.9798 | 0.734 bpm | 4.63 ms |
| Local Pan–Tompkins | **0.9928** | 0.9659 | 0.9792 | 0.792 bpm | 2.90 ms |
| WFDB XQRS | 0.9729 | 0.9734 | 0.9731 | 0.931 bpm | 76.35 ms |
| WFDB GQRS | 0.9670 | 0.9669 | 0.9670 | **0.299 bpm** | 114.25 ms |
| ecg2rr BiLSTM | 0.9710 | 0.9468 | 0.9587 | 0.601 bpm | 530.33 ms |
| **Production firmware C** | 0.9332 | 0.9293 | **0.9313** | 0.405 bpm | **1.64 ms** |
| Pan–Tompkins++ | 0.8780 | 0.9882 | 0.9299 | 0.336 bpm | 7.65 ms |
| Engzee | 0.9741 | 0.8654 | 0.9166 | 3.261 bpm | 114.17 ms |
| Christov | 0.6380 | 0.8970 | 0.7456 | 12.535 bpm | 271.66 ms |
| SWT | 0.7140 | 0.7341 | 0.7239 | 3.102 bpm | 35.49 ms |
| Library Pan–Tompkins | 0.5382 | 0.5488 | 0.5434 | 1.362 bpm | 24.49 ms |
| Hamilton | 0.4076 | 0.4164 | 0.4120 | 1.663 bpm | 21.22 ms |
| Two-average | 0.2165 | 0.2192 | 0.2179 | 0.494 bpm | 13.47 ms |
| Matched filter | 0.0933 | 0.0973 | 0.0953 | 3.625 bpm | 19.38 ms |

The very low 50 ms scores of several `py-ecg-detectors` implementations are
mainly fixed algorithmic output delay, not catastrophic beat-count failure.
No detector-specific time shift was estimated from this test set, because that
would be post-test calibration. This is why all tolerances are retained.

## Results at beat-event tolerance 150 ms

| Rank | Detector | F1 | BPM MAE | Median latency / 60 s |
|---:|---|---:|---:|---:|
| 1 | WFDB GQRS | **0.9925** | **0.299 bpm** | 114.25 ms |
| 2 | Fast-QRS | 0.9916 | 0.445 bpm | 7.56 ms |
| 3 | Adaptive PT v1 | 0.9878 | 0.342 bpm | 4.73 ms |
| 4 | Adaptive PT v2 frozen | 0.9855 | 0.734 bpm | 4.63 ms |
| 5 | Two-average | 0.9815 | 0.494 bpm | 13.47 ms |
| 6 | Local Pan–Tompkins | 0.9815 | 0.792 bpm | 2.90 ms |
| 7 | WFDB XQRS | 0.9788 | 0.931 bpm | 76.35 ms |
| 8 | Library Pan–Tompkins | 0.9782 | 1.362 bpm | 24.49 ms |
| 9 | Production firmware C | 0.9691 | 0.405 bpm | **1.64 ms** |
| 10 | Matched filter | 0.9687 | 3.625 bpm | 19.38 ms |
| 11 | SWT | 0.9686 | 3.102 bpm | 35.49 ms |
| 12 | ecg2rr BiLSTM | 0.9671 | 0.601 bpm | 530.33 ms |
| 13 | Hamilton | 0.9624 | 1.663 bpm | 21.22 ms |
| 14 | Pan–Tompkins++ | 0.9369 | 0.336 bpm | 7.65 ms |
| 15 | Engzee | 0.9240 | 3.261 bpm | 114.17 ms |
| 16 | Christov | 0.8264 | 12.535 bpm | 271.66 ms |

## Selection conclusion

- **Firmware now:** keep the production hybrid when minimum compute and true
  causal streaming dominate. It is fastest and its BPM MAE is good, but strict
  localization and event F1 leave room for improvement.
- **Best research/default trade-off:** Adaptive PT v1. It remains much faster
  than GQRS and has strong 50/150 ms F1 plus low BPM error.
- **Best strict localization in this run:** Fast-QRS, at moderate host cost.
- **Best broad event F1/BPM:** GQRS, but its latency is ~24x Adaptive v1 and it
  is not an obvious microcontroller port.
- **Do not select by architecture reputation:** Pan–Tompkins++, BiLSTM and
  several classics do not win this fixed external protocol.

This screening does not include added NSTDB noise or real AD8232 data. The next
deployment decision must combine this external clean/pathological result with
the locked 53-condition noise test and on-device RAM/cycle measurements.

Artifacts: `outputs/mitdb_all_detectors/{summary.csv,per_window.csv,
summary.json,manifest.json}`. Runner: `experiments/evaluate_mitdb_external.py`.
