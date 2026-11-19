# Current firmware DSP replay on ESP32

This isolated ESP-IDF project links the unchanged production QRS, RR/Extra Trees INT8 and Orphanidou/template SQI sources. It does not alter the production build profile or configuration.

The benchmark uses six fixed AFDB ECG1 excerpts, 120 seconds each, starting at 10% of the source record. Selection is independent of labels and predictions. ADC conversion matches the current host replay; state resets only at each excerpt. These excerpts measure computational cost, not an AF accuracy cohort.

## Prepare and build

Run from the repository root with the project Python:

```powershell
.venv/Scripts/python.exe experiment/src/device_pipeline_benchmark/export_vectors.py --output experiment/report/benchmark_runs/device_pipeline_20261105/vectors
```

In the existing ESP-IDF 6.0.1 PowerShell environment, use absolute paths for generated vectors and the build directory:

```powershell
idf.py -C experiment/src/device_pipeline_benchmark -B experiment/report/benchmark_runs/device_pipeline_20261105/build -DREPLAY_VECTOR_DIR=D:/DOC/ECG_Diagnosis_System/experiment/report/benchmark_runs/device_pipeline_20261105/vectors build
```

The hardware owner backs up the attached board and chooses its serial port, flashes this image, captures UART from `PIPE_REPLAY_BEGIN` through `PIPE_REPLAY_END`, and restores the production image afterwards. Keep the backup, flash arguments, image hashes, build-size JSON and raw UART in the run directory. The production build's `flasher_args.json` identifies its original offsets and binaries; confirm the backup/current-production image before restoring.

## Timing and memory boundaries

Each sample timer includes unchanged `ecg_signal_process`, `af_runtime_push_peak`/`latest`, and `sqi_runtime_push`, plus the small cycle-counter instrumentation. It excludes input fetching before the timer, UART logging, sorting, sleep, physical ADC, timer dispatch, IPC queues, BLE and SD. All 120 seconds are timed; startup windows are retained. A result above the nominal 2777.78 us period is a computational exceedance, not an observed live sample drop.

Per-record sample, AF inference call, SQI evaluation call and internal SQI-finalize quantiles use nearest-rank percentiles. Per-10-second output is the sum of measured sample cycles. A reusable 3600-sample buffer emits exact sorted cycle/count histograms after every block, allowing the host to reconstruct record and pooled quantiles without rounding cycles. Sorting and histogram UART output are outside the timer. Static timing arrays total20544 bytes and are reported separately from production working memory. The main task stays on CPU0; the image runs at 160 MHz and performance optimization. Heap values are internal 8-bit-capable free/minimum heap. Linked image/map data includes the 518400-byte embedded input corpus. Stack high-water mark is unused bytes on ESP-IDF.

`reference_*.npz` and `protocol.json` provide data/model/source hashes, host peak/AF/SQI events and counts for the benchmark's host/device parity audit. No weights or thresholds are modified.

After a complete UART capture:

```powershell
.venv/Scripts/python.exe experiment/src/device_pipeline_benchmark/analyze_uart.py --uart experiment/report/benchmark_runs/device_pipeline_20261105/uart.log --vectors experiment/report/benchmark_runs/device_pipeline_20261105/vectors --output experiment/report/benchmark_runs/device_pipeline_20261105
```

This checks exact QRS/AF events, exact SQI decisions, at most one integer unit for SQI quantized ratio/correlation, input checksum, corrected/BPM checksum, event counts and unchanged production source hashes. The cycle histogram must contain exactly3600 samples per block and reproduce each saved block sum. It writes CSV timing/record/block/histogram tables, `summary.json` and `parity_audit.json`. Sample quantiles are calculated both per record and on all259200 samples pooled; per-10-second compute quantiles pool all72 blocks. Percentiles are not averaged.

## Floating point contraction audit

In the 05/11/2026 capture, four original host corrected/BPM checksums differed from hardware. The actual Xtensa notch object uses four `madd.s` instructions; the original host GCC build does not enable FMA. Compiling the unchanged QRS/notch sources on the host with `-mfma -ffp-contract=fast` reproduces all six hardware checksums exactly. The original non-FMA host reference is preserved. The two arithmetic modes differ on six of259200 notch samples, each by one ADC count (0.005mV); every BPM sample and all QRS/AF/SQI event outputs agree exactly.

The checksum remains mandatory. Generate an explicit arithmetic audit and pass it to the parser:

```powershell
.venv/Scripts/python.exe experiment/src/device_pipeline_benchmark/audit_fp_contraction.py --run experiment/report/benchmark_runs/device_pipeline_20261105
.venv/Scripts/python.exe experiment/src/device_pipeline_benchmark/analyze_uart.py --uart experiment/report/benchmark_runs/device_pipeline_20261105/uart.log --vectors experiment/report/benchmark_runs/device_pipeline_20261105/vectors --output experiment/report/benchmark_runs/device_pipeline_20261105 --fp-reference experiment/report/benchmark_runs/device_pipeline_20261105/checksum_diagnostic/fp_audit.json
```

The arithmetic audit saves compiler flags, source/library hashes, the actual Xtensa disassembly, complete FMA corrected/BPM arrays and the six differing sample positions. It requires reproduction of the original host reference, unchanged BPM, at most one-count notch differences, and exact complete hardware output checksums on every record. The final parser also rechecks those arrays and preserves whether the original non-FMA checksum matched each record.
