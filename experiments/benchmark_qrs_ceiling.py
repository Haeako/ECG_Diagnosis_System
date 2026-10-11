"""Common-protocol QRS benchmark including the published SWT-SepConv ceiling.

The runner deliberately scores every detector with the same annotations, ECG
channel, windows, one-to-one matcher, and tolerances.  It does not apply a
detector-specific delay correction.
"""
from __future__ import annotations

import argparse
import csv
import ctypes
import json
import os
import subprocess
import sys
import time
from pathlib import Path

os.environ.setdefault("MPLCONFIGDIR", str((Path("outputs") / "mplconfig").resolve()))
os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")

import numpy as np
import psutil
import torch
import wfdb
from scipy import signal

from benchmark_bpm_qrs import load_classic_suite, load_external_detectors
from ecg_benchmark.rpeaks import (
    detect_r_peaks_adaptive,
    detect_r_peaks_adaptive_portable,
    detect_r_peaks_adaptive_v2,
    detect_r_peaks_pan_tompkins,
    estimate_bpm,
    score_r_peaks,
)
from evaluate_mitdb_external import BEATS, TOLS, firmware_detector

ROOT = Path(__file__).resolve().parents[1]
SOTA_ROOT = ROOT / "external" / "ECG_peak_detection"


class SwtSepConvDetector:
    """Adapter around the authors' preprocessing, checkpoint and localizer."""

    def __init__(self, root: Path = SOTA_ROOT):
        sys.path.insert(0, str(root))
        from utils.db_loader import DB_loading
        from utils.sep_conv import Sep_conv_detector, count_parameters

        # The upstream constructor reads an Excel database catalogue, which is
        # unnecessary for the signal-only adapter and would add openpyxl solely
        # for metadata.  Its preprocessing methods are otherwise stateless.
        self.preprocessor = DB_loading.__new__(DB_loading)
        self.model = Sep_conv_detector()
        checkpoint = root / "model" / "trained_model.pt"
        state = torch.load(checkpoint, map_location="cpu", weights_only=True)
        self.model.load_state_dict(state)
        self.model.eval()
        self.parameters = int(count_parameters(self.model))
        self.artifact_bytes = checkpoint.stat().st_size

    def __call__(self, ecg: np.ndarray, fs: float) -> np.ndarray:
        from utils.localizer import Localizer

        ecg360 = self.preprocessor.resample_ecg(np.asarray(ecg, float), fs, 360)
        feature = self.preprocessor.transform(ecg360, use_swt=True)
        padding = -len(feature) % 2048
        if padding:
            feature = np.pad(feature, ((0, padding), (0, 0)))
        tensor = torch.from_numpy(np.swapaxes(feature, 0, 1)[None]).float()
        with torch.inference_mode():
            pred = torch.sigmoid(self.model(tensor)).squeeze().numpy()[: len(ecg360)]
        localizer = Localizer(np.empty(0, dtype=int), pred, np.zeros(len(pred), dtype=int))
        localizer.find_peak()
        return np.rint(localizer.list_peak.astype(float) * fs / 360).astype(np.int64)


def adaptive_portable_int16_detector(output: Path):
    class Trace(ctypes.Structure):
        _fields_ = [
            ("envelope", ctypes.c_float), ("signal_level", ctypes.c_float),
            ("noise_level", ctypes.c_float), ("event_index", ctypes.c_int32),
        ]

    dll = output / "adaptive_pt_portable_int16.dll"
    subprocess.run([
        "gcc", "-std=c99", "-O2", "-shared",
        str(ROOT / "experiments/adaptive_pt_portable_int16_host.c"),
        "-o", str(dll),
    ], check=True)
    library = ctypes.CDLL(str(dll))
    run = library.adaptive_pt_run_int16
    run.argtypes = [
        np.ctypeslib.ndpointer(np.int16, flags="C_CONTIGUOUS"), ctypes.c_uint32,
        np.ctypeslib.ndpointer(np.uint32, flags="C_CONTIGUOUS"),
        ctypes.POINTER(Trace),
    ]
    run.restype = ctypes.c_uint32

    def detect(ecg: np.ndarray, fs: float) -> np.ndarray:
        count360 = int(round(len(ecg) * 360 / fs))
        ecg360 = np.asarray(ecg, dtype=np.float64) if np.isclose(fs, 360) else signal.resample(ecg, count360)
        adc = np.ascontiguousarray(np.clip(np.rint(ecg360 * 200.0), -32768, 32767), dtype=np.int16)
        peaks = np.empty(len(adc), dtype=np.uint32)
        count = int(run(adc, len(adc), peaks, None))
        return np.rint(peaks[:count].astype(float) * fs / 360).astype(np.int64)

    return detect


