"""Build record-grouped AF/non-AF RR windows from a WFDB rhythm database."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import numpy as np
import wfdb

from features import FEATURE_NAMES, rr_features

def rhythm_at_samples(annotation: wfdb.Annotation, samples: np.ndarray) -> np.ndarray:
    change_samples = []
    change_labels = []
    for sample, auxiliary in zip(annotation.sample, annotation.aux_note):
        note = auxiliary.strip().lstrip("(").upper()
        if note:
            change_samples.append(sample)
            change_labels.append(note)
    if not change_samples:
        return np.full(samples.size, "UNKNOWN", dtype=object)
    indices = np.searchsorted(np.asarray(change_samples), samples, side="right") - 1
    labels = np.full(samples.size, "UNKNOWN", dtype=object)
    valid = indices >= 0
    labels[valid] = np.asarray(change_labels, dtype=object)[indices[valid]]
    return labels


def build_rows(record_path: Path, rhythm_annotation: str, beat_annotation: str, beats_per_window: int, stride: int) -> list[dict]:
    header = wfdb.rdheader(str(record_path))
    rhythm_ann = wfdb.rdann(str(record_path), rhythm_annotation)
    beat_ann = wfdb.rdann(str(record_path), beat_annotation)
    samples = np.asarray(beat_ann.sample)
    labels = rhythm_at_samples(rhythm_ann, samples)
    rows = []
    for start in range(0, len(samples) - beats_per_window, stride):
        stop = start + beats_per_window + 1
        window_labels = labels[start:stop]
        is_af = np.isin(window_labels, ["AFIB", "AF"]) 
        if np.all(is_af):
            label = 1
        elif not np.any(is_af) and np.all(window_labels != "UNKNOWN"):
            label = 0
        else:
            continue
        rr_ms = np.diff(samples[start:stop]) * 1000.0 / float(header.fs)
        if np.any((rr_ms < 250.0) | (rr_ms > 2500.0)):
            continue
        row = {"record_id": record_path.name, "window_start_sample": int(samples[start]), "label": label}
        row.update(rr_features(rr_ms))
        rows.append(row)
    return rows


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--wfdb-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path("outputs/af_detection/features.csv"))
    parser.add_argument("--rhythm-annotation", default="atr")
    parser.add_argument("--beat-annotation", default="qrs")
    parser.add_argument("--beats-per-window", type=int, default=30)
    parser.add_argument("--stride", type=int, default=15)
    args = parser.parse_args()
    records_file = args.wfdb_dir / "RECORDS"
    records = records_file.read_text(encoding="ascii").split() if records_file.exists() else [p.stem for p in args.wfdb_dir.glob("*.hea")]
    rows = []
    for record in records:
        rows.extend(build_rows(args.wfdb_dir / record, args.rhythm_annotation, args.beat_annotation, args.beats_per_window, args.stride))
    if not rows:
        raise SystemExit("No pure AF/non-AF windows found; check rhythm annotations and annotation extension")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=["record_id", "window_start_sample", "label", *FEATURE_NAMES])
        writer.writeheader()
        writer.writerows(rows)
    positives = sum(row["label"] for row in rows)
    print(f"Wrote {len(rows)} windows ({positives} AF, {len(rows) - positives} non-AF) to {args.output}")


if __name__ == "__main__":
    main()
