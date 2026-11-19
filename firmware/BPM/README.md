# BPM firmware module

The `bpm-fast-qrs` profile links only the frozen Fast-QRS C kernel and a
10-second block benchmark. It runs no SQI/AF, acquisition, storage or radio task.
The kernel workspace is static; the input is 3,600 float samples at 360 Hz.
Peak capacity is 128, checked before output. Source upstream remains untouched.

The benchmark resets each window, calculates RR from adjacent detected peaks,
and outputs BPM as `60 * 360 / mean(diff(peaks))`. Fewer than two peaks means
no BPM. RR intervals are not filtered or repaired. Cross-window RR handling
and live sample capture are outside this benchmark.

Build from the workspace root with the configured ESP-IDF environment:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .cache/invoke_idf.ps1 -C firmware/BPM -B build-fast-qrs -DECG_BUILD_PROFILE=bpm-fast-qrs -DQRS_COMMON_INPUT_DIR=D:/DOC/ECG_Diagnosis_System/outputs/BPM/esp_candidates_edb_20261114/vectors build
```

The input header contains the fixed 21 EDB screen cases. One warmup and five
timed calls run per case. The timer includes detector, reset, peak validation
and RR/BPM; input reconstruction and UART are outside. Output availability
still requires the completed 10-second input window.

Device capture uses `server/device_profile.py` with `--domain BPM`,
`--profile bpm-fast-qrs` and `--build build-fast-qrs`. The newly flashed
benchmark stays installed by default. The historical standalone capture used
`--restore-after`; that optional mode verifies the previous full-flash backup
and restores it, and is not the current requested workflow.
Use a fresh output directory. `server/BPM/report_device_fast_qrs.py` verifies
the recorded run against host C peaks and independently recomputes mean-RR BPM.
Published results belong in `experiment/report/qrs_rr_bpm/REPORT.md`.

For current 10-second context / 5-second update tests, use
[`../benchmark`](../benchmark/README.md) with `ECG_TEST_BPM=ON`; its dummy
stream and overlapping context differ from this reset10s block fixture.
The corresponding full EDB accuracy task is
`python server/BPM/run.py fast-sliding --output outputs/BPM/my_sliding_run`.
