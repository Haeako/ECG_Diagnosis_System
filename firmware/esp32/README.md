# ESP32 algorithm profiling

This project builds one selected BPM, SQI or AFib benchmark. The acquisition,
SD recording, BLE, Wi-Fi/MQTT, state machine and application tasks were removed.
The original algorithm computations remain under `main/components/`.

Start with [the build guide](main/BUILDING.md). To run the same kernels on a
computer without ESP-IDF, use [the standalone modules](../modules/README.md).

The default profile is `bpm-runtime`. Each profile has its own build directory
and sdkconfig. Old production build directories are not compatible.
Historical measurements remain in `experiment/report/`.
