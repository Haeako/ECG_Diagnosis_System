# ECG Live Monitor V1 — acceptance matrix

Verified 2026-10-24 (Asia/Bangkok). **Software/mock acceptance PASS; physical acceptance BLOCKED.** The full hardware Definition of Done is not claimed.

| Requirement | Result | Evidence |
|---|---|---|
| Existing firmware behavior preserved | PASS | No firmware C/H edits. BLE structs/UUIDs/commands checked in `ecg_ble.c`; queue/downsample behavior checked in `ble_transmit.c`. |
| Original Gateway CLI/self-test | PASS | `edge_gateway.py --self-test`: `PASS BLE protocol v1: batch=20 event=20`. Original flags, legacy tables and Windows BLE options retained. |
| Phase 1 before dashboard polish | PASS | Initial 18 backend tests; browser minimum Connect/START/STOP/Canvas passed. `outputs/live-monitor-phase1.png`. |
| Binary decoding/endianness/field offsets | PASS | `test_wire_offsets_and_signed_samples`; 20-byte layouts, signed samples, telemetry fields. |
| Sequence rollover/loss/duplicate rejection | PASS | `test_sequence_rollover_loss_and_rejected_packets_do_not_advance`. Loss is a packet count, not just a mismatch flag. |
| Sample/event timestamps | PASS | `test_clock_rollover_and_backdated_peak`; uint32 rollover, backdated R peaks, clock-reset rejection. |
| Malformed/unsupported packets | PASS | Ten invalid-packet cases; unsupported version/type, length, count, period, flags, SQI. Invalid packets do not mutate sequence state. |
| Valid vs SQI-invalid/insufficient metrics | PASS | `test_sqi_invalid_and_af_screening`; sentinel 255 → null, BPM/RR suppressed when unusable. UI also marks stale results unavailable. |
| AF screening semantics | PASS | `af_valid=null` because firmware does not transmit validity; unknown negative, raw-positive vs suspected flags, research/non-diagnosis notice. |
| Discovery/GATT subscription/commands (mocked) | PASS | `test_real_adapter_path_uses_existing_finder_and_gatt_uuids`; real adapter branch with injected client/finder validates UUIDs and write-without-response. |
| One connection owner/duplicate connect | PASS | Idempotent connect, async lifecycle lock, one notification subscription verified in `test_vertical_slice_and_sessions`. |
| Connection/subscribe/command failures | PASS | Cleanup and failed-command partial-session tests; bounded connection, subscription and command waits. |
| WebSocket/origins | PASS | End-to-end TestClient and browser tests; disallowed HTTP/WS origins rejected. Slow consumers have bounded queues and explicit gaps. |
| Queue overflow/stale callbacks | PASS | `test_ingress_overflow_is_counted_and_stale_callbacks_ignored`: 600 notifications → 512 accepted, 88 explicit drops; obsolete epoch ignored. |
| Disconnect/reconnect/cancel retry | PASS | Link-loss session marked partial, reconnect has one subscription, no START auto-replay, explicit disconnect prevents pending retry. |
| SQLite/restart/partial sessions | PASS | Store/list/pagination/export/delete tests; stale recording recovered as partial. Active session deletion/export rejected. |
| Four functional pages | PASS | Browser E2E navigates Live Monitor/Sessions/Diagnostics/Settings and performs page actions. |
| Demo/real normalized data compatibility | PASS | 3 Vitest tests; deterministic Demo and real WebSocket transport both run through ECGStore/Canvas in E2E. |
| Theme/navigation/display pause while recording | PASS | E2E verifies frozen canvas while revisions increase, Light/Dark/System, unchanged server session ID and rising samples after navigation/theme change/reload. |
| Canvas/gaps/R-peak markers/bounded buffers | PASS | Store gap and 4500-sample bound tests; screenshot/visual inspection; markers use event time. Window options 5/10/20/30 s. |
| Sessions/replay/export/delete | PASS | Browser Demo JSON and Gateway CSV downloads parsed; paged playback, restart/pause, deletion and backend JSON/CSV verified. |
| Responsive light/dark UI | PASS | Desktop 1440 px and mobile 390 px screenshots inspected; mobile document has no horizontal overflow. |
| Frontend production build | PASS | `npm --prefix web run build`: TypeScript + Vite success, 30 modules; `web/dist/` generated. Browser E2E rerun against production preview. |
| Browser runtime errors | PASS | E2E completed with zero captured JavaScript page errors. |
| Full Python regression suite | PASS | `.venv\Scripts\python -m pytest tests -q`: **57 passed**, 3 upstream deprecation warnings, 27.31 s. Includes 22 new Gateway tests. |
| Final targeted Gateway checks | PASS | 22 passed after sharing Windows client options; legacy self-test passed again. |
| Frontend unit suite | PASS | `npm --prefix web test`: **3 passed**. |
| Accelerated 30-minute synthetic data soak | PASS | 38,250 packets, 180,000 samples persisted; fast consumer received all 38,250; 0 missing/invalid/ingress drops. 126.889 s wall time. Intentionally stalled consumer caused 38,656 discarded queued messages (includes status), queue remained bounded at ≤128. `outputs/live-monitor-soak.json`. This is **not** a 30-minute physical or wall-clock BLE test. |
| Physical ESP32 discovery | BLOCKED | Windows BLE scan observed 9 advertisement entries, no matching ECG-Monitor name/service. `outputs/live-monitor-hardware-discovery.json`. |
| Actual ESP32 GATT notifications and changing waveform | BLOCKED | No matching physical device available. Mock evidence cannot establish this. |
| Actual BPM/SQI/AF telemetry | BLOCKED | Requires actual device and appropriate firmware windows; BLE v1 cannot independently prove AF validity. |
| Physical START/STOP and microSD state | BLOCKED | Requires device observation; command write success is not firmware acknowledgment. |
| Physical disconnect/reconnect | BLOCKED | Requires actual device. |
| Save/replay/export a real recording | BLOCKED | Only synthetic recordings available in this run. |
| 30-minute real streaming stability | BLOCKED | Must run with actual ESP32 and save wall-clock start/end plus counters and recording. |
| Qwen independent review | BLOCKED | Configured `qwen_review` returned local Ollama unavailable/timed out. No retry/reinstall/reconfiguration; Codex continued verification. |

