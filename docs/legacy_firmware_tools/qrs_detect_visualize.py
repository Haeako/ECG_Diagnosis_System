#!/usr/bin/env python3
"""Detect QRS complexes in ESP32 ECG CSV recordings and save plots."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path


def moving_average(values: list[float], window: int) -> list[float]:
    if window <= 1:
        return values[:]

    half = window // 2
    prefix = [0.0]
    for value in values:
        prefix.append(prefix[-1] + value)

    averaged: list[float] = []
    for idx in range(len(values)):
        start = max(0, idx - half)
        end = min(len(values), idx + half + 1)
        averaged.append((prefix[end] - prefix[start]) / (end - start))
    return averaged


def median(values: list[float]) -> float:
    ordered = sorted(values)
    if not ordered:
        return 0.0
    middle = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[middle]
    return (ordered[middle - 1] + ordered[middle]) / 2.0


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    if not ordered:
        return 0.0
    position = min(max(fraction, 0.0), 1.0) * (len(ordered) - 1)
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def robust_sigma(values: list[float]) -> float:
    center = median(values)
    deviations = [abs(value - center) for value in values]
    return median(deviations) * 1.4826


def estimate_sample_rate(timestamps_ms: list[int]) -> float:
    intervals = [
        timestamps_ms[idx] - timestamps_ms[idx - 1]
        for idx in range(1, len(timestamps_ms))
        if timestamps_ms[idx] > timestamps_ms[idx - 1]
    ]
    if not intervals:
        return 250.0
    return 1000.0 / median([float(interval) for interval in intervals])


def load_ecg_csv(path: Path) -> dict[str, list[float] | list[int]]:
    timestamps_ms: list[int] = []
    filtered: list[float] = []
    firmware_peaks: list[int] = []
    firmware_bpm: list[float] = []

    with path.open("r", newline="") as file:
        reader = csv.DictReader(file)
        required = {"timestamp_ms", "filtered"}
        if not required.issubset(reader.fieldnames or set()):
            raise ValueError(f"{path} is missing one of: {', '.join(sorted(required))}")

        for idx, row in enumerate(reader):
            timestamps_ms.append(int(row["timestamp_ms"]))
            filtered.append(float(row["filtered"]))
            if int(row.get("is_peak") or 0) != 0:
                firmware_peaks.append(idx)
            firmware_bpm.append(float(row.get("bpm") or 0.0))

    if not timestamps_ms:
        raise ValueError(f"{path} contains no samples")

    return {
        "timestamps_ms": timestamps_ms,
        "filtered": filtered,
        "firmware_peaks": firmware_peaks,
        "firmware_bpm": firmware_bpm,
    }


def detect_qrs(timestamps_ms: list[int], filtered: list[float]) -> dict[str, list[float] | list[int] | float]:
    sample_rate = estimate_sample_rate(timestamps_ms)
    baseline_window = max(3, int(sample_rate * 0.20))
    integration_window = max(3, int(sample_rate * 0.10))
    refractory_samples = max(1, int(sample_rate * 0.22))
    search_radius = max(1, int(sample_rate * 0.05))

    baseline = moving_average(filtered, baseline_window)
    centered = [value - base for value, base in zip(filtered, baseline)]
    derivative = [0.0]
    derivative.extend(centered[idx] - centered[idx - 1] for idx in range(1, len(centered)))
    energy = moving_average([value * value for value in derivative], integration_window)

    noise = median(energy)
    spread = robust_sigma(energy)
    threshold = max(noise + 4.0 * spread, percentile(energy, 0.80) * 0.35)

    candidates: list[int] = []
    last_peak = -refractory_samples
    idx = 1
    while idx < len(energy) - 1:
        is_local_max = energy[idx] >= energy[idx - 1] and energy[idx] > energy[idx + 1]
        if is_local_max and energy[idx] >= threshold and idx - last_peak >= refractory_samples:
            start = max(0, idx - search_radius)
            end = min(len(centered), idx + search_radius + 1)
            peak = max(range(start, end), key=lambda candidate: centered[candidate])
            if candidates and peak - candidates[-1] < refractory_samples:
                if centered[peak] > centered[candidates[-1]]:
                    candidates[-1] = peak
                    last_peak = peak
            else:
                candidates.append(peak)
                last_peak = peak
            idx = max(idx + 1, last_peak + refractory_samples // 2)
        else:
            idx += 1

    bpm_times: list[float] = []
    bpm_values: list[float] = []
    for previous, current in zip(candidates, candidates[1:]):
        rr_seconds = (timestamps_ms[current] - timestamps_ms[previous]) / 1000.0
        if rr_seconds > 0:
            bpm_times.append((timestamps_ms[current] - timestamps_ms[0]) / 1000.0)
            bpm_values.append(60.0 / rr_seconds)

    return {
        "sample_rate": sample_rate,
        "centered": centered,
        "energy": energy,
        "threshold": threshold,
        "peaks": candidates,
        "bpm_times": bpm_times,
        "bpm_values": bpm_values,
    }


def interpolate_baseline(
    timestamps_ms: list[int],
    signal: list[float],
    peaks: list[int],
) -> dict[str, list[float] | list[int]]:
    if len(peaks) < 2:
        baseline = moving_average(signal, max(3, len(signal) // 12))
        return {
            "midpoints": [],
            "baseline": baseline,
            "corrected": [value - base for value, base in zip(signal, baseline)],
        }

    midpoints: list[int] = []
    for previous, current in zip(peaks, peaks[1:]):
        midpoint_time = (timestamps_ms[previous] + timestamps_ms[current]) / 2.0
        midpoint = min(
            range(previous, current + 1),
            key=lambda idx: abs(timestamps_ms[idx] - midpoint_time),
        )
        midpoints.append(midpoint)

    anchor_indices = [0, *midpoints, len(signal) - 1]
    anchor_values = [signal[midpoints[0]], *[signal[idx] for idx in midpoints], signal[midpoints[-1]]]
    baseline = [0.0] * len(signal)

    for left_anchor, right_anchor, left_value, right_value in zip(
        anchor_indices,
        anchor_indices[1:],
        anchor_values,
        anchor_values[1:],
    ):
        span = max(1, right_anchor - left_anchor)
        for idx in range(left_anchor, right_anchor + 1):
            fraction = (idx - left_anchor) / span
            baseline[idx] = left_value + (right_value - left_value) * fraction

    corrected = [value - base for value, base in zip(signal, baseline)]
    return {
        "midpoints": midpoints,
        "baseline": baseline,
        "corrected": corrected,
    }


def plot_result(
    path: Path,
    output: Path,
    timestamps_ms: list[int],
    filtered: list[float],
    firmware_peaks: list[int],
    detection: dict[str, list[float] | list[int] | float],
    baseline_data: dict[str, list[float] | list[int]] | None = None,
) -> None:
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        save_svg_result(
            path,
            output.with_suffix(".svg"),
            timestamps_ms,
            filtered,
            firmware_peaks,
            detection,
            baseline_data,
        )
        return

    times = [(timestamp - timestamps_ms[0]) / 1000.0 for timestamp in timestamps_ms]
    peaks = detection["peaks"]
    bpm_times = detection["bpm_times"]
    bpm_values = detection["bpm_values"]

    if baseline_data is None:
        fig, axes = plt.subplots(
            2,
            1,
            sharex=True,
            figsize=(14, 7),
            gridspec_kw={"height_ratios": [3, 1]},
        )
        axis_ecg, axis_bpm = axes
        axis_corrected = None
    else:
        fig, axes = plt.subplots(
            3,
            1,
            sharex=True,
            figsize=(14, 9),
            gridspec_kw={"height_ratios": [3, 3, 1]},
        )
        axis_ecg, axis_corrected, axis_bpm = axes

    axis_ecg.plot(times, filtered, linewidth=0.8, color="#2364aa", label="Filtered ECG")
    if baseline_data is not None:
        baseline = baseline_data["baseline"]
        midpoints = baseline_data["midpoints"]
        axis_ecg.plot(times, baseline, linewidth=1.6, color="#7d3c98", label="Mid-beat baseline")
        axis_ecg.scatter(
            [times[idx] for idx in midpoints],
            [filtered[idx] for idx in midpoints],
            s=28,
            color="#7d3c98",
            marker="x",
            label="Mid-beat points",
            zorder=5,
        )
    axis_ecg.scatter(
        [times[idx] for idx in peaks],
        [filtered[idx] for idx in peaks],
        s=32,
        color="#d7263d",
        marker="o",
        label="Detected QRS",
        zorder=4,
    )
    if firmware_peaks:
        axis_ecg.scatter(
            [times[idx] for idx in firmware_peaks],
            [filtered[idx] for idx in firmware_peaks],
            s=34,
            facecolors="none",
            edgecolors="#1b998b",
            marker="s",
            label="CSV is_peak",
            zorder=3,
        )
    axis_ecg.set_title(f"{path.name} - QRS detection and baseline reconstruction")
    axis_ecg.set_ylabel("Filtered ADC")
    axis_ecg.grid(True, alpha=0.25)
    axis_ecg.legend(loc="upper right")

    if axis_corrected is not None and baseline_data is not None:
        corrected = baseline_data["corrected"]
        axis_corrected.plot(times, corrected, linewidth=0.8, color="#2f855a", label="Baseline-corrected ECG")
        axis_corrected.axhline(0, linewidth=0.8, color="#777", alpha=0.45)
        axis_corrected.scatter(
            [times[idx] for idx in peaks],
            [corrected[idx] for idx in peaks],
            s=30,
            color="#d7263d",
            marker="o",
            zorder=4,
        )
        axis_corrected.set_ylabel("Corrected ADC")
        axis_corrected.grid(True, alpha=0.25)
        axis_corrected.legend(loc="upper right")

    axis_bpm.plot(bpm_times, bpm_values, linewidth=1.2, color="#f46036")
    axis_bpm.set_xlabel("Time (s)")
    axis_bpm.set_ylabel("BPM")
    axis_bpm.grid(True, alpha=0.25)

    fig.tight_layout()
    fig.savefig(output, dpi=160)
    plt.close(fig)


def plot_qrs_only(
    path: Path,
    output: Path,
    timestamps_ms: list[int],
    filtered: list[float],
    firmware_peaks: list[int],
    detection: dict[str, list[float] | list[int] | float],
) -> None:
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        save_svg_result(path, output.with_suffix(".svg"), timestamps_ms, filtered, firmware_peaks, detection)
        return

    times = [(timestamp - timestamps_ms[0]) / 1000.0 for timestamp in timestamps_ms]
    peaks = detection["peaks"]
    bpm_times = detection["bpm_times"]
    bpm_values = detection["bpm_values"]

    fig, (axis_ecg, axis_bpm) = plt.subplots(
        2,
        1,
        sharex=True,
        figsize=(14, 7),
        gridspec_kw={"height_ratios": [3, 1]},
    )

    axis_ecg.plot(times, filtered, linewidth=0.8, color="#2364aa", label="Filtered ECG")
    axis_ecg.scatter(
        [times[idx] for idx in peaks],
        [filtered[idx] for idx in peaks],
        s=32,
        color="#d7263d",
        marker="o",
        label="Detected QRS",
        zorder=4,
    )
    if firmware_peaks:
        axis_ecg.scatter(
            [times[idx] for idx in firmware_peaks],
            [filtered[idx] for idx in firmware_peaks],
            s=34,
            facecolors="none",
            edgecolors="#1b998b",
            marker="s",
            label="CSV is_peak",
            zorder=3,
        )
    axis_ecg.set_title(f"{path.name} - QRS detection")
    axis_ecg.set_ylabel("Filtered ADC")
    axis_ecg.grid(True, alpha=0.25)
    axis_ecg.legend(loc="upper right")

    axis_bpm.plot(bpm_times, bpm_values, linewidth=1.2, color="#f46036")
    axis_bpm.set_xlabel("Time (s)")
    axis_bpm.set_ylabel("BPM")
    axis_bpm.grid(True, alpha=0.25)

    fig.tight_layout()
    fig.savefig(output, dpi=160)
    plt.close(fig)


def scale_points(
    xs: list[float],
    ys: list[float],
    width: int,
    height: int,
    left: int,
    top: int,
    right: int,
    bottom: int,
) -> tuple[list[tuple[float, float]], tuple[float, float, float, float]]:
    min_x = min(xs)
    max_x = max(xs)
    min_y = min(ys)
    max_y = max(ys)
    if math.isclose(min_x, max_x):
        max_x = min_x + 1.0
    if math.isclose(min_y, max_y):
        max_y = min_y + 1.0

    plot_width = width - left - right
    plot_height = height - top - bottom
    points = []
    for x_value, y_value in zip(xs, ys):
        x = left + (x_value - min_x) / (max_x - min_x) * plot_width
        y = top + (max_y - y_value) / (max_y - min_y) * plot_height
        points.append((x, y))
    return points, (min_x, max_x, min_y, max_y)


def polyline(points: list[tuple[float, float]]) -> str:
    return " ".join(f"{x:.1f},{y:.1f}" for x, y in points)


def save_svg_result(
    path: Path,
    output: Path,
    timestamps_ms: list[int],
    filtered: list[float],
    firmware_peaks: list[int],
    detection: dict[str, list[float] | list[int] | float],
    baseline_data: dict[str, list[float] | list[int]] | None = None,
) -> None:
    width = 1400
    height = 900 if baseline_data is not None else 720
    left = 72
    right = 28
    ecg_top = 64
    ecg_bottom = 500 if baseline_data is not None else 270
    corrected_top = 455
    corrected_bottom = 270
    bpm_top = 700 if baseline_data is not None else 520
    bpm_bottom = 62

    times = [(timestamp - timestamps_ms[0]) / 1000.0 for timestamp in timestamps_ms]
    peaks = detection["peaks"]
    bpm_times = detection["bpm_times"]
    bpm_values = detection["bpm_values"]
    ecg_points, _ = scale_points(times, filtered, width, height, left, ecg_top, right, ecg_bottom)

    parts = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        f'<text x="{left}" y="34" font-family="Arial, sans-serif" font-size="22" fill="#202124">{path.name} - QRS baseline reconstruction</text>',
        f'<rect x="{left}" y="{ecg_top}" width="{width-left-right}" height="{height-ecg_top-ecg_bottom}" fill="#fbfcfe" stroke="#d0d7de"/>',
        f'<polyline points="{polyline(ecg_points)}" fill="none" stroke="#2364aa" stroke-width="1.2"/>',
    ]

    if baseline_data is not None:
        baseline_points, _ = scale_points(
            times,
            baseline_data["baseline"],
            width,
            height,
            left,
            ecg_top,
            right,
            ecg_bottom,
        )
        parts.append(f'<polyline points="{polyline(baseline_points)}" fill="none" stroke="#7d3c98" stroke-width="2.0"/>')
        for idx in baseline_data["midpoints"]:
            x, y = ecg_points[idx]
            parts.append(
                f'<path d="M{x - 5:.1f},{y - 5:.1f} L{x + 5:.1f},{y + 5:.1f} M{x + 5:.1f},{y - 5:.1f} L{x - 5:.1f},{y + 5:.1f}" stroke="#7d3c98" stroke-width="1.8"/>'
            )

    for idx in peaks:
        x, y = ecg_points[idx]
        parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.2" fill="#d7263d"/>')

    for idx in firmware_peaks:
        x, y = ecg_points[idx]
        parts.append(
            f'<rect x="{x - 4.5:.1f}" y="{y - 4.5:.1f}" width="9" height="9" fill="none" stroke="#1b998b" stroke-width="1.6"/>'
        )

    parts.extend(
        [
            f'<text x="{left}" y="{height-ecg_bottom+24}" font-family="Arial, sans-serif" font-size="14" fill="#555">Filtered ADC</text>',
            f'<circle cx="{width - 236}" cy="34" r="5" fill="#d7263d"/><text x="{width - 224}" y="39" font-family="Arial, sans-serif" font-size="14" fill="#333">Detected QRS</text>',
            f'<rect x="{width - 116}" y="28" width="10" height="10" fill="none" stroke="#1b998b" stroke-width="1.5"/><text x="{width - 100}" y="39" font-family="Arial, sans-serif" font-size="14" fill="#333">CSV is_peak</text>',
        ]
    )

    if baseline_data is not None:
        corrected_points, _ = scale_points(
            times,
            baseline_data["corrected"],
            width,
            height,
            left,
            corrected_top,
            right,
            corrected_bottom,
        )
        parts.extend(
            [
                f'<rect x="{left}" y="{corrected_top}" width="{width-left-right}" height="{height-corrected_top-corrected_bottom}" fill="#fbfcfe" stroke="#d0d7de"/>',
                f'<polyline points="{polyline(corrected_points)}" fill="none" stroke="#2f855a" stroke-width="1.2"/>',
                f'<text x="{left}" y="{height-corrected_bottom+24}" font-family="Arial, sans-serif" font-size="14" fill="#555">Baseline-corrected ADC</text>',
            ]
        )
        for idx in peaks:
            x, y = corrected_points[idx]
            parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.0" fill="#d7263d"/>')

    parts.append(
        f'<rect x="{left}" y="{bpm_top}" width="{width-left-right}" height="{height-bpm_top-bpm_bottom}" fill="#fbfcfe" stroke="#d0d7de"/>'
    )

    if bpm_times and bpm_values:
        bpm_points, _ = scale_points(bpm_times, bpm_values, width, height, left, bpm_top, right, bpm_bottom)
        parts.append(f'<polyline points="{polyline(bpm_points)}" fill="none" stroke="#f46036" stroke-width="1.8"/>')
    parts.extend(
        [
            f'<text x="{left}" y="{height-22}" font-family="Arial, sans-serif" font-size="14" fill="#555">Time (s)</text>',
            f'<text x="{left}" y="{bpm_top-10}" font-family="Arial, sans-serif" font-size="14" fill="#555">BPM</text>',
            "</svg>",
        ]
    )
    output.write_text("\n".join(parts), encoding="utf-8")


def collect_files(input_dir: Path) -> list[Path]:
    return sorted(path for path in input_dir.glob("ecg_*.csv") if path.is_file())


def main() -> None:
    parser = argparse.ArgumentParser(description="Detect and visualize QRS complexes in ECG CSV files.")
    parser.add_argument("--input-dir", type=Path, default=Path("data"))
    parser.add_argument("--output-dir", type=Path, default=Path("qrs_baseline_results"))
    parser.add_argument(
        "--qrs-only",
        action="store_true",
        help="Only draw QRS detection, without mid-beat baseline reconstruction.",
    )
    args = parser.parse_args()

    files = collect_files(args.input_dir)
    if not files:
        raise SystemExit(f"No ECG CSV files found in {args.input_dir}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    summary_path = args.output_dir / "summary.csv"

    with summary_path.open("w", newline="") as summary_file:
        writer = csv.writer(summary_file)
        writer.writerow(
            [
                "file",
                "samples",
                "sample_rate_hz",
                "detected_qrs",
                "midbeat_points",
                "mean_bpm",
                "csv_is_peak",
            ]
        )

        for file_path in files:
            data = load_ecg_csv(file_path)
            timestamps_ms = data["timestamps_ms"]
            filtered = data["filtered"]
            firmware_peaks = data["firmware_peaks"]
            detection = detect_qrs(timestamps_ms, filtered)
            baseline_data = None if args.qrs_only else interpolate_baseline(
                timestamps_ms,
                filtered,
                detection["peaks"],
            )
            bpm_values = detection["bpm_values"]
            mean_bpm = sum(bpm_values) / len(bpm_values) if bpm_values else 0.0

            output = args.output_dir / f"{file_path.stem}_qrs_baseline.png"
            plot_result(
                file_path,
                output,
                timestamps_ms,
                filtered,
                firmware_peaks,
                detection,
                baseline_data,
            )
            writer.writerow(
                [
                    file_path.name,
                    len(filtered),
                    f"{detection['sample_rate']:.2f}",
                    len(detection["peaks"]),
                    len(baseline_data["midpoints"]) if baseline_data is not None else 0,
                    f"{mean_bpm:.1f}",
                    len(firmware_peaks),
                ]
            )
            print(f"{file_path.name}: {len(detection['peaks'])} QRS, mean BPM {mean_bpm:.1f}")

    print(f"Saved plots and summary to: {args.output_dir}")


if __name__ == "__main__":
    main()
