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

ADC (360 Hz) -> streaming 10-second SQI v1 -> SD recording and BLE
```

Core files:

- `main/task/Core1/ECGPipeline/ExFeature/ExtractFeature.c`: filtering,
  R-peak detection, and RR/BPM;
- `main/task/Core1/ECGPipeline/ecg_pipeline.c`: ADC processing task,
  buffering, SD writer, and BLE startup;
- `main/task/Core0/StateMachine/StateMachine.c`: INIT/IDLE/RECORD state
  machine, commands, and health checks;
- `main/task/Core0/BLETransmit/ble_transmit.c`: BLE transmit task;
- `main/components/esp_ble/ecg_ble.c`: BLE GATT transport.

Runtime ownership is split by CPU core: Core 0 runs the application state
machine, SD writer, NimBLE host, and BLE transmitter; Core 1 runs the 360 Hz
ADC/filter/BPM/SQI processing task. `main/main.c` is only the bootstrap entry;
the control implementation lives in `task/Core0/StateMachine/StateMachine.c`.

The state machine is the sole owner of application mode. GPIO, BLE, pipeline,
and health-check producers post facts through `state_machine_post_event()`;
they cannot request a target mode. The supervisor validates each event against
the current mode and applies the resulting transition. Pipeline workers are
created once and block on their FreeRTOS queue/semaphore while idle. On STOP,
acquisition is halted first and the SD worker drains its queued and delayed
R-peak records before acknowledging shutdown; only then is the card unmounted
and the supervisor publishes IDLE.

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
timestamp_ms,raw_adc,centered,notch_49_51hz,bandpass_5_18hz,ma_qrs_100ms,threshold,is_r_peak,bpm,sqi_score,sqi_status,sqi_rule_mask,sqi_hr_bpm,sqi_max_rr_ms,sqi_rr_ratio_x1000,sqi_template_corr_x1000,sqi_window_id,sqi_finalize_us,af_valid,af_probability_x10000,af_score,af_raw_prediction,af_confirmed,af_window_id,af_event_id,af_new_window,af_new_event
```

`bpm` holds the latest valid RR-derived value between beats so it can be
plotted as a time series; it is zero until the first valid RR interval.
The SD writer delays rows by up to one second and moves `is_r_peak=1` back to
the sample nearest `r_peak_timestamp_ms`, including across CSV segment edges.

SQI v1 is an Orphanidou-style bounded-state gate evaluated on non-overlapping
10-second windows. It combines physiological HR/RR rules with average
beat-template correlation. `sqi_score` is the correlation on a 0--100 scale;
255 means insufficient data. `sqi_status` is 0 unusable, 1 usable, and 2
insufficient. The Orphanidou result currently runs in shadow mode: it is logged
but does not suppress ECG samples or change an AF decision. `sqi_rule_mask`
uses bits 0..3 for HR, max-RR, RR-ratio, and template-correlation pass states;
ratio and correlation telemetry are scaled by 1000. `sqi_window_id` identifies
the completed 10-second window and `sqi_finalize_us` records its finalize time.
The usable threshold is 66. This shadow result does not suppress BPM; all
values are exported so downstream software can apply its own policy. It is not
a clinical validation result.

AF screening runs independently on the device. It uses the frozen 80-tree
INT8 Extra Trees model and 30 consecutive RR intervals, with inference every
15 new intervals. The five features are pNN50, five-bin Shannon entropy, SD2,
SD1/SD2, and median RR. `af_raw_prediction` is the current model threshold;
`af_confirmed` requires at least three positives among the latest four AF
windows. An invalid RR outside 250--2500 ms clears the AF history so a stale
event cannot survive a detector discontinuity. This is a screening prototype,
not a clinical diagnosis.

