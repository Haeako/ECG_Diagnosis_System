"""Summarize the ECG BLE gateway SQLite database."""

from __future__ import annotations

import argparse
import json
import sqlite3
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("database", type=Path)
    args = parser.parse_args()

    if not args.database.is_file():
        raise SystemExit(f"Database not found: {args.database}")

    with sqlite3.connect(args.database) as database:
        result = {
            "database": str(args.database.resolve()),
            "bytes": args.database.stat().st_size,
            "tables": [
                row[0]
                for row in database.execute(
                    "SELECT name FROM sqlite_master "
                    "WHERE type='table' ORDER BY name"
                )
            ],
            "ecg_samples": database.execute(
                "SELECT COUNT(*) FROM ecg_samples"
            ).fetchone()[0],
            "events": database.execute(
                "SELECT COUNT(*) FROM events"
            ).fetchone()[0],
            "sample_summary": database.execute(
                "SELECT MIN(received_utc), MAX(received_utc), "
                "MIN(timestamp_ms), MAX(timestamp_ms), "
                "COALESCE(SUM(packet_gap), 0) FROM ecg_samples"
            ).fetchone(),
            "sample_values": database.execute(
                "SELECT MIN(sample_value), MAX(sample_value), "
                "AVG(sample_value), COUNT(DISTINCT sequence) "
                "FROM ecg_samples"
            ).fetchone(),
            "event_summary": database.execute(
                "SELECT MIN(received_utc), MAX(received_utc) FROM events"
            ).fetchone(),
            "event_flags": database.execute(
                "SELECT flags, COUNT(*) FROM events "
                "GROUP BY flags ORDER BY flags"
            ).fetchall(),
            "event_quality": database.execute(
                "SELECT sqi_status, sqi_score, COUNT(*) FROM events "
                "GROUP BY sqi_status, sqi_score "
                "ORDER BY sqi_status, sqi_score"
            ).fetchall(),
            "last_events": database.execute(
                "SELECT received_utc, timestamp_ms, flags, bpm, rr_ms, "
                "sqi_score, sqi_status, sqi_rule_mask, af_probability, "
                "af_score, af_event_id FROM events "
                "ORDER BY rowid DESC LIMIT 10"
            ).fetchall(),
        }

    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
