# Gateway and live web

Run from `server_size/`. Requirements: Python 3.10+ and Node.js 22+.

```powershell
python -m venv .venv
.venv/Scripts/python.exe -m pip install -r requirements.txt
npm --prefix web ci
```

Start the API and real BLE gateway:

```powershell
.venv/Scripts/python.exe gateway/edge_gateway.py --serve --database runtime/ecg-live.sqlite3
```

In a second terminal:

```powershell
npm --prefix web run dev -- --host 127.0.0.1
```

The API listens at `http://127.0.0.1:8765`. Alternatively, use
`start_web.bat` on Windows to build and start the API and web preview.
The launcher expects `.venv/` and installed web dependencies in this folder.

For a hardware-independent check, add `--mock` to the API command.
Real mode requires the ECG-Monitor BLE peripheral.
Runtime SQLite files are created locally and ignored by Git.

Existing protocol, HTTP and WebSocket tests:

```powershell
.venv/Scripts/python.exe -m pip install -r requirements-dev.txt
.venv/Scripts/python.exe -m pytest tests -q
npm --prefix web test
npm --prefix web run build
```
