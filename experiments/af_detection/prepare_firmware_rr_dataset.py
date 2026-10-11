"""Build AF features from R peaks produced by the production firmware C code."""

from __future__ import annotations

import argparse
import csv
import ctypes
import hashlib
import json
import subprocess
from pathlib import Path

import numpy as np
import wfdb
from scipy.signal import butter, resample_poly, sosfilt, sosfilt_zi

from features import FEATURE_NAMES, rr_features
from prepare_wfdb_dataset import rhythm_at_samples


ROOT = Path(__file__).resolve().parents[2]
FIRMWARE_FS = 360


def source_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def compile_detector(output_dir: Path) -> tuple[ctypes.CDLL, list[Path]]:
    firmware = ROOT / "firmware/esp32/main"
    sources = [
        ROOT / "experiments/firmware_qtdb_host.c",
        firmware / "ecg_signal.c",
        firmware / "components/filter/SBfilter.c",
    ]
    library = output_dir / "firmware_detector.dll"
    subprocess.run(
        ["gcc", "-std=c99", "-O2", "-shared", f"-I{firmware}",
         *map(str, sources), "-o", str(library)],
        check=True,
    )
    return ctypes.CDLL(str(library)), sources


def waveform_to_adc(signal_mv: np.ndarray, source_fs: int) -> np.ndarray:
    if not np.all(np.isfinite(signal_mv)):
        raise ValueError("Waveform contains non-finite samples")
    signal_360 = resample_poly(signal_mv, FIRMWARE_FS, source_fs)
    highpass = butter(1, 0.5, btype="highpass", fs=FIRMWARE_FS, output="sos")
    signal_360, _ = sosfilt(
        highpass, signal_360, zi=sosfilt_zi(highpass) * signal_360[0]
    )
    return np.ascontiguousarray(
        np.clip(np.rint(signal_360 * 200.0 + 2048.0), 0, 4095), dtype=np.int32
    )


def detect_peaks_ms(library: ctypes.CDLL, adc: np.ndarray) -> np.ndarray:
    run = library.run_detector
    run.argtypes = [
        np.ctypeslib.ndpointer(np.int32, flags="C_CONTIGUOUS"), ctypes.c_uint,
        np.ctypeslib.ndpointer(np.uint32, flags="C_CONTIGUOUS"),
    ]
    run.restype = ctypes.c_uint
    peaks = np.empty(len(adc), dtype=np.uint32)
    count = run(adc, len(adc), peaks)
    return peaks[:count].copy()


def peak_scores(reference_ms: np.ndarray, detected_ms: np.ndarray, tolerance_ms: int = 50) -> dict:
    i = j = matched = 0
    while i < len(reference_ms) and j < len(detected_ms):
        if detected_ms[j] < reference_ms[i] - tolerance_ms:
            j += 1
        elif detected_ms[j] > reference_ms[i] + tolerance_ms:
            i += 1
        else:
            matched += 1
            i += 1
            j += 1
    fp, fn = len(detected_ms) - matched, len(reference_ms) - matched
    return {
        "tp": matched, "fp": fp, "fn": fn,
        "sensitivity": matched / (matched + fn) if matched + fn else 0.0,
        "ppv": matched / (matched + fp) if matched + fp else 0.0,
    }


