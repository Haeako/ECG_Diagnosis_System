# Research Scope

## Research question

Classical signal quality assessment (SQA/SQI) asks whether an observed ECG is
usable or corrupted. CleanGuard asks a different model-level question:

> When a denoiser receives an ECG that is already clean, how much clinically
> relevant signal does it change unnecessarily?

An SQI can label both the clean input and an over-smoothed output as acceptable.
It does not directly test whether the denoiser should have left the input alone.
CleanGuard therefore complements, rather than replaces, SQA/SQI.

## Hypothesis

Denoisers with similar reconstruction quality on noisy ECG can have materially
different clean-passthrough damage. Larger CleanGuard damage is expected to be
associated with degradation in an annotation-based downstream task, measured
here by missed/extra R peaks and RR-interval error after Pan-Tompkins detection.

The experiment must test this association across methods, records, noise types,
and SNR levels. A single example or a ranking disagreement is evidence for a
trade-off, not proof that CleanGuard predicts every downstream failure.

## Metric protocol

Let `D` be a denoiser, `x` a clean ECG, and `y` its noisy version.

```text
Utility:    distance(x, D(y))
CleanGuard: distance(x, D(x))
Clean effect: compare PanTompkins(D(x)) with dataset R annotations
Utility task: compare PanTompkins(D(y)) with dataset R annotations
```

The primary global CleanGuard components are PRD and derivative RMSE. RMSE and
cosine similarity are supporting components. CleanGuard+ adds the following
local protocol for a sampling rate `fs`:

```text
windows: 1 second, 0.5-second hop
CG-WorstRMSE: maximum window RMSE
CG-MaxLocalPRD: maximum window PRD
CG-BoundaryRMSE: RMSE over the first and last second
CG-SC-PRD: PRD after the best integer shift limited to +/-20 ms
```

For clean input `x`, clean output `xc = D(x)`, and sliding-window set `W`:

```text
CG-WorstRMSE = max_{w in W} RMSE(x[w], xc[w])
CG-MaxLocalPRD = max_{w in W} PRD(x[w], xc[w])
tau* = argmin_{|tau| <= 20 ms} RMSE(x, shift(xc, tau))
CG-SC-PRD = PRD(x, shift(xc, tau*))
```

Shift correction is an auxiliary cause-analysis metric. Exact sample alignment
remains primary, and DTW is excluded because it can hide clinically relevant
timing errors. The primary CleanGuard validation endpoint
is clean-passthrough R-peak F1; miss rate and RR MAE expose clinically
interpretable failure modes. The same endpoints after noisy-input denoising are
reported separately as practical utility, not conflated with clean damage.

Results are computed per window; local maxima are taken within each window
before aggregation across records. Report mean,
standard deviation, median, and a record-level confidence interval in the paper.
Do not concatenate all samples into one pseudo-observation.

## What this does not claim

- CleanGuard is not an input-only SQI and does not detect lead-off, saturation,
  motion artifact, baseline wander, muscle artifact, or powerline noise by
  itself.
- R-peak preservation does not establish preservation of ST segments, P waves,
  T waves, or diagnostic morphology.
- The compact detector is Pan-Tompkins-style and must not be described as an
  exact reproduction of every step or threshold in the 1985 implementation.
- QTDB `pu1`/`pu0` annotations are preferred. Detector-generated pseudo-labels
  are not accepted as ground truth in the final experiment.
- MECG-E and DeScoD results must only be included when their exact checkpoints,
  preprocessing, data split, and inference command are reproducible.

## Relation to a future SQI module

An input-only SQI remains useful as a separate deployment gate:

```text
raw ECG -> SQI (good / acceptable / bad)
        -> denoiser, if needed
        -> CleanGuard is evaluated offline during model selection
        -> Pan-Tompkins and downstream task
```

SQI features such as kurtosis, skewness, entropy, band powers, clipping ratio,
RR stability, and QRS-template consistency should be validated against labelled
quality data. They should not be mixed into the CleanGuard definition.
