# Pan–Tompkins library rerun

## Question

Does the noise-dependent runtime observed in the local compact Pan–Tompkins
implementation persist when Pan–Tompkins is replaced by an existing library?

## Protocol

- Library: `py-ecg-detectors==1.3.5`, method `pan_tompkins_detector`.
- Comparator: frozen `adaptive_pt`.
- 12 QTDB development records, four deterministic 30 s windows per record.
- NSTDB muscle artifact: clean, +12, +6, 0 and -6 dB.
- 1,772 annotated beats per condition; one-to-one matching at 150 ms.
- Warm-up before timing; seven timed repetitions per window.
- Timing run has `tracemalloc` disabled. A separate run retains memory
  profiling because `tracemalloc` strongly distorts Python runtime.

## Timing-only result

| Condition | Library PT F1 | Library PT latency | Adaptive F1 | Adaptive latency |
|---|---:|---:|---:|---:|
| clean | 0.9817 | 7.374 ms | 0.9960 | 2.623 ms |
| MA +12 dB | 0.9784 | 9.352 ms | 0.9960 | 2.651 ms |
| MA +6 dB | 0.9573 | 10.388 ms | 0.9882 | 2.668 ms |
| MA 0 dB | 0.8844 | 12.080 ms | 0.9475 | 2.668 ms |
| MA -6 dB | 0.7785 | 13.552 ms | 0.8204 | 2.676 ms |

Library Pan–Tompkins runtime rises by 83.8% from clean to -6 dB, whereas
Adaptive PT rises by 2.0%. The trend therefore is not unique to the local
Pan–Tompkins implementation. However, the earlier statement that
Pan–Tompkins is fastest on clean data does not hold for this library: Adaptive
PT is 2.81 times faster in this matched timing run.

## Code-level explanation

The library performs a causal 5–15 Hz bandpass, difference, squaring and a
150 ms moving-window average. Its `panPeakDetect` then scans every sample and,
for every local maximum, updates Python lists and adaptive signal/noise levels.
It may also scan peaks between accepted beats for search-back. Muscle artifact
creates more local maxima, so the data-dependent Python work increases with
noise. Adaptive PT uses SciPy's compiled `find_peaks` with the refractory
distance applied during candidate extraction, leaving little Python work per
extra noisy maximum.

This explains computational runtime, not physiological decision delay. A C
port can have a different absolute cost, although a candidate-driven design
can still do more work under noise.

## Memory-profiler warning

With `tracemalloc` enabled, median clean/-6 dB runtime was 61.86/92.33 ms for
library PT and 7.78/7.96 ms for Adaptive PT. These values quantify execution
under allocation tracing, not normal runtime. Peak traced allocation was about
299 KiB for library PT and 469 KiB for Adaptive PT; neither is ESP32 RAM.

## Artifacts

- Timing: `outputs/bpm_qrs_library_pt_timing/`
- Memory-profiled rerun: `outputs/bpm_qrs_library_pt_rerun/`
- Runner: `experiments/benchmark_bpm_qrs.py`

Reproduce timing:

```powershell
.\.venv\Scripts\python.exe experiments\benchmark_bpm_qrs.py `
  --classic-suite external\benchmark-qrs-detectors `
  --detectors bench_pan_tompkins adaptive_pt `
  --repeats 7 --no-profile-memory `
  --output outputs\bpm_qrs_library_pt_timing
```