def build_record(record_path: Path, library: ctypes.CDLL, beats_per_window: int,
                 stride: int, channel: int) -> tuple[list[dict], dict]:
    record = wfdb.rdrecord(str(record_path), channels=[channel])
    if int(record.fs) != record.fs:
        raise ValueError(f"{record_path.name}: non-integer sampling rate {record.fs}")
    adc = waveform_to_adc(record.p_signal[:, 0], int(record.fs))
    detected_ms = detect_peaks_ms(library, adc)
    detected_source_samples = np.rint(detected_ms * record.fs / 1000.0).astype(np.int64)
    rhythm = wfdb.rdann(str(record_path), "atr")
    labels = rhythm_at_samples(rhythm, detected_source_samples)
    reference = wfdb.rdann(str(record_path), "qrs")
    reference_ms = np.asarray(reference.sample, dtype=np.float64) * 1000.0 / record.fs
    quality = peak_scores(reference_ms, detected_ms.astype(np.float64))

    rows = []
    for start in range(0, len(detected_ms) - beats_per_window, stride):
        stop = start + beats_per_window + 1
        window_labels = labels[start:stop]
        is_af = np.isin(window_labels, ["AFIB", "AF"])
        if np.all(is_af):
            label = 1
        elif not np.any(is_af) and np.all(window_labels != "UNKNOWN"):
            label = 0
        else:
            continue
        rr_ms = np.diff(detected_ms[start:stop]).astype(np.float64)
        if np.any((rr_ms < 250.0) | (rr_ms > 2500.0)):
            continue
        row = {
            "record_id": record_path.name,
            "window_start_sample": int(detected_source_samples[start]),
            "label": label,
            "rpeak_sensitivity_record": quality["sensitivity"],
            "rpeak_ppv_record": quality["ppv"],
        }
        row.update(rr_features(rr_ms))
        rows.append(row)
    return rows, {
        "record_id": record_path.name, "channel": channel,
        "source_fs": record.fs, "source_samples": record.sig_len,
        "detected_peaks": len(detected_ms), "reference_peaks": len(reference_ms),
        **quality,
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wfdb-dir", type=Path, default=Path("data/afdb"))
    parser.add_argument("--experiment", type=Path,
                        default=Path("outputs/af_detection/final_benchmark/experiment.json"))
    parser.add_argument("--split", choices=("train", "test"), default="test")
    parser.add_argument("--output-dir", type=Path,
                        default=Path("outputs/af_detection/firmware_rr"))
    parser.add_argument("--beats-per-window", type=int, default=30)
    parser.add_argument("--stride", type=int, default=15)
    parser.add_argument("--channel", type=int, default=0)
    args = parser.parse_args()

    experiment = json.loads(args.experiment.read_text(encoding="utf-8"))
    records = experiment[f"{args.split}_records"]
    args.output_dir.mkdir(parents=True, exist_ok=True)
    library, sources = compile_detector(args.output_dir)
    rows, record_metrics, excluded = [], [], []
    for name in records:
        header = args.wfdb_dir / f"{name}.hea"
        waveform = args.wfdb_dir / f"{name}.dat"
        if not waveform.exists() or header.stat().st_size < 32:
            excluded.append({"record_id": name, "reason": "waveform_unavailable"})
            print(f"Skipping {name}: waveform unavailable", flush=True)
            continue
        print(f"Processing {name}...", flush=True)
        record_rows, metrics = build_record(
            args.wfdb_dir / name, library, args.beats_per_window,
            args.stride, args.channel,
        )
        rows.extend(record_rows)
        record_metrics.append(metrics)
    if not rows:
        raise SystemExit("No pure AF/non-AF windows were produced")

    output_csv = args.output_dir / f"features_firmware_rr_{args.split}.csv"
    fields = ["record_id", "window_start_sample", "label",
              "rpeak_sensitivity_record", "rpeak_ppv_record", *FEATURE_NAMES]
    with output_csv.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    manifest = {
        "split": args.split, "requested_records": records,
        "processed_records": [item["record_id"] for item in record_metrics],
        "excluded_records": excluded, "windows": len(rows),
        "beats_per_window": args.beats_per_window, "stride": args.stride,
        "channel": args.channel, "adc_model": "200 counts/mV + 2048, clipped 12-bit",
        "firmware_fs": FIRMWARE_FS, "record_metrics": record_metrics,
        "source_sha256": {str(path.relative_to(ROOT)): source_hash(path) for path in sources},
        "warning": "Test output is evaluation-only; do not retrain or tune thresholds on it.",
    }
    (args.output_dir / f"manifest_{args.split}.json").write_text(
        json.dumps(manifest, indent=2), encoding="utf-8"
    )
    print(f"Wrote {len(rows)} windows to {output_csv}")


if __name__ == "__main__":
    main()
