# Standalone BPM QRS modules

These are independent C kernels for 360 Hz, 3600-sample (10 s) ECG windows.
Input is calibrated mV after the common AFE. Each call resets detector state.
Use a 5 s hop in the caller to reproduce the current BPM window schedule.
Do not link multiple implementations of the common `ecg_qrs_*` API into one image.

## Selection

```powershell
cmake -S firmware/BPM/modules -B .cache/bpm_modules -G Ninja -DECG_BPM_ALGORITHM=fast_qrs
cmake --build .cache/bpm_modules --target ecg_qrs_fast_qrs
```

Host targets are `ecg_qrs_<name>`; `ecg_bpm_selected` aliases the selected target.
Individual host targets can be disabled with `-DECG_BPM_BUILD_<name>=OFF`.

For ESP-IDF, add this directory to `EXTRA_COMPONENT_DIRS` before the IDF project
include and configure `idf.py -DECG_BPM_ALGORITHM=fast_qrs build`. The component
registers only the selected kernel and adapter. Include `qrs_module.h` in the
benchmark caller. This does not add acquisition, SQI, AF, or a production pipeline.

Names: `pan`, `elgendi`, `fast_qrs`, `panpp`, `wim_nguyen`, `wim_elgendi`,
`wim_pantompkins`, `wim_hamilton`, `wim_arteaga_falconi`, `wim_shaik`,
`wim_zhai`, `wim_xu`.

## Caller contract

Allocate `ecg_qrs_workspace_bytes()` bytes once, aligned for `double` (malloc
provides sufficient alignment), plus `float input[3600]` and
`uint16_t peaks[128]`. Keep the large workspace off the FreeRTOS task stack.
Call `ecg_qrs_detect(input,3600,peaks,128,workspace,bytes)`; `SIZE_MAX` is an
invalid-input/capacity error. No heap allocation, I/O, or peripheral dependency
occurs inside the module. Outputs are sample indices in the input window.
Apply chronological ordering and the benchmark RR/BPM convention in the caller;
do not replace an invalid BPM by zero.

## Numerical evidence and limits

Reproduce with:

```powershell
.\server\BPM\.venv\Scripts\python.exe server/BPM/check_c_modules_parity.py
```

The audit uses the 66 fixed input vectors from the latest host timing run:
three held-out EDB records at all 22 noise conditions. Each 10 s case compares
BPM in two 5 s windows. Relative error is `100*abs(C-Python)/abs(Python)`;
every jointly valid window must be <=1%, and validity masks must agree.
Both-invalid windows verify validity only, not numerical accuracy.
Peak arrays, per-case results, source hashes, and workspace sizes are retained
under `outputs/BPM/c_modules_parity_optimized_fft_20261116`.

This is host C/Python parity, not a guarantee for all ECG or device numerical
parity. Flashing, ESP32 timing, peak alignment and full-cohort C accuracy are
separate checks. The 10 ms admission screen uses the maximum of 198 hot host
calls, not their mean. Python process memory is not ESP32 workspace.

The local Pan, Pan++, Shaik and Xu variants use double intermediate state to
reduce differences from NumPy/SciPy. They consume more RAM and may execute
slower on ESP32. Non-overlapping buffer lifetimes share storage in Pan++;
Shaik stores piecewise segment thresholds compactly and reuses dead input
storage. Xu uses a radix-2/3/5 FFT Hilbert transform instead of quadratic
circular convolution; its phase sign crossings are preserved.
Pan corrects search-back insertion and its 250 ms missed-peak
distance; Pan++ suppresses a repeated emission within one candidate iteration,
matching `server/BPM/panpp_fixed.py`. Original kernels and cloned repositories
are untouched. Other modules reference the existing project C kernels directly.
`firmware_C` is the existing native baseline, not a Python-to-C port.

All modules must still pass the active firmware's RAM/stack checks and an ESP32
replay before being labelled device-ready. Workspace alone excludes the runtime,
input/output, task stacks and other enabled modules.
