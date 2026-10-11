"""Compile the host C port and verify peak/state parity against Python."""
from __future__ import annotations

import argparse
import csv
import ctypes
import hashlib
import json
import subprocess
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import wfdb
from scipy import signal

from ecg_benchmark.rpeaks import AdaptivePTPortable

ROOT = Path(__file__).resolve().parents[1]


class CTrace(ctypes.Structure):
    _fields_ = [
        ("filtered", ctypes.c_double),
        ("envelope", ctypes.c_double),
        ("threshold", ctypes.c_double),
        ("signal_level", ctypes.c_double),
        ("noise_level", ctypes.c_double),
        ("event_index", ctypes.c_int64),
    ]


@dataclass
class Vector:
    name: str
    samples: np.ndarray
    source: str


def resample_360(values: np.ndarray, fs: float) -> np.ndarray:
    if np.isclose(fs, 360):
        return np.asarray(values, dtype=np.float64)
    return signal.resample_poly(np.asarray(values, dtype=np.float64), 360, int(round(fs)))


def load_segment(path: Path, start_seconds: float, duration_seconds: float) -> np.ndarray:
    header = wfdb.rdheader(str(path))
    fs = float(header.fs)
    start = int(round(start_seconds * fs))
    stop = min(header.sig_len, start + int(round(duration_seconds * fs)))
    values, _ = wfdb.rdsamp(str(path), sampfrom=start, sampto=stop, channels=[0])
    return resample_360(values[:, 0] - np.median(values[:, 0]), fs)


def vectors() -> list[Vector]:
    fs = 360
    upright = np.zeros(fs * 20, dtype=np.float64)
    inverted = 0.03 * np.sin(2 * np.pi * 0.25 * np.arange(fs * 20) / fs)
    shape = np.asarray([0.05, 0.35, 1.0, 1.5, 1.0, 0.35, 0.05])
    for peak in range(fs, len(upright), fs):
        upright[peak - 3 : peak + 4] = shape
        inverted[peak - 3 : peak + 4] -= shape
    rng = np.random.default_rng(20261009)
    inverted += rng.normal(0.0, 0.005, size=len(inverted))

    result = [
        Vector("synthetic_upright", upright, "deterministic 60 BPM impulses"),
        Vector("synthetic_inverted", inverted, "inverted QRS + drift + seeded noise"),
        Vector("qtdb_sel100_start", load_segment(ROOT / "data/qtdb/sel100", 0, 60), "QTDB sel100 0-60 s lead 0"),
        Vector("qtdb_sel123_middle", load_segment(ROOT / "data/qtdb/sel123", 420, 60), "QTDB sel123 420-480 s lead 0"),
        Vector("nstdb_118e00_end", load_segment(ROOT / "data/nstdb/118e00", 1740, 60), "NSTDB 118e00 1740-1800 s lead 0"),
        Vector("nstdb_118e_6_end", load_segment(ROOT / "data/nstdb/118e_6", 1740, 60), "NSTDB 118e_6 1740-1800 s lead 0"),
    ]
    return result


def python_run(samples: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    detector = AdaptivePTPortable(360)
    trace = np.empty((len(samples), 4), dtype=np.float64)
    peaks = []
    for index, value in enumerate(samples):
        event = detector.push(float(value))
        if event is not None:
            peaks.append(event)
        trace[index] = (
            detector.env_1,
            detector.signal_level,
            detector.noise_level,
            -1 if event is None else event,
        )
    return np.asarray(peaks, dtype=np.int64), trace


def compile_c(output: Path) -> Path:
    dll = output / "adaptive_pt_portable_host.dll"
    subprocess.run([
        "gcc", "-std=c99", "-O2", "-shared",
        str(ROOT / "experiments/adaptive_pt_portable_host.c"),
        "-o", str(dll),
    ], check=True)
    return dll


def c_run(library: ctypes.CDLL, samples: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    run = library.adaptive_pt_run
    run.argtypes = [
        np.ctypeslib.ndpointer(np.float64, flags="C_CONTIGUOUS"),
        ctypes.c_uint32,
        np.ctypeslib.ndpointer(np.uint32, flags="C_CONTIGUOUS"),
        ctypes.POINTER(CTrace),
    ]
    run.restype = ctypes.c_uint32
    samples = np.ascontiguousarray(samples, dtype=np.float64)
    peaks = np.empty(len(samples), dtype=np.uint32)
    c_trace = (CTrace * len(samples))()
    count = int(run(samples, len(samples), peaks, c_trace))
    trace = np.empty((len(samples), 4), dtype=np.float64)
    for index, row in enumerate(c_trace):
        trace[index] = (row.envelope, row.signal_level, row.noise_level, row.event_index)
    return peaks[:count].astype(np.int64), trace


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("outputs/adaptive_pt_python_c_parity"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    library = ctypes.CDLL(str(compile_c(args.output)))
    state_bytes = int(library.adaptive_pt_state_bytes())

    rows = []
    golden = {"fs_hz": 360, "state_bytes_host_double": state_bytes, "vectors": []}
    all_pass = True
    for vector in vectors():
        py_peaks, py_trace = python_run(vector.samples)
        c_peaks, c_trace = c_run(library, vector.samples)
        count_equal = len(py_peaks) == len(c_peaks)
        max_peak_delta = int(np.max(np.abs(py_peaks - c_peaks))) if count_equal and len(py_peaks) else (0 if count_equal else -1)
        peak_pass = count_equal and max_peak_delta <= 1
        envelope_error = float(np.max(np.abs(py_trace[:, 0] - c_trace[:, 0])))
        signal_error = float(np.max(np.abs(py_trace[:, 1] - c_trace[:, 1])))
        noise_error = float(np.max(np.abs(py_trace[:, 2] - c_trace[:, 2])))
        state_pass = bool(np.allclose(py_trace[:, :3], c_trace[:, :3], rtol=1e-11, atol=1e-13))
        event_trace_pass = np.array_equal(py_trace[:, 3], c_trace[:, 3])
        passed = peak_pass and state_pass and event_trace_pass
        all_pass &= passed
        digest = hashlib.sha256(np.ascontiguousarray(vector.samples).tobytes()).hexdigest()
        rows.append({
            "vector": vector.name, "samples": len(vector.samples),
            "python_peaks": len(py_peaks), "c_peaks": len(c_peaks),
            "max_peak_delta_samples": max_peak_delta,
            "max_envelope_abs_error": envelope_error,
            "max_signal_level_abs_error": signal_error,
            "max_noise_level_abs_error": noise_error,
            "event_trace_equal": event_trace_pass,
            "pass": passed,
        })
        golden["vectors"].append({
            "name": vector.name, "source": vector.source, "samples": len(vector.samples),
            "input_sha256_float64": digest, "expected_peaks": py_peaks.tolist(),
        })

    with (args.output / "parity_results.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    golden["acceptance"] = {
        "peak_count_equal": True,
        "max_peak_delta_samples": 1,
        "event_trace_equal": True,
        "state_rtol": 1e-11,
        "state_atol": 1e-13,
    }
    golden["overall_pass"] = all_pass
    (args.output / "golden_vectors.json").write_text(json.dumps(golden, indent=2), encoding="utf-8")
    print(json.dumps({"overall_pass": all_pass, "state_bytes": state_bytes, "vectors": rows}, indent=2))
    if not all_pass:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
