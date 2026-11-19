# ESP32 firmware workspace

Use an ESP-IDF 6.x terminal with the ESP32 toolchain installed:

```powershell
cd device_size/firmware_ws
idf.py set-target esp32
idf.py build
idf.py -p COM18 flash monitor
```

Replace `COM18` with the board's port. Configure the board and battery calibration
with `idf.py menuconfig`. Recording starts with GPIO0 or the BLE `start` command.

The application includes acquisition, SD recording, BLE and the existing BPM,
SQI and AF runtimes. Algorithms and wire protocol are taken from production;
this restructure does not change their decisions.

Host battery verification, from repository root:

```powershell
python -m pip install -r device_size/requirements-dev.txt
python -m pytest device_size/tests -q
```

Build products and locally generated sdkconfig files are ignored by Git.
