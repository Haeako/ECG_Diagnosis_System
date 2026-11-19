# ESP32 C module replay

Runs the prepared BPM C modules on 66 fixed 10 s input vectors from the EDB
holdout timing experiment. No acquisition, SQI or AF gate is linked. Workspace
is allocated in internal heap for one method at a time, freed before the next.
The firmware prints sample peak indices, one hot detector-call latency per
case, workspace size and resource telemetry. UART printing is outside timing.
These timings exclude common AFE and RR/BPM computation. The host verifier
computes BPM for the two 5 s windows from the captured ESP32 peak indices.

```powershell
.\server\BPM\.venv\Scripts\python.exe server/BPM/prepare_c_device_replay.py
powershell -NoProfile -ExecutionPolicy Bypass -File .cache/invoke_idf.ps1 -C firmware/BPM/replay -B build-bpm-module-replay -DECG_BPM_ALGORITHM=all build
# Or select one module, for example -DECG_BPM_ALGORITHM=fast_qrs
& C:/Espressif/tools/python/v6.0.1/venv/Scripts/python.exe server/device_profile.py --domain BPM --profile bpm-c-module-replay --build build-bpm-module-replay --output outputs/BPM/my_device_replay --port COM18 --capture-seconds 1800 --input-scope "66 selected EDB holdout vectors; C module numerical replay"
.\server\BPM\.venv\Scripts\python.exe server/BPM/verify_c_device_replay.py --output outputs/BPM/my_device_replay
```

Input and dispatch headers, source list and protocol are generated with hashes
from the completed host parity run. Reuse a fresh output directory per flash.
Keep the installed benchmark; restoration is not requested. Reset replays the
same vectors. Disable the task watchdog for these accelerated batch experiments;
the interrupt watchdog remains enabled. This does not validate live scheduling.
