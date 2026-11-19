"""Frozen local ConvNeXtV2-inspired AF model on completed 10-second windows.

This is the project's independently implemented model, not the checkpoint
published by Kraft & Rumm.  Its existing FP32 weights were trained with
LTAFDB and SHDB-AF; AFDB was excluded from that training.  No training or
threshold fitting takes place in this module.
"""
from __future__ import annotations

import csv
import hashlib
import json
import math
from pathlib import Path
import time
from typing import Any

import numpy as np
from scipy import signal as sig


TARGET_FS = 125
WINDOW_SECONDS = 10
WINDOW_SAMPLES = TARGET_FS * WINDOW_SECONDS
CHECKPOINT_SHA256 = "6bc9d46ab2e43185a607e146dc3087a9053ad5d963d669bcc7e683d3b4a6473f"
EXPECTED_PARAMETERS = 766_818


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _subjects(path: Path) -> list[str]:
    with path.open(encoding="utf-8", newline="") as stream:
        return [row["subject"] for row in csv.DictReader(stream)]


def preprocess_window(signal_mv: np.ndarray, fs: float, mains_hz: float = 50) -> np.ndarray:
    """Return the historical model's float32 (1250,1) input for one window.

    Resampling, fifth-order 0.5 Hz high-pass, notch Q=30, median centering,
    population-standard-deviation scaling and clipping reproduce the earlier
    local experiment.  This adapter filters each completed block independently;
    the historical evaluation filtered whole records before slicing blocks.
    """
    if not np.isfinite(fs) or fs <= 0 or float(fs) != int(fs):
        raise ValueError("fs must be a positive integer sample rate")
    fs_int = int(fs)
    if not np.isfinite(mains_hz) or not 0 < mains_hz < TARGET_FS / 2:
        raise ValueError("mains_hz must be between zero and 62.5 Hz")
    raw = np.asarray(signal_mv)
    if raw.ndim != 1 or raw.size != fs_int * WINDOW_SECONDS:
        raise ValueError("ConvNeXt input must be exactly one 10-second ECG lead")
    if not np.isfinite(raw).all():
        raise ValueError("ConvNeXt input must contain only finite physical ECG samples")
    raw = raw.astype(np.float32)
    divisor = math.gcd(fs_int, TARGET_FS)
    x = sig.resample_poly(raw, TARGET_FS // divisor, fs_int // divisor).astype(np.float32)
    sos = sig.butter(5, 0.5, btype="highpass", fs=TARGET_FS, output="sos")
    x = sig.sosfiltfilt(sos, x).astype(np.float32)
    b, a = sig.iirnotch(float(mains_hz), 30, fs=TARGET_FS)
    x = sig.filtfilt(b, a, x).astype(np.float32)
    x = x.reshape(1, WINDOW_SAMPLES)
    median = np.median(x, axis=1, keepdims=True)
    scale = np.std(x, axis=1, keepdims=True)
    x = np.clip((x - median) / np.maximum(scale, 1e-5), -8, 8)
    return x.reshape(WINDOW_SAMPLES, 1).astype(np.float32)


def _build_model(tf: Any) -> Any:
    """Exact local architecture and layer names from the archived runner."""
    class GRN(tf.keras.layers.Layer):
        def build(self, shape):
            self.gamma = self.add_weight(name="gamma", shape=(1, 1, shape[-1]), initializer="zeros")
            self.beta = self.add_weight(name="beta", shape=(1, 1, shape[-1]), initializer="zeros")

        def call(self, x):
            gx = tf.sqrt(tf.reduce_sum(tf.square(x), axis=1, keepdims=True) + 1e-6)
            nx = gx / (tf.reduce_mean(gx, axis=-1, keepdims=True) + 1e-6)
            return x + self.gamma * (x * nx) + self.beta

    def block(x, channels, name):
        residual = x
        x = tf.keras.layers.DepthwiseConv1D(7, padding="same", name=f"{name}_dw")(x)
        x = tf.keras.layers.LayerNormalization(epsilon=1e-6, name=f"{name}_ln")(x)
        x = tf.keras.layers.Dense(channels * 4, name=f"{name}_expand")(x)
        x = tf.keras.layers.Activation("gelu", name=f"{name}_gelu")(x)
        x = GRN(name=f"{name}_grn")(x)
        x = tf.keras.layers.Dense(channels, name=f"{name}_project")(x)
        return tf.keras.layers.Add(name=f"{name}_add")([residual, x])

    inp = tf.keras.Input((WINDOW_SAMPLES, 1), name="ecg_125hz_10s")
    x = tf.keras.layers.Conv1D(16, 4, strides=4, padding="valid", name="stem")(inp)
    x = tf.keras.layers.LayerNormalization(epsilon=1e-6, name="stem_ln")(x)
    for stage, (channels, depth) in enumerate(zip((16, 32, 64, 128), (3, 3, 9, 3))):
        if stage:
            x = tf.keras.layers.LayerNormalization(epsilon=1e-6, name=f"down{stage}_ln")(x)
            x = tf.keras.layers.Conv1D(channels, 2, strides=2, padding="valid", name=f"down{stage}")(x)
        for index in range(depth):
            x = block(x, channels, f"s{stage}_b{index}")
    x = tf.keras.layers.GlobalAveragePooling1D(name="global_pool")(x)
    x = tf.keras.layers.LayerNormalization(epsilon=1e-6, name="head_ln")(x)
    out = tf.keras.layers.Dense(2, name="af_logits")(x)
    return tf.keras.Model(inp, out, name="singlelead_convnextv2_inspired")


class FrozenConvNeXt:
    """Read-only existing FP32 model; AF is softmax class index one."""

    def __init__(self, repo_root: Path, threads: int = 2, mains_hz: float = 50) -> None:
        if isinstance(threads, bool) or int(threads) != threads or threads < 1:
            raise ValueError("threads must be a positive integer")
        if not np.isfinite(mains_hz) or not 0 < mains_hz < TARGET_FS / 2:
            raise ValueError("mains_hz must be between zero and 62.5 Hz")
        import tensorflow as tf

        root = Path(repo_root).resolve()
        artifacts = root / "experiment/report/benchmark_runs/af_convnext_singlelead_20261028_int8_final"
        paths = {
            "checkpoint": artifacts / "float_model.weights.h5",
            "original_protocol": artifacts / "protocol.json",
            "training_inventory": artifacts / "training_inventory.csv",
            "train_subjects": artifacts / "train_subjects.csv",
            "validation_subjects": artifacts / "validation_subjects.csv",
            "adapter_source": Path(__file__).resolve(),
        }
        for path in paths.values():
            if not path.is_file():
                raise FileNotFoundError(f"Required frozen ConvNeXt artifact missing: {path}")
        if _sha256(paths["checkpoint"]) != CHECKPOINT_SHA256:
            raise ValueError("ConvNeXt checkpoint SHA256 differs from the frozen artifact")
        threading_status = "configured before TensorFlow runtime initialization"
        try:
            tf.config.threading.set_intra_op_parallelism_threads(int(threads))
            tf.config.threading.set_inter_op_parallelism_threads(int(threads))
        except RuntimeError:
            threading_status = "TensorFlow runtime already initialized; existing thread settings retained"
        self._tf = tf
        with tf.device("/CPU:0"):
            self._model = _build_model(tf)
            if self._model.count_params() != EXPECTED_PARAMETERS:
                raise ValueError("Local ConvNeXt architecture parameter count changed")
            self._model.load_weights(paths["checkpoint"])
        self._model.trainable = False
        self._mains_hz = float(mains_hz)
        train_subjects = _subjects(paths["train_subjects"])
        validation_subjects = _subjects(paths["validation_subjects"])
        if set(train_subjects) & set(validation_subjects):
            raise ValueError("Frozen ConvNeXt historical train/validation subjects overlap")
        protocol = json.loads(paths["original_protocol"].read_text(encoding="utf-8"))
        self.metadata: dict[str, Any] = {
            "model": "Frozen local ConvNeXtV2-inspired 1D AF classifier, FP32",
            "provenance": "Independent project implementation, not the authors' published checkpoint",
            "original_architecture": protocol["model"],
            "parameters": EXPECTED_PARAMETERS,
            "training_performed": False,
            "custom_ecgfounder_head": False,
            "runtime": "TensorFlow CPU, direct model call with training=False",
            "tensorflow_version": tf.__version__,
            "threads_requested": int(threads),
            "threading_status": threading_status,
            "input": {"lead": 0, "window_seconds": WINDOW_SECONDS,
                      "target_fs_hz": TARGET_FS, "target_samples": WINDOW_SAMPLES,
                      "units": "physical ECG; normalization removes absolute scale"},
            "preprocessing": {
                "nonfinite": "reject the entire supplied window",
                "resampling": "float32 scipy.signal.resample_poly to 125 Hz",
                "filter": f"fifth-order zero-phase 0.5 Hz high-pass followed by {mains_hz:g} Hz notch, Q=30",
                "filter_scope": "completed 10-second window only",
                "normalization": "per-window median centering / max(population std,1e-5); clip [-8,8]",
                "change_from_historical": "historical runner filtered each complete record before windowing; this adapter filters completed blocks independently",
            },
            "output": "softmax(logits)[1], AF probability; no threshold fitted here",
            "historical_training": {
                "datasets": protocol["training_datasets"],
                "train_subject_count": len(train_subjects),
                "validation_subject_count": len(validation_subjects),
                "train_subjects": train_subjects,
                "validation_subjects": validation_subjects,
                "train_windows": protocol["train_windows"],
                "validation_windows": protocol["validation_windows"],
                "SHDB_AF": "all 93 subjects appear in historical training or validation; SHDB is not an unseen test for this model",
                "AFDB": protocol["external_test"],
                "AFDB_caveat": protocol.get("afdb_caveat", "AFDB appeared in earlier project evaluations"),
            },
            "latency": "perf_counter host CPU time; excludes acquisition, transport, model load and queueing",
            "artifacts": {name: {"path": path.relative_to(root).as_posix(),
                                  "sha256": _sha256(path), "bytes": path.stat().st_size}
                          for name, path in paths.items()},
        }

    def predict_window(self, signal_mv: np.ndarray, fs: float, *, mains_hz: float | None = None) -> dict:
        """Predict AF from exactly ten seconds without using future samples."""
        started = time.perf_counter()
        x = preprocess_window(signal_mv, fs, self._mains_hz if mains_hz is None else mains_hz)
        preprocessed = time.perf_counter()
        with self._tf.device("/CPU:0"):
            logits = self._model(x[None, ...], training=False)
            probabilities = self._tf.nn.softmax(logits, axis=1).numpy()[0]
        finished = time.perf_counter()
        if probabilities.shape != (2,) or not np.isfinite(probabilities).all():
            raise ValueError("Frozen ConvNeXt returned invalid probabilities")
        return {
            "probability": float(probabilities[1]),
            "class_probabilities": probabilities.copy(),
            "preprocessing_ms": (preprocessed - started) * 1000,
            "inference_ms": (finished - preprocessed) * 1000,
            "total_ms": (finished - started) * 1000,
        }

METHOD='convnext'

def load(output=None):
    return FrozenConvNeXt(Path(__file__).resolve().parents[3],threads=2,mains_hz=60)

def predict(model,window,fs=360):
    value=model.predict_window(window,fs)
    return value['probability'],int(value['probability']>=.5)
