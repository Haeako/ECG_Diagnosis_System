"""Benchmark BPM/QRS detectors on QTDB ECG mixed with NSTDB noise."""

from __future__ import annotations

import argparse
import csv
import json
import os
import sys
import time
import tracemalloc
from pathlib import Path

import numpy as np
import wfdb
from wfdb import processing
from scipy import signal

from ecg_benchmark.rpeaks import (
    detect_r_peaks_adaptive,
    detect_r_peaks_pan_tompkins,
    estimate_bpm,
    score_r_peaks,
)

BEAT_SYMBOLS = set("NLRBAaJSVrFejE/fQ?")
DEFAULT_RECORDS = ("sel123", "sel233", "sel302", "sel307", "sel820", "sel853", "sel16420", "sel16795", "sele0106", "sele0121", "sel14046", "sel15814")
BUILTIN_DETECTORS = {"pan_tompkins": detect_r_peaks_pan_tompkins, "adaptive_pt": detect_r_peaks_adaptive}


def load_external_detectors(root: Path) -> dict[str, object]:
    """Load cloned detectors without installing or modifying their sources."""

    detectors: dict[str, object] = {}

    fast_src = root / "fast-qrs-detector" / "src"
    sys.path.insert(0, str(fast_src.resolve()))
    from fast_qrs_detector import qrs_detector
    detectors["fast_qrs"] = lambda x, fs: np.asarray(qrs_detector(x, int(round(fs)), min_rr_sec=0.24), dtype=np.int64)

    ptpp_root = root / "pan-tompkins-plus-plus"
    sys.path.insert(0, str(ptpp_root.resolve()))
    # SciPy moved window functions under signal.windows after this repo was published.
    if not hasattr(signal, "flattop"):
        signal.flattop = signal.windows.flattop  # type: ignore[attr-defined]
    from algos.pan_tompkins_plus_plus import Pan_Tompkins_Plus_Plus
    ptpp = Pan_Tompkins_Plus_Plus()
    detectors["pan_tompkins_pp"] = lambda x, fs: np.asarray(ptpp.rpeak_detection(x, int(round(fs))), dtype=np.int64)

    # The bundled 2019 HDF5 model contains the removed Keras `time_major`
    # option. Rebuild from the saved JSON after dropping that no-op field,
    # then attach it to the upstream detector without editing the clone.
    os.environ.setdefault("TF_CPP_MIN_LOG_LEVEL", "2")
    os.environ.setdefault("MPLCONFIGDIR", str((Path("outputs") / "mplconfig").resolve()))
    from keras.layers import Bidirectional, Dense, Input, LSTM
    from keras.models import Sequential
    ecg2rr_root = root / "ecg2rr"
    sys.path.insert(0, str(ecg2rr_root.resolve()))
    from ecg2rr.detector import ECG_detector
    model_path = ecg2rr_root / "ecg2rr" / "lstm.h5"
    model = Sequential([
        Input(shape=(1000, 1)),
        Bidirectional(LSTM(64, return_sequences=True)),
        Bidirectional(LSTM(64, return_sequences=True)),
        Dense(1, activation="sigmoid"),
    ])
    model.load_weights(model_path)
    lstm = ECG_detector.__new__(ECG_detector)
    lstm.model = model
    lstm.win_size, lstm.stride, lstm.threshold = 1000, 250, 0.05
    lstm.iput_fs, lstm.resample = 250, False

    def detect_lstm(x: np.ndarray, fs: float) -> np.ndarray:
        rate = int(round(fs))
        values = np.asarray(x, dtype=np.float64)
        # Upstream calls an obsolete two-argument wfdb.resample_ann API.
        # Resample explicitly so the frozen model still runs on modern WFDB.
        if rate != 250:
            model_input = signal.resample_poly(values, 250, rate)
        else:
            model_input = values
        lstm.iput_fs, lstm.resample = 250, False
        peaks, probabilities = lstm.find_peaks(model_input)
        if peaks.size > 1:
            peaks = lstm.remove_close(peaks, probabilities, threshold_ms=240)
        if rate != 250 and peaks.size:
            peaks = np.rint(peaks.astype(np.float64) * rate / 250.0).astype(np.int64)
            peaks = processing.correct_peaks(
                sig=values, peak_inds=peaks,
                search_radius=max(1, int(rate / 50)),
                smooth_window_size=20, peak_dir="up",
            )
        return np.asarray(peaks, dtype=np.int64)

    detectors["ecg2rr_lstm"] = detect_lstm
    return detectors


