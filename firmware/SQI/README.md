# SQI ESP32 backend

This ESP-IDF project has a separate build cache and accepts only SQI profiles.
Shared frozen kernels remain in `firmware/esp32/main/components`; source
selection is controlled by CMake, with no production acquisition application.

Allowed profiles: `sqi-orphanidou` (default), `sqi-synthetic`, `sqi-xqrs`,
`sqi-logistic`, `sqi-kramer`, `sqi-kramer-rules`, `sqi-ho`, `sqi-clifford`,
`sqi-zhao`. Each profile retains its own input/provider and timing scope.
Their costs cannot be treated as identical waveform-to-score pipelines.

From the workspace root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .cache/invoke_idf.ps1 -C firmware/SQI -B build-sqi-orphanidou -DECG_BUILD_PROFILE=sqi-orphanidou build
```

Use a dedicated build directory per profile. Capture via
`server/device_profile.py --domain SQI --profile sqi-orphanidou --build build-sqi-orphanidou --output outputs/SQI/my_device_run --port COM18`
using the configured ESP-IDF Python interpreter. Substitute the actual port
and a fresh output directory. The benchmark remains installed by default.

For a dummy stream with the current 10-second window / 1-second hop, use
[`../benchmark`](../benchmark/README.md), set `ECG_TEST_SQI=ON` and disable
other modules if desired. It supplies known dummy peaks independently of BPM;
this checks runtime state/cost, not BUTQDB accuracy or end-to-end detector cost.

Host labeled evaluation lives in [`../../server/SQI`](../../server/SQI/README.md).
Historical Logistic C training-record provenance was recovered and its
exported coefficients reconstructed exactly on 17 November. The full BUTQDB
host C evaluation is audited at `outputs/SQI/logistic_native_full_20261117`;
its poor quality-classification results do not justify using it as a gate.
Synthetic device cost does not establish dataset accuracy on ESP32.
The historical cascade was removed from the active benchmark scope at the
user's request; it is not a pending port or a prerequisite for the four
additional frozen host SQI methods. Other implementation limitations remain
explicit in the summary table.
