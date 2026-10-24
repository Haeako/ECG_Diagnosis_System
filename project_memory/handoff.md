# Project handoff

## Current Context

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
