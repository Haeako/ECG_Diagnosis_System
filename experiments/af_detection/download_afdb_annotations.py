"""Download AFDB waveforms and annotations used by the AF experiments."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import wfdb

SIGNAL_UNAVAILABLE = {"00735", "03665"}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("data/afdb"))
    parser.add_argument("--experiment", type=Path)
    parser.add_argument("--split", choices=("train", "test"), default="test")
    args = parser.parse_args()
    if args.experiment:
        experiment = json.loads(args.experiment.read_text(encoding="utf-8"))
        records = experiment[f"{args.split}_records"]
    else:
        records = wfdb.get_record_list("afdb")
    files = ["RECORDS"]
    for record in records:
        files.extend((f"{record}.hea", f"{record}.atr", f"{record}.qrs"))
        if record not in SIGNAL_UNAVAILABLE:
            files.append(f"{record}.dat")
    args.output.mkdir(parents=True, exist_ok=True)
    wfdb.dl_files("afdb", str(args.output), files, keep_subdirs=False, overwrite=False)
    print(f"Downloaded waveforms and annotations for {len(records)} AFDB records to {args.output}")
    unavailable = sorted(set(records).intersection(SIGNAL_UNAVAILABLE))
    if unavailable:
        print(f"No waveform exists upstream for: {', '.join(unavailable)}")


if __name__ == "__main__":
    main()
