#!/usr/bin/env python3
"""Offline ECG pipeline module tester.

Runs the current firmware-style ECG stages over CSV files and saves one
visualization per filter combination. Files that only contain the old
timestamp_ms,filtered,is_peak,bpm format are still supported by using
``filtered`` as the test input signal.
"""

from __future__ import annotations

import argparse
import csv
import math
from dataclasses import dataclass, field
from itertools import combinations
from pathlib import Path


ADC_BASELINE = 2048
BASELINE_WINDOW_SAMPLES = 360
PAN_REFRACTORY_MS = 400
PAN_R_MIN_ABS = 900.0
BPM_UPDATE_MS = 5000

LP_COEFFS = [
    0.11024641707542467, 0.22049283415084933, 0.11024641707542467, 0.9612453444136209, -0.2548166794342573,
    0.0625, 0.125, 0.0625, 1.229621470846513, -0.605156831162516,
]
HP_COEFFS = [
    0.988662800744742, -1.977325601489484, 0.988662800744742, 1.9839288807182445, -0.9840044255274392,
    1.0, -2.0, 1.0, 1.9932673287733085, -0.9933432291755304,
]
SB_COEFFS = [
    0.9774538849801447, -1.2566383415479088, 0.9774538849801452, 1.2702232305463554, -0.9839595276131086,
    1.0, -1.285624172002176, 1.0000000000000004, 1.2804320449668785, -0.9840484710030306,
    1.0, -1.285624172002176, 1.0000000000000004, 1.2689222348576417, -0.9932985239996022,
    1.0, -1.285624172002176, 1.0000000000000004, 1.2936022087572714, -0.9933882936628773,
]