The production C path was replayed end-to-end on six waveform-available AFDB
holdout records (17,932 pure-rhythm windows): raw BA 0.8878, confirmed 3-of-4
BA 0.8824, and probability AUC 0.9720. Confirmation reduced false-positive
windows from 558 to 391 while increasing false-negative windows from 1,191 to
1,368. See `benchmark_results/af_end_to_end_metrics.json`; this remains a
development result, not clinical validation.

Each recording chooses an unused 16-bit session token and writes 8.3-safe
files in the SD root: `EAB12.CSV` for events and `DAB12000.CSV`,
`DAB12001.CSV`, ... for 60-second full-rate segments. These names work when
FAT long-filename and directory creation are unavailable and prevent ordinary
uptime-based collisions after reboot. The journal contains
`AF_SUSPECTED`, `AF_ENDED`, `BAD_SQI`, and `SQI_RECOVERED` rows, so events are
preserved even when no BLE central is connected.

If the FAT directory cannot allocate a new entry, the firmware does not stop
in a retry loop or erase the card. It appends a mixed fallback stream to the
pre-existing `test.txt`, with `SAMPLE,` and `EVENT,` row prefixes. Cleaning or
reformatting that SD card restores the preferred separate-file layout.

Automatic recording is not part of the production state machine. After the
boot-time SD/ADC health check, the device remains in IDLE until the physical
GPIO0 button or a BLE `start` command is received. No menuconfig option can
silently restore boot-time recording. While recording, GPIO0 or a BLE `stop`
command stops the session and returns the device to IDLE.

The GPIO35 SD-card detect interrupt is confirmed in the state task only after
a 250 ms quiet interval. This prevents electrical edge bursts from filling the
transition queue while preserving the periodic health check and SD write-error
recovery paths.

The SD driver keeps separate buffered file handles for the full-rate ECG and
event journal; the ECG writer emits 32-row batches. On the tested 3,810 MB
SDHC card this sustained more than 65 seconds at 360 Hz across repeated
CSV rotations without a ring-buffer drop. This is a short hardware
smoke test, not a 24-hour endurance claim.

The binary header is configured for the board's detected 4 MB SPI flash, so
boot no longer emits the former “detected 4096k, image header 2048k” warning.

During RECORD, UART emits a 10-second `HEALTH` heartbeat containing effective
sample rate, ADC/corrected sample, BPM, SQI status/score/window, AF status/
probability/window and free heap. This makes standalone operation observable
without requiring a BLE central.

Hardware note: the AD8232/ECG front end was not connected during the current
board smoke test. Therefore the expected safe behavior is `sqi_status=0`
(unusable/bad quality), `sqi_score=0`, and no valid AF window. The un-gated QRS
detector can still emit spurious candidate peaks and BPM values on a floating
input; consumers must ignore BPM/AF whenever SQI is unusable. This verifies
that the quality gate rejects a disconnected input; it does not validate ECG
acquisition, BPM accuracy, or AF detection on a person.

The fail-safe invariant was rechecked over live UART on 18 October 2026. Two
successive 10-second windows reported `sqi=0/0`, `bpm=0`, and `af=0` at a
measured 361.0 Hz sample rate. Any `usable/good` SQI result while this front
end remains disconnected must be treated and reported as a fail-open defect.

A live 30-second BLE/SQLite capture later stored 2,695 waveform samples in
539 packets and eight event packets with zero sequence gaps. All eight events
were correctly `sqi_status=0`, `sqi_score=0`, and BAD-SQI flagged; AF remained
invalid. Five event packets also carried raw R-peak flags and two transient BPM
values (32 and 38), which are explicitly invalid because the SQI gate is bad.

BLE protocol v1 uses fixed 20-byte notifications. Packet type 1 contains five
downsampled morphology-preserving `corrected` Int16 samples plus sequence and
first timestamp. Packet type 2 contains BPM/RR, SQI, AF probability/score and
event flags. AF and bad-SQI event packets take priority over background
waveform delivery; full-rate 360 Hz samples remain on SD.

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
