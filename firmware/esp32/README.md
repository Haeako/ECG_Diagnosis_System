# ESP32 ECG Firmware

ESP-IDF application for a single-lead ECG device. The runtime path is:

```text
ADC (360 Hz)
-> breakout analog 0.5-40 Hz
-> fixed 49-51 Hz digital notch (always on)
-> firmware causal 5-18 Hz QRS band-pass
-> polarity-independent squared energy
-> 100/600 ms moving averages, minimum 60 ms block
-> Pan-style adaptive decision and 300 ms refractory
-> raw-history dominant QRS extremum alignment, RR interval, BPM
-> SD recording and BLE
```

Core files:

- `main/ecg_signal.c`: filtering, R-peak detection and RR/BPM;
- `main/ecg_pipeline.c`: ADC task, buffering, SD segments, and BLE startup;
- `main/main.c`: INIT/IDLE/RECORD state machine and health checks;
- `main/components/ble`: BLE GATT transport.

Build with a configured ESP-IDF environment:

```bash
idf.py set-target esp32
idf.py build
```

## MabuTrace profiling

MabuTrace is enabled in this profiling firmware. Configure the station
SSID/password and HTTP port under `ECG Monitor Configuration`:

```bash
idf.py menuconfig
idf.py build flash monitor
```

If no station SSID is configured (or connection fails), connect to the
`ECG_TRACE_SETUP` access point with password `ecgtrace`, then open
`http://192.168.4.1/` to save station credentials. The trace capture UI is at
`http://<device-ip>:81/` (or the configured port). Open the captured JSON in
[Perfetto](https://ui.perfetto.dev). For a production build, remove the
MabuTrace dependency and instrumentation so its 64 KiB trace buffer and
FreeRTOS task-switch hooks are not linked.

Automatic FreeRTOS task-switch events are disabled in this firmware because
MabuTrace 1.0.3 supports only 15 task IDs, while the combined Wi-Fi and NimBLE
runtime creates more tasks. Explicit ECG and SD duration traces remain enabled.

The firmware benchmark should report processing latency, missed ADC
samples, minimum free heap, binary size, and sustained real-time operation.

The `filtered` field is an aggressive QRS-band signal and must not be used for
ECG morphology/ST/T interpretation. On a peak event, `r_peak_timestamp_ms` is
the aligned historical R time; `is_peak` is emitted later when the causal block
decision completes.

SD recordings expose the processing stages in this order:

```text
timestamp_ms,raw_adc,centered,notch_49_51hz,bandpass_5_18hz,ma_qrs_100ms,threshold,is_r_peak,bpm
```

`bpm` holds the latest valid RR-derived value between beats so it can be
plotted as a time series; it is zero until the first valid RR interval.
The SD writer delays rows by up to one second and moves `is_r_peak=1` back to
the sample nearest `r_peak_timestamp_ms`, including across CSV segment edges.

## QTDB MLII replay

From the repository root, run:

```powershell
.\.venv\Scripts\python.exe experiments\firmware_qtdb_benchmark.py
```

This compiles the actual `ecg_signal.c` and `SBfilter.c` using host GCC and
replays each full record without resetting between 10-second segments. Only
the channel explicitly named MLII in the QTDB header is used. In `sel114`,
this is channel 1. No other lead is substituted. `selection.json` lists all
included records and exclusions; `per_record.csv` preserves every result.

On the 13 locally available MLII records with `.atr`, the current host run
reports TP=14,108, FP=19, FN=48: Se=99.6609%, PPV=99.8655%, F1=99.7631%,
and event ACC=TP/(TP+FP+FN)=99.5273%. These are pooled results; individual
records can be below 99%. They are not conventional sample accuracy with TN.

Protocol: resample QTDB 250 to 360 Hz, simulate causal first-order 0.5 Hz
AC coupling, use fixed 200 ADC counts/mV around 2048 and 12-bit clipping,
then replay the causal firmware. No artificial noise is added; original
recording noise remains. One-to-one event matching uses unmodified `.atr`
timestamps and ±50 ms. Source hashes are saved with the result.

This is a development benchmark, not a patient-held-out validation or an
on-device ADC/BLE/SD test. Full-record annotation coverage still needs a
separate audit. AFE gain and analog transfer function are modeled, not
measured. Do not generalize this result to all QTDB leads or real wearable
signals. The rejected non-MLII records remain documented in the selection
manifest, following the requested MLII-only scope.
