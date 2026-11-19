"""Frozen released ECGFounder inference on independent 10-second windows.

``FrozenServer`` retains all 150 released outputs, including atrial fibrillation.
``LegacyAFProbeServer`` preserves the earlier encoder plus custom AF probe for
replaying the previous experiment. Neither adapter trains any model.
"""
from __future__ import annotations

from fractions import Fraction
import hashlib
import importlib.util
import json
from pathlib import Path
import time
from types import ModuleType
from typing import Any

import numpy as np
from scipy.signal import resample_poly
from scipy.special import expit


TARGET_FS = 500
WINDOW_SECONDS = 10
WINDOW_SAMPLES = TARGET_FS * WINDOW_SECONDS
PUBLISHED_CHECKPOINT_SHA256 = "f863a38897fb49a27fec7e44008ea3c7bdbd29c77fa4a02ecbb8c56df4f37603"


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _load_source(path: Path, name: str) -> ModuleType:
    """Import the archived source directly, without changing sys.path or files."""
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise ImportError(f"Cannot import ECGFounder source: {path}")
    module = importlib.util.module_from_spec(spec)
    # SourceFileLoader.exec_module can create __pycache__ inside the cloned
    # upstream repository. Execute its original source without writing caches.
    exec(compile(path.read_bytes(), str(path), "exec"), module.__dict__)
    return module


