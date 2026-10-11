"""Turn ESP-IDF size data and AF_PROFILE_JSON serial output into a report."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


PROFILE_PREFIX = "AF_PROFILE_JSON:"


def parse_serial_log(path: Path) -> dict:
    profiles = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if PROFILE_PREFIX in line:
            profiles.append(json.loads(line.split(PROFILE_PREFIX, 1)[1]))
    if not profiles:
        raise ValueError(f"No {PROFILE_PREFIX} record found in {path}")
    return profiles[-1]


def parse_size_output(path: Path) -> dict:
    text = path.read_text(encoding="utf-8", errors="replace")
    fields = {}
    patterns = {
        "firmware_flash_bytes": r"Total image size:\s*(\d+)",
        "dram_used_bytes": r"DRAM[^\n]*?used\s+(\d+)",
        "iram_used_bytes": r"IRAM[^\n]*?used\s+(\d+)",
    }
    for key, pattern in patterns.items():
        match = re.search(pattern, text, flags=re.IGNORECASE)
        if match:
            fields[key] = int(match.group(1))
    return fields


def markdown(profile: dict) -> str:
    rows = [
        ("Model constants", profile.get("model_bytes"), "bytes"),
        ("Firmware image", profile.get("firmware_flash_bytes"), "bytes"),
        ("DRAM used", profile.get("dram_used_bytes"), "bytes"),
        ("IRAM used", profile.get("iram_used_bytes"), "bytes"),
        ("Minimum free heap", profile.get("minimum_free_heap_bytes"), "bytes"),
        ("Stack high-water", profile.get("stack_high_water_words"), "words"),
        ("Inference latency p50", profile.get("latency_us_p50"), "us/window"),
        ("Inference latency p95", profile.get("latency_us_p95"), "us/window"),
        ("Inference latency max", profile.get("latency_us_max"), "us/window"),
        ("Mean comparisons", profile.get("comparisons_mean"), "comparisons/window"),
    ]
    lines = [
        "# ESP32 AF baseline hardware profile", "",
        "| Metric | Value | Unit |", "|---|---:|---|",
    ]
    for name, value, unit in rows:
        lines.append(f"| {name} | {value if value is not None else 'not measured'} | {unit} |")
    lines.extend([
        "", "Model inference is measured separately from signal filtering, R-peak",
        "detection, feature extraction, SD, and BLE. Report end-to-end latency",
        "separately before making a real-time system claim.", "",
    ])
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--serial-log", type=Path, required=True)
    parser.add_argument("--size-log", type=Path)
    parser.add_argument("--output-dir", type=Path,
                        default=Path("outputs/af_detection/esp32_profile"))
    args = parser.parse_args()

    profile = parse_serial_log(args.serial_log)
    if args.size_log:
        profile.update(parse_size_output(args.size_log))
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "hardware_profile.json").write_text(
        json.dumps(profile, indent=2), encoding="utf-8"
    )
    (args.output_dir / "REPORT.md").write_text(markdown(profile), encoding="utf-8")
    print(json.dumps(profile, indent=2))


if __name__ == "__main__":
    main()
