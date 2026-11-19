# ESP32 benchmark backend

This project loads independent BPM, SQI and AF test modules. Select them in
`config.cmake` or override the CMake options. It supplies a deterministic
75-BPM dummy ECG and independent known peak timestamps. Modules do not gate
one another; the fixture is the test input provider.

Values in `config.cmake` initialize a fresh build cache. For an existing cache,
pass `-DECG_TEST_BPM=ON/OFF`, `-DECG_TEST_SQI=ON/OFF` and `-DECG_TEST_AF=ON/OFF`
when switching modules. The captured `CMakeCache.txt` records the resolved
selection rather than relying on the defaults in the source file.

| Module | Algorithm | Input/context | Update |
|---|---|---|---|
| BPM | Fast-QRS C | 10-second float360Hz window | Every5s, matching `BPM_UPDATE_MS`; BPM uses RR ending in the latest5s |
| SQI | Native Orphanidou + template runtime | 10-second int16 waveform + known dummy peaks | Native1-second hop |
| AF | Native RR + ExtraTrees INT8 runtime | Known dummy peak stream; 30RR | Native15RR stride; existing confirmation state |

Sampling is360Hz, fixed by the existing firmware contract. Default duration
is60 logical seconds. Logical timestamps advance at360Hz while execution is
accelerated; this tests window/state behavior and compute cost, not real-time
ADC scheduling. The first full BPM/SQI window arrives after10 logical seconds.
AF must collect31 peaks before its first30RR window.

```powershell
# All modules
powershell -NoProfile -ExecutionPolicy Bypass -File .cache/invoke_idf.ps1 -C firmware/benchmark -B build-dummy-modules -DECG_TEST_BPM=ON -DECG_TEST_SQI=ON -DECG_TEST_AF=ON -DECG_TEST_SECONDS=60 build

# BPM only (same cache can be reconfigured; disabled kernels are unlinked)
powershell -NoProfile -ExecutionPolicy Bypass -File .cache/invoke_idf.ps1 -C firmware/benchmark -B build-dummy-modules -DECG_TEST_BPM=ON -DECG_TEST_SQI=OFF -DECG_TEST_AF=OFF build
```

Run `python firmware/benchmark/check_config.py` to inspect all8 source
combinations, including ESP-IDF early dependency discovery. To flash/capture,
use the configured ESP-IDF Python interpreter:

```powershell
python server/device_profile.py --domain MODULES --profile dummy-modules --build build-dummy-modules --output outputs/benchmarks/my_dummy_run --port COM18 --input-scope "dummy regular75BPM; logical production window schedules"
```

The newly flashed benchmark stays installed by default. Reset the board to
rerun the fixture. Each run records UART, source hashes, resolved CMake cache,
SDK config, images and size. Use a fresh output directory each time.
`--restore-after` is an explicit optional legacy mode and is not used here.

Verify the captured logical schedules and dummy output with:

```powershell
python server/verify_dummy_modules.py --output outputs/benchmarks/my_dummy_run
```

This is a benchmark fixture, not labeled AF/SQI dataset accuracy. The existing
EDB/BUTQDB/AFDB/SHDB experiments remain separate dataset protocols. A benchmark
run is identified by algorithm/module, input provider, protocol and backend;
ESP32 is the backend, not the complete deployed acquisition pipeline.

To compile all eight selections without accessing a device, run from the
workspace root with the ESP-IDF toolchain installed:

```powershell
python server/check_dummy_builds.py --output outputs/benchmarks/my_build_matrix --build build-dummy-selection
python server/report_dummy_builds.py --output outputs/benchmarks/my_build_matrix
```

The checker reconfigures a dedicated cache sequentially, verifies `libmain.a`
members and saves each binary, section sizes, resolved cache and source hashes.
Static DRAM from ELF does not include runtime heap/stack peaks. A compile check
does not replace the UART dummy-window check or labeled dataset evaluation.
