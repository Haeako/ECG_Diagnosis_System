from __future__ import annotations

import argparse
import csv
import importlib.util
from pathlib import Path

import numpy as np


def load_ptpp_class():
    spec = importlib.util.spec_from_file_location("ptpp", "pan-tompkins_pp.py")
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module.Pan_Tompkins_Plus_Plus


def load_signal(path: Path, column: str):
    rows = np.genfromtxt(str(path), delimiter=",", names=True)
    if rows.ndim == 0:
        rows = np.asarray([rows])
    signal = rows[column].astype(float)
    csv_peaks = np.flatnonzero(rows["is_peak"].astype(float) != 0)
    return signal, csv_peaks


def load_c_peaks(path: Path):
    peaks = []
    with path.open("r", newline="") as f:
        reader = csv.DictReader(f)
        for row in reader:
            peaks.append(int(row["index"]))
    return np.asarray(peaks, dtype=int)


def match_count(a: np.ndarray, b: np.ndarray, tol: int):
    used = np.zeros(len(b), dtype=bool)
    matched = 0
    for value in a:
        best = -1
        best_dist = tol + 1
        for idx, other in enumerate(b):
            if used[idx]:
                continue
            dist = abs(int(value) - int(other))
            if dist <= tol and dist < best_dist:
                best = idx
                best_dist = dist
        if best >= 0:
            used[best] = True
            matched += 1
    return matched


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("folder", nargs="?", default="pre_move_data")
    parser.add_argument("--c-folder", default="visualize/pan_tompkins_pp_c")
    parser.add_argument("--column", default="filtered")
    parser.add_argument("--fs", type=int, default=360)
    parser.add_argument("--tol-ms", type=float, default=80)
    args = parser.parse_args()

    tol = round(args.tol_ms * args.fs / 1000)
    Detector = load_ptpp_class()
    detector = Detector()

    files = sorted(Path(args.folder).glob("ecg_*.csv"))
    print("file,csv,python,c,py_c_match,c_vs_py_extra,c_vs_py_missed")
    total_csv = total_py = total_c = total_match = 0
    for file_path in files:
        signal, csv_peaks = load_signal(file_path, args.column)
        py_peaks = detector.rpeak_detection(signal, args.fs).astype(int)
        c_peak_file = Path(args.c_folder) / f"{file_path.name}_c_peaks.csv"
        c_peaks = load_c_peaks(c_peak_file)
        matched = match_count(py_peaks, c_peaks, tol)
        extra = len(c_peaks) - matched
        missed = len(py_peaks) - matched
        total_csv += len(csv_peaks)
        total_py += len(py_peaks)
        total_c += len(c_peaks)
        total_match += matched
        print(f"{file_path.name},{len(csv_peaks)},{len(py_peaks)},{len(c_peaks)},{matched},{extra},{missed}")

    print(f"TOTAL,{total_csv},{total_py},{total_c},{total_match},{total_c-total_match},{total_py-total_match}")


if __name__ == "__main__":
    main()
