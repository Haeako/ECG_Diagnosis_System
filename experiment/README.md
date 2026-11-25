# ECG experiments: independent module code

- BPM/: Python methods, preprocessing, metrics, private utils and ESP project.
- SQI/: Python methods, preprocessing, metrics, private utils and ESP project.
- AFib/: Python methods, preprocessing, metrics, private utils and ESP project.

Each ESP directory has its own main, C kernels, headers, build helper, workspace
and selected_methods.json. No ESP source or CMake file depends on a sibling
module or experiment/firmware. Copying an ESP folder is sufficient for the project
source; an installed ESP-IDF SDK/toolchain is still required.

In a module's ESP terminal:

```powershell
.\invoke_idf.ps1 -C . -B build_isolated build
```

See each module's ESP/README.md for method selection and flash/monitor commands.
Use the new build_isolated cache, because older build directories refer to the
removed shared firmware source.

Python entry points remain <module>/reproduce.py and use <module>.utils. Historical
datasets, cached results, model artifacts and audited references retain their
existing locations under experiment; source code is no longer shared between
modules. No benchmarks or device runs were repeated during this separation.
Random seed remains fixed at 42 in each module's utils/reproducibility.py.

## ESP scope: latency + memory only

The three ESP projects contain no conditioning filters, BLE, acquisition, SD or
filesystem writes. Fixed inputs are compiled in; results go to UART only.
Use ESP/build_latency_memory for the next build. BPM/SQI kernels with conditioning
removed are explicitly marked as latency/memory variants. Old accuracy tables
and old device timing measurements do not validate these modified kernels.
Original native sources for Python accuracy stay under each module's
native/reference_main; ESP does not link them. No benchmark/device run repeated.