class FrozenServer:
    """Original released 1-lead checkpoint with its complete 150-class head.

    The inherited helper only prepares completed windows. This constructor
    never loads the old AF probe or its scaler. AF probability is the sigmoid
    of output index 5, using the exact released ``tasks.txt`` ordering.
    """

    def __init__(self, repo_root: Path, threads: int = 2, *, mmap_checkpoint: bool = False) -> None:
        if isinstance(threads, bool) or int(threads) != threads or threads < 1:
            raise ValueError("threads must be a positive integer")
        try:
            import torch
        except ImportError as exc:
            raise ImportError("FrozenServer requires the existing PyTorch runtime") from exc

        self.repo_root = Path(repo_root).resolve()
        source = self.repo_root / (
            "experiment/src/esp32_archive/experiment_code_and_assets/"
            "external/ECGFounder"
        )
        paths = {
            "released_checkpoint": source / "checkpoint/1_lead_ECGFounder.pth",
            "task_order": source / "tasks.txt",
            "architecture_source": source / "net1d.py",
            "preprocessing_source": source / "util.py",
            "published_evaluation_source": source / "ptbxl_eval.py",
        }
        for path in paths.values():
            if not path.is_file():
                raise FileNotFoundError(f"Required published ECGFounder artifact missing: {path}")
        checkpoint_sha = _sha256(paths["released_checkpoint"])
        if checkpoint_sha != PUBLISHED_CHECKPOINT_SHA256:
            raise ValueError("Released 1-lead ECGFounder checkpoint SHA256 differs from the verified original")

        self.class_names = tuple(paths["task_order"].read_text(encoding="utf-8").splitlines())
        if len(self.class_names) != 150 or self.class_names.count("ATRIAL FIBRILLATION") != 1:
            raise ValueError("The released task list must contain 150 outputs and one AF task")
        self.af_index = self.class_names.index("ATRIAL FIBRILLATION")
        if self.af_index != 5:
            raise ValueError("The published AF task must be at zero-based index 5")

        net1d = _load_source(paths["architecture_source"], "_fusion_published_ecgfounder_net1d")
        official_util = _load_source(paths["preprocessing_source"], "_fusion_published_ecgfounder_util")
        self._filter_bandpass = official_util.filter_bandpass
        self._torch = torch
        torch.set_num_threads(int(threads))
        self._model = net1d.Net1D(
            in_channels=1,
            base_filters=64,
            ratio=1,
            filter_list=[64, 160, 160, 400, 400, 1024, 1024],
            m_blocks_list=[2, 2, 2, 3, 3, 4, 4],
            kernel_size=16,
            stride=2,
            groups_width=16,
            n_classes=150,
            verbose=False,
            use_bn=False,
            use_do=False,
            return_features=False,
        )
        # The verified original checkpoint also contains optimizer/scheduler
        # metadata, including NumPy scalars. Only state_dict is loaded into the
        # model; optimizer metadata is discarded without being instantiated.
        checkpoint = torch.load(paths["released_checkpoint"], map_location="cpu", weights_only=False,
                                mmap=mmap_checkpoint)
        state = checkpoint["state_dict"]
        if tuple(state["dense.weight"].shape) != (150, 1024) or tuple(state["dense.bias"].shape) != (150,):
            raise ValueError("Released checkpoint does not contain the original 150-class dense head")
        self._model.load_state_dict(state, strict=True)
        if not (
            torch.equal(self._model.dense.weight.detach(), state["dense.weight"])
            and torch.equal(self._model.dense.bias.detach(), state["dense.bias"])
        ):
            raise ValueError("Released classifier head was not retained exactly")
        state_tensor_count = len(state)
        self._model.eval().requires_grad_(False)
        del state, checkpoint

        self.metadata: dict[str, Any] = {
            "model": "Original released ECGFounder 1-lead FP32 Net1D, full 150-class classifier",
            "source": "https://github.com/PKUDigitalHealth/ECGFounder",
            "runtime": "CPU; batch size 1; torch.inference_mode",
            "torch_version": torch.__version__,
            "threads": int(threads),
            "training_performed": False,
            "published_head_retained": True,
            "custom_af_head_loaded": False,
            "custom_scaler_loaded": False,
            "strict_state_dict_load": True,
            "state_tensor_count": state_tensor_count,
            "model_parameters": sum(p.numel() for p in self._model.parameters()),
            "head_weight_shape": list(self._model.dense.weight.shape),
            "output": {
                "classes": 150,
                "activation": "independent sigmoid per released task, as in upstream ptbxl_eval.py",
                "af_zero_based_index": self.af_index,
                "af_task_name": self.class_names[self.af_index],
                "class_names": list(self.class_names),
            },
            "input": {
                "lead": 0,
                "units": "physical ECG units; per-window z-score removes absolute scale",
                "window_seconds": WINDOW_SECONDS,
                "target_fs_hz": TARGET_FS,
                "target_samples": WINDOW_SAMPLES,
            },
            "preprocessing": {
                "nonfinite": "interpolate within this window only; reject all-nonfinite windows",
                "resampling": "scipy.signal.resample_poly within this window only, to 500 Hz",
                "filter": "unmodified upstream filter_bandpass: 50 Hz notch, 0.67-40 Hz bandpass, 0.4 s median baseline removal",
                "filter_scope": "completed 10-second window only; no ECG beyond this window is read",
                "normalization": "per-window float32 population mean/std; denominator std + 1e-8",
                "deployment_adaptation": "official filter restricted to completed windows; window edges differ from full-record filtering",
            },
            "thresholds": {"fixed": 0.5},
            "latency": {
                "clock": "time.perf_counter; observed local CPU wall time",
                "preprocessing_ms": "validation, within-window interpolation, resampling, official filtering, normalization",
                "inference_ms": "tensor creation, released 150-class network and independent sigmoids",
                "excluded": "10-second acquisition, network/transport, queuing, model loading",
            },
            "checkpoint_sha256_verified": checkpoint_sha,
            "checkpoint_memory_mapped": bool(mmap_checkpoint),
            "artifacts": {
                name: {
                    "path": path.relative_to(self.repo_root).as_posix(),
                    "sha256": checkpoint_sha if name == "released_checkpoint" else _sha256(path),
                    "bytes": path.stat().st_size,
                }
                for name, path in paths.items()
            },
        }

    def predict_window(self, signal: np.ndarray, fs: float) -> dict[str, Any]:
        """Return published AF probability, all 150 probabilities, and timings."""
        t0 = time.perf_counter()
        values = self._prepare_window(signal, fs)
        t1 = time.perf_counter()
        with self._torch.inference_mode():
            tensor = self._torch.from_numpy(values).reshape(1, 1, WINDOW_SAMPLES)
            logits = self._model(tensor)
            if tuple(logits.shape) != (1, len(self.class_names)):
                raise ValueError("Released model returned an unexpected output shape")
            probabilities = self._torch.sigmoid(logits)[0].cpu().numpy().copy()
        t2 = time.perf_counter()
        if not np.isfinite(probabilities).all():
            raise ValueError("Released ECGFounder returned nonfinite task probabilities")
        return {
            "probability": float(probabilities[self.af_index]),
            "class_probabilities": probabilities,
            "preprocessing_ms": (t1 - t0) * 1000.0,
            "inference_ms": (t2 - t1) * 1000.0,
        }

    def _prepare_window(self, signal: np.ndarray, fs: float) -> np.ndarray:
            """Return model input [5000] using only the supplied completed window."""
            fs = float(fs)
            if not np.isfinite(fs) or fs <= 0:
                raise ValueError("fs must be finite and positive")
            raw = np.asarray(signal, dtype=np.float64)
            if raw.ndim != 1:
                raise ValueError("signal must be a one-dimensional single-lead window")
            expected = int(round(fs * WINDOW_SECONDS))
            if expected < 2 or len(raw) != expected:
                raise ValueError(f"Expected exactly 10 seconds ({expected} samples at {fs} Hz), got {len(raw)}")
            finite = np.isfinite(raw)
            if not finite.any():
                raise ValueError("The completed window has no finite samples")
            if not finite.all():
                raw = np.interp(np.arange(len(raw)), np.flatnonzero(finite), raw[finite])
            if fs != TARGET_FS:
                ratio = (Fraction(TARGET_FS) / Fraction(str(fs))).limit_denominator(1_000_000)
                raw = resample_poly(raw, ratio.numerator, ratio.denominator)
            if len(raw) != WINDOW_SAMPLES:
                raise ValueError(f"Resampling did not produce {WINDOW_SAMPLES} samples: got {len(raw)}")
            values = np.asarray(raw, dtype=np.float32)
            values = self._filter_bandpass(values[None, :], TARGET_FS)[0].astype(np.float32)
            values = ((values - values.mean()) / (values.std() + 1e-8)).astype(np.float32)
            if not np.isfinite(values).all():
                raise ValueError("Window preprocessing produced nonfinite samples")
            return np.ascontiguousarray(values)

METHOD='ecgfounder'

def load(output=None):
    return FrozenServer(Path(__file__).resolve().parents[3],threads=2)

def predict(model,window,fs=360):
    value=model.predict_window(window,fs)
    return value['probability'],int(value['probability']>=.5)
