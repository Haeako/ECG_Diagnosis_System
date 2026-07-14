#!/usr/bin/env python3
"""Plot ECG CSV files recorded by the ESP32 firmware."""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path


def collect_csv_files(paths: list[Path]) -> list[Path]:
    files: list[Path] = []

    for path in paths:
        if path.is_dir():
            files.extend(sorted(path.glob("ecg_*.csv")))
        elif path.is_file():
            files.append(path)
        else:
            raise FileNotFoundError(f"Path not found: {path}")

    if not files:
        raise FileNotFoundError("No ECG CSV files found")

    return sorted(files)


def load_samples(files: list[Path]) -> dict[str, list[float] | list[str]]:
    timestamps: list[float] = []
    series: dict[str, list[float]] = {}
    peaks_x: list[float] = []
    bpm_x: list[float] = []
    bpm_y: list[float] = []
    first_timestamp: int | None = None
    signal_columns: list[str] = []

    for file_path in files:
        with file_path.open("r", newline="") as file:
            reader = csv.DictReader(file)
            fieldnames = reader.fieldnames or []
            required = {"timestamp_ms"}
            if not required.issubset(reader.fieldnames or set()):
                raise ValueError(f"{file_path} is missing one of: {', '.join(sorted(required))}")
            file_signal_columns = [
                field
                for field in fieldnames
                if field not in {"timestamp_ms", "is_peak"}
            ]
            for field in file_signal_columns:
                if field not in series:
                    series[field] = []
                    signal_columns.append(field)

            for row in reader:
                timestamp_ms = int(row["timestamp_ms"])

                if first_timestamp is None:
                    first_timestamp = timestamp_ms

                t_sec = (timestamp_ms - first_timestamp) / 1000.0
                timestamps.append(t_sec)

                for field in signal_columns:
                    if field in row and row[field] != "":
                        series[field].append(float(row[field]))
                    else:
                        series[field].append(float("nan"))

                if int(float(row.get("is_peak") or 0)) != 0:
                    peaks_x.append(t_sec)

                bpm = float(row.get("bpm") or 0.0)
                if bpm > 0:
                    bpm_x.append(t_sec)
                    bpm_y.append(bpm)

    if not timestamps:
        raise ValueError("CSV files contain no samples")

    return {
        "timestamps": timestamps,
        "columns": signal_columns,
        **series,
        "peaks_x": peaks_x,
        "bpm_x": bpm_x,
        "bpm_y": bpm_y,
    }


def plot_ecg(data: dict[str, list[float] | list[str]], output: Path | None, title: str) -> None:
    try:
        if output is not None:
            import matplotlib

            matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError as exc:
        if output is None:
            raise SystemExit(
                "matplotlib is required for interactive display. Use -o to save SVG."
            ) from exc
        save_svg_ecg(data, output.with_suffix(".svg"), title)
        print(f"Saved plot: {output.with_suffix('.svg')}")
        return

    columns = [str(column) for column in data["columns"]]
    fig, axes = plt.subplots(len(columns), 1, sharex=True, figsize=(14, max(7, 2.0 * len(columns))))
    if len(columns) == 1:
        axes = [axes]

    for axis, column in zip(axes, columns):
        axis.plot(data["timestamps"], data[column], linewidth=0.85, color="#1f77b4")
        if column in {"raw_adc", "raw_centered", "corrected", "filtered"} and data["peaks_x"]:
            peak_values = values_at_times(data["timestamps"], data[column], data["peaks_x"])
            axis.scatter(data["peaks_x"], peak_values, s=22, color="#d62728", label="is_peak", zorder=3)
            axis.legend(loc="upper right")
        axis.set_ylabel(column)
        axis.grid(True, alpha=0.25)
    axes[0].set_title(title)
    axes[-1].set_xlabel("Time (s)")

    fig.tight_layout()
    if output is not None:
        fig.savefig(output, dpi=160)
        print(f"Saved plot: {output}")
    else:
        plt.show()


def scale_points(
    xs: list[float],
    ys: list[float],
    width: int,
    top: int,
    height: int,
    left: int,
    right: int,
) -> list[tuple[float, float]]:
    min_x = min(xs)
    max_x = max(xs)
    min_y = min(ys)
    max_y = max(ys)
    if math.isclose(min_x, max_x):
        max_x = min_x + 1.0
    if math.isclose(min_y, max_y):
        max_y = min_y + 1.0

    plot_width = width - left - right
    return [
        (
            left + (x_value - min_x) / (max_x - min_x) * plot_width,
            top + (max_y - y_value) / (max_y - min_y) * height,
        )
        for x_value, y_value in zip(xs, ys)
    ]


def polyline(points: list[tuple[float, float]]) -> str:
    return " ".join(f"{x:.1f},{y:.1f}" for x, y in points)


def values_at_times(xs: list[float], ys: list[float], target_xs: list[float]) -> list[float]:
    lookup = {round(x, 6): y for x, y in zip(xs, ys)}
    return [lookup.get(round(x, 6), 0.0) for x in target_xs]


def save_svg_ecg(data: dict[str, list[float] | list[str]], output: Path, title: str) -> None:
    width = 1500
    columns = [str(column) for column in data["columns"]]
    panel_h = 170
    gap = 42
    top0 = 70
    height = top0 + len(columns) * panel_h + (len(columns) - 1) * gap + 55
    left = 84
    right = 28
    parts = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        f'<text x="{left}" y="36" font-family="Arial, sans-serif" font-size="22" fill="#202124">{title}</text>',
    ]

    colors = ["#2364aa", "#c0392b", "#2f855a", "#8e44ad", "#e67e22", "#34495e"]
    for idx, column in enumerate(columns):
        top = top0 + idx * (panel_h + gap)
        color = colors[idx % len(colors)]
        points = scale_points(data["timestamps"], data[column], width, top, panel_h, left, right)
        parts.extend([
            f'<rect x="{left}" y="{top}" width="{width-left-right}" height="{panel_h}" fill="#fbfcfe" stroke="#d0d7de"/>',
            f'<polyline points="{polyline(points)}" fill="none" stroke="{color}" stroke-width="1.1"/>',
            f'<text x="18" y="{top + panel_h // 2}" font-family="Arial, sans-serif" font-size="14" fill="#555">{column}</text>',
        ])
        if column in {"raw_adc", "raw_centered", "corrected", "filtered"}:
            peak_lookup = set(round(x, 6) for x in data["peaks_x"])
            for point_idx, time_value in enumerate(data["timestamps"]):
                if round(time_value, 6) in peak_lookup:
                    x, y = points[point_idx]
                    parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.0" fill="#d7263d"/>')

    parts.extend([
        f'<text x="{left}" y="{height - 24}" font-family="Arial, sans-serif" font-size="14" fill="#555">time (s)</text>',
        "</svg>",
    ])
    output.write_text("\n".join(parts), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description="Visualize ESP32 ECG CSV recordings.")
    parser.add_argument(
        "paths",
        nargs="+",
        type=Path,
        help="CSV file(s) or folder(s) containing ecg_*.csv files",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        help="Save plot image instead of opening an interactive window",
    )
    parser.add_argument(
        "--title",
        default="ECG Recording",
        help="Plot title",
    )
    args = parser.parse_args()

    files = collect_csv_files(args.paths)
    print(f"Loading {len(files)} file(s)")
    for file_path in files:
        print(f"  {file_path}")

    data = load_samples(files)
    print(f"Samples: {len(data['timestamps'])}, peaks: {len(data['peaks_x'])}")
    plot_ecg(data, args.output, args.title)


if __name__ == "__main__":
    main()