def select_channel(meta: dict, policy: str) -> int:
    if policy == "first":
        return 0
    names = [str(x).upper().replace(" ", "") for x in meta.get("sig_name", [])]
    for preferred in ("MLII", "II"):
        if preferred in names:
            return names.index(preferred)
    return 0


def record_names(database: Path) -> list[str]:
    records_file = database / "RECORDS"
    if records_file.exists():
        return [x.strip() for x in records_file.read_text().splitlines() if x.strip()]
    return sorted(x.stem for x in database.glob("*.dat"))


def firmware_at_360(output: Path):
    native = firmware_detector(output)

    def detect(ecg: np.ndarray, fs: float) -> np.ndarray:
        if np.isclose(fs, 360):
            return native(ecg, 360)
        count = int(round(len(ecg) * 360 / fs))
        ecg360 = signal.resample(np.asarray(ecg, float), count)
        peaks360 = native(ecg360, 360)
        return np.rint(peaks360.astype(float) * fs / 360).astype(np.int64)

    return detect


def write_csv(path: Path, rows: list[dict]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", type=Path, required=True)
    parser.add_argument("--annotation", default="atr")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--windows", type=int, default=2)
    parser.add_argument("--window-sec", type=float, default=60)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--max-records", type=int)
    parser.add_argument("--exclude-records", nargs="*", default=[])
    parser.add_argument("--channel", choices=("first", "lead-ii"), default="first")
    parser.add_argument("--detectors", nargs="+", default=[
        "swt_sepconv", "firmware_current", "local_pan_tompkins",
        "adaptive_pt_v1", "adaptive_pt_v2_frozen", "fast_qrs",
        "wfdb_gqrs", "wfdb_xqrs",
    ])
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    sota = SwtSepConvDetector()
    methods = {
        "swt_sepconv": sota,
        "firmware_current": firmware_at_360(args.output),
        "local_pan_tompkins": detect_r_peaks_pan_tompkins,
        "adaptive_pt_v1": detect_r_peaks_adaptive,
        "adaptive_pt_v2_frozen": detect_r_peaks_adaptive_v2,
        "adaptive_pt_portable": detect_r_peaks_adaptive_portable,
        "adaptive_pt_portable_int16": adaptive_portable_int16_detector(args.output),
    }
    methods.update(load_external_detectors(ROOT / "external"))
    methods.update(load_classic_suite(ROOT / "external" / "benchmark-qrs-detectors"))
    missing = set(args.detectors) - set(methods)
    if missing:
        raise ValueError(f"Unknown detectors: {sorted(missing)}")
    methods = {name: methods[name] for name in args.detectors}

    rows: list[dict] = []
    used_records: list[str] = []
    skipped: dict[str, str] = {}
    rss_before = psutil.Process().memory_info().rss
    peak_rss = rss_before
    names = [name for name in record_names(args.database) if name not in set(args.exclude_records)]
    if args.max_records is not None:
        names = names[: args.max_records]
    for ri, record in enumerate(names, 1):
        path = args.database / record
        try:
            header = wfdb.rdheader(str(path))
            channel = select_channel(header.__dict__, args.channel)
            values, meta = wfdb.rdsamp(str(path), channels=[channel])
            ann = wfdb.rdann(str(path), args.annotation)
        except Exception as exc:
            skipped[record] = f"{type(exc).__name__}: {exc}"
            continue
        fs = float(meta["fs"])
        ecg = values[:, 0]
        ecg = ecg - np.nanmedian(ecg)
        refs = np.asarray([s for s, symbol in zip(ann.sample, ann.symbol) if symbol in BEATS], dtype=np.int64)
        size = int(round(args.window_sec * fs))
        if len(ecg) < size or not len(refs):
            skipped[record] = "record too short or contains no beat annotations"
            continue
        used_records.append(record)
        starts = np.linspace(0, len(ecg) - size, args.windows, dtype=np.int64)
        for wi, start in enumerate(starts):
            segment = ecg[start : start + size]
            reference = refs[(refs >= start) & (refs < start + size)] - start
            for name, detector in methods.items():
                detector(segment, fs)
                timings = []
                estimate = np.empty(0, dtype=np.int64)
                for _ in range(args.repeats):
                    tick = time.perf_counter_ns()
                    estimate = np.asarray(detector(segment, fs), dtype=np.int64)
                    timings.append((time.perf_counter_ns() - tick) / 1e6)
                peak_rss = max(peak_rss, psutil.Process().memory_info().rss)
                bpm_error = abs(estimate_bpm(reference, fs) - estimate_bpm(estimate, fs))
                for tolerance in TOLS:
                    score = score_r_peaks(reference, estimate, fs, tolerance)
                    rows.append({
                        "record": record, "window": wi, "channel": channel,
                        "detector": name, "tolerance_ms": tolerance,
                        "tp": int(score["RPeak_TP"]), "fp": int(score["RPeak_FP"]),
                        "fn": int(score["RPeak_FN"]), "bpm_abs_error": bpm_error,
                        "latency_ms": float(np.median(timings)),
                    })
        print(f"[{ri:03d}] {record}", flush=True)

    if not rows:
        raise RuntimeError(f"No evaluable records in {args.database}; skipped={skipped}")
    summary: list[dict] = []
    for name in methods:
        for tolerance in TOLS:
            group = [r for r in rows if r["detector"] == name and r["tolerance_ms"] == tolerance]
            tp, fp, fn = (sum(r[key] for r in group) for key in ("tp", "fp", "fn"))
            precision = tp / max(tp + fp, 1)
            recall = tp / max(tp + fn, 1)
            summary.append({
                "detector": name, "tolerance_ms": tolerance,
                "records": len(used_records), "windows": len(group), "beats": tp + fn,
                "precision": precision, "recall": recall,
                "f1": 2 * precision * recall / max(precision + recall, 1e-12),
                "bpm_mae": float(np.nanmean([r["bpm_abs_error"] for r in group])),
                "latency_median_ms": float(np.median([r["latency_ms"] for r in group])),
                "latency_p95_ms": float(np.percentile([r["latency_ms"] for r in group], 95)),
            })
    write_csv(args.output / "per_window.csv", rows)
    write_csv(args.output / "summary.csv", summary)
    manifest = {
        "database": str(args.database.resolve()), "records": used_records, "skipped": skipped,
        "excluded_records": args.exclude_records,
        "annotation": args.annotation, "channel_policy": args.channel,
        "windows_per_record": args.windows, "window_sec": args.window_sec,
        "tolerances_ms": TOLS, "detectors": list(methods), "timing_repeats": args.repeats,
        "delay_correction": False, "swt_sepconv_parameters": sota.parameters,
        "firmware_resampled_to_hz": 360,
        "swt_sepconv_checkpoint_bytes": sota.artifact_bytes,
        "process_rss_delta_kib_warning": (peak_rss - rss_before) / 1024,
        "memory_note": "Process RSS is host-only and must not be interpreted as ESP32 working RAM.",
    }
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    (args.output / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(json.dumps({"records": len(used_records), "detectors": len(methods), "rows": len(rows)}, indent=2))


if __name__ == "__main__":
    main()
