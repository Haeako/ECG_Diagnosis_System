# Build and profile individual algorithms

Run from `firmware/esp32` in an ESP-IDF 6.x terminal. Use fresh build directories;
the production profile and its SQI cost/telemetry switches were removed.

```powershell
idf.py -B build/bpm-runtime -DECG_BUILD_PROFILE=bpm-runtime build
idf.py -B build/sqi-orphanidou -DECG_BUILD_PROFILE=sqi-orphanidou build
idf.py -B build/af-runtime -DECG_BUILD_PROFILE=af-runtime build
```

Flash and monitor the chosen build (substitute the board's actual port):

```powershell
idf.py -B build/bpm-runtime -DECG_BUILD_PROFILE=bpm-runtime -p COM18 flash monitor
idf.py -B build/bpm-runtime -DECG_BUILD_PROFILE=bpm-runtime size
```

The default is `bpm-runtime`. `bpm-runtime` and `af-runtime` exercise the original
streaming runtimes with synthetic inputs and emit `MODULE_PROFILE,...,END`.
Timing covers kernel calls and loop/checksum overhead; the per-block watchdog
delay is excluded. `sqi-orphanidou` retains its existing runtime benchmark and
UART statistics. These synthetic runs measure cost, not clinical accuracy.

Available alternative profiles:

- AFib inference only: `af-extra-trees`, `af-logistic`, `af-random-forest`.
- QRS: `qrs-pan`, `qrs-elgendi`, `qrs-common`, `qrs-han-cnn`.
- SQI: `sqi-synthetic`, `sqi-xqrs`, `sqi-logistic`, `sqi-kramer`,
  `sqi-kramer-rules`, `sqi-ho`, `sqi-clifford`, `sqi-zhao`, `sqi-orphanidou`.

`qrs-common` requires `-DQRS_COMMON_INPUT_DIR=<directory>` containing the exported
`qrs_common_test_vectors.h`. TFLM is optional for `qrs-common` via
`-DECG_ENABLE_TFLM=ON` and required by `qrs-han-cnn`. Other profiles omit TFLM.
Alternative algorithms retain their own computational dependencies.

CMake presets use separate `build/<preset>` directories. Automatic sdkconfig
files are local to each build; explicitly supplied SDKCONFIG paths are honored.
Do not reuse a cache containing removed production switches.

Check profile registration, isolated linkage and option validation without IDF:

```powershell
python main/cmake/check_profiles.py
```

This checks configuration only. Compile and device measurements are separate.
The host libraries and input formats are documented in
[the module guide](../../modules/README.md).
