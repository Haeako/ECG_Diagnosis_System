"""Apply the frozen device INT8 AF forest to independently supplied R peaks.

The production ``af_runtime.c`` owns the 30-RR feature calculation, stride,
reset and 3-of-4 confirmation. ``af_model_int8.c`` owns quantization and forest
inference. This adapter neither fits a model nor changes either source file.

SWT peaks can be injected after each completed server block. Source peak times
are not processing timestamps: callers must map returned peak indices to the
block availability before constructing as-of decisions or explanations.
"""
from __future__ import annotations

import ctypes
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import threading
import time

import numpy as np


FEATURE_NAMES = ("pnn50", "shannon_entropy", "sd2_ms", "sd1_sd2", "median_rr_ms")
_LOCK = threading.Lock()  # Production RR runtime owns static process state.
_MAX_U32 = int(np.iinfo(np.uint32).max)


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class FrozenServerRRAF:
    """Run the unchanged production RR model on server peaks or five features."""

    def __init__(self, repo_root: Path, build_dir: Path):
        self.repo_root = Path(repo_root).resolve()
        self.build_dir = Path(build_dir).resolve()
        self.build_dir.mkdir(parents=True, exist_ok=True)
        model_dir = self.repo_root / "firmware/esp32/main/components/af_model"
        shim = Path(__file__).with_name("server_rr_af_host.c")
        sources = [shim, model_dir / "af_runtime.c", model_dir / "af_model_int8.c"]
        paths = [*sources, model_dir / "af_runtime.h", model_dir / "af_model_int8.h",
                 Path(__file__).resolve()]
        hashes = {path.relative_to(self.repo_root).as_posix(): _sha256(path)
                  for path in paths}
        digest = hashlib.sha256("".join(hashes.values()).encode("ascii")).hexdigest()[:16]
        compiler = shutil.which("gcc")
        if compiler is None:
            raise RuntimeError("GCC is required for the frozen production INT8 AF runtime")
        extension = ".dll" if os.name == "nt" else ".so"
        library_path = self.build_dir / f"server_rr_af_{digest}{extension}"
        command = [compiler, "-std=c99", "-O2", "-shared"]
        if os.name != "nt":
            command.append("-fPIC")
        command += [f"-I{model_dir}", *map(str, sources), "-o", str(library_path)]
        if os.name != "nt":
            command.append("-lm")
        started = time.perf_counter()
        cached = library_path.is_file()
        if not cached:
            subprocess.run(command, check=True, capture_output=True, text=True)
        self._library = ctypes.CDLL(str(library_path))
        u32 = np.ctypeslib.ndpointer(dtype=np.uint32, flags="C_CONTIGUOUS")
        u16 = np.ctypeslib.ndpointer(dtype=np.uint16, flags="C_CONTIGUOUS")
        f32 = np.ctypeslib.ndpointer(dtype=np.float32, flags="C_CONTIGUOUS")
        i8 = np.ctypeslib.ndpointer(dtype=np.int8, flags="C_CONTIGUOUS")
        self._replay = self._library.server_rr_af_replay
        self._replay.argtypes = [u32, ctypes.c_uint32, u32, f32, i8]
        self._replay.restype = None
        self._predict = self._library.server_rr_af_predict_features
        self._predict.argtypes = [f32, ctypes.c_uint32, i8, u16, u16]
        self._predict.restype = None
        compiler_version = subprocess.run(
            [compiler, "--version"], check=True, capture_output=True, text=True
        ).stdout.splitlines()[0]
        self.metadata = {
            "branch": "server RR to frozen production device ExtraTrees INT8",
            "training_performed": False, "calibration_performed": False,
            "model": "production af_model_int8.c", "model_tree_count": 80,
            "feature_names": list(FEATURE_NAMES),
            "feature_implementation": "unchanged production af_runtime.c; float32",
            "rr_count": 30, "rr_stride": 15, "rr_limits_ms": [250, 2500],
            "invalid_rr": "reset RR buffer and confirmation immediately",
            "raw_score_threshold": 10240, "score_denominator": 80 * 255,
            "probability": "floor(score*10000/(80*255))/10000",
            "confirmation": "3 of last 4 raw predictions; requires 4-window history",
            "replay_reset": "once at start of each replay_peaks call",
            "peak_timestamps": "caller-provided strictly increasing integer source milliseconds",
            "availability": "caller maps peak_index/model_peak_index to completed server block availability; no fabricated processing timestamp",
            "source_sha256": hashes,
            "library_sha256": _sha256(library_path),
            "library_path": str(library_path), "library_cached": cached,
            "compiler": compiler_version, "compile_command": command,
            "build_load_seconds": time.perf_counter() - started,
            "execution": "host CPU inference; not measured ESP32 or network latency",
            "explanation_scope": "five RR features to INT8 AF score; not waveform morphology or final LR fusion",
        }

    def replay_peaks(self, peaks_ms: np.ndarray) -> dict:
        """Reset once, then push a complete session's ordered source peaks.

        Every output array has one row per supplied peak. ``model_peak_index``
        is -1 when invalid and otherwise identifies the peak that triggered
        the latest AF inference. ``features`` is finite only on ``new_window``;
        ``quantized`` is meaningful on those same rows. No RR is repaired or
        silently removed; the original runtime resets on implausible intervals.
        """
        peaks = np.asarray(peaks_ms)
        if peaks.ndim != 1:
            raise ValueError("peaks_ms must be a one-dimensional integer array")
        if peaks.size and peaks.dtype.kind not in "iu":
            raise ValueError("Convert source peaks explicitly to integer milliseconds before replay")
        if len(peaks) >= _MAX_U32:
            raise ValueError("Peak count exceeds the uint32 replay capacity")
        if peaks.size:
            if np.any(peaks < 0) or np.any(peaks > _MAX_U32):
                raise ValueError("Peak milliseconds must fit uint32")
            if np.any(np.diff(peaks.astype(np.int64)) <= 0):
                raise ValueError("Peak milliseconds must be strictly increasing")
        peaks = np.ascontiguousarray(peaks, dtype=np.uint32)
        rows = np.empty((len(peaks), 15), dtype=np.uint32)
        features = np.empty((len(peaks), len(FEATURE_NAMES)), dtype=np.float32)
        quantized = np.empty_like(features, dtype=np.int8)
        started = time.perf_counter()
        if len(peaks):
            with _LOCK:
                self._replay(peaks, len(peaks), rows, features, quantized)
        names = ("peak_ms", "valid", "new_window", "raw_prediction", "confirmed",
                 "new_event", "score", "probability_x10000", "event_id", "window_id",
                 "window_start_ms", "window_end_ms", "model_peak_index", "rr_ms", "rr_count")
        result = {name: rows[:, index].copy() for index, name in enumerate(names)}
        result["peak_index"] = np.arange(len(peaks), dtype=np.int64)
        for name in ("valid", "new_window", "raw_prediction", "confirmed", "new_event"):
            result[name] = result[name].astype(bool)
        result["model_peak_index"] = result["model_peak_index"].astype(np.int64)
        result["model_peak_index"][result["model_peak_index"] == _MAX_U32] = -1
        result["probability"] = result["probability_x10000"].astype(np.float64) / 10000.
        result["probability"][~result["valid"]] = np.nan
        features[~result["new_window"]] = np.nan
        result["features"] = features
        result["quantized"] = quantized
        result["inference_seconds"] = time.perf_counter() - started
        return result

    def predict_features(self, values: np.ndarray) -> dict:
        """Predict batches of five features through the original C quantizer.

        Input is converted to float32 exactly as for the production runtime.
        This function has no RR state or confirmation stage, and is suitable
        for exhaustive five-feature empirical interventional SHAP coalitions.
        """
        features = np.asarray(values)
        if features.ndim != 2 or features.shape[1] != len(FEATURE_NAMES):
            raise ValueError("Expected an (n,5) feature matrix in production feature order")
        if len(features) >= _MAX_U32:
            raise ValueError("Feature row count exceeds uint32 capacity")
        with np.errstate(over="ignore", invalid="ignore"):
            features = np.ascontiguousarray(features, dtype=np.float32)
        if not np.isfinite(features).all():
            raise ValueError("Feature inputs must remain finite after float32 conversion")
        quantized = np.empty(features.shape, dtype=np.int8)
        scores = np.empty(len(features), dtype=np.uint16)
        comparisons = np.empty(len(features), dtype=np.uint16)
        if len(features):
            with _LOCK:
                self._predict(features, len(features), quantized, scores, comparisons)
        probability_x10000 = ((scores.astype(np.uint32) * 10000) // (80 * 255)).astype(np.uint16)
        return {"quantized": quantized, "score": scores, "comparisons": comparisons,
                "probability_x10000": probability_x10000,
                "probability": probability_x10000.astype(np.float64) / 10000.,
                "raw_prediction": scores >= 10240}

METHOD='extra_trees_int8_rr'

def load(output):
    return FrozenServerRRAF(Path(__file__).resolve().parents[3],output)

def predict(model,frame):
    result=model.predict_features(frame.loc[:,FEATURE_NAMES].to_numpy())
    # Rank AUC uses the exact forest score; telemetry rounds separately to1e-4.
    return result['score'].astype(float)/(80*255),result['raw_prediction'].astype(int)
