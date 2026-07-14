# ESP32 ECG Firmware

ESP-IDF application for a single-lead ECG device. The runtime path is:

```text
ADC (360 Hz)
-> moving-average baseline removal
-> high-pass, band-stop, low-pass, Kalman filters
-> R-peak, RR interval, BPM, RR-based AF screening
-> SD recording and BLE
```

Core files:

- `main/ecg_signal.c`: filtering, R-peak detection, RR/BPM, and AF invocation;
- `main/ecg_pipeline.c`: ADC task, buffering, SD segments, and BLE startup;
- `main/main.c`: INIT/IDLE/RECORD state machine and health checks;
- `main/components/afib`: lightweight RR-irregularity screening;
- `main/components/ble`: BLE GATT transport.

Build with a configured ESP-IDF environment:

```bash
idf.py set-target esp32
idf.py build
```

The AF output is a screening heuristic (`normal`/`suspected`), not a clinical
diagnosis. The firmware benchmark should report processing latency, missed ADC
samples, minimum free heap, binary size, and sustained real-time operation.
