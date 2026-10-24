# ECG Live Monitor V1 — Windows localhost

Run PowerShell commands from the repository root. Python 3.10+ and Node.js 22+ are required. The existing research/firmware build is independent of the web application.

## Install

```powershell
python -m venv .venv
.venv\Scripts\python -m pip install -r firmware\esp32\tools\requirements-gateway.txt
npm --prefix web ci
npm --prefix web run build
```

The production static output is `web/dist/`. It is generated locally and already covered by the repository's existing `dist/` ignore rule. No external fonts, CDN assets or cloud service are required at runtime.

## Start with a real ESP32

After installing dependencies, double-click `start_web.bat` in the repository
root. It starts the real BLE Gateway and production web preview, waits for both
to respond, then opens the browser. Existing matching servers are reused;
an occupied port belonging to a mock Gateway or another app is reported as an
error. Keep the two server windows open; use Ctrl+C in each to stop them.
`start_web.bat --check` checks prerequisites without starting servers.
The launcher builds the frontend if `web/dist/index.html` is missing. After
editing frontend source, run `npm --prefix web run build` to refresh the build.

Terminal 1 (keep running):

```powershell
.venv\Scripts\python firmware\esp32\tools\edge_gateway.py --serve --database outputs\ecg-live.sqlite3
```

Terminal 2:

```powershell
npm --prefix web run preview -- --port 4173 --strictPort
```

Open http://127.0.0.1:4173. In **Settings**, select **Real Gateway (BLE)**, keep `http://127.0.0.1:8765`, then **Apply connection**. Go to **Live Monitor → Connect → START**. Use Diagnostics → Scan devices to check discovery. Firmware normally advertises as `ECG-Monitor`; the BLE device name is configurable in Settings.

Windows Bluetooth must be on. Close other BLE clients holding the ESP32 connection. Use only one Gateway process/worker for a given device/database. Do not run the legacy CLI against that device at the same time.

START sends `start` and opens a local Gateway session. STOP sends `stop` and closes the local session. BLE v1 does not confirm the firmware recording/microSD state. Check **Actual streaming** and the physical device when verifying commands. Pressing GPIO0 on the board can start firmware streaming independently; a Gateway session opens only when START is requested through this Gateway.

Disconnect or unexpected link loss closes the local session as **partial**. Unexpected link loss triggers bounded automatic reconnect with backoff (1–30 seconds). Reconnection does not send START automatically or resume a closed session; inspect the state and explicitly START a new session. Restarting the Gateway recovers unfinished sessions as partial. Closing/reloading the browser does not stop Gateway recording.

## Demo and reproducible mocked hardware

The default **Deterministic Demo** runs in the browser and does not require Python. Click Connect and START. It uses the same normalized store and renderer. Its synthetic sessions are volatile, bounded to 8 sessions of 30 minutes each, and disappear on refresh/source change. Export anything needed before leaving Demo.

For the complete byte-level BLE → Python → WebSocket path, launch a separate mock Gateway:

```powershell
.venv\Scripts\python firmware\esp32\tools\edge_gateway.py --serve --mock --database outputs\ecg-mock.sqlite3
```

Choose **Real Gateway (BLE)** in the frontend to exercise the HTTP/WebSocket transport; the server reports `source=demo` and the UI visibly labels the mock data. Do not run real and mock Gateways on the same port.

## Development and verification

```powershell
npm --prefix web run dev -- --port 4173 --strictPort
.venv\Scripts\python -m pip install -r firmware\esp32\tools\requirements-gateway-test.txt
.venv\Scripts\python -m playwright install chromium
.venv\Scripts\python firmware\esp32\tools\edge_gateway.py --self-test
.venv\Scripts\python -m pytest tests\test_live_gateway.py -q
npm --prefix web test
npm --prefix web run build
```

With the frontend on 4173 and **mock** Gateway on 8765 running:

```powershell
.venv\Scripts\python tests\e2e_live_monitor.py
.venv\Scripts\python tests\live_monitor_stability.py
```

E2E creates/records/deletes test sessions and disconnects the mock device; it checks `source=demo` before doing so. Browser screenshots go to `outputs/live-monitor-e2e/`. The accelerated soak checks 30 **simulated device minutes**, SQLite and bounded queues, not 30 minutes of real BLE stability. Results go to `outputs/live-monitor-soak.json`.

Existing research regression tests require the project dependencies:

```powershell
.venv\Scripts\python -m pip install -e .
.venv\Scripts\python -m pytest tests -q
```

Legacy CLI remains available:

