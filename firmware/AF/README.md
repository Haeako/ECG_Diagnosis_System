# AF ESP32 backend

This ESP-IDF project has a separate build cache and accepts only `af-runtime`
(default), `af-extra-trees`, `af-logistic` and `af-random-forest`. Shared frozen
kernels are imported from `firmware/esp32/main/components`; CMake selects the
runtime benchmark or a classifier kernel without production acquisition tasks.

From the workspace root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .cache/invoke_idf.ps1 -C firmware/AF -B build-af-runtime -DECG_BUILD_PROFILE=af-runtime build
```

Use a dedicated build directory per profile. Capture via
`server/device_profile.py --domain AF --profile af-runtime --build build-af-runtime --output outputs/AFib/my_device_run --port COM18`
using the configured ESP-IDF Python interpreter. Substitute the actual port
and a fresh output directory. The benchmark remains installed by default.

Synthetic runtime and fixed-vector classifier profiles measure different
scopes; neither is labeled AFDB/SHDB accuracy. For the current 30-RR window /
15-RR stride with confirmation state, use
[`../benchmark`](../benchmark/README.md) and set `ECG_TEST_AF=ON`. Other modules
can be disabled independently. Known dummy peaks are supplied directly to AF;
SQI does not suppress AF decisions. Logical accelerated execution does not
establish real-time ADC scheduling.

Frozen model and cache evaluation tasks are documented in
[`../../server/AF`](../../server/AF/README.md). Dataset split, peak provider,
coverage and threshold policy must accompany every accuracy result; device
kernel timing cannot substitute for server waveform-branch latency.
