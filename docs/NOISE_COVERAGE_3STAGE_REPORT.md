# Three-stage QRS noise-coverage report

## Leakage control

This is a locked evaluation of the already frozen Adaptive PT detector. The 12
records used in all earlier exploratory/tuning runs are permanently assigned to
the development set and excluded:

`sel123, sel233, sel302, sel307, sel820, sel853, sel16420, sel16795,
sele0106, sele0121, sel14046, sel15814`.

All 70 remaining locally available QTDB records with source `.atr` annotations
form the test set. No detector parameter was selected from their results. The
exact split, detector source SHA-256 and run parameters are stored in
`outputs/noise_coverage_3stage/split_manifest.json` and `run_config.json`.
Each condition contains 210 deterministic 30-second windows. Random noise
offsets and fault positions use stable SHA-256-derived seeds.

The complete test produced 89,040 per-window/tolerance rows. Scores are reported
at 25, 50, 100 and 150 ms rather than choosing a favorable tolerance after the
run. NSTDB `bw`, `ma`, and `em` files were downloaded from PhysioNet and verified
against the official SHA-256 manifest.

## Important annotation finding

QTDB `.atr` files are inherited from each record's source database. They are
reference beat annotations, but are not a homogeneous manually marked R-peak
standard. For example, several ESC-derived records place the source fiducial
about 52–62 ms before the signal-0 R peak. This creates the following clean
Adaptive PT result:

| Tolerance | Precision | Recall | F1 |
|---:|---:|---:|---:|
| 25 ms | 0.6617 | 0.6559 | 0.6588 |
| 50 ms | 0.8894 | 0.8815 | 0.8854 |
| 100 ms | 0.9992 | 0.9904 | 0.9948 |
| 150 ms | 0.9999 | 0.9911 | 0.9954 |

Thus 150 ms is meaningful for beat-event robustness but cannot establish
precise R-wave localization. The 50 ms results below are deliberately retained
as a strict, source-fiducial-sensitive view. A future localization study should
use homogeneous R-peak annotations or source-aware timing offsets.

## Stage 1 — additive NSTDB noise

Stage 1 covers baseline wander (`bw`), muscle artifact (`ma`) and electrode
motion (`em`) at +24, +18, +12, +6, 0, -6 and -12 dB.

Selected Adaptive PT results:

| Condition | F1 @ 50 ms | F1 @ 150 ms | BPM MAE |
|---|---:|---:|---:|
| clean | 0.8854 | 0.9954 | 0.27 bpm |
| BW -12 dB | 0.8819 | 0.9938 | 0.31 bpm |
| MA 0 dB | 0.8641 | 0.9773 | 0.70 bpm |
| MA -6 dB | 0.7876 | 0.9035 | 3.33 bpm |
| MA -12 dB | 0.6013 | 0.7300 | 12.15 bpm |
| EM 0 dB | 0.8756 | 0.9918 | 0.33 bpm |
| EM -6 dB | 0.8063 | 0.9312 | 5.32 bpm |
| EM -12 dB | 0.6006 | 0.7642 | 24.31 bpm |

Baseline wander is handled very well by the 5–25 Hz QRS bandpass. Muscle and
electrode-motion noise are the principal failure modes. At -12 dB, neither the
event sequence nor BPM estimate is reliable.

Across all Stage-1 conditions at 50 ms, mean F1 is 0.8498 for Adaptive PT and
0.8081 for the conventional local Pan-Tompkins baseline.

## Stage 2 — device and acquisition faults

Stage 2 covers 50 Hz interference, amplitude clipping, contiguous sample
dropout, gain changes, polarity inversion, 6–12 bit quantization and impulse
spikes. Mean F1 at 50 ms is 0.8792 for Adaptive PT versus 0.8484 for the
baseline.

The worst Adaptive PT cases are:

| Condition | Precision | Recall | F1 | BPM MAE |
|---|---:|---:|---:|---:|
| clip at 80th amplitude percentile | 0.8378 | 0.8434 | 0.8406 | 1.25 bpm |
| 5% block dropout | 0.8812 | 0.8378 | 0.8590 | 0.41 bpm |
| clip at 90th percentile | 0.8626 | 0.8575 | 0.8600 | 0.25 bpm |
| 3% block dropout | 0.8864 | 0.8575 | 0.8717 | 0.38 bpm |
| 2 spikes/second | 0.8651 | 0.8792 | 0.8721 | 0.77 bpm |

Gain and polarity changes cause little degradation because detection and final
peak relocation are magnitude-based. Clipping, missing samples and impulses
are more important than power-line interference for this architecture.

## Stage 3 — compound stress

| Compound condition | Adaptive F1 @ 50 ms | Baseline F1 @ 50 ms | Adaptive F1 @ 150 ms | Adaptive BPM MAE |
|---|---:|---:|---:|---:|
| wearable mild | 0.8849 | 0.8617 | 0.9959 | 0.26 bpm |
| baseline + motion | 0.8820 | 0.8615 | 0.9952 | 0.28 bpm |
| sensor fault | 0.8711 | 0.8483 | 0.9825 | 0.36 bpm |
| wearable hard | 0.8307 | 0.8053 | 0.9586 | 1.02 bpm |
| motion extreme | 0.6338 | 0.4737 | 0.7792 | 9.72 bpm |
| mixed extreme | 0.2314 | 0.1757 | 0.4713 | 30.19 bpm |

The frozen Adaptive PT consistently exceeds the baseline, but fails under the
two extreme compound conditions. These results are evidence against a blanket
robustness or SOTA claim.

## Overall conclusion

At 50 ms across all 53 conditions, Adaptive PT mean F1 is 0.8492 versus 0.8116
for the baseline. At 150 ms the corresponding values are 0.9662 and 0.9487.
The detector is promising for ordinary wearable noise, especially baseline
wander, but it is not robust enough for extreme electrode motion, severe
muscle artifact, or combined sensor failure.

No post-test parameter changes should be described as part of this locked run.
Any new algorithm revision must receive a new version/hash, be developed only
on the 12-record development split, and then be evaluated on a different
external locked dataset rather than reusing these 70 records as a fresh test.

## Artifacts

- `outputs/noise_coverage_3stage/per_window.csv`
- `outputs/noise_coverage_3stage/summary.csv`
- `outputs/noise_coverage_3stage/summary.json`
- `outputs/noise_coverage_3stage/split_manifest.json`
- `outputs/noise_coverage_3stage/run_config.json`
- `outputs/noise_coverage_3stage/stage1_f1_vs_snr.png`
- `outputs/noise_coverage_3stage/stage2_f1.png`
- `outputs/noise_coverage_3stage/stage3_f1.png`
- `outputs/noise_coverage_3stage/adaptive_precision_recall.png`

## Reproduce

```powershell
.\.venv\Scripts\python.exe experiments/download_ecg_quality_data.py --data-dir data
.\.venv\Scripts\python.exe experiments/noise_coverage_3stage.py `
  --output outputs/noise_coverage_3stage
```