```powershell
.venv\Scripts\python firmware\esp32\tools\edge_gateway.py --name ECG-Monitor --start --duration 60 --database outputs\legacy-cli.sqlite3
```

## Configuration and limits

- Bind defaults to `127.0.0.1:8765`. `--host`, `--port`, `--database`, `--origin` are configurable. Allowed origins default to localhost/127.0.0.1 on 5173 and 4173. `--origin` is repeatable and **replaces** defaults. Origin matching is exact.
- Frontend Gateway URL is persistent; `https://` maps automatically to `wss://`. Future Tailscale/private reverse-proxy use only needs host, URL, TLS/proxy and allowed-origin configuration. Authentication and remote deployment are not implemented; this release is for localhost.
- Static hosting serves only the frontend. It never replaces the running Python Gateway. Serve the static build over HTTP, not `file://`.
- Canonical real/mock-Gateway recordings live in SQLite (`live_sessions`, `live_packets`). Existing legacy `ecg_samples` and `events` tables are preserved. JSON exports preserve normalized packets and raw telemetry fields; CSV has separate waveform/event rows and no synthesized missing samples.
- Samples and R-peak times are in device milliseconds. Samples within batches use the transmitted 10 ms period; actual upstream downsampling can be irregular. Values are corrected ADC counts, not calibrated mV. Firmware acquisition is 360 Hz; BLE output is nominally ~100 Hz.
- AF validity is not present in BLE v1. Raw probability is exported with `af_valid=null`/unknown. Positive flags are screening signals; zero flags do not establish a valid negative result. Missing event packets cannot be counted because events have no sequence field.
- Ingress queue: 512 packets; per-browser queue: 128 messages; display: 4500 samples and 200 events; Gateway log: 100 entries. Slow-client drops produce an explicit gap message. Samples and events are never interpolated over loss.
- Replay is paged (500 packets), supports pause/resume, restart and 0.5–4× speed. It uses a separate store and saved receive timing; navigation away stops playback only. Recordings and live transport continue.
- Sessions use one SQLite commit per received packet. Database/disk errors stop local recording and appear in Diagnostics. Back up the SQLite file with the Gateway stopped (or use SQLite backup tools).

## Physical acceptance

Use the checklist in `ACCEPTANCE_MATRIX.md`. With real hardware, record diagnostics before and after a 30-minute wall-clock capture, including packet/sample counts, sequence losses, invalid packets, queue drops, reconnects, errors and exported session. Confirm actual waveform and telemetry on the connected board. Do not substitute synthetic results for those checks.

## API v1

All routes are prefixed with `/api/v1`. Browser origins are validated for HTTP and WebSocket. Commands use JSON bodies. Documentation is also available on the Gateway `/docs` page.

| Method/path | Behavior |
|---|---|
| `GET /state` | Connection, recent streaming, last command sent, local session ID, counters, receive rate, bounded logs. |
| `GET /devices` | Scan matching advertised ECG service/name (or explicit mock device). |
| `POST /connect` | `{"name":"ECG-Monitor"}`; idempotent connection and subscription. |
| `POST /disconnect` | Explicit disconnect, cancel reconnect, close session as partial. |
| `POST /command` | `{"command":"start"}` or `{"command":"stop"}` only. |
| `WS /ws` | Outgoing envelopes with `v:1` and `type:state/waveform/event/gap`. Commands use HTTP, not WebSocket. |
| `GET /sessions` | Session metadata, status and packet counts. |
| `GET /sessions/{id}/packets?after=0&limit=500` | Ordered packets, each with a cursor. Maximum page size 2000. |
| `GET /sessions/{id}/export?format=json` | Stream JSON or CSV for a stopped session; includes source/validity semantics. |
| `DELETE /sessions/{id}` | Delete stopped session and associated packets. |

Waveform messages carry `source`, `epoch`, `received_utc`, `sequence`, `period_ms`, `missing_packets` and `samples:[{t,v}]`. Event messages carry timestamp `t`, R-peak/AF/SQI flags, raw and validity-gated BPM/RR, SQI, raw AF probability/score/event ID and `af_valid:null`. Epoch changes reset display buffers and prevent joining different device timelines. A `gap` message explicitly breaks the next rendered segment.

`connection=connected`, `streaming=true`, `command_sent`, and `session_id` mean different things: connected GATT, waveform received within 2 seconds, a successful command write without firmware acknowledgment, and active local SQLite capture respectively. HTTP errors use 403 for rejected origins, 404 for missing sessions, 409 for invalid current state, 422 for invalid input, and 503 for scan/connect failures.
