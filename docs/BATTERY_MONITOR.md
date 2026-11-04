# Battery monitor and source review — 2026-11-01

Software validation is separate from PCB acceptance. No board was flashed or measured.

## Hardware evidence

Inspected the adjacent hardware project (not modified):

- `../PCB/Portable_ECG/digital.kicad_sch`: actual battery circuit symbols, values and net labels.
- `../PCB/Portable_ECG/release/archive/JLCPCB_20261009/evidence/final.xml`: exported connectivity.
- `../PCB/Portable_ECG/Portable_ECG.kicad_pcb`: current footprint pad nets corroborate the export.

The verified chain is `BAT_P → U303 TPS22919 IN → OUT/BAT_SENSE_SW → R307 100k → VBAT_ADC → R308 100k → GND`. `VBAT_ADC` connects to U301 ESP32-WROOM-32U pad 8, **GPIO32 / ADC1_CH4**, and C306 100 nF to GND. ESP-IDF's ESP32 `soc/adc_channel.h` independently confirms GPIO32/channel 4. ECG remains ADC1_CH0.

R313/R314 are 10k/10k: `+3V3_GOOD → BAT_SENSE_EN → GND`. This enables U303 electrically (nominal 1.65 V); there is no MCU enable pin to toggle. U303 QOD is connected to VBAT_ADC. The switched divider ratio is 2:1, so 4.20 V battery gives approximately 2.10 V at the ADC. The divider/C306 time constant is approximately 5 ms. U303 ON threshold and QOD behavior are described in the [TI TPS22919 datasheet](https://www.ti.com/lit/ds/symlink/tps22919.pdf).

TP4056 charging/status nets drive charge LEDs, not ESP32 inputs. **Charging state and physical battery presence are unknown**; a charger can energize BAT_P without proving a cell is attached. Firmware must not infer either from voltage alone. The assembled PCB revision and actual component values still require bench confirmation.

## Computation and scheduling

- ESP-IDF ADC1, 12-bit, 12 dB attenuation, line-fitting calibration. Use eFuse calibration; if unavailable, the default is unavailable telemetry. A measured reference can be configured explicitly; there is no fabricated 3.3 V/4095 or nominal 1100 mV fallback. See [Espressif calibration guidance](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/adc/adc_calibration.html).
- `VBAT_mV = calibrated_ADC_mV × (Rtop + Rbottom) / Rbottom × gain_permille / 1000 + offset_mV`. All divider, board gain/offset, optional reference, empty/full endpoints and warning threshold are Kconfig settings (`CONFIG_ECG_BATTERY_*`). Default gain/offset are 1000/0.
- One conversion at most every 250 ms; average eight valid readings (~2 s). Reject ADC read/calibration errors, raw rails, calibrated ADC outside 100–2450 mV, and reconstructed battery outside 2800–4350 mV. Default divider plus battery bounds imply ADC 1400–2175 mV, inside the useful ESP32 attenuation range. These are software plausibility bounds, not electrical protection.
- Estimated percentage is `clamp(round(100 × (VBAT − 3300) / (4200 − 3300)), 0, 100)`. This deliberately coarse voltage-only approximation is **not a cell-specific discharge curve or fuel gauge**. No measured cell characterization is available. Load sag, charging surface voltage, temperature, aging and resistor tolerance affect the estimate. Display is prefixed `~`; no charging icon or remaining-runtime claim.
- Low warning: ≤15% for three filtered windows (~6 s); clear at ≥20%. Invalid/stale samples suppress warning; long sampling gaps discard the partial filter. This is an informational estimate, not a battery protection mechanism.
- No new task/timer. The existing ECG task polls while idle using a finite semaphore timeout, including after STOP. During recording it polls only after DSP/queue processing and only when elapsed time since the latest ISR is <1 ms, leaving >1.7 ms nominal margin at 360 Hz. Battery work is included in existing processing-time metrics. ADC access and START/STOP teardown share one mutex/unit owner; the battery never waits for the mutex. Battery channel errors do not disable the ECG channel. Calibration is used only by ECG task; BLE reads a short locked snapshot.
- ADC1 avoids ADC2/Wi-Fi ownership conflicts. ADC conversions still take finite CPU time. Scheduling margin is not proof of zero jitter or zero analog mux disturbance: both need measurement. See [Espressif oneshot ownership guidance](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/adc/adc_oneshot.html).

## Telemetry contract

Existing BLE UUIDs, commands, type 1 waveform and type 2 event bytes are unchanged. The existing BLE_TX task additionally sends at most one 20-byte battery packet every 5 s (~4 payload bytes/s), including while idle. A repeat is a low-rate freshness heartbeat.

New version 1, type 3 packet is little-endian `<BBBBIHBB8s>`:

| Offset | Field |
|---|---|
| 0, 1 | Version 1, type 3 |
| 2 | Flags: bit 0 valid, bit 1 low estimate |
| 3 | Reserved zero |
| 4–7 | Measurement timestamp, uint32 milliseconds |
| 8–9 | Battery millivolts, uint16 |
| 10 | Percentage 0–100, or 255 unavailable |
| 11 | Charging: 255 unknown |
| 12–19 | Reserved zeros |

Unavailable means flags=0, voltage=0, percent=255, charging=255. Snapshot older than 10 s is unavailable. Reserved/invalid values are rejected by the shared decoder. Battery decoding does not change waveform sequence or ECG clock anchors.

Path: battery snapshot → existing BLE data characteristic → shared `edge_gateway.PacketDecoder` → `live_gateway` → WebSocket/state → `ECGStore` → `BatteryStatus`. CLI SQLite adds `battery_samples`; live sessions reuse normalized packet storage, JSON export and paged replay. CSV appends four battery columns; existing column names/order are retained. Existing recordings and older firmware remain readable; absence of type 3 means unknown. External legacy clients that reject unknown packet types must be updated to skip type 3. CSV consumers assuming exactly 15 columns must accept the appended fields.

The UI shows an icon and `~N%`, supports both themes and small screens, and shows `—` for unavailable, invalid, disconnected, offline or >15 s old transport data. Low indication only uses valid recent firmware warning flags. Demo does not invent battery values. The header describes the currently connected device, not historical session battery state.

## Validation and remaining PCB work

Final software results: **PASS** ESP-IDF v6.1 build (binary 0xb3940 bytes, 52% app partition free), **PASS** 61 pytest tests including two production-C host harnesses, **PASS** four Vitest tests, **PASS** TypeScript/Vite build, **PASS** Chromium battery checks after UI extraction, **PASS** legacy CLI protocol self-test and `git diff --check`. Existing dependency deprecation warnings and a CMake preset warning remain; no dependency upgrades were made. Hardware validation is **PENDING**, not PASS.

Automated coverage includes actual production C compiled with host ADC/calibration stubs (conversion, clamping, filtering, calibration absent/failure, rail/out-of-range samples, tick wrap, staleness, low debounce/hysteresis, idle/active ADC ownership, lock contention and channel failure isolation); wire decoding; waveform sequence compatibility; Gateway WebSocket/state/SQLite/JSON/CSV; frontend age handling; and Chromium Light/Dark/mobile/unknown/low/invalid/disconnect/stale checks. Browser checks use synthetic telemetry, never claim PCB measurements. Run:

```powershell
.venv/Scripts/python.exe -m pytest -q
npm --prefix web test
npm --prefix web run build
.venv/Scripts/python.exe tests/e2e_battery.py
# In an exported ESP-IDF environment, from firmware/esp32:
idf.py build
```

PENDING on actual PCB: confirm BOM/revision and U303 enable; compare calibrated readings with DMM at multiple voltages; tune board gain/offset only from measurements; assess loaded/resting/charging behavior and battery absence; verify startup/STOP/recovery; compare ECG ADC levels, missed samples, jitter/processing maxima, SD drops and BLE throughput at 360 Hz with battery polling active. Do not claim hardware PASS or accurate capacity from mock/host tests.

## Safe source organization review

Reviewed repository-wide tracked/unignored source inventory, file sizes, exact-content duplicates, build definitions/imports and focused runtime boundaries (192 source files before UI extraction; generated dependencies/build output excluded). This is a structural review, not a line-by-line scientific algorithm audit.

| Area | Boundary / finding / decision |
|---|---|
| `firmware/esp32/main/drivers` | Hardware access. ADC unit lifetime stays here; battery voltage policy stays in `components/battery`. Existing ADC public entry points retained. |
| `main/components` / `task` | DSP, AF/SQI, metrics, BLE components plus task orchestration. Battery has explicit init/poll/snapshot API; existing task ownership preserved. |
| `ECGPipeline/ecg_pipeline.c` (~716 lines) | Acquisition, DSP orchestration, recording lifecycle and SD formatting coexist. SD writer extraction could help, but timing/concurrency scope is broader than battery; deferred. |
| `StateMachine.c` (~591 lines) | State transitions/recovery/LED ownership. Keep prior LED and PCB fixes intact. No new battery state-machine responsibilities. |
| `tools/live_gateway.py` (~520 lines) | Session storage, connection service and HTTP/WS routes. Existing shared decoder prevents another protocol implementation. Separating storage/routes is possible later; no new abstraction needed for three battery fields. |
| `web/src` | Extracted `BatteryStatus.tsx` from `main.tsx`; `model.ts` remains the shared packet/store/transport contract. App settings/replay/navigation and DemoTransport separation deferred. |
| `src/ecg_benchmark`, `experiments`, `tests` | Research library, reproducible experiments, verification. Scientific dependencies are used across workflows; no proven unused dependency removed. Gateway requirements remain separate from research extras. |
| `main/pipeline_module_test.py` | Host-only CSV analysis script under firmware main; moving to tools is plausible but may break documented paths. Deferred. |
| `af_model_int8.c` (17,773 lines) | Generated model coefficients, not a hand-written oversized module. Keep generated artifact together. |
| `docs/legacy_firmware_source`, `docs/legacy_firmware_tools` | Explicit historical archive. Four exact duplicate pairs found: Kalman.h, Queue.c, semaphore.c/.h versus active component paths. Archives are not linked by current CMake; retained for provenance, not treated as dead code. |
| Naming / optional code | `Queue.c`, `MQTT.c`, `StateMachine.c` versus snake_case is inconsistent. Network/queue files not in this build may serve other configurations; absence from one build is insufficient proof for deletion. No mass renames/removals. |

No dependencies upgraded; no files deleted or relocated. Phase 2 only extracted the directly affected UI component and documented findings, after Phase 1 software verification. Qwen exploration was attempted but local Ollama was unavailable/timed out; Codex completed review without agent retries.

Changed-file map (firmware paths below start at `firmware/esp32/`):

| Status | Files | Reason |
|---|---|---|
| Added | `main/components/battery/battery.c`, `battery.h` | Calibration, sampling/filter, estimated percentage and coherent snapshot |
| Added | `web/src/BatteryStatus.tsx` | Isolated battery presentation |
| Added | `tests/test_battery_firmware.py`, `tests/e2e_battery.py` | Host C and browser regression checks |
| Added | `docs/BATTERY_MONITOR.md` | Evidence, protocol, validation and structural review |
| Modified | `main/CMakeLists.txt`, `main/Kconfig.projbuild` | Register module and configurable constants |
| Modified | `main/drivers/adc/adc.c`, `adc.h` | One ADC owner, serialized teardown and battery channel reads |
| Modified | `main/task/Core1/ECGPipeline/ecg_pipeline.c` | Schedule optional battery work in existing task |
| Modified | `main/components/esp_ble/ecg_ble.c`, `ecg_ble.h`; `main/task/Core0/BLETransmit/ble_transmit.c` | Low-rate battery notification through existing BLE owner |
| Modified | `tools/edge_gateway.py`, `tools/live_gateway.py` | Shared decode, state, persistence and exports |
| Modified | `web/src/model.ts`, `main.tsx`, `style.css`, `model.test.ts`; `tests/test_live_gateway.py` | UI/store integration and regression coverage |
| Modified | `project_memory/handoff.md`, `project_memory/index.md` | Persist current project facts |
| Moved/deleted | None | Unrelated code/archives retained |

Earlier local `StateMachine.c` / `user_button.c` LED changes and `dependencies.lock` are preserved, not battery refactoring. The lock file SHA256 matches its pre-task value.
