"""Frozen production device AF branch for synchronized host replay.

The detector, RR features, quantization, Extra Trees and confirmation are the
actual current firmware C sources. This adapter never fits a model. A replay
resets once at record start and keeps state across every server window.

Input is a one-dimensional physical ECG waveform in mV. Input preprocessing
matches the existing validated host harness: offline polyphase resampling to
360 Hz, causal first-order 0.5 Hz high-pass (AFE approximation), then 200 ADC
counts/mV plus 2048, rounded and clipped to 12 bits. Offline polyphase filtering
has local lookahead; processing timestamps describe model-output availability,
not a measurement of end-to-end physical acquisition/network latency.
"""
from __future__ import annotations

import ctypes
from fractions import Fraction
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time

import numpy as np
from scipy.signal import butter, resample_poly, sosfilt, sosfilt_zi


FEATURE_NAMES = ("pnn50", "shannon_entropy", "sd2_ms", "sd1_sd2", "median_rr_ms")
FIRMWARE_FS = 360
_REPLAY_LOCK = threading.Lock()  # The firmware C state is static, as on device.


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class FrozenDevice:
    """Compile and observe the frozen production detector and AF runtime.

    ``replay`` returns peak-event arrays. ``available_ms`` is when a peak was
    detected, which can be later than its aligned ``peak_ms``. Align decisions
    with ``searchsorted(available_ms, decision_ms, side='right') - 1``; use the
    valid flag before consuming its probability. ``model_available_ms`` dates
    the most recent AF inference, rather than the most recent detected beat.
    SQI fields form a separate change-event timeline, initially insufficient.
    ``corrected_mv`` exports the actual firmware notch output shared by the
    device QRS/SQI paths (the old header comment calls this raw centered).
    """

    def __init__(self, repo_root: Path, build_dir: Path):
        self.repo_root = Path(repo_root).resolve()
        self.build_dir = Path(build_dir).resolve()
        self.build_dir.mkdir(parents=True, exist_ok=True)
        firmware = self.repo_root / "firmware/esp32/main"
        shim = Path(__file__).with_name("fusion_sync_device_host.c")
        sources = [
            shim,
            firmware / "components/bpm/ExtractFeature.c",
            firmware / "components/filter/SBfilter.c",
            firmware / "components/af_model/af_runtime.c",
            firmware / "components/af_model/af_model_int8.c",
            *[firmware / f"components/sqi_gate/{name}.c" for name in
              ("sqi_runtime", "sqi_orphanidou", "sqi_template_matching")],
        ]
        headers = [
            firmware / "components/bpm/ExtractFeature.h",
            firmware / "components/filter/SBfilter.h",
            *[firmware / f"components/af_model/{name}.h" for name in
              ("af_runtime", "af_model_int8")],
            *[firmware / f"components/sqi_gate/{name}.h" for name in
              ("sqi_runtime", "sqi_orphanidou", "sqi_template_matching")],
        ]
        hashes = {str(p.relative_to(self.repo_root)).replace("\\", "/"): _sha256(p)
                  for p in [*sources, *headers, Path(__file__).resolve()]}
        digest = hashlib.sha256("".join(hashes.values()).encode()).hexdigest()[:16]
        compiler = shutil.which("gcc")
        if compiler is None:
            raise RuntimeError("GCC is required to replay the current firmware C branch")
        extension = ".dll" if os.name == "nt" else ".so"
        library_path = self.build_dir / f"fusion_sync_device_{digest}{extension}"
        command = [compiler, "-std=c99", "-O2", "-shared"]
        if os.name != "nt":
            command.append("-fPIC")
        command += [f"-I{firmware}", *map(str, sources), "-o", str(library_path)]
        if os.name != "nt":
            command.append("-lm")
        started = time.perf_counter()
        cached = library_path.is_file()
        if not cached:
            subprocess.run(command, check=True, capture_output=True, text=True)
        self._library = ctypes.CDLL(str(library_path))
        run = self._library.fusion_sync_replay
        u32 = np.ctypeslib.ndpointer(np.uint32, flags="C_CONTIGUOUS")
        i32 = np.ctypeslib.ndpointer(np.int32, flags="C_CONTIGUOUS")
        i16 = np.ctypeslib.ndpointer(np.int16, flags="C_CONTIGUOUS")
        u16 = np.ctypeslib.ndpointer(np.uint16, flags="C_CONTIGUOUS")
        f32 = np.ctypeslib.ndpointer(np.float32, flags="C_CONTIGUOUS")
        run.argtypes = [i32, ctypes.c_uint32, i16, u16, u32, f32, ctypes.c_uint32,
                        i32, ctypes.c_uint32, u32]
        run.restype = ctypes.c_int
        self._run = run
        compiler_version = subprocess.run(
            [compiler, "--version"], check=True, capture_output=True, text=True
        ).stdout.splitlines()[0]
        self.metadata = {
            "branch": "current production C continuous QRS + frozen ExtraTrees INT8",
            "model": "af_model_int8.c", "model_tree_count": 80,
            "raw_score_threshold": 10240, "score_denominator": 80 * 255,
            "probability": "floor(score*10000/(80*255))/10000",
            "firmware_sample_rate_hz": FIRMWARE_FS,
            "rr_count": 30, "rr_stride": 15, "rr_limits_ms": [250, 2500],
            "feature_names": list(FEATURE_NAMES),
            "confirmation": "3 of last 4 raw predictions, requires 4-window history",
            "sqi": "current Orphanidou + template, 10s window / 1s hop, shadow only",
            "source_sha256": hashes,
            "library_sha256": _sha256(library_path),
            "compiler": compiler_version, "compile_command": command,
            "library_cached": cached,
            "build_load_seconds": time.perf_counter() - started,
            "execution": "host CPU replay; not ESP32/network latency",
        }

    def replay(self, signal: np.ndarray, fs: float) -> dict:
        """Replay a complete mV record, preserving continuous device state."""
        started = time.perf_counter()
        values = np.asarray(signal, dtype=np.float64)
        if values.ndim != 1 or values.size == 0:
            raise ValueError("Device input must be a nonempty 1-D ECG waveform in mV")
        if not np.isfinite(values).all():
            raise ValueError("Device ECG input contains non-finite samples")
        if not np.isfinite(fs) or fs <= 0:
            raise ValueError("Sampling frequency must be finite and positive")
        ratio = Fraction(FIRMWARE_FS / float(fs)).limit_denominator(100000)
        up, down = ratio.numerator, ratio.denominator
        resampled = resample_poly(values, up, down)
        hp = butter(1, 0.5, btype="highpass", fs=FIRMWARE_FS, output="sos")
        centered, _ = sosfilt(hp, resampled, zi=sosfilt_zi(hp) * resampled[0])
        unbounded = np.rint(centered * 200.0 + 2048.0)
        clipped = int(np.count_nonzero((unbounded < 0) | (unbounded > 4095)))
        adc = np.ascontiguousarray(np.clip(unbounded, 0, 4095), dtype=np.int32)
        if len(adc) >= np.iinfo(np.uint32).max:
            raise ValueError("Record exceeds the uint32 host replay sample capacity")
        preprocess_seconds = time.perf_counter() - started
        # 300-ms QRS refractory means >100 samples/peak; this generous capacity
        # follows the existing native timeline harness and remains bounds checked.
        peak_capacity = len(adc) // 50 + 100
        sqi_capacity = len(adc) // FIRMWARE_FS + 2
        peak_rows = np.empty((peak_capacity, 16), dtype=np.uint32)
        features = np.empty((peak_capacity, 5), dtype=np.float32)
        sqi_rows = np.empty((sqi_capacity, 9), dtype=np.int32)
        corrected = np.empty(len(adc), dtype=np.int16)
        bpm = np.empty(len(adc), dtype=np.uint16)
        counts = np.zeros(2, dtype=np.uint32)
        replay_started = time.perf_counter()
        with _REPLAY_LOCK:
            status = self._run(adc, len(adc), corrected, bpm, peak_rows, features, peak_capacity,
                               sqi_rows, sqi_capacity, counts)
        native_seconds = time.perf_counter() - replay_started
        if status:
            raise RuntimeError(f"C replay stopped safely at output capacity: {status}")
        peak_rows = peak_rows[:int(counts[0])].copy()
        features = features[:int(counts[0])].copy()
        sqi_rows = sqi_rows[:int(counts[1])].copy()
        names = ("available_ms", "peak_ms", "valid", "new_window",
                 "raw_prediction", "confirmed", "score", "probability_x10000",
                 "window_id", "window_start_ms", "window_end_ms", "rr_count",
                 "event_id", "new_event", "model_available_ms", "rr_ms")
        result = {name: peak_rows[:, i].copy() for i, name in enumerate(names)}
        for name in ("valid", "new_window", "raw_prediction", "confirmed", "new_event"):
            result[name] = result[name].astype(bool)
        result["model_available_ms"] = result["model_available_ms"].astype(np.int64)
        result["model_available_ms"][result["model_available_ms"] == 2**32 - 1] = -1
        result["probability"] = result["probability_x10000"].astype(float) / 10000.0
        result["probability"][~result["valid"]] = np.nan
        features[~result["new_window"]] = np.nan
        result["features"] = features
        result["sample_ms"] = ((np.arange(len(adc), dtype=np.uint64) * 1000 + 180)
                               // FIRMWARE_FS).astype(np.uint32)
        result["corrected_mv"] = corrected.astype(np.float32) / np.float32(200.0)
        result["bpm"] = bpm
        sqi_names = ("sqi_available_ms", "sqi_status", "sqi_score", "sqi_window_id",
                     "sqi_rule_mask", "sqi_heart_rate_bpm", "sqi_max_rr_ms",
                     "sqi_rr_ratio_x1000", "sqi_template_correlation_x1000")
        result.update({name: sqi_rows[:, i].copy() for i, name in enumerate(sqi_names)})
        if np.any(np.diff(result["peak_ms"].astype(np.int64)) <= 0):
            raise RuntimeError("Production detector returned non-increasing aligned peaks")
        if np.any(result["available_ms"] < result["peak_ms"]):
            raise RuntimeError("Peak timestamp cannot follow its processing availability")
        result["metadata"] = {
            **self.metadata, "input_sample_rate_hz": float(fs),
            "input_samples": int(values.size), "adc_samples": int(len(adc)),
            "last_sample_ms": int(result["sample_ms"][-1]),
            "sample_span_seconds": len(adc) / FIRMWARE_FS,
            "adc_clipped_samples": clipped,
            "input_sha256": hashlib.sha256(values.tobytes()).hexdigest(),
            "adc_sha256": hashlib.sha256(adc.tobytes()).hexdigest(),
            "preprocess_seconds": preprocess_seconds,
            "native_replay_seconds": native_seconds,
            "total_seconds": time.perf_counter() - started,
            "preprocessing": {
                "resampling": "offline centered polyphase FIR (legacy replay parity)",
                "resample_up": up, "resample_down": down,
                "resample_lookahead_input_seconds_approx":
                    (10 * max(up, down) / up / float(fs)) if up != down else 0.0,
                "afe_approximation": "causal first-order 0.5 Hz high-pass at360Hz",
                "adc": "rint(mV*200+2048), clipped to [0,4095]",
                "exported_corrected_mv":
                    "actual firmware SBfilter49-51Hz notch int16 output /200",
            },
        }
        return result