@dataclass
class IirCascade:
    coeffs: list[float]
    state: list[float] = field(init=False)

    def __post_init__(self) -> None:
        self.state = [0.0] * (4 * (len(self.coeffs) // 5))

    def push(self, value: float) -> float:
        output = value
        for stage in range(len(self.coeffs) // 5):
            base = stage * 5
            state_base = stage * 4
            b0, b1, b2, a1, a2 = self.coeffs[base:base + 5]
            x1, x2, y1, y2 = self.state[state_base:state_base + 4]
            acc = x2 * b2 + x1 * b1 + output * b0 + y2 * a2 + y1 * a1
            self.state[state_base:state_base + 4] = [output, x1, acc, y1]
            output = acc
        return output


@dataclass
class MovingAverage:
    window: int = BASELINE_WINDOW_SAMPLES
    buffer: list[float] = field(init=False)
    total: float = 0.0
    index: int = 0
    count: int = 0

    def __post_init__(self) -> None:
        self.buffer = [0.0] * self.window

    def push(self, sample: float) -> float:
        if self.count < self.window:
            self.count += 1
        else:
            self.total -= self.buffer[self.index]
        self.buffer[self.index] = sample
        self.total += sample
        self.index = (self.index + 1) % self.window
        return self.total / self.count


@dataclass
class Kalman:
    err_measure: float = 1.0
    err_estimate: float = 1.0
    q: float = 0.001
    last_estimate: float = 0.0

    def push(self, mea: float) -> float:
        gain = self.err_estimate / (self.err_estimate + self.err_measure)
        current = self.last_estimate + gain * (mea - self.last_estimate)
        self.err_estimate = (1.0 - gain) * self.err_estimate + abs(self.last_estimate - current) * self.q
        self.last_estimate = current
        return current


@dataclass
class PanDetector:
    signal_level: float = 0.0
    noise_level: float = 0.0
    last_peak_ms: int = 0
    bpm_window_start_ms: int = 0
    rr_sum_ms: int = 0
    rr_count: int = 0
    bpm: int = 0
    previous_abs: float = 0.0
    candidate_abs: float = 0.0
    candidate_ms: int = 0

    def push(self, filtered: float, now_ms: int) -> tuple[bool, int, int, int, int]:
        current_abs = abs(filtered)
        threshold = self.noise_level + 0.60 * (self.signal_level - self.noise_level)
        peak_ms = 0
        rr_ms = 0
        instant_bpm = 0

        if self.signal_level == 0.0:
            self.signal_level = current_abs
            threshold = PAN_R_MIN_ABS
        threshold = max(threshold, PAN_R_MIN_ABS)

        local_extreme = self.candidate_abs >= self.previous_abs and self.candidate_abs > current_abs
        is_peak = False
        if local_extreme and self.candidate_abs >= threshold and (
            self.last_peak_ms == 0 or (self.candidate_ms - self.last_peak_ms) >= PAN_REFRACTORY_MS
        ):
            rr = self.candidate_ms - self.last_peak_ms
            is_peak = True
            self.signal_level = 0.125 * self.candidate_abs + 0.875 * self.signal_level
            peak_ms = self.candidate_ms
            if self.last_peak_ms != 0 and 250 <= rr <= 2000:
                self.rr_sum_ms += rr
                self.rr_count += 1
                rr_ms = rr
                instant_bpm = 60000 // rr
            self.last_peak_ms = self.candidate_ms
        elif local_extreme:
            self.noise_level = 0.125 * self.candidate_abs + 0.875 * self.noise_level

        self.previous_abs = self.candidate_abs
        self.candidate_abs = current_abs
        self.candidate_ms = now_ms

        if now_ms - self.bpm_window_start_ms >= BPM_UPDATE_MS:
            if self.rr_count > 0 and self.rr_sum_ms > 0:
                self.bpm = (60000 * self.rr_count) // self.rr_sum_ms
            self.rr_sum_ms = 0
            self.rr_count = 0
            self.bpm_window_start_ms = now_ms

        return is_peak, peak_ms, rr_ms, instant_bpm, self.bpm


def collect_files(input_dir: Path) -> list[Path]:
    return sorted(path for path in input_dir.glob("ecg_*.csv") if path.is_file())


def load_csv(path: Path) -> dict[str, list[float] | list[int] | str]:
    timestamps: list[int] = []
    input_signal: list[float] = []
    csv_peaks: list[int] = []
    mode = "filtered-as-input"

    with path.open("r", newline="") as file:
        reader = csv.DictReader(file)
        fields = set(reader.fieldnames or [])
        if "timestamp_ms" not in fields:
            raise ValueError(f"{path} is missing timestamp_ms")
        if "raw_adc" in fields:
            value_field = "raw_adc"
            mode = "raw-adc"
        elif "raw_centered" in fields:
            value_field = "raw_centered"
            mode = "raw-centered"
        elif "filtered" in fields:
            value_field = "filtered"
        else:
            raise ValueError(f"{path} needs raw_adc, raw_centered, or filtered")

        for idx, row in enumerate(reader):
            timestamps.append(int(float(row["timestamp_ms"])))
            value = float(row[value_field])
            input_signal.append(value)
            if int(float(row.get("is_peak") or 0)) != 0:
                csv_peaks.append(idx)

    if not timestamps:
        raise ValueError(f"{path} has no samples")
    return {"timestamps": timestamps, "input": input_signal, "csv_peaks": csv_peaks, "mode": mode}


def firmware_stages(
    timestamps: list[int],
    signal: list[float],
    mode: str,
    filter_chain: tuple[str, ...],
    use_kalman: bool,
) -> dict[str, list[float] | list[int] | str]:
    ma = MovingAverage()
    filters = {
        "HP": IirCascade(HP_COEFFS),
        "SB": IirCascade(SB_COEFFS),
        "LP": IirCascade(LP_COEFFS),
    }
    kalman = Kalman()
    detector = PanDetector(bpm_window_start_ms=timestamps[0])

    raw_centered: list[float] = []
    baseline: list[float] = []
    corrected: list[float] = []
    filtered: list[float] = []
    peaks: list[int] = []
    bpm: list[int] = []

    for idx, (timestamp, sample) in enumerate(zip(timestamps, signal)):
        centered = sample - ADC_BASELINE if mode == "raw-adc" else sample
        base = ma.push(centered)
        value = centered - base
        for name in filter_chain:
            value = filters[name].push(value)
        if use_kalman:
            value = kalman.push(value)
        is_peak, _, _, _, current_bpm = detector.push(value, timestamp)

        raw_centered.append(centered)
        baseline.append(base)
        corrected.append(centered - base)
        filtered.append(value)
        bpm.append(current_bpm)
        if is_peak:
            peaks.append(max(0, idx - 1))

    interp = interpolate_baseline(timestamps, filtered, peaks)
    return {
        "raw_centered": raw_centered,
        "ma_baseline": baseline,
        "ma_corrected": corrected,
        "filtered": filtered,
        "peaks": peaks,
        "bpm": bpm,
        "interp_baseline": interp["baseline"],
        "interp_corrected": interp["corrected"],
        "interp_points": interp["midpoints"],
        "chain": chain_name(filter_chain, use_kalman),
    }


def interpolate_baseline(timestamps: list[int], signal: list[float], peaks: list[int]) -> dict[str, list[float] | list[int]]:
    if len(peaks) < 2:
        return {"baseline": [0.0] * len(signal), "corrected": signal[:], "midpoints": []}

    midpoints: list[int] = []
    for previous, current in zip(peaks, peaks[1:]):
        midpoint_time = (timestamps[previous] + timestamps[current]) / 2.0
        midpoints.append(min(range(previous, current + 1), key=lambda idx: abs(timestamps[idx] - midpoint_time)))

    anchors = [0, *midpoints, len(signal) - 1]
    values = [signal[midpoints[0]], *[signal[idx] for idx in midpoints], signal[midpoints[-1]]]
    baseline = [0.0] * len(signal)
    for left, right, left_value, right_value in zip(anchors, anchors[1:], values, values[1:]):
        span = max(1, right - left)
        for idx in range(left, right + 1):
            fraction = (idx - left) / span
            baseline[idx] = left_value + (right_value - left_value) * fraction
    return {"baseline": baseline, "corrected": [value - base for value, base in zip(signal, baseline)], "midpoints": midpoints}


def plot(
    path: Path,
    output: Path,
    timestamps: list[int],
    csv_peaks: list[int],
    result: dict[str, list[float] | list[int] | str],
    mode: str,
    show_interpolation: bool,
) -> Path:
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        svg_output = output.with_suffix(".svg")
        save_svg(path, svg_output, timestamps, csv_peaks, result, mode, show_interpolation)
        return svg_output

    times = [(timestamp - timestamps[0]) / 1000.0 for timestamp in timestamps]
    peaks = result["peaks"]
    interp_points = result["interp_points"]

    panel_count = 5 if show_interpolation else 3
    fig, axes = plt.subplots(panel_count, 1, sharex=True, figsize=(15, 11 if show_interpolation else 7))
    axes[0].plot(times, result["raw_centered"], linewidth=0.7, color="#34495e")
    axes[0].plot(times, result["ma_baseline"], linewidth=1.0, color="#c0392b")
    axes[0].set_ylabel("input/MA")
    axes[0].set_title(f"{path.name} | mode={mode} | chain={result['chain']}")

    axes[1].plot(times, result["ma_corrected"], linewidth=0.7, color="#2364aa")
    axes[1].axhline(0, linewidth=0.8, color="#777", alpha=0.5)
    axes[1].set_ylabel("MA corrected")

    axes[2].plot(times, result["filtered"], linewidth=0.7, color="#2f855a")
    axes[2].scatter([times[idx] for idx in peaks], [result["filtered"][idx] for idx in peaks], s=24, color="#d7263d", label="detected R")
    if csv_peaks:
        axes[2].scatter([times[idx] for idx in csv_peaks], [result["filtered"][idx] for idx in csv_peaks], s=28, facecolors="none", edgecolors="#1b998b", label="CSV is_peak")
    axes[2].legend(loc="upper right")
    axes[2].set_ylabel("filtered")

    if show_interpolation:
        axes[3].plot(times, result["filtered"], linewidth=0.65, color="#95a5a6")
        axes[3].plot(times, result["interp_baseline"], linewidth=1.2, color="#8e44ad")
        axes[3].scatter([times[idx] for idx in interp_points], [result["filtered"][idx] for idx in interp_points], s=22, marker="x", color="#8e44ad")
        axes[3].set_ylabel("interp")

        axes[4].plot(times, result["interp_corrected"], linewidth=0.7, color="#e67e22")
        axes[4].axhline(0, linewidth=0.8, color="#777", alpha=0.5)
        axes[4].set_ylabel("interp corr")

    axes[-1].set_xlabel("time (s)")

    for axis in axes:
        axis.grid(True, alpha=0.25)
    fig.tight_layout()
    fig.savefig(output, dpi=150)
    plt.close(fig)
    return output


def scaled_points(
    times: list[float],
    values: list[float],
    width: int,
    top: int,
    height: int,
    left: int,
    right: int,
) -> list[tuple[float, float]]:
    min_x = min(times)
    max_x = max(times)
    min_y = min(values)
    max_y = max(values)
    if math.isclose(min_x, max_x):
        max_x = min_x + 1.0
    if math.isclose(min_y, max_y):
        max_y = min_y + 1.0
    plot_width = width - left - right
    return [
        (
            left + (x - min_x) / (max_x - min_x) * plot_width,
            top + (max_y - y) / (max_y - min_y) * height,
        )
        for x, y in zip(times, values)
    ]


def polyline(points: list[tuple[float, float]]) -> str:
    return " ".join(f"{x:.1f},{y:.1f}" for x, y in points)


def save_svg(
    path: Path,
    output: Path,
    timestamps: list[int],
    csv_peaks: list[int],
    result: dict[str, list[float] | list[int] | str],
    mode: str,
    show_interpolation: bool,
) -> None:
    width = 1500
    left = 86
    right = 28
    panel_h = 150
    gap = 48
    top0 = 70
    panel_count = 5 if show_interpolation else 3
    height = top0 + panel_count * panel_h + (panel_count - 1) * gap + 55
    times = [(timestamp - timestamps[0]) / 1000.0 for timestamp in timestamps]
    panels = [
        ("input/MA", [("input", result["raw_centered"], "#34495e"), ("MA", result["ma_baseline"], "#c0392b")]),
        ("MA corrected", [("corrected", result["ma_corrected"], "#2364aa")]),
        ("filtered + R", [("filtered", result["filtered"], "#2f855a")]),
    ]
    if show_interpolation:
        panels.extend([
            ("interpolation", [("filtered", result["filtered"], "#95a5a6"), ("interp baseline", result["interp_baseline"], "#8e44ad")]),
            ("interp corrected", [("interp corrected", result["interp_corrected"], "#e67e22")]),
        ])
    parts = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        f'<text x="{left}" y="34" font-family="Arial, sans-serif" font-size="22" fill="#202124">{path.name} | mode={mode} | chain={result["chain"]}</text>',
    ]

    for panel_idx, (label, series) in enumerate(panels):
        top = top0 + panel_idx * (panel_h + gap)
        all_values: list[float] = []
        for _, values, _ in series:
            all_values.extend(values)
        points_by_name = {
            name: scaled_points(times, values, width, top, panel_h, left, right)
            for name, values, _ in series
        }
        parts.append(f'<rect x="{left}" y="{top}" width="{width-left-right}" height="{panel_h}" fill="#fbfcfe" stroke="#d0d7de"/>')
        parts.append(f'<text x="18" y="{top + 82}" font-family="Arial, sans-serif" font-size="14" fill="#555">{label}</text>')
        for name, _, color in series:
            parts.append(f'<polyline points="{polyline(points_by_name[name])}" fill="none" stroke="{color}" stroke-width="1.2"/>')

        if panel_idx == 2:
            filtered_points = points_by_name["filtered"]
            for idx in result["peaks"]:
                x, y = filtered_points[idx]
                parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="4.0" fill="#d7263d"/>')
            for idx in csv_peaks:
                x, y = filtered_points[idx]
                parts.append(f'<rect x="{x - 4:.1f}" y="{y - 4:.1f}" width="8" height="8" fill="none" stroke="#1b998b" stroke-width="1.5"/>')
        if panel_idx == 3:
            filtered_points = points_by_name["filtered"]
            for idx in result["interp_points"]:
                x, y = filtered_points[idx]
                parts.append(f'<path d="M{x - 5:.1f},{y - 5:.1f} L{x + 5:.1f},{y + 5:.1f} M{x + 5:.1f},{y - 5:.1f} L{x - 5:.1f},{y + 5:.1f}" stroke="#8e44ad" stroke-width="1.6"/>')

    parts.extend([
        f'<text x="{left}" y="{height - 20}" font-family="Arial, sans-serif" font-size="14" fill="#555">time (s)</text>',
        "</svg>",
    ])
    output.write_text("\n".join(parts), encoding="utf-8")


def chain_name(filter_chain: tuple[str, ...], use_kalman: bool) -> str:
    parts = ["MA", *filter_chain]
    if use_kalman:
        parts.append("Kalman")
    return "+".join(parts)


def filter_chains(include_sb: bool) -> list[tuple[tuple[str, ...], bool]]:
    names = ["HP", "LP"] + (["SB"] if include_sb else [])
    chains: list[tuple[tuple[str, ...], bool]] = [((), False), ((), True)]
    for size in range(1, len(names) + 1):
        chains.extend((tuple(combo), True) for combo in combinations(names, size))
    firmware = ("HP", "SB", "LP") if include_sb else ("HP", "LP")
    if (firmware, True) not in chains:
        chains.append((firmware, True))
    return chains


def main() -> None:
    parser = argparse.ArgumentParser(description="Test ECG pipeline modules and save visualizations.")
    parser.add_argument("--input-dir", type=Path, default=Path("data"))
    parser.add_argument("--output-dir", type=Path, default=Path("main") / "pipeline_module_results")
    parser.add_argument("--include-sb", action="store_true", help="Also test SB/notch filter combinations.")
    parser.add_argument("--show-interpolation", action="store_true", help="Draw interpolation panels. Hidden by default because it depends on R-peak quality.")
    parser.add_argument("--limit", type=int, default=0, help="Limit number of CSV files for a quick run.")
    args = parser.parse_args()

    files = collect_files(args.input_dir)
    if args.limit > 0:
        files = files[:args.limit]
    if not files:
        raise SystemExit(f"No ecg_*.csv files found in {args.input_dir}")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    chains = filter_chains(args.include_sb)
    summary_path = args.output_dir / "summary.csv"
    with summary_path.open("w", newline="") as summary_file:
        writer = csv.writer(summary_file)
        writer.writerow(["file", "mode", "chain", "samples", "detected_r", "csv_is_peak", "image"])
        for file_path in files:
            data = load_csv(file_path)
            timestamps = data["timestamps"]
            signal = data["input"]
            csv_peaks = data["csv_peaks"]
            mode = str(data["mode"])
            for chain, use_kalman in chains:
                result = firmware_stages(timestamps, signal, mode, chain, use_kalman)
                chain_name = str(result["chain"]).replace("+", "_")
                output = args.output_dir / f"{file_path.stem}__{chain_name}.png"
                actual_output = plot(file_path, output, timestamps, csv_peaks, result, mode, args.show_interpolation)
                writer.writerow([file_path.name, mode, result["chain"], len(signal), len(result["peaks"]), len(csv_peaks), actual_output.name])
                print(f"{file_path.name} [{result['chain']}]: {len(result['peaks'])} R peaks -> {actual_output}")
    print(f"Saved summary: {summary_path}")


if __name__ == "__main__":
    main()
