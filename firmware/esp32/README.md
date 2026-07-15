# ESP32 ECG Firmware

ESP-IDF application for a single-lead ECG device. The runtime path is:

```text
ADC (360 Hz)
-> breakout analog 0.5-40 Hz
-> firmware causal 5-18 Hz QRS band-pass
-> positive half-wave and squared energy
-> 100/600 ms moving averages, minimum 60 ms block
-> Pan-style adaptive decision and 300 ms refractory
-> raw-history R alignment, RR interval, BPM, RR-based AF screening
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

The `filtered` field is an aggressive QRS-band signal and must not be used for
ECG morphology/ST/T interpretation. On a peak event, `r_peak_timestamp_ms` is
the aligned historical R time; `is_peak` is emitted later when the causal block
decision completes.
