"""Verify the int16/float32 C port against Python on quantized golden vectors."""
from __future__ import annotations

import argparse
import csv
import ctypes
import json
import subprocess
from pathlib import Path

import numpy as np

from ecg_benchmark.rpeaks import detect_r_peaks_adaptive_portable
from verify_adaptive_pt_python_c import ROOT, vectors


class CTraceInt16(ctypes.Structure):
    _fields_ = [
        ("envelope", ctypes.c_float),
        ("signal_level", ctypes.c_float),
        ("noise_level", ctypes.c_float),
        ("event_index", ctypes.c_int32),
    ]


def quantize(values: np.ndarray) -> tuple[np.ndarray, float]:
    # Fixed conversion used by the existing firmware host adapter.  A single
    # gain is deliberately used for every record so parity cannot benefit from
    # per-vector range normalization unavailable on the device.
    gain = 200.0
    quantized = np.clip(np.rint(values * gain), -32768, 32767).astype(np.int16)
    return quantized, gain


def compile_c(output: Path) -> Path:
    dll = output / "adaptive_pt_portable_int16_host.dll"
    subprocess.run([
        "gcc", "-std=c99", "-O2", "-shared",
        str(ROOT / "experiments/adaptive_pt_portable_int16_host.c"),
        "-o", str(dll),
    ], check=True)
    return dll


def c_run(library: ctypes.CDLL, samples: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    run = library.adaptive_pt_run_int16
    run.argtypes = [
        np.ctypeslib.ndpointer(np.int16, flags="C_CONTIGUOUS"), ctypes.c_uint32,
        np.ctypeslib.ndpointer(np.uint32, flags="C_CONTIGUOUS"),
        ctypes.POINTER(CTraceInt16),
    ]
    run.restype = ctypes.c_uint32
    samples = np.ascontiguousarray(samples, dtype=np.int16)
    peaks = np.empty(len(samples), dtype=np.uint32)
    trace_buffer = (CTraceInt16 * len(samples))()
    count = int(run(samples, len(samples), peaks, trace_buffer))
    trace = np.asarray([
        (row.envelope, row.signal_level, row.noise_level, row.event_index)
        for row in trace_buffer
    ], dtype=np.float64)
    return peaks[:count].astype(np.int64), trace


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("outputs/adaptive_pt_int16_parity"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    library = ctypes.CDLL(str(compile_c(args.output)))
    library.adaptive_pt_int16_state_bytes.restype = ctypes.c_uint32
    state_bytes = int(library.adaptive_pt_int16_state_bytes())

    rows = []
    all_pass = True
    for vector in vectors():
        samples, gain = quantize(vector.samples)
        python_peaks = detect_r_peaks_adaptive_portable(samples.astype(np.float64), 360)
        c_peaks, _trace = c_run(library, samples)
        count_equal = len(python_peaks) == len(c_peaks)
        deltas = np.abs(python_peaks - c_peaks) if count_equal else np.asarray([])
        maximum_delta = int(np.max(deltas)) if len(deltas) else (0 if count_equal else -1)
        passed = count_equal and maximum_delta <= 1
        all_pass &= passed
        rows.append({
            "vector": vector.name, "samples": len(samples), "int16_gain": gain,
            "python_peaks": len(python_peaks), "c_peaks": len(c_peaks),
            "max_peak_delta_samples": maximum_delta,
            "peaks_with_delta_gt_1": int(np.sum(deltas > 1)) if count_equal else -1,
            "pass": passed,
        })

    with (args.output / "parity_results.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    summary = {
        "overall_pass": all_pass,
        "configuration": "int16 input/raw history + float32 DSP state",
        "state_bytes_host": state_bytes,
        "acceptance": {"peak_count_equal": True, "max_peak_delta_samples": 1},
        "vectors": rows,
    }
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary, indent=2))
    if not all_pass:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