def load_classic_suite(root: Path) -> dict[str, object]:
    """Load the lightweight detector set used by benchmark-qrs-detectors."""

    from ecgdetectors import Detectors
    from wfdb import processing

    cache: dict[int, Detectors] = {}

    def instance(fs: float) -> Detectors:
        rate = int(round(fs))
        if rate not in cache:
            cache[rate] = Detectors(rate)
        return cache[rate]

    def wrap(method: str):
        return lambda x, fs: np.asarray(getattr(instance(fs), method)(x), dtype=np.int64)

    templates = root / "templates"

    def matched(x: np.ndarray, fs: float) -> np.ndarray:
        rate = int(round(fs))
        template = templates / f"template_{rate}hz.csv"
        if not template.exists():
            return np.array([], dtype=np.int64)
        return np.asarray(instance(fs).matched_filter_detector(x, str(template)), dtype=np.int64)

    return {
        "bench_pan_tompkins": wrap("pan_tompkins_detector"),
        "hamilton": wrap("hamilton_detector"),
        "christov": wrap("christov_detector"),
        "engzee": wrap("engzee_detector"),
        "swt": wrap("swt_detector"),
        "matched_filter": matched,
        "two_average": wrap("two_average_detector"),
        "wfdb_gqrs": lambda x, fs: np.asarray(processing.qrs.gqrs_detect(sig=x, fs=fs), dtype=np.int64),
        "wfdb_xqrs": lambda x, fs: np.asarray(processing.xqrs_detect(sig=x, fs=fs, verbose=False), dtype=np.int64),
    }


def annotations(path: Path) -> np.ndarray:
    ann = wfdb.rdann(str(path), "atr")
    return np.asarray([s for s, symbol in zip(ann.sample, ann.symbol) if symbol in BEAT_SYMBOLS], dtype=np.int64)


def mix_snr(clean: np.ndarray, noise: np.ndarray, snr_db: float) -> np.ndarray:
    cp, npower = float(np.mean(clean * clean)), float(np.mean(noise * noise))
    return clean + noise * np.sqrt(cp / max(npower, 1e-12) / (10.0 ** (snr_db / 10.0)))


