# Project handoff

## Current Context

2026-11-01 battery/LED work: calibrated GPIO32/ADC1_CH4 battery monitoring is
implemented through BLE type 3, shared decoder, Gateway storage/WebSocket and
web battery component. Hardware evidence and configuration/protocol/source-review
details are in `docs/BATTERY_MONITOR.md`; read that instead of rescanning KiCad.
The 100k/100k switched divider is verified from schematic/netlist/current PCB.
Percentage is a coarse voltage estimate; charging/presence are unknown. Calibration
unavailable or stale/invalid/disconnected telemetry shows unknown. PCB voltage,
ECG timing/mux effects, charging/load and physical recovery checks remain PENDING.
No flash, commit or push. Local Qwen unavailable; no retries.

Prior LED task changes remain: active-low R25/G27/B26 reflect committed state
(FAULT red, PREPARE R/G/B 150 ms, IDLE green, RECORD blue); one State Task helper,
button GPIO16 and optional `GPIO_NUM_NC` LED. User SD_DET active-low and all other
SD/BLE/DSP changes preserved. `dependencies.lock` retained exactly as found
(SHA256 D9A8BB2C715016E175629354FFDC0E7F200B7485B80A5485FDFAF3F6D54E6464).
Battery sampling adds no task/timer, uses the existing ECG task and serialized
ADC1 ownership; finite 250 ms wait ensures sampling resumes after STOP.
Phase 2 extracted only BatteryStatus UI and documented deferred structural work;
no archive deletion, mass rename or dependency upgrade.
Validation: ESP-IDF v6.1 build PASS (0xb3940 bytes, 52% app partition free),
61 pytest tests PASS (includes production battery/ADC C harnesses), four Vitest
tests PASS, TypeScript/Vite build PASS, Chromium battery Light/Dark/mobile and
failure/stale checks PASS after extraction; legacy CLI self-test PASS.

The dated entries below describe earlier work; their firmware-unchanged statement
and test counts predate the November battery/LED changes.

2026-10-24 ignore-policy follow-up: user authorized updating root `.gitignore`.
Added Node dependencies/build metadata, Python caches, runtime SQLite databases
and sidecars, local environment values (template exceptions), browser test outputs
and the local BUTQDB archive. Source, package-lock.json, setup scripts and research
CSV reports remain eligible for Git. Existing tracked files are not untracked by
these rules; `butqdb.zip` was already tracked and was left in the index.

2026-10-24 launcher follow-up: root `start_web.bat` starts/reuses the real BLE
Gateway (8765) and production preview (4173), validates HTTP readiness and opens
the browser. `--check` validates prerequisites only. Details in LOCAL_SETUP.md.

2026-10-24: ECG Live Monitor V1 software implementation complete; physical hardware acceptance BLOCKED (BLE scan saw 9 advertisements, no matching ECG device). All details/evidence are in `docs/PROJECT_STATE.md`, `docs/ACCEPTANCE_MATRIX.md`, and `docs/LOCAL_SETUP.md`.

- Architecture: existing `edge_gateway.py` CLI and shared `PacketDecoder`; optional `--serve` loads `live_gateway.py` (one BLE owner, FastAPI/WS, SQLite). Firmware is unchanged.
- Frontend: `web/` React/TS/Vite; Canvas bounded store, 4 pages, browser Demo and Gateway transport, paged replay. Real recordings live only in SQLite. Demo browser sessions are volatile and labeled.
- Protocol caveats: `corrected` counts, nominal 10 ms reconstructed BLE sample times vs 360 Hz acquisition; SQI 255 insufficient; BLE v1 omits AF validity and firmware command acknowledgment; no valid negative AF conclusion inferred.
- Validation: 57 Python tests, 3 Vitest tests, legacy CLI self-test, build, development and production browser E2E passed. Accelerated 30-device-minute soak: 38,250 packets, 180,000 samples; physical 30-minute test still blocked.
- Current run left preview 4173 and non-mock Gateway 8765; check availability before starting more processes. Do not disturb pre-existing port 5173. Temporary mock data/screenshots are under ignored `outputs/`.
- Next: connect actual ESP32, execute physical acceptance checklist, capture actual telemetry, commands/device behavior, reconnect, real session export and 30-minute counters. Do not mark hardware PASS from mocks.
- Qwen review MCP unavailable; no retries/reconfiguration performed.

## Relevant memory entries

- `docs/PROJECT_STATE.md`: current scope and phase evidence.
- `docs/LOCAL_SETUP.md`: reproducible setup, API contract and runtime constraints.
- `docs/ACCEPTANCE_MATRIX.md`: test results and physical acceptance checklist.

## Recent Handoffs

None; project memory initialized for this implementation. No previous project memory existed.
