"""Leakage-controlled three-stage noise stress test for QRS detectors."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import time
import tracemalloc
from pathlib import Path
from typing import Callable

os.environ.setdefault("MPLCONFIGDIR", str((Path("outputs") / "mplconfig").resolve()))
import matplotlib.pyplot as plt
import numpy as np
import wfdb
from scipy import signal

from ecg_benchmark.rpeaks import (
    detect_r_peaks_adaptive,
    detect_r_peaks_pan_tompkins,
    estimate_bpm,
    score_r_peaks,
)

BEAT_SYMBOLS = set("NLRBAaJSVrFejE/fQ?")
# These records were used in prior exploratory runs and are permanently
# excluded from the locked evaluation split.
DEVELOPMENT_RECORDS = {
    "sel123", "sel233", "sel302", "sel307", "sel820", "sel853",
    "sel16420", "sel16795", "sele0106", "sele0121", "sel14046", "sel15814",
}
TOLERANCES_MS = (25.0, 50.0, 100.0, 150.0)
DETECTORS: dict[str, Callable[[np.ndarray, float], np.ndarray]] = {
    "pan_tompkins": detect_r_peaks_pan_tompkins,
    "adaptive_pt_frozen": detect_r_peaks_adaptive,
}


def library_pan_tompkins() -> Callable[[np.ndarray, float], np.ndarray]:
    """Return the unmodified py-ecg-detectors Pan–Tompkins implementation."""
    from ecgdetectors import Detectors

    cache: dict[int, Detectors] = {}

    def detect(x: np.ndarray, fs: float) -> np.ndarray:
        rate = int(round(fs))
        if rate not in cache:
            cache[rate] = Detectors(rate)
        return np.asarray(cache[rate].pan_tompkins_detector(x), dtype=np.int64)

    return detect


def stable_rng(*parts: object) -> np.random.Generator:
    digest = hashlib.sha256("|".join(map(str, parts)).encode()).digest()
    return np.random.default_rng(int.from_bytes(digest[:8], "little"))


def annotation_peaks(path: Path, start: int, stop: int) -> np.ndarray:
    ann = wfdb.rdann(str(path), "atr")
    peaks = np.asarray([s for s, symbol in zip(ann.sample, ann.symbol) if symbol in BEAT_SYMBOLS], dtype=np.int64)
    return peaks[(peaks >= start) & (peaks < stop)] - start


def normalized_noise(noise: np.ndarray, size: int, key: tuple[object, ...]) -> np.ndarray:
    rng = stable_rng(*key)
    if len(noise) < size:
        noise = np.resize(noise, size)
    offset = int(rng.integers(0, max(1, len(noise) - size + 1)))
    out = np.asarray(noise[offset:offset + size], dtype=np.float64)
    return out - np.median(out)


def add_at_snr(clean: np.ndarray, noise: np.ndarray, snr_db: float) -> np.ndarray:
    clean_ac = clean - np.mean(clean)
    cp = float(np.mean(clean_ac * clean_ac))
    npower = float(np.mean(noise * noise))
    return clean + noise * np.sqrt(cp / max(npower, 1e-12) / (10.0 ** (snr_db / 10.0)))


def quantize(x: np.ndarray, bits: int) -> np.ndarray:
    limit = max(float(np.max(np.abs(x))), 1e-12)
    levels = 2 ** (bits - 1) - 1
    return np.round(np.clip(x / limit, -1.0, 1.0) * levels) * limit / levels


def dropout(x: np.ndarray, fraction: float, rng: np.random.Generator, fs: float) -> np.ndarray:
    out = x.copy()
    remaining = int(round(len(out) * fraction))
    while remaining > 0:
        width = min(remaining, max(1, int(rng.integers(max(1, int(.02 * fs)), max(2, int(.20 * fs))))))
        start = int(rng.integers(0, max(1, len(out) - width + 1)))
        out[start:start + width] = out[start - 1] if start else 0.0
        remaining -= width
    return out


def spike_noise(x: np.ndarray, rate_hz: float, amplitude: float, rng: np.random.Generator, fs: float) -> np.ndarray:
    out = x.copy()
    count = max(1, int(round(rate_hz * len(x) / fs)))
    positions = rng.choice(len(x), size=min(count, len(x)), replace=False)
    scale = max(float(np.std(x)), 1e-12) * amplitude
    out[positions] += rng.choice((-1.0, 1.0), size=len(positions)) * scale
    return out


def apply_stage2(clean: np.ndarray, fs: float, kind: str, level: float, key: tuple[object, ...]) -> np.ndarray:
    rng = stable_rng(*key)
    if kind == "powerline_50hz":
        tone = np.sin(2 * np.pi * 50.0 * np.arange(len(clean)) / fs)
        return add_at_snr(clean, tone, level)
    if kind == "clipping_percentile":
        threshold = float(np.percentile(np.abs(clean), level))
        return np.clip(clean, -threshold, threshold)
    if kind == "dropout_fraction":
        return dropout(clean, level, rng, fs)
    if kind == "gain":
        return clean * level
    if kind == "polarity":
        return -clean
    if kind == "quantization_bits":
        return quantize(clean, int(level))
    if kind == "spikes_per_second":
        return spike_noise(clean, level, 6.0, rng, fs)
    raise ValueError(kind)


def compound(clean: np.ndarray, fs: float, noises: dict[str, np.ndarray], name: str, key: tuple[object, ...]) -> np.ndarray:
    rng = stable_rng(*key)
    if name == "wearable_mild":
        x = add_at_snr(clean, noises["ma"], 12); x = add_at_snr(x, np.sin(2*np.pi*50*np.arange(len(x))/fs), 18)
        return quantize(x, 10)
    if name == "wearable_hard":
        x = add_at_snr(clean, noises["em"], 0); x = add_at_snr(x, np.sin(2*np.pi*50*np.arange(len(x))/fs), 6)
        return dropout(np.clip(x, -np.percentile(np.abs(x), 95), np.percentile(np.abs(x), 95)), .01, rng, fs)
    if name == "motion_extreme":
        x = add_at_snr(clean, noises["em"], -6); x = add_at_snr(x, noises["ma"], 0)
        return dropout(np.clip(x, -np.percentile(np.abs(x), 90), np.percentile(np.abs(x), 90)), .03, rng, fs)
    if name == "baseline_motion":
        return add_at_snr(add_at_snr(clean, noises["bw"], 0), noises["em"], 6)
    if name == "sensor_fault":
        return quantize(dropout(clean * .25, .03, rng, fs), 6)
    if name == "mixed_extreme":
        x = clean.copy()
        for noise_name in ("bw", "ma", "em"):
            x = add_at_snr(x, noises[noise_name], -6)
        x = spike_noise(x, .5, 6, rng, fs)
        return dropout(np.clip(x, -np.percentile(np.abs(x), 90), np.percentile(np.abs(x), 90)), .05, rng, fs)
    raise ValueError(name)


def conditions(clean: np.ndarray, fs: float, noise_bank: dict[str, np.ndarray], key: tuple[object, ...]):
    yield "stage1", "clean", clean
    for noise_name in ("bw", "ma", "em"):
        noise = normalized_noise(noise_bank[noise_name], len(clean), (*key, noise_name))
        for snr in (24, 18, 12, 6, 0, -6, -12):
            yield "stage1", f"{noise_name}_{snr:+d}dB", add_at_snr(clean, noise, snr)
    stage2 = {
        "powerline_50hz": (18, 12, 6, 0),
        "clipping_percentile": (99, 95, 90, 80),
        "dropout_fraction": (.005, .01, .03, .05),
        "gain": (.25, .5, 2, 4),
        "polarity": (-1,),
        "quantization_bits": (12, 10, 8, 6),
        "spikes_per_second": (.1, .5, 1, 2),
    }
    for kind, levels in stage2.items():
        for level in levels:
            yield "stage2", f"{kind}_{level:g}", apply_stage2(clean, fs, kind, level, (*key, kind, level))
    local = {name: normalized_noise(values, len(clean), (*key, name)) for name, values in noise_bank.items()}
    for name in ("wearable_mild", "wearable_hard", "motion_extreme", "baseline_motion", "sensor_fault", "mixed_extreme"):
        yield "stage3", name, compound(clean, fs, local, name, (*key, name))


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)


def aggregate(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    output = []
    keys = sorted({(str(r["stage"]), str(r["condition"]), str(r["detector"]), float(r["tolerance_ms"])) for r in rows})
    for stage, condition, detector, tolerance in keys:
        group = [r for r in rows if (r["stage"], r["condition"], r["detector"], r["tolerance_ms"]) == (stage, condition, detector, tolerance)]
        tp, fp, fn = (sum(float(r[k]) for r in group) for k in ("tp", "fp", "fn"))
        precision, recall = tp / max(tp + fp, 1), tp / max(tp + fn, 1)
        output.append({
            "stage": stage, "condition": condition, "detector": detector, "tolerance_ms": tolerance,
            "records": len({r["record"] for r in group}), "windows": len(group), "beats": int(tp + fn),
            "precision": precision, "recall": recall, "f1": 2*precision*recall/max(precision+recall, 1e-12),
            "bpm_mae": float(np.nanmean([r["bpm_abs_error"] for r in group])),
            "latency_median_ms": float(np.median([r["latency_ms"] for r in group])),
            "ram_peak_max_kib": float(max(r["ram_peak_kib"] for r in group)),
        })
    return output


def visualize(summary: list[dict[str, object]], output: Path,
              detectors: dict[str, Callable[[np.ndarray, float], np.ndarray]]) -> None:
    primary = [r for r in summary if r["tolerance_ms"] == 50.0]
    colors = {"pan_tompkins": "#777777", "adaptive_pt_frozen": "#0072B2",
              "library_pan_tompkins": "#D55E00"}
    fig, axes = plt.subplots(1, 3, figsize=(15, 4.5), sharey=True)
    for axis, noise in zip(axes, ("bw", "ma", "em")):
        for detector in detectors:
            points = []
            for row in primary:
                if row["stage"] == "stage1" and str(row["condition"]).startswith(noise + "_"):
                    snr = int(str(row["condition"]).split("_")[1].replace("dB", ""))
                    if row["detector"] == detector: points.append((snr, float(row["f1"])))
            points.sort()
            axis.plot([p[0] for p in points], [p[1] for p in points], marker="o", label=detector, color=colors[detector])
        axis.set(title=noise.upper(), xlabel="SNR (dB)", ylim=(0, 1.02)); axis.grid(alpha=.25)
    axes[0].set_ylabel("Micro F1 @ 50 ms"); axes[-1].legend(loc="lower right")
    fig.tight_layout(); fig.savefig(output / "stage1_f1_vs_snr.png", dpi=180); plt.close(fig)

    for stage in ("stage2", "stage3"):
        selected = [r for r in primary if r["stage"] == stage]
        conditions_order = sorted({str(r["condition"]) for r in selected})
        fig, axis = plt.subplots(figsize=(max(10, len(conditions_order)*.42), 5))
        x = np.arange(len(conditions_order)); width = .38
        for index, detector in enumerate(detectors):
            lookup = {str(r["condition"]): float(r["f1"]) for r in selected if r["detector"] == detector}
            axis.bar(x + (index-.5)*width, [lookup[c] for c in conditions_order], width, label=detector, color=colors[detector])
        axis.set_xticks(x, conditions_order, rotation=70, ha="right"); axis.set_ylim(0, 1.02)
        axis.set_ylabel("Micro F1 @ 50 ms"); axis.grid(axis="y", alpha=.25); axis.legend()
        fig.tight_layout(); fig.savefig(output / f"{stage}_f1.png", dpi=180); plt.close(fig)

    selected = [r for r in primary if r["detector"] == "adaptive_pt_frozen"]
    selected.sort(key=lambda r: float(r["f1"]))
    fig, axis = plt.subplots(figsize=(7, 6))
    axis.scatter([r["recall"] for r in selected], [r["precision"] for r in selected], c=[r["f1"] for r in selected], cmap="viridis", s=32)
    axis.set(xlabel="Recall", ylabel="Precision", xlim=(0, 1.02), ylim=(0, 1.02), title="Adaptive PT robustness @ 50 ms")
    axis.grid(alpha=.25); fig.tight_layout(); fig.savefig(output / "adaptive_precision_recall.png", dpi=180); plt.close(fig)


def run(args: argparse.Namespace) -> None:
    detectors = dict(DETECTORS)
    if args.library_pan_tompkins:
        detectors["library_pan_tompkins"] = library_pan_tompkins()
    if args.detectors:
        missing = sorted(set(args.detectors) - set(detectors))
        if missing:
            raise ValueError(f"Unknown detector(s): {', '.join(missing)}")
        detectors = {name: detectors[name] for name in args.detectors}
    eligible = sorted(path.stem for path in args.qtdb_dir.glob("*.atr") if path.stem not in DEVELOPMENT_RECORDS)
    if args.max_records:
        eligible = eligible[:args.max_records]
    noise_bank = {}
    for name in ("bw", "ma", "em"):
        samples, meta = wfdb.rdsamp(str(args.nstdb_dir / name)); noise_bank[name] = (samples[:, 0], float(meta["fs"]))
    rows = []
    for record in eligible:
        samples, meta = wfdb.rdsamp(str(args.qtdb_dir / record)); fs = float(meta["fs"])
        clean_record = samples[:, args.channel].astype(np.float64); clean_record -= np.median(clean_record)
        size = int(round(args.window_sec * fs)); starts = np.linspace(0, len(clean_record)-size, args.windows_per_record, dtype=np.int64)
        resampled = {}
        for name, (values, source_fs) in noise_bank.items():
            resampled[name] = values if np.isclose(source_fs, fs) else signal.resample_poly(values, int(fs), int(source_fs))
        for window, start in enumerate(starts):
            clean = clean_record[start:start+size]; ref = annotation_peaks(args.qtdb_dir/record, int(start), int(start+size))
            if ref.size < 2: continue
            for stage, condition, observed in conditions(clean, fs, resampled, (record, window, int(start))):
                for detector_name, detector in detectors.items():
                    detector(observed, fs)
                    if args.profile_memory:
                        tracemalloc.start()
                    before = time.perf_counter_ns(); detected = detector(observed, fs)
                    latency = (time.perf_counter_ns()-before)/1e6
                    if args.profile_memory:
                        _, peak_memory = tracemalloc.get_traced_memory(); tracemalloc.stop()
                    else:
                        peak_memory = float("nan")
                    bpm_error = abs(estimate_bpm(ref, fs)-estimate_bpm(detected, fs))
                    for tolerance in TOLERANCES_MS:
                        score = score_r_peaks(ref, detected, fs, tolerance)
                        rows.append({
                            "stage": stage, "condition": condition, "record": record, "window": window,
                            "start_sample": int(start), "detector": detector_name, "tolerance_ms": tolerance,
                            "tp": int(score["RPeak_TP"]), "fp": int(score["RPeak_FP"]), "fn": int(score["RPeak_FN"]),
                            "bpm_abs_error": bpm_error, "latency_ms": latency, "ram_peak_kib": peak_memory/1024,
                        })
    summary = aggregate(rows); args.output.mkdir(parents=True, exist_ok=True)
    write_csv(args.output/"per_window.csv", rows); write_csv(args.output/"summary.csv", summary)
    (args.output/"summary.json").write_text(json.dumps(summary, indent=2, allow_nan=True), encoding="utf-8")
    split = {"development_excluded": sorted(DEVELOPMENT_RECORDS), "locked_test": eligible,
             "rule": "all QTDB .atr records excluding every record used during prior detector development"}
    (args.output/"split_manifest.json").write_text(json.dumps(split, indent=2), encoding="utf-8")
    config = {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()}
    config.update({"tolerances_ms": TOLERANCES_MS, "adaptive_parameters_frozen": True,
                   "detector_source_sha256": hashlib.sha256(Path("src/ecg_benchmark/rpeaks.py").read_bytes()).hexdigest()})
    (args.output/"run_config.json").write_text(json.dumps(config, indent=2), encoding="utf-8")
    visualize(summary, args.output, detectors)
    print(json.dumps({"test_records": len(eligible), "rows": len(rows), "summary_rows": len(summary)}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qtdb-dir", type=Path, default=Path("data/qtdb")); parser.add_argument("--nstdb-dir", type=Path, default=Path("data/nstdb"))
    parser.add_argument("--output", type=Path, default=Path("outputs/noise_coverage_3stage")); parser.add_argument("--window-sec", type=float, default=30)
    parser.add_argument("--windows-per-record", type=int, default=3); parser.add_argument("--channel", type=int, default=0)
    parser.add_argument("--max-records", type=int, default=0, help="Smoke-test limit; zero uses the complete locked test split")
    parser.add_argument("--library-pan-tompkins", action="store_true",
                        help="Add py-ecg-detectors Pan-Tompkins")
    parser.add_argument("--detectors", nargs="+", default=None,
                        help="Run only these detector names")
    parser.add_argument("--profile-memory", action=argparse.BooleanOptionalAction,
                        default=True, help="Measure Python allocations with tracemalloc")
    run(parser.parse_args())
