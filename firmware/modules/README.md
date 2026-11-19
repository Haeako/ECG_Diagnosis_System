# Standalone ECG modules

Three independent static libraries compile the original C algorithm kernels.
No ESP-IDF, peripheral, transport or application pipeline is needed on the host.
Each runtime currently has one private static state: reset before a new record,
and use separate processes for concurrent records.

| Library | Header under `firmware/esp32/main/` | Input and API |
|---|---|---|
| `ecg_bpm` | `components/bpm/ExtractFeature.h` | `ecg_signal_init/reset(start_ms)`, then `ecg_signal_process(raw_adc, timestamp_ms)` at 360 Hz; returns peaks, RR and BPM |
| `ecg_sqi` | `components/sqi_gate/sqi_runtime.h` | `sqi_runtime_reset(start_ms)`, then `sqi_runtime_push(centered_sample, is_peak, peak_ms, timestamp_ms)` at 360 Hz |
| `ecg_afib` | `components/af_model/af_runtime.h` | `af_runtime_reset()`, then `af_runtime_push_peak(peak_ms)`; `af_runtime_latest()` and `af_runtime_last_features()` expose results |

BPM keeps its fixed notch, causal QRS filter, detection and BPM averaging.
SQI uses a 10-second window and a 1-second hop; the caller supplies R-peaks.
AFib keeps the frozen INT8 Extra Trees model, 30 RR intervals, 15 RR stride
and confirmation logic. It accepts peak timestamps without an ECG detector.
SQI does not gate AFib. None of these libraries calls another module.

## Build and run

Use CMake, a native C compiler, and Ninja on PATH (another native generator
also works). From the repository root:

```powershell
cmake -S firmware/modules -B build/modules-host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/modules-host
ctest --test-dir build/modules-host --output-on-failure
.\build\modules-host\profile_bpm.exe --repeat 10
.\build\modules-host\profile_sqi.exe --repeat 10
.\build\modules-host\profile_afib.exe --repeat 10
```

On Linux/macOS omit `.exe`. For a compiler or Ninja outside PATH, pass
`-DCMAKE_C_COMPILER=<path>` and `-DCMAKE_MAKE_PROGRAM=<path>` at configuration.
To build just one module and profiler: `cmake --build build/modules-host --target profile_bpm`.
Downstream CMake projects can `add_subdirectory()` this directory and link
`ecg_bpm`, `ecg_sqi` or `ecg_afib` directly.

## Supply module input

`--input path/to/headerless.csv` loads a record before timing. Blank lines and
headers are rejected. Inputs contain at most 1,000,000 rows:

- BPM: one raw ADC integer per line, range 0–4095, baseline 2048.
- SQI: `centered_sample,is_peak` per line; sample is INT16 and flag is 0 or 1.
  The marked sample's timestamp is used as the R-peak timestamp.
- AFib: one strictly increasing peak timestamp in milliseconds per line,
  range 0–2147483647. Timestamp zero follows the runtime's ignored-peak rule.

BPM/SQI timestamps are inferred at 360 Hz with rounded integer milliseconds.
For delayed detections or explicit peak timestamps, call the library API directly.
Without an input file, each program uses deterministic synthetic input.
`--repeat N` resets the module and replays the same record N times (default 10).

The output includes module, platform, input, rows, calls, elapsed milliseconds,
mean microseconds per push and a checksum. Parsing, allocation and reset are
excluded; loop and checksum overhead are included. Mean push cost includes both
inference/finalization and inexpensive calls between windows. Use the existing
device benchmark profiles for detailed window statistics. This host runner does
not measure peak RAM, device latency, accuracy or a complete system's throughput.

## Migration

The former `task/Core1/ECGPipeline/ExFeature/` kernel now lives in
`components/bpm/`. Its function names remain compatible; its result struct now
contains BPM/QRS fields only. SQI and AFib return their own result structs.
Acquisition drivers, SD, BLE, network, application queues, recovery, state
machine, full pipeline and production build options were removed. Historical
reports and explicit offline comparison/fusion experiments remain research assets.
Third-party cloned repositories are unchanged.

See [ESP32 profiling](../esp32/main/BUILDING.md) for device builds.