## Failures encountered and resolved

- Initial frontend build caught JavaScript exponentiation syntax; fixed and rebuilt successfully.
- Initial browser E2E tried Connect while the prior Phase 1 mock connection was still active; the test now verifies a mock server and explicitly disconnects it at setup. Both development and production E2E passed afterward.
- Initial regression collection used system Python missing `wfdb`; installed declared project dependencies in `.venv` and reran the full suite successfully.

No unresolved software test failures. Upstream warnings: two WFDB/Pandas deprecations and one Starlette TestClient/httpx deprecation; no warnings suppressed.

## Physical sign-off procedure

1. Flash/use the existing compatible firmware, enable Windows Bluetooth, connect the ECG input safely, and start the **non-mock** Gateway.
2. Save discovery result and device identity; Connect and verify notifications in Diagnostics.
3. START; observe device recording behavior and real changing ECG. Save actual BPM/RR, SQI and any AF telemetry, including unavailable/insufficient states.
4. Record wall-clock start/end for at least 30 minutes. Save Gateway state/counters before and after: packets, samples, missing packets, invalid packets, receive rate, ingress/WS drops, reconnects, errors. Report observed values, never assumed zero loss.
5. STOP; verify device stopped, end the local session, replay and export JSON/CSV. Compare exports to the stored packet count.
6. Disconnect/reconnect physically, verify subscription and new session behavior; test interrupted recording recovery.
7. Attach evidence and change only the physically verified rows from BLOCKED to PASS/FAIL.
