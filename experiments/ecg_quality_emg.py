"""Offline ECG muscle-noise benchmark and full-rate AD8232 SD inspection."""
from __future__ import annotations

import argparse
import hashlib
from importlib.metadata import version
import json
import os
from pathlib import Path
import platform
import re
import sys
import time

import numpy as np
import wfdb

from ecg_benchmark.quality import (
    BEAT_SYMBOLS, PROTOCOL, decide, evaluate_peaks, evaluation_starts, input_quality,
    mix_noise, noise_partitions, read_sd_segments, resample_noise, select_threshold, split_groups,
)
from ecg_benchmark.quality_reporting import (
    feature_diagnostics, make_plots, summarize, write_csv, write_json,
)


def file_hash(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_identity(record: str, comments: list[str]) -> tuple[str, str]:
    """QTDB source family from documented record ranges; preserve header ID.

    These are source-record clusters, not guaranteed distinct patients. The
    data do not provide a universal cross-database patient linkage.
    """
    suffix = record.removeprefix("sel")
    if suffix.startswith("e"):
        database = "European_ST_T"
    elif suffix.isdigit():
        number = int(suffix)
        if 100 <= number <= 234:
            database = "MIT_BIH_Arrhythmia"
        elif 300 <= number < 400:
            database = "MIT_BIH_ST_Change"
        elif 800 <= number < 900:
            database = "MIT_BIH_Supraventricular"
        elif number in (14046, 14157, 14172, 15814):
            database = "MIT_BIH_Long_Term"
        elif 16000 <= number < 18000:
            database = "MIT_BIH_Normal_Sinus_Rhythm"
        else:
            database = "QTDB_Sudden_Death"
    else:
        database = "unknown"
    match = re.search(r"from record\s+([^,\s]+)", "\n".join(comments))
    original = match.group(1) if match else suffix
    return database, f"{database}:{original}"


def checksums(directory: Path) -> dict[str, str]:
    path = directory / "SHA256SUMS.txt"
    if not path.exists():
        raise FileNotFoundError(f"Missing official checksum manifest: {path}; run download_ecg_quality_data.py")
    result = {}
    for line in path.read_text().splitlines():
        digest, name = line.split(maxsplit=1)
        result[name.lstrip("*").removeprefix("./")] = digest
    return result


def verify(path: Path, expected: dict[str, str]) -> dict:
    actual = file_hash(path)
    if expected.get(path.name) != actual:
        raise ValueError(f"Official SHA256 mismatch or absent checksum: {path}")
    return {"path": str(path.resolve()), "sha256": actual, "verified": True}


def load_inventory(root: Path, records: list[str] | None) -> tuple[list[dict], list[dict]]:
    expected = checksums(root)
    if records is None:
        records_path = root / "RECORDS"
        verify(records_path, expected)
        records = records_path.read_text().split()
    inventory, skipped = [], []
    for name in records:
        if not re.fullmatch(r"sel[a-zA-Z0-9]+", name):
            raise ValueError(f"Invalid QTDB record name: {name}")
        if not (root / f"{name}.hea").exists():
            raise FileNotFoundError(f"Missing waveform header: {name}; download dataset first")
        hashes = [verify(root / f"{name}{ext}", expected) for ext in (".hea", ".dat")]
        header = wfdb.rdheader(str(root / name))
        database, group = source_identity(name, header.comments)
        entry = {"record": name, "source_database": database, "source_group": group,
                 "source_comments": header.comments, "channel": 0, "lead": header.sig_name[0],
                 "fs": header.fs, "samples": header.sig_len, "files": hashes}
        annotation_file = root / f"{name}.atr"
        if not annotation_file.exists():
            # Missing reference data are expected only if absent in the official release.
            if annotation_file.name in expected:
                raise FileNotFoundError(f"Expected reference file missing: {annotation_file}")
            skipped.append({**entry, "reason": "no_official_atr_reference"})
            continue
        hashes.append(verify(annotation_file, expected))
        ann = wfdb.rdann(str(root / name), "atr")
        beat_mask = np.asarray([s in BEAT_SYMBOLS for s in ann.symbol])
        beats = ann.sample[beat_mask]
        if len(beats) < 3 or np.any(np.diff(beats) <= 0):
            skipped.append({**entry, "reason": "insufficient_or_invalid_reference_beats"})
            continue
        entry.update(annotation="atr", beat_symbols=sorted(set(np.asarray(ann.symbol)[beat_mask])),
                     reference_beat_count=len(beats))
        inventory.append(entry)
    return inventory, skipped


def environment(args) -> dict:
    module_root = Path(__file__).resolve().parents[1] / "src" / "ecg_benchmark"
    return {"protocol": PROTOCOL, "arguments": vars(args), "python": platform.python_version(),
            "platform": platform.platform(), "versions": {p: version(p) for p in ("numpy", "scipy", "wfdb", "matplotlib", "pytest")},
            "code_sha256": {p.name: file_hash(p) for p in [Path(__file__), module_root / "quality.py",
                                                        module_root / "quality_reporting.py", module_root / "rpeaks.py"]},
            "reference_tolerance_ms": 50, "detector_agreement_tolerance_ms": 50,
            "rr_target": "FP=FN=0, adjacent RR MAE<=20 ms, at least 2 matched RR pairs",
            "calibration_constraint": "bad retained / all retained <=0.05; at least 30 retained development windows",
            "signal_context_seconds": 2, "window_seconds": 10, "channel": 0,
            "source_group_limitation": "Source-record groups, not verified cross-database patient identities",
            "noise_limitation": "One ma recording; disjoint time blocks, not independent noise subjects",
            "firmware_equivalence": False}


def run_benchmark(args) -> None:
    started = time.perf_counter()
    inventory, skipped = load_inventory(args.qtdb_dir, args.records)
    split = split_groups([r["source_group"] for r in inventory], args.seed)
    for entry in inventory:
        entry["split"] = split[entry["source_group"]]
    noise_hashes = [verify(args.nstdb_dir / name, checksums(args.nstdb_dir)) for name in ("ma.hea", "ma.dat")]
    noise, noise_meta = wfdb.rdsamp(str(args.nstdb_dir / "ma"), channels=[0])
    partitions = noise_partitions(len(noise))
    rows, windows, cache, example_pool = [], [], {}, {}
    for record_index, entry in enumerate(inventory):
        record = entry["record"]
        samples, meta = wfdb.rdsamp(str(args.qtdb_dir / record), channels=[0])
        ecg = samples[:, 0]
        ann = wfdb.rdann(str(args.qtdb_dir / record), "atr")
        mask = np.asarray([s in BEAT_SYMBOLS for s in ann.symbol])
        beat_times, symbols = ann.sample[mask], np.asarray(ann.symbol)[mask]
        fs, partition = float(meta["fs"]), entry["split"]
        size, pad = round(10 * fs), round(2 * fs)
        core = slice(pad, pad+size)
        key = (partition, fs)
        if key not in cache:
            lo, hi = partitions[partition]
            # Split native noise first, then resample: FIR cannot mix test/development samples.
            cache[key] = resample_noise(noise[lo:hi, 0], noise_meta["fs"], fs)
        noise_block = cache[key]
        starts = evaluation_starts(len(ecg), fs, args.windows_per_record)
        if not len(starts):
            skipped.append({"record": record, "reason": "too_short_for_context_window"})
        for window_index, start in enumerate(starts):
            clean = ecg[start-pad:start+size+pad]
            # Window-specific deterministic RNG, invariant to record iteration order.
            token = hashlib.sha256(f"{args.seed}:{record}:{int(start)}".encode()).digest()
            rng = np.random.default_rng(int.from_bytes(token[:8], "little"))
            margin = round(2 * fs)
            if len(noise_block) < len(clean) + 2*margin:
                raise ValueError("Noise partition too short")
            offset = int(rng.integers(margin, len(noise_block)-len(clean)-margin+1))
            n = noise_block[offset:offset+len(clean)]
            selected = (beat_times >= start) & (beat_times < start+size)
            reference = beat_times[selected] - start
            types = symbols[selected].tolist()
            window_id = f"{record}:{int(start)}"
            window_info = {"window_id": window_id, "record": record, "start_sample": int(start),
                           "core_samples": size, "context_samples": pad, "fs": fs,
                           "noise_native_partition": partitions[partition],
                           "noise_resampled_offset": offset, "noise_resampled_length": len(clean),
                           "split": partition, "source_group": entry["source_group"]}
            windows.append(window_info)
            for snr in (None, 24, 18, 12, 6, 0, -6):
                if snr is None:
                    observed, scale = clean.copy(), 0.0
                elif not np.all(np.isfinite(clean)) or np.var(clean[core]) == 0:
                    observed, scale = clean.copy(), None
                else:
                    observed, scale = mix_noise(clean, n, snr, core)
                sqi, primary, secondary = input_quality(observed, fs, core)
                metrics = evaluate_peaks(reference, primary, fs)
                row = {**window_info, "source_database": entry["source_database"], "lead": entry["lead"],
                       "annotation": "atr", "snr": "original" if snr is None else str(snr),
                       "noise_scale": scale, "mixture_valid": snr is None or scale is not None,
                       "beat_symbols": "".join(sorted(set(types))),
                       "has_abnormal_beat": any(s != "N" for s in types), **sqi, **metrics}
                rows.append(row)
                # Bounded evidence pool, chosen without tuning on test labels.
                if partition == "test" and row["rr_target_ok"] is not None:
                    bucket = (row["rr_target_ok"], round(row["agreement"] or 0, 2), row["input_reason"])
                    if bucket not in example_pool:
                        example_pool[bucket] = {"row": row, "signal": observed[core].copy(), "fs": fs,
                                               "reference": reference, "primary": primary, "secondary": secondary}
        print(f"[{record_index+1}/{len(inventory)}] {record}: {len(starts)} windows x 7 conditions", flush=True)

    threshold, curve = select_threshold([r for r in rows if r["split"] == "development"])
    calibration = {"protocol": PROTOCOL, "threshold": threshold,
                   "status": "calibrated" if threshold is not None else "no_feasible_threshold",
                   "input_domain": "QTDB native sampling; not calibrated for AD8232",
                   "fs_values": sorted({r["fs"] for r in inventory}),
                   "constraint_denominator": "all retained reference-evaluable development windows",
                   "max_false_accept": .05, "min_retained": 30}
    for row in rows:
        row["status"], row["reason"] = decide(row, threshold)
    summary = {partition: summarize([r for r in rows if r["split"] == partition], args.seed, args.bootstrap)
               for partition in ("development", "test")}
    by_snr = []
    subgroup_metrics = {}
    for partition in ("development", "test"):
        for subgroup in ("all", "with_abnormal_beats", "only_N_beats"):
            subset = [r for r in rows if r["split"] == partition
                      and (subgroup == "all" or r["has_abnormal_beat"] == (subgroup == "with_abnormal_beats"))]
            subgroup_metrics[f"{partition}:{subgroup}"] = summarize(subset, args.seed, args.bootstrap)
            for snr in ("original", "24", "18", "12", "6", "0", "-6"):
                stats = summarize([r for r in subset if r["snr"] == snr], args.seed, args.bootstrap)
                by_snr.append({"split": partition, "subgroup": subgroup, "snr": snr, **stats})
    examples = {}
    for sample in example_pool.values():
        row = sample["row"]
        if threshold is None:
            category = "reference_good" if row["rr_target_ok"] else "reference_bad"
            if row.get("agreement") == 1 and not row["rr_target_ok"]:
                category = "detectors_agree_but_wrong"
        else:
            category = ("accepted" if row["status"] == "usable" else "rejected") + ("_good" if row["rr_target_ok"] else "_bad")
        if category not in examples:
            examples[category] = {**sample, "category": category,
                                  "title": f"{category}: {row['record']} / {row['snr']} dB / agreement={row['agreement']}"}
    write_csv(args.output / "windows.csv", rows)
    write_csv(args.output / "threshold_sweep.csv", curve)
    write_csv(args.output / "by_snr.csv", by_snr)
    write_csv(args.output / "feature_diagnostics.csv", feature_diagnostics(rows))
    write_json(args.output / "calibration.json", calibration)
    write_json(args.output / "summary.json", {"overall": summary, "subgroups": subgroup_metrics})
    write_json(args.output / "manifest.json", {"included": inventory, "excluded": skipped, "windows": windows,
                                              "noise_files": noise_hashes, "noise_native_partitions": partitions})
    write_json(args.output / "examples.json", [{"category": e["category"], "window": e["row"]} for e in examples.values()])
    make_plots(args.output, rows, curve, by_snr, list(examples.values()))
    write_json(args.output / "run_status.json", {"status": "completed", "elapsed_seconds": time.perf_counter()-started,
                                               "calibration_status": calibration["status"], "windows": len(rows)})
    print(json.dumps({"threshold": threshold, "test": summary["test"]}, indent=2), flush=True)


def run_sd(args) -> None:
    segments, events = read_sd_segments(args.csv)
    # SD has no paired reference: no accuracy labels and no clinical quality claim.
    rows = []
    for segment_index, (times, ecg) in enumerate(segments):
        starts = evaluation_starts(len(ecg), 360, max(1, len(ecg)//3600))
        if not len(starts):
            rows.append({"segment": segment_index, "status": "insufficient_data", "reason": "short_segment",
                         "reference_evaluable": False, "samples": len(ecg)})
        for start in starts:
            chunk = ecg[start-720:start+3600+720]
            sqi, _, _ = input_quality(chunk, 360, slice(720, 4320))
            rows.append({"segment": segment_index, "start_timestamp_ms": times[start], "fs": 360,
                         "status": "insufficient_data", "reason": sqi["input_reason"] if sqi["input_reason"] != "ok"
                         else "no_ad8232_calibration", "reference_evaluable": False, **sqi})
    write_csv(args.output / "sd_windows.csv", rows)
    write_json(args.output / "sd_manifest.json", {"file": str(args.csv.resolve()), "sha256": file_hash(args.csv),
                                                "segments": len(segments), "timestamp_events": events,
                                                "note": "SQIs only; no reference, no AD8232-calibrated threshold"})
    write_json(args.output / "run_status.json", {"status": "completed", "mode": "sd-csv", "windows": len(rows)})
    print(f"SD: {len(segments)} segments, {len(rows)} rows; no reference accuracy claims")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_subparsers(dest="mode", required=True)
    bench = modes.add_parser("benchmark")
    bench.add_argument("--qtdb-dir", type=Path, default=Path("data/qtdb"))
    bench.add_argument("--nstdb-dir", type=Path, default=Path("data/nstdb"))
    bench.add_argument("--records", nargs="+")
    bench.add_argument("--windows-per-record", type=int, default=30)
    bench.add_argument("--seed", type=int, default=42)
    bench.add_argument("--bootstrap", type=int, default=1000)
    bench.add_argument("--output", type=Path, default=Path("outputs/ecg_quality_emg"))
    sd = modes.add_parser("sd-csv")
    sd.add_argument("--csv", type=Path, required=True)
    sd.add_argument("--output", type=Path, default=Path("outputs/ecg_quality_emg_sd"))
    args = parser.parse_args()
    if args.mode == "benchmark" and (not 1 <= args.windows_per_record <= 30 or args.bootstrap < 0):
        parser.error("windows-per-record must be 1..30 and bootstrap must be nonnegative")
    # Refuse reuse to prevent mixing stale charts with a failed/new run.
    if args.output.exists() and any(args.output.iterdir()):
        parser.error("Output directory must be empty or new; use a new --output")
    args.output.mkdir(parents=True, exist_ok=True)
    os.environ.setdefault("MPLCONFIGDIR", str(args.output.resolve() / "mplconfig"))
    write_json(args.output / "config.json", environment(args))
    try:
        run_benchmark(args) if args.mode == "benchmark" else run_sd(args)
    except Exception as exc:
        write_json(args.output / "run_status.json", {"status": "failed", "error": str(exc), "type": type(exc).__name__})
        raise


if __name__ == "__main__":
    main()
