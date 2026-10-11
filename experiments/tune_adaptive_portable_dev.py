"""Tune the causal Adaptive PT only on the declared QTDB development split."""
from __future__ import annotations

import argparse
import csv
import itertools
import json
import time
from pathlib import Path

import numpy as np
import wfdb

from ecg_benchmark.rpeaks import AdaptivePTPortable, score_r_peaks
from noise_coverage_3stage import DEVELOPMENT_RECORDS, annotation_peaks


def detect(x: np.ndarray, fs: float, params: dict[str, float]) -> np.ndarray:
    detector = AdaptivePTPortable(fs, **params)
    result = [event for value in x if (event := detector.push(float(value))) is not None]
    return np.asarray(result, dtype=np.int64)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qtdb-dir", type=Path, default=Path("data/qtdb"))
    parser.add_argument("--output", type=Path, default=Path("outputs/adaptive_portable_dev"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    cases = []
    for record in sorted(DEVELOPMENT_RECORDS):
        samples, meta = wfdb.rdsamp(str(args.qtdb_dir / record), channels=[0])
        fs = float(meta["fs"])
        raw = samples[:, 0] - np.median(samples[:, 0])
        size = int(30 * fs)
        for start in np.linspace(0, len(raw) - size, 3, dtype=np.int64):
            reference = annotation_peaks(args.qtdb_dir / record, int(start), int(start + size))
            if len(reference) >= 2:
                cases.append((raw[start : start + size], reference, fs))

    rows = []
    grid = itertools.product((70.0, 90.0, 110.0), (80.0, 100.0, 120.0, 140.0), (0.20, 0.25, 0.30, 0.35))
    for integration, relocation, fraction in grid:
        params = {
            "integration_ms": integration,
            "relocation_ms": relocation,
            "threshold_fraction": fraction,
        }
        totals = {50.0: [0, 0, 0], 100.0: [0, 0, 0]}
        latencies = []
        for x, reference, fs in cases:
            tick = time.perf_counter_ns()
            estimate = detect(x, fs, params)
            latencies.append((time.perf_counter_ns() - tick) / 1e6)
            for tolerance in totals:
                score = score_r_peaks(reference, estimate, fs, tolerance)
                totals[tolerance][0] += int(score["RPeak_TP"])
                totals[tolerance][1] += int(score["RPeak_FP"])
                totals[tolerance][2] += int(score["RPeak_FN"])
        result = {}
        for tolerance, (tp, fp, fn) in totals.items():
            result[f"f1_{int(tolerance)}ms"] = 2 * tp / max(2 * tp + fp + fn, 1)
        rows.append({
            **params,
            "objective": 0.7 * result["f1_50ms"] + 0.3 * result["f1_100ms"],
            **result,
            "latency_median_ms": float(np.median(latencies)),
        })
    rows.sort(key=lambda row: (-row["objective"], row["latency_median_ms"]))
    with (args.output / "grid_results.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    frozen = {
        "version": "adaptive_pt_portable_v1",
        "development_records": sorted(DEVELOPMENT_RECORDS),
        "test_records_used": [],
        "selection_rule": "maximize 0.7*F1@50ms + 0.3*F1@100ms on clean QTDB development windows",
        "parameters": {key: rows[0][key] for key in ("integration_ms", "relocation_ms", "threshold_fraction")},
        "development_metrics": rows[0],
    }
    (args.output / "frozen_config.json").write_text(json.dumps(frozen, indent=2), encoding="utf-8")
    print(json.dumps(frozen, indent=2))


if __name__ == "__main__":
    main()
