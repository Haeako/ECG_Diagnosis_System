# ECG Live Monitor V1 — implementation checkpoint

Updated: 2026-10-24 (Asia/Bangkok).

## Confirmed source behavior

- Firmware unchanged. BLE UUIDs, packed 20-byte structs and commands verified against `ecg_ble.c` and `ble_transmit.c`.
- Acquisition is 360 Hz; BLE carries downsampled `corrected` counts, nominal 10 ms periods. Per-sample times within a batch are estimates, not full-rate ADC timestamps.
- SQI: 0 unusable, 1 usable, 2 insufficient; score 255 is insufficient. BLE v1 omits AF validity, AF window ID and command/recording acknowledgment. Negative AF results cannot be established from the packet alone.
- R-peak events may arrive with earlier timestamps; decoder handles this and uint32 clock/uint16 sequence rollover. Reconnect starts a new timestamp epoch.

## Phase checkpoints

1. PASS: existing CLI decoder extracted into shared `PacketDecoder`; original CLI tables and flags retained. Byte-level mock → Gateway → WebSocket → minimal browser canvas verified before full dashboard work. Initial backend suite: 18 passed. Screenshot: `outputs/live-monitor-phase1.png`.
2. PASS: React/TypeScript/Vite dashboard, four pages, normalized store, canvas, Demo transport, SQLite sessions, replay and exports. Production build and browser E2E passed.
3. PASS: bounded ingress/subscriber/browser queues, gap markers, reconnect/backoff, origin checks, session crash recovery. 30 simulated device minutes: 38,250 packets/180,000 samples persisted; no fast-consumer losses, ingress drops or invalid packets; stalled subscriber stayed bounded. This is accelerated synthetic evidence, not physical stability.
4. BLOCKED for physical hardware: Windows BLE scan observed 9 advertisements, no matching ECG device/service. Evidence: `outputs/live-monitor-hardware-discovery.json`. No physical test marked PASS.

## Current continuation

Software work is complete. Verification: 57 Python tests, 3 frontend tests, legacy self-test, production build and browser E2E passed. Three upstream deprecation warnings remain. See `ACCEPTANCE_MATRIX.md` for evidence and `LOCAL_SETUP.md` for commands/API semantics. Next step is physical ESP32 acceptance, including a 30-minute wall-clock capture and actual session export. Local Qwen MCP returned unavailable/timed out; continued without retry/reconfiguration.

The current session leaves production preview on http://127.0.0.1:4173 and a non-mock Gateway on http://127.0.0.1:8765. No device connection is initiated automatically. Check processes/ports before restarting; these servers may stop when the host session ends. Port 5173 was already occupied before this work, so it was not used or stopped.

## Files and architecture

- `firmware/esp32/tools/edge_gateway.py`: existing CLI + shared BLE decoder + optional `--serve` entry point.
- `firmware/esp32/tools/live_gateway.py`: single BLE owner, binary mock, SQLite session store, HTTP/WebSocket API.
- `web/`: independent application. Real Gateway sessions are canonical SQLite data. Browser Demo sessions are bounded volatile memory, explicitly labeled.
- `tests/test_live_gateway.py`: protocol, lifecycle, storage, WebSocket, origin and recovery checks.

No firmware, DSP, SQI, AF model, microSD or FreeRTOS changes.