def main(args: argparse.Namespace) -> list[dict[str, object]]:
    detectors = dict(BUILTIN_DETECTORS)
    if args.external_root is not None:
        detectors.update(load_external_detectors(args.external_root))
    if args.classic_suite is not None:
        detectors.update(load_classic_suite(args.classic_suite))
    if args.detectors is not None:
        missing = sorted(set(args.detectors) - set(detectors))
        if missing:
            raise ValueError(
                f"Unknown detector(s): {', '.join(missing)}. "
                f"Available: {', '.join(sorted(detectors))}"
            )
        detectors = {name: detectors[name] for name in args.detectors}
    noise, noise_meta = wfdb.rdsamp(str(args.nstdb_dir / args.noise_record))
    noise, noise_fs = noise[:, 0] - np.median(noise[:, 0]), float(noise_meta["fs"])
    rows: list[dict[str, object]] = []
    for record in args.records:
        samples, meta = wfdb.rdsamp(str(args.qtdb_dir / record))
        fs = float(meta["fs"])
        clean = samples[:, args.channel].astype(np.float64)
        clean -= np.median(clean)
        ref_all = annotations(args.qtdb_dir / record)
        size = int(round(args.window_sec * fs))
        starts = np.linspace(0, max(0, len(clean) - size), args.windows_per_record, dtype=np.int64)
        source_noise = noise
        if not np.isclose(noise_fs, fs):
            source_noise = signal.resample_poly(noise, int(fs), int(noise_fs))
        for window_index, start in enumerate(starts):
            stop = min(len(clean), int(start) + size)
            segment = clean[int(start):stop]
            ref = ref_all[(ref_all >= start) & (ref_all < stop)] - int(start)
            offset = (window_index * size) % max(1, len(source_noise) - len(segment) + 1)
            noise_window = source_noise[offset:offset + len(segment)]
            for snr in args.snr_db:
                observed = segment if np.isinf(snr) else mix_snr(segment, noise_window, snr)
                condition = "clean" if np.isinf(snr) else f"{args.noise_record}_{snr:g}dB"
                for name, detector in detectors.items():
                    detector(observed, fs)  # warm caches/import paths outside timing
                    latencies: list[float] = []
                    if args.profile_memory:
                        tracemalloc.start()
                    for _ in range(args.repeats):
                        before = time.perf_counter_ns()
                        detected = detector(observed, fs)
                        latencies.append((time.perf_counter_ns() - before) / 1e6)
                    if args.profile_memory:
                        _, peak_bytes = tracemalloc.get_traced_memory()
                        tracemalloc.stop()
                    else:
                        peak_bytes = float("nan")
                    metrics = score_r_peaks(ref, detected, fs, args.tolerance_ms)
                    rows.append({
                        "record": record, "window": window_index, "condition": condition,
                        "snr_db": snr, "detector": name, "samples": len(segment), "duration_s": len(segment) / fs,
                        "latency_ms": float(np.median(latencies)), "ram_peak_kib": peak_bytes / 1024.0,
                        "realtime_factor": (float(np.median(latencies)) / 1000.0) / (len(segment) / fs),
                        "bpm_reference": estimate_bpm(ref, fs), "bpm_estimate": estimate_bpm(detected, fs),
                        "bpm_abs_error": abs(estimate_bpm(ref, fs) - estimate_bpm(detected, fs)), **metrics,
                    })

    summary: list[dict[str, object]] = []
    for detector in detectors:
        for condition in sorted({str(row["condition"]) for row in rows}):
            group = [row for row in rows if row["detector"] == detector and row["condition"] == condition]
            tp, fp, fn = (sum(float(row[key]) for row in group) for key in ("RPeak_TP", "RPeak_FP", "RPeak_FN"))
            precision, recall = tp / max(tp + fp, 1), tp / max(tp + fn, 1)
            bpm_errors = np.asarray([row["bpm_abs_error"] for row in group], dtype=np.float64)
            summary.append({
                "detector": detector, "condition": condition, "windows": len(group), "beats": int(tp + fn),
                "precision": precision, "recall": recall,
                "f1": 2 * precision * recall / max(precision + recall, 1e-12),
                "bpm_mae": float(np.nanmean(bpm_errors)) if np.any(np.isfinite(bpm_errors)) else float("nan"),
                "latency_median_ms": float(np.median([row["latency_ms"] for row in group])),
                "latency_p95_ms": float(np.percentile([row["latency_ms"] for row in group], 95)),
                "ram_peak_max_kib": (
                    float(np.nanmax([row["ram_peak_kib"] for row in group]))
                    if np.any(np.isfinite([row["ram_peak_kib"] for row in group]))
                    else float("nan")
                ),
                "realtime_factor_median": float(np.median([row["realtime_factor"] for row in group])),
            })
    args.output.mkdir(parents=True, exist_ok=True)
    for filename, content in (("per_window.csv", rows), ("summary.csv", summary)):
        with (args.output / filename).open("w", newline="", encoding="utf-8") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(content[0])); writer.writeheader(); writer.writerows(content)
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2, allow_nan=True), encoding="utf-8")
    (args.output / "run_config.json").write_text(json.dumps({k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()}, indent=2), encoding="utf-8")
    return summary


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qtdb-dir", type=Path, default=Path("data/qtdb"))
    parser.add_argument("--nstdb-dir", type=Path, default=Path("data/nstdb"))
    parser.add_argument("--output", type=Path, default=Path("outputs/bpm_qrs_benchmark"))
    parser.add_argument("--records", nargs="+", default=list(DEFAULT_RECORDS))
    parser.add_argument("--noise-record", default="ma")
    parser.add_argument("--snr-db", nargs="+", type=float, default=[float("inf"), 12.0, 6.0, 0.0, -6.0])
    parser.add_argument("--window-sec", type=float, default=30.0)
    parser.add_argument("--windows-per-record", type=int, default=4)
    parser.add_argument("--channel", type=int, default=0)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--tolerance-ms", type=float, default=150.0)
    parser.add_argument("--external-root", type=Path, default=None,
                        help="Load cloned Fast-QRS, Pan-Tompkins++, and ecg2rr from this directory")
    parser.add_argument("--classic-suite", type=Path, default=None,
                        help="Load py-ecg-detectors and WFDB algorithms used by benchmark-qrs-detectors")
    parser.add_argument("--detectors", nargs="+", default=None,
                        help="Run only these detector names after loading the requested suites")
    parser.add_argument("--profile-memory", action=argparse.BooleanOptionalAction,
                        default=True,
                        help="Measure Python allocations with tracemalloc (disable for undistorted timing)")
    return parser.parse_args()


if __name__ == "__main__":
    print(json.dumps(main(parse_args()), indent=2, allow_nan=True))
