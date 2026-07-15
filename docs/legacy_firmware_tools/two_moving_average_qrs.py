# -*- coding: utf-8 -*-
"""Two Moving Average QRS detector prototype for ESP32 ECG CSV files.

This is intentionally written close to an MCU implementation:
- no scipy dependency
- moving averages are simple sliding sums
- R candidates come from blocks where short MA > long MA + offset
- each block is remapped to the strongest ECG extremum
- final peaks pass a refractory rule
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import numpy as np


def collect_csv_files(paths: list[Path]) -> list[Path]:
    files: list[Path] = []
    for path in paths:
        if path.is_dir():
            files.extend(sorted(path.glob("ecg_*.csv")))
        elif path.is_file():
            files.append(path)
        else:
            raise FileNotFoundError(f"Path not found: {path}")

    files = sorted(files)
    if not files:
        raise FileNotFoundError("No ECG CSV files found")
    return files


def load_ecg_csv(file_path: Path, signal_column: str) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    timestamps: list[float] = []
    signal_values: list[float] = []
    csv_peak_indices: list[int] = []
    first_timestamp: int | None = None

    with file_path.open("r", newline="") as file:
        reader = csv.DictReader(file)
        fieldnames = set(reader.fieldnames or [])
        required = {"timestamp_ms", signal_column}
        if not required.issubset(fieldnames):
            missing = ", ".join(sorted(required - fieldnames))
            raise ValueError(f"{file_path} missing column(s): {missing}")

        for sample_index, row in enumerate(reader):
            timestamp_ms = int(float(row["timestamp_ms"]))
            if first_timestamp is None:
                first_timestamp = timestamp_ms
            timestamps.append((timestamp_ms - first_timestamp) / 1000.0)
            signal_values.append(float(row[signal_column]))
            if int(float(row.get("is_peak") or 0)) != 0:
                csv_peak_indices.append(sample_index)

    if not signal_values:
        raise ValueError(f"{file_path} contains no samples")

    return (
        np.asarray(timestamps),
        np.asarray(signal_values),
        np.asarray(csv_peak_indices, dtype=int),
    )


def moving_average_causal(values: np.ndarray, window: int) -> np.ndarray:
    """Causal moving average matching a small ring-buffer MCU implementation."""
    window = max(1, int(window))
    out = np.empty_like(values, dtype=float)
    acc = 0.0
    for i, value in enumerate(values):
        acc += value
        if i >= window:
            acc -= values[i - window]
            out[i] = acc / window
        else:
            out[i] = acc / (i + 1)
    return out


def find_true_r_index(ecg: np.ndarray, start: int, end: int, polarity: str) -> int:
    segment = ecg[start:end]
    if len(segment) == 0:
        return start
    if polarity == "positive":
        local = int(np.argmax(segment))
    elif polarity == "negative":
        local = int(np.argmin(segment))
    else:
        local = int(np.argmax(np.abs(segment)))
    return start + local


def two_moving_average_detect(
    ecg: np.ndarray,
    fs: int,
    qrs_ms: float = 111.0,
    beat_ms: float = 667.0,
    beta: float = 0.02,
    min_rr_ms: float = 250.0,
    min_block_ms: float = 40.0,
    search_ms: float = 80.0,
    min_amp: float = 250.0,
    polarity: str = "abs",
) -> tuple[np.ndarray, dict[str, np.ndarray]]:
    """Return detected R indices and debug arrays.

    The common two-MA idea:
    squared ECG -> short MA estimates QRS energy -> long MA estimates beat/noise
    -> QRS blocks where short MA is above adaptive threshold.
    """
    if len(ecg) == 0:
        return np.asarray([], dtype=int), {}

    qrs_w = max(1, round(qrs_ms * fs / 1000.0))
    beat_w = max(qrs_w + 1, round(beat_ms * fs / 1000.0))
    min_rr = max(1, round(min_rr_ms * fs / 1000.0))
    min_block = max(1, round(min_block_ms * fs / 1000.0))
    search = max(1, round(search_ms * fs / 1000.0))

    centered = ecg - np.mean(ecg)
    squared = centered * centered
    qrs_ma = moving_average_causal(squared, qrs_w)
    beat_ma = moving_average_causal(squared, beat_w)
    threshold = beat_ma + beta * np.mean(squared)
    above = qrs_ma > threshold

    candidates: list[int] = []
    i = 0
    while i < len(above):
        if not above[i]:
            i += 1
            continue

        start = i
        while i < len(above) and above[i]:
            i += 1
        end = i
        if (end - start) < min_block:
            continue

        search_start = max(0, start - search)
        search_end = min(len(ecg), end + search)
        r_index = find_true_r_index(ecg, search_start, search_end, polarity)
        if abs(ecg[r_index]) >= min_amp:
            candidates.append(r_index)

    candidates = sorted(set(candidates))
    accepted: list[int] = []
    for candidate in candidates:
        if not accepted:
            accepted.append(candidate)
            continue

        if candidate - accepted[-1] < min_rr:
            if abs(ecg[candidate]) > abs(ecg[accepted[-1]]):
                accepted[-1] = candidate
        else:
            accepted.append(candidate)

    debug = {
        "squared": squared,
        "qrs_ma": qrs_ma,
        "beat_ma": beat_ma,
        "threshold": threshold,
        "above": above.astype(float),
    }
    return np.asarray(accepted, dtype=int), debug


def plot_detection(
    times: np.ndarray,
    ecg: np.ndarray,
    detected: np.ndarray,
    csv_peaks: np.ndarray,
    debug: dict[str, np.ndarray],
    output: Path,
    title: str,
    signal_column: str,
) -> None:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    valid_detected = detected[(detected >= 0) & (detected < len(ecg))]
    valid_csv = csv_peaks[(csv_peaks >= 0) & (csv_peaks < len(ecg))]

    fig, axes = plt.subplots(3, 1, sharex=True, figsize=(16, 8.5))
    axes[0].plot(times, ecg, linewidth=0.65, color="#1f77b4", label=signal_column)
    axes[0].scatter(times[valid_detected], ecg[valid_detected], s=24, color="#d62728",
                    label="Two-MA R")
    if len(valid_csv):
        axes[0].scatter(times[valid_csv], ecg[valid_csv], s=38, facecolors="none",
                        edgecolors="#2ca02c", label="CSV is_peak")
    axes[0].set_ylabel(signal_column)
    axes[0].set_title(title)
    axes[0].grid(True, alpha=0.25)
    axes[0].legend(loc="upper right")

    axes[1].plot(times, debug["qrs_ma"], linewidth=0.8, label="MA QRS/short")
    axes[1].plot(times, debug["threshold"], linewidth=0.8, label="MA beat + offset")
    axes[1].fill_between(times, 0, debug["qrs_ma"].max(), where=debug["above"] > 0,
                         color="#ffcc66", alpha=0.25, label="QRS block")
    axes[1].set_ylabel("energy")
    axes[1].grid(True, alpha=0.25)
    axes[1].legend(loc="upper right")

    rr_ms = np.diff(times[valid_detected]) * 1000.0
    bpm = np.where(rr_ms > 0, 60000.0 / rr_ms, np.nan)
    if len(bpm):
        axes[2].plot(times[valid_detected][1:], bpm, marker="o", markersize=3,
                     linewidth=0.9, color="#9467bd")
    axes[2].set_ylabel("BPM from RR")
    axes[2].set_xlabel("Time (s)")
    axes[2].grid(True, alpha=0.25)

    fig.tight_layout()
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(output, dpi=160)
    plt.close(fig)


def main() -> None:
    parser = argparse.ArgumentParser(description="Run Two Moving Average QRS detection on ECG CSV files.")
    parser.add_argument("paths", nargs="*", default=[Path("pre_move_data")], type=Path)
    parser.add_argument("--fs", type=int, default=360)
    parser.add_argument("--column", default="filtered")
    parser.add_argument("--qrs-ms", type=float, default=111.0)
    parser.add_argument("--beat-ms", type=float, default=667.0)
    parser.add_argument("--beta", type=float, default=0.02)
    parser.add_argument("--min-rr-ms", type=float, default=250.0)
    parser.add_argument("--min-block-ms", type=float, default=40.0)
    parser.add_argument("--search-ms", type=float, default=80.0)
    parser.add_argument("--min-amp", type=float, default=250.0)
    parser.add_argument("--polarity", choices=["abs", "positive", "negative"], default="abs")
    parser.add_argument("-o", "--output", type=Path, default=Path("visualize") / "two_moving_average")
    args = parser.parse_args()

    files = collect_csv_files(args.paths)
    print(f"Loading {len(files)} file(s)")

    total_samples = 0
    total_csv_peaks = 0
    total_detected = 0
    for file_path in files:
        times, ecg, csv_peaks = load_ecg_csv(file_path, args.column)
        detected, debug = two_moving_average_detect(
            ecg=ecg,
            fs=args.fs,
            qrs_ms=args.qrs_ms,
            beat_ms=args.beat_ms,
            beta=args.beta,
            min_rr_ms=args.min_rr_ms,
            min_block_ms=args.min_block_ms,
            search_ms=args.search_ms,
            min_amp=args.min_amp,
            polarity=args.polarity,
        )
        output = args.output / f"{file_path.stem}_two_ma.png"
        title = f"Two Moving Average QRS Detection - {file_path.name}"
        plot_detection(times, ecg, detected, csv_peaks, debug, output, title, args.column)

        duration_s = times[-1] - times[0] if len(times) > 1 else 0.0
        bpm_est = (len(detected) * 60.0 / duration_s) if duration_s > 0 else 0.0
        total_samples += len(ecg)
        total_csv_peaks += len(csv_peaks)
        total_detected += len(detected)
        print(
            f"{file_path.name}: samples={len(ecg)}, csv_peak={len(csv_peaks)}, "
            f"two_ma_peak={len(detected)}, bpm_est={bpm_est:.1f}, saved={output}"
        )

    print(f"Total samples: {total_samples}")
    print(f"Total CSV is_peak: {total_csv_peaks}")
    print(f"Total Two-MA peaks: {total_detected}")
    print(f"Saved {len(files)} plot(s) in: {args.output}")


if __name__ == "__main__":
    main()
