"""Minimal BLE/SQLite gateway for the ECG edge-first prototype."""
from __future__ import annotations

import argparse
import asyncio
import sqlite3
import struct
import sys
from datetime import datetime
from pathlib import Path

DATA_UUID = "7b9a0002-6e6f-4e69-8d65-636700000001"
COMMAND_UUID = "7b9a0003-6e6f-4e69-8d65-636700000001"
BATCH = struct.Struct("<BBBBHI5h")
EVENT = struct.Struct("<BBBBIHHBBHHH")

FLAG_R_PEAK = 1 << 0
FLAG_AF_RAW = 1 << 1
FLAG_AF_CONFIRMED = 1 << 2
FLAG_AF_NEW = 1 << 3
FLAG_BAD_SQI = 1 << 4


def open_database(path: Path) -> sqlite3.Connection:
    database = sqlite3.connect(path)
    database.execute("PRAGMA journal_mode=WAL")
    database.execute(
        """CREATE TABLE IF NOT EXISTS ecg_samples(
        received_utc TEXT, sequence INTEGER, timestamp_ms INTEGER,
        sample_value INTEGER, packet_gap INTEGER)"""
    )
    database.execute(
        """CREATE TABLE IF NOT EXISTS events(
        received_utc TEXT, timestamp_ms INTEGER, flags INTEGER,
        bpm INTEGER, rr_ms INTEGER, sqi_score INTEGER, sqi_status INTEGER,
        sqi_rule_mask INTEGER, af_probability REAL, af_score INTEGER,
        af_event_id INTEGER)"""
    )
    database.commit()
    return database


async def find_device(name: str):
    from bleak import BleakScanner
    device = await BleakScanner.find_device_by_filter(
        lambda candidate, adv: candidate.name == name or adv.local_name == name,
        timeout=10.0,
    )
    if device is None:
        raise RuntimeError(f"BLE device {name!r} not found")
    return device


async def run(args) -> None:
    from bleak import BleakClient
    database = open_database(args.database)
    initial_samples = database.execute(
        "SELECT COUNT(*) FROM ecg_samples"
    ).fetchone()[0]
    initial_events = database.execute(
        "SELECT COUNT(*) FROM events"
    ).fetchone()[0]
    expected_sequence: int | None = None
    packet_gaps = 0
    connected = False

    def notification(_sender, payload: bytearray) -> None:
        nonlocal expected_sequence, packet_gaps
        if len(payload) != 20 or payload[0] != 1:
            print(f"Ignored unsupported packet length={len(payload)}")
            return
        received = datetime.utcnow().isoformat(timespec="milliseconds") + "Z"
        if payload[1] == 1:
            version, kind, count, period_ms, sequence, first_ms, *samples = BATCH.unpack(payload)
            gap = int(expected_sequence is not None and sequence != expected_sequence)
            packet_gaps += gap
            expected_sequence = (sequence + 1) & 0xFFFF
            database.executemany(
                "INSERT INTO ecg_samples VALUES(?,?,?,?,?)",
                [(received, sequence, first_ms + i * period_ms, value, gap)
                 for i, value in enumerate(samples[:count])],
            )
        elif payload[1] == 2:
            (version, kind, flags, rule_mask, timestamp_ms, bpm, rr_ms,
             sqi_score, sqi_status, af_probability, af_score,
             af_event_id) = EVENT.unpack(payload)
            database.execute(
                "INSERT INTO events VALUES(?,?,?,?,?,?,?,?,?,?,?)",
                (received, timestamp_ms, flags, bpm, rr_ms, sqi_score,
                 sqi_status, rule_mask, af_probability / 10000.0,
                 af_score, af_event_id),
            )
            if flags & FLAG_AF_NEW:
                print(f"AF_SUSPECTED event={af_event_id} p={af_probability/10000:.3f} bpm={bpm}")
            elif flags & FLAG_BAD_SQI:
                print(f"BAD_SQI score={sqi_score} rules=0x{rule_mask:02x}")
            elif flags & FLAG_AF_CONFIRMED:
                print(f"AF_ACTIVE event={af_event_id} p={af_probability/10000:.3f}")
        database.commit()

    try:
        device = await find_device(args.name)
        print(f"Discovered {device.name} ({device.address})", flush=True)
        client_options = ({"winrt": {"address_type": "public",
                                      "use_cached_services": False}}
                          if sys.platform == "win32" else {})
        async with BleakClient(device, **client_options) as client:
            connected = True
            print(f"Connected to {device.name} ({device.address})", flush=True)
            await client.start_notify(DATA_UUID, notification)
            if args.start:
                await client.write_gatt_char(COMMAND_UUID, b"start", response=False)
                print("Sent BLE start command", flush=True)
            else:
                print("Waiting for GPIO0; device remains IDLE until pressed", flush=True)
            started = asyncio.get_running_loop().time()
            while client.is_connected:
                if args.duration > 0 and (
                    asyncio.get_running_loop().time() - started
                ) >= args.duration:
                    break
                await asyncio.sleep(1.0)
    finally:
        database.commit()
        final_samples = database.execute(
            "SELECT COUNT(*) FROM ecg_samples"
        ).fetchone()[0]
        final_events = database.execute(
            "SELECT COUNT(*) FROM events"
        ).fetchone()[0]
        database.close()
        added_samples = final_samples - initial_samples
        added_events = final_events - initial_events
        print(
            f"Session summary: samples={added_samples} events={added_events} "
            f"packet_gaps={packet_gaps} database={args.database}",
            flush=True,
        )
        if connected and added_samples == 0 and not args.start:
            print(
                "No samples received. If GATT connected, press GPIO0 while "
                "the gateway is running or rerun with --start.",
                flush=True,
            )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--name", default="ECG-Monitor")
    parser.add_argument("--database", type=Path, default=Path("ecg_gateway.sqlite3"))
    parser.add_argument("--start", action="store_true",
                        help="send the start command after connecting")
    parser.add_argument("--duration", type=float, default=0.0,
                        help="disconnect after N seconds (0 keeps running)")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        assert BATCH.size == 20 and EVENT.size == 20
        batch = BATCH.pack(1, 1, 5, 10, 7, 1000, 1, 2, 3, 4, 5)
        event = EVENT.pack(1, 2, FLAG_AF_NEW, 15, 2000, 75, 800,
                           90, 1, 8765, 15000, 3)
        assert BATCH.unpack(batch)[4:6] == (7, 1000)
        assert EVENT.unpack(event)[2] == FLAG_AF_NEW
        print("PASS BLE protocol v1: batch=20 event=20")
        return
    try:
        asyncio.run(run(args))
    except Exception as error:
        raise SystemExit(
            f"BLE gateway failed: {type(error).__name__}: {error}"
        ) from None


if __name__ == "__main__":
    main()
