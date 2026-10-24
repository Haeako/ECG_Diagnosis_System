@echo off
setlocal
title ECG Live Monitor - Launcher
pushd "%~dp0"
if errorlevel 1 exit /b 1

echo ==========================================
echo   ECG Live Monitor - Local BLE + Web
echo ==========================================
echo.

if not exist ".venv\Scripts\python.exe" (
    echo [ERROR] Python environment is missing. Run:
    echo   python -m venv .venv
    echo   .venv\Scripts\python -m pip install -r firmware\esp32\tools\requirements-gateway.txt
    goto :failed
)
where npm.cmd >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Install Node.js 22 or newer, then reopen this file.
    goto :failed
)
".venv\Scripts\python.exe" -c "import bleak, fastapi, uvicorn, websockets" >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Gateway dependencies are missing. Run:
    echo   .venv\Scripts\python -m pip install -r firmware\esp32\tools\requirements-gateway.txt
    goto :failed
)
if not exist "web\node_modules\vite\bin\vite.js" (
    echo [ERROR] Web dependencies are missing. Run:
    echo   npm --prefix web ci
    goto :failed
)
if /i "%~1"=="--check" (
    echo [OK] Python, Gateway dependencies, Node/npm and Vite are available.
    goto :done
)
if not exist "outputs" mkdir "outputs"
if not exist "web\dist\index.html" (
    echo Building the web application...
    call npm.cmd --prefix web run build
    if errorlevel 1 goto :failed
)

call :port_open 8765
if errorlevel 1 (
    echo Starting the real BLE Gateway...
    start "ECG Gateway - keep this window open" "%ComSpec%" /k call ".venv\Scripts\python.exe" firmware\esp32\tools\edge_gateway.py --serve --database outputs\ecg-live.sqlite3
) else (
    echo Port 8765 is already in use. Checking the Gateway...
)
call :wait_gateway
if errorlevel 1 (
    echo [ERROR] No real ECG Gateway responded on port 8765.
    echo Check the Gateway window. If a mock Gateway or another program uses
    echo this port, stop it first and run this file again.
    goto :failed
)

call :port_open 4173
if errorlevel 1 (
    echo Starting the web application...
    start "ECG Web - keep this window open" "%ComSpec%" /k call npm.cmd --prefix web run preview -- --port 4173 --strictPort
) else (
    echo Port 4173 is already in use. Checking the web application...
)
call :wait_web
if errorlevel 1 (
    echo [ERROR] ECG web application did not respond on port 4173.
    echo Check the ECG Web window or the program using this port.
    goto :failed
)

echo.
echo [OK] Web:     http://127.0.0.1:4173
echo [OK] Gateway: http://127.0.0.1:8765
echo In Settings, select Real Gateway BLE and click Apply connection.
echo Then use Live Monitor: Connect, START.
echo Keep the Gateway and Web windows open. Use Ctrl+C in each to stop.
start "" "http://127.0.0.1:4173"
goto :done

:port_open
".venv\Scripts\python.exe" -c "import socket,sys; s=socket.socket(); s.settimeout(1); result=s.connect_ex(('127.0.0.1',int(sys.argv[1]))); s.close(); sys.exit(0 if result==0 else 1)" %1
exit /b %errorlevel%

:wait_gateway
powershell.exe -NoProfile -Command "foreach ($attempt in 1..30) { try { $s = Invoke-RestMethod 'http://127.0.0.1:8765/api/v1/state' -TimeoutSec 1; if ($s.v -eq 1 -and $s.type -eq 'state' -and $s.source -eq 'ble') { exit 0 }; exit 1 } catch { Start-Sleep -Milliseconds 300 } }; exit 1"
exit /b %errorlevel%

:wait_web
powershell.exe -NoProfile -Command "foreach ($attempt in 1..30) { try { $r = Invoke-WebRequest 'http://127.0.0.1:4173' -UseBasicParsing -TimeoutSec 1; if ($r.Content -match '<title>ECG Live Monitor</title>') { exit 0 }; exit 1 } catch { Start-Sleep -Milliseconds 300 } }; exit 1"
exit /b %errorlevel%

:failed
echo.
echo See docs\LOCAL_SETUP.md for setup instructions.
pause
popd
exit /b 1

:done
popd
exit /b 0
