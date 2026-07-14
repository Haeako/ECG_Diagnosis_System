"""Final reference experiment: denoising utility, CleanGuard, and R-peak loss."""

from __future__ import annotations

import argparse
import csv
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import numpy as np
import wfdb
from scipy import signal

from ecg_benchmark.baselines import fir_bandpass, fir_highpass, wavelet_denoise
from ecg_benchmark.metrics import clean_guard, reconstruction_metrics
from ecg_benchmark.rpeaks import detect_r_peaks_pan_tompkins, score_r_peaks


DEFAULT_RECORDS = ("sel123", "sel233", "sel302", "sel307", "sel820", "sel853")
BEAT_SYMBOLS = set("NLRBAaJSVrFejE/fQ?")


@dataclass(frozen=True)
class Method:
    name: str
    family: str
    apply: Callable[[np.ndarray, float], np.ndarray]


def methods() -> tuple[Method, ...]:
    return (
        Method("noisy", "input", lambda x, fs: x.copy()),
        Method("fir_highpass_0p5hz", "fir", lambda x, fs: fir_highpass(x, fs, 0.5, 101)),
        Method("fir_bandpass_0p5_40hz", "fir", lambda x, fs: fir_bandpass(x, fs, 0.5, 40.0, 101)),
        Method("wavelet_db4", "wavelet", lambda x, fs: wavelet_denoise(x, "db4")),
        Method("wavelet_sym4", "wavelet", lambda x, fs: wavelet_denoise(x, "sym4")),
    )


def normalize(x: np.ndarray) -> np.ndarray:
    values = np.asarray(x, dtype=np.float64)
    return values - np.median(values)


def mix_at_snr(clean: np.ndarray, noise: np.ndarray, target_db: float) -> np.ndarray:
    clean_power = float(np.mean(clean**2))
    noise_power = float(np.mean(noise**2))
    if noise_power <= 0.0:
        raise ValueError("Noise window has zero power")
    scale = np.sqrt(clean_power / (10.0 ** (target_db / 10.0)) / noise_power)
    return clean + scale * noise


def read_noise(path: Path, record: str, target_fs: float) -> np.ndarray:
    samples, fields = wfdb.rdsamp(str(path / record))
    noise = normalize(samples[:, 0])
    source_fs = float(fields["fs"])
    if not np.isclose(source_fs, target_fs):
        gcd = np.gcd(int(round(source_fs)), int(round(target_fs)))
        noise = signal.resample_poly(noise, int(round(target_fs)) // gcd, int(round(source_fs)) // gcd)
    return noise


def annotation_peaks(path: Path, start: int, end: int) -> tuple[np.ndarray, str]:
    for extension in ("pu1", "pu0", "atr"):
        try:
            annotation = wfdb.rdann(str(path), extension)
        except Exception:
            continue
        samples = np.asarray(annotation.sample, dtype=np.int64)
        symbols = np.asarray(annotation.symbol)
        mask = (samples >= start) & (samples < end)
        mask &= symbols == "N" if extension != "atr" else np.asarray([s in BEAT_SYMBOLS for s in symbols])
        peaks = samples[mask] - start
        if peaks.size:
            return peaks, extension
    raise FileNotFoundError(f"No supported R-peak annotation for {path}")


def window_starts(length: int, size: int, count: int) -> np.ndarray:
    if length < size:
        return np.asarray([], dtype=np.int64)
    if count == 0:
        return np.arange(0, length - size + 1, size, dtype=np.int64)
    return np.linspace(0, length - size, count, dtype=np.int64)


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    fields = sorted({key for row in rows for key in row})
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def finite_mean(rows: list[dict[str, object]], key: str) -> float:
    values = np.asarray([float(row[key]) for row in rows if key in row], dtype=np.float64)
    return float(np.nanmean(values)) if values.size and np.any(np.isfinite(values)) else float("nan")


def prefixed(prefix: str, values: dict[str, float]) -> dict[str, float]:
    return {f"{prefix}_{key}": value for key, value in values.items()}


def run(args: argparse.Namespace) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    noise_cache: dict[float, np.ndarray] = {}
    noise_offset: dict[float, int] = {}

    for record in args.records:
        path = args.qtdb_dir / record
        samples, fields = wfdb.rdsamp(str(path))
        fs = float(fields["fs"])
        clean_record = normalize(samples[:, 0])
        size = int(round(args.window_sec * fs))
        if fs not in noise_cache:
            noise_cache[fs] = read_noise(args.nstdb_dir, args.noise_record, fs)
            noise_offset[fs] = 0

        for start_value in window_starts(len(clean_record), size, args.windows_per_record):
            start = int(start_value)
            clean = clean_record[start : start + size]
            offset = noise_offset[fs]
            if offset + size > len(noise_cache[fs]):
                offset = 0
            noise = noise_cache[fs][offset : offset + size]
            noise_offset[fs] = offset + size
            noisy = mix_at_snr(clean, noise, args.target_snr_db)
            ref_peaks, annotation = annotation_peaks(path, start, start + size)

            for method in methods():
                denoised = np.asarray(method.apply(noisy[None, :], fs))[0]
                clean_output = np.asarray(method.apply(clean[None, :], fs))[0]
                noisy_detected = detect_r_peaks_pan_tompkins(denoised, fs)
                clean_detected = detect_r_peaks_pan_tompkins(clean_output, fs)
                row: dict[str, object] = {
                    "record": record,
                    "start_sample": start,
                    "fs": fs,
                    "method": method.name,
                    "family": method.family,
                    "annotation": annotation,
                }
                row.update(reconstruction_metrics(clean[None, :], noisy[None, :], denoised[None, :]))
                row.update(clean_guard(clean[None, :], clean_output[None, :]))
                row.update(
                    prefixed(
                        "NoisyDenoised",
                        score_r_peaks(ref_peaks, noisy_detected, fs, args.tolerance_ms),
                    )
                )
                row.update(
                    prefixed(
                        "CleanPass",
                        score_r_peaks(ref_peaks, clean_detected, fs, args.tolerance_ms),
                    )
                )
                rows.append(row)

    summary: list[dict[str, object]] = []
    for method in methods():
        selected = [row for row in rows if row["method"] == method.name]
        numeric = sorted(
            key for key, value in selected[0].items()
            if isinstance(value, (int, float, np.integer, np.floating)) and key not in {"start_sample", "fs"}
        )
        item: dict[str, object] = {
            "method": method.name,
            "family": method.family,
            "n_windows": len(selected),
        }
        item.update({key: finite_mean(selected, key) for key in numeric})
        summary.append(item)

    args.output.mkdir(parents=True, exist_ok=True)
    write_csv(args.output / "per_window.csv", rows)
    write_csv(args.output / "summary.csv", summary)
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    (args.output / "run_config.json").write_text(
        json.dumps({key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()}, indent=2),
        encoding="utf-8",
    )
    return summary


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qtdb-dir", type=Path, required=True)
    parser.add_argument("--nstdb-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("outputs/qtdb_cleanguard_rpeaks"))
    parser.add_argument("--records", nargs="+", default=list(DEFAULT_RECORDS))
    parser.add_argument("--noise-record", default="bw")
    parser.add_argument("--target-snr-db", type=float, default=6.0)
    parser.add_argument("--window-sec", type=float, default=10.0)
    parser.add_argument("--windows-per-record", type=int, default=8, help="Use 0 for non-overlapping windows")
    parser.add_argument("--tolerance-ms", type=float, default=150.0)
    return parser.parse_args()


if __name__ == "__main__":
    arguments = parse_args()
    print(json.dumps(run(arguments), indent=2))
