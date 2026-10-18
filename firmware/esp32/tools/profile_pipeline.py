"""Capture ESP32 PERF_CSV serial records and produce a concise assessment."""
from __future__ import annotations

import argparse
import csv
import re
import statistics
import time
from pathlib import Path

import serial

PREFIX = "PERF_CSV,"
HEADER = "PERF_CSV_HEADER,"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM18")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--seconds", type=int, default=120)
    parser.add_argument("--csv", type=Path, default=Path("out/pipeline_profile.csv"))
    parser.add_argument("--raw-log", type=Path, default=Path("out/pipeline_profile.log"))
    args = parser.parse_args()
    args.csv.parent.mkdir(parents=True, exist_ok=True)
    args.raw_log.parent.mkdir(parents=True, exist_ok=True)

    header: list[str] | None = None
    rows: list[list[str]] = []
    deadline = time.monotonic() + args.seconds
    with serial.Serial(args.port, args.baud, timeout=0.5) as port, \
         args.raw_log.open("w", encoding="utf-8") as raw:
        while time.monotonic() < deadline:
            line = port.readline().decode("utf-8", errors="replace").strip()
            if not line:
                continue
            raw.write(line + "\n")
            clean = re.sub(r"\x1b\[[0-9;]*m", "", line)
            if HEADER in clean:
                header = clean[clean.index(HEADER) + len(HEADER):].split(",")
            elif PREFIX in clean:
                rows.append(clean[clean.index(PREFIX) + len(PREFIX):].split(","))

    if header is None or not rows:
        raise SystemExit("No PERF_CSV records captured; put device in RECORD mode.")
    valid = [row for row in rows if len(row) == len(header)]
    with args.csv.open("w", newline="", encoding="utf-8") as output:
        writer = csv.writer(output)
        writer.writerow(header)
        writer.writerows(valid)

    records = [dict(zip(header, row)) for row in valid]
    def values(name: str) -> list[float]:
        return [float(row[name]) for row in records]
    drops = sum(sum(values(name)) for name in
                ("notify_overruns", "sd_drops", "ble_drops", "sd_errors"))
    print(f"windows={len(records)} csv={args.csv} raw={args.raw_log}")
    print(f"pipeline observed max={max(values('pipeline_max_us')):.0f} us "
          f"(deadline 2770 us)")
    print(f"SD reported p99<= {max(values('sd_p99_us')):.0f} us, "
          f"observed max={max(values('sd_max_us')):.0f} us")
    print(f"CPU mean core0={statistics.fmean(values('cpu0_pct')):.1f}% "
          f"core1={statistics.fmean(values('cpu1_pct')):.1f}%")
    print(f"drops/errors total={drops:.0f}")


if __name__ == "__main__":
    main()
