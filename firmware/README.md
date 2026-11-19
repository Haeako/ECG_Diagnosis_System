# Isolated firmware domains

Each ESP-IDF project owns a domain, independent cache/sdkconfig and build image.
All reuse frozen algorithm source under `esp32/main/components/`; there is no
production acquisition application. Profiles outside the selected domain fail
at configuration time. Default CPU frequency is 160 MHz for these projects.

```powershell
idf.py -C firmware/BPM -B firmware/BPM/build build
idf.py -C firmware/SQI -B firmware/SQI/build build
idf.py -C firmware/AF -B firmware/AF/build build
```

Select a within-domain alternative with `-DECG_BUILD_PROFILE=<profile>`.
Flash and capture one project at a time on the connected ESP. Host metrics and
ESP32 timings must retain separate scope, input provenance and source hashes.
Shared source does not imply identical measurement scopes: SQI methods can need
external R-peaks, while AF kernel benchmarks can accept already formed features.
