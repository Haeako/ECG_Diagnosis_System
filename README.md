# ECG device deployment

Branch: `feature/firmware`. The ESP32 application is in
[`device_size/firmware_ws`](device_size/firmware_ws/).

Source: production firmware from `origin/master` at `f2f65c0`.
Acquisition, SD recording, BLE, battery, BPM, SQI and AF runtime are retained.
Benchmark entry points, test vectors and profiling build modes are excluded.

See [device setup](device_size/README.md) for build and flash commands.
Gateway and web deployment live on `feature/server` under `server_size/`.
Research and evaluation code live on `feature/experiment`.
