# Existing SQI C routes: current hardware cost capture

This isolated project links unchanged firmware kernels. It measures two routes on one fixed synthetic waveform, with 3 excluded warmups and 21 measured calls each:

- bSQI: XQRS secondary detector on 14 seconds, retain the 10-second core, insert supplied primary peaks, then agreement at 50 ms.
- Logistic: fill the 10-second core, run the same secondary detector and supplied primary list, calculate five features and evaluate the existing frozen C head.

The primary detector is excluded. The waveform has two seconds of context on each side; this batch operation uses future context. These are fixed-input compute costs. No BUTQDB/CinC accuracy or complete firmware/live latency is inferred. The logistic coefficients belong to the existing historical C head; they are not the BUTQDB Project-5SQI OOF classifier. No verified complete current cascade artifact is available, so the cascade keeps its historical scope.

## Prepare, build and summarize

From the repository root, before building/flashing:

```powershell
.venv/Scripts/python.exe experiment/src/device_sqi_cost_benchmark/prepare_protocol.py --output experiment/report/benchmark_runs/device_sqi_cost_20261105
```

The manifest preserves source/configuration hashes and the exact synthetic float32 input. It refuses to overwrite an existing protocol. In the established ESP-IDF shell:

```powershell
idf.py -C experiment/src/device_sqi_cost_benchmark -B experiment/report/benchmark_runs/device_sqi_cost_20261105/build build
```

The hardware owner chooses the attached port, preserves its original image, flashes and captures exactly one `SQI_COST: BEGIN` through `SQI_COST: END` run, then restores the original image. Afterwards:

```powershell
.venv/Scripts/python.exe experiment/src/device_sqi_cost_benchmark/analyze_uart.py --uart experiment/report/benchmark_runs/device_sqi_cost_20261105/uart.log --protocol experiment/report/benchmark_runs/device_sqi_cost_20261105/protocol.json --output experiment/report/benchmark_runs/device_sqi_cost_20261105 --build experiment/report/benchmark_runs/device_sqi_cost_20261105/build
```

The parser rejects incomplete/duplicated runs, wrong settings, missing measured calls, non-finite/non-repeatable scores and changed linked source. It recalculates nearest-rank percentiles from saved UART timings and records static objects, heap, unused stack and optional linked artifacts separately. The image links both routes and includes benchmark infrastructure; app size is not incremental model flash, and free-heap differences are not an algorithm's complete RAM footprint.
