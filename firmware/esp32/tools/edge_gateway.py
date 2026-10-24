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
SERVICE_UUID = "7b9a0001-6e6f-4e69-8d65-636700000001"


class PacketDecoder:
    """Shared BLE v1 decoder. Event time can precede the latest waveform.

    Sample timestamps are firmware-advertised estimates, not ADC timestamps.
    AF validity is absent from BLE v1 and must never be inferred from a zero.
    """

    def __init__(self):
        self.sequence = None
        self.anchor = None
        self.last_wave = None

    def timestamp(self, raw):
        if self.anchor is None:
            return raw
        return self.anchor + ((raw - (self.anchor & 0xFFFFFFFF) + 2**31) % 2**32 - 2**31)

    def decode(self, payload):
        if len(payload) != 20 or payload[0] != 1:
            raise ValueError("Expected 20-byte BLE v1 packet")
        if payload[1] == 1:
            _, _, count, period, seq, raw, *values = BATCH.unpack(payload)
            if not 1 <= count <= 5 or period != 10:
                raise ValueError("Invalid BLE v1 count or sample period")
            delta = 1 if self.sequence is None else (seq - self.sequence) & 0xFFFF
            if delta == 0 or delta >= 0x8000:
                raise ValueError("Duplicate or out-of-order waveform sequence")
            first = self.timestamp(raw)
            if self.last_wave is not None and first <= self.last_wave:
                raise ValueError("Device clock moved backwards; reconnect to start a new epoch")
            self.sequence = seq
            self.last_wave = first
            end = first + (count - 1) * period
            self.anchor = max(self.anchor if self.anchor is not None else end, end)
            return {"type": "waveform", "sequence": seq, "period_ms": period,
                    "missing_packets": delta - 1,
                    "samples": [{"t": first + i * period, "v": value}
                                for i, value in enumerate(values[:count])]}
        if payload[1] == 2:
            (_, _, flags, rules, raw, bpm, rr, score, status,
             probability, af_score, event_id) = EVENT.unpack(payload)
            if flags & ~31 or rules & ~15 or status not in (0, 1, 2):
                raise ValueError("Invalid event flags, rules or SQI status")
            if score not in range(101) and score != 255 or probability > 10000:
                raise ValueError("Invalid SQI score or AF probability")
            timestamp = self.timestamp(raw)
            self.anchor = max(self.anchor if self.anchor is not None else timestamp, timestamp)
            usable = status == 1 and not flags & FLAG_BAD_SQI and score != 255
            return {"type": "event", "t": timestamp, "flags": flags,
                    "r_peak": bool(flags & FLAG_R_PEAK), "bpm_raw": bpm, "rr_raw": rr,
                    "bpm": bpm if usable and bpm else None,
                    "rr_ms": rr if usable and rr else None,
                    "sqi_score": None if score == 255 else score, "sqi_status": status,
                    "sqi_rule_mask": rules, "af_probability_raw": probability / 10000,
                    "af_valid": None, "af_score": af_score, "af_event_id": event_id,
                    "af": "sqi-invalid" if not usable else
                          "suspected" if flags & (FLAG_AF_CONFIRMED | FLAG_AF_NEW) else
                          "raw-positive" if flags & FLAG_AF_RAW else "unknown"}
        raise ValueError("Unknown BLE packet type")


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


def ble_client_options():
    """Preserve the existing Windows connection/cache policy for both entrypoints."""
    return ({"winrt": {"address_type": "public", "use_cached_services": False}}
            if sys.platform == "win32" else {})


async def run(args) -> None:
    from bleak import BleakClient
    database = open_database(args.database)
    initial_samples = database.execute(
        "SELECT COUNT(*) FROM ecg_samples"
    ).fetchone()[0]
    initial_events = database.execute(
        "SELECT COUNT(*) FROM events"
    ).fetchone()[0]
    decoder = PacketDecoder()
    packet_gaps = 0
    connected = False

    def notification(_sender, payload: bytearray) -> None:
        nonlocal packet_gaps
        try:
            message = decoder.decode(payload)
        except ValueError as error:
            print(f"Ignored packet: {error}")
            return
        received = datetime.utcnow().isoformat(timespec="milliseconds") + "Z"
        if message["type"] == "waveform":
            packet_gaps += message["missing_packets"]
            database.executemany(
                "INSERT INTO ecg_samples VALUES(?,?,?,?,?)",
                [(received, message["sequence"], item["t"], item["v"],
                  message["missing_packets"]) for item in message["samples"]],
            )
        else:
            database.execute(
                "INSERT INTO events VALUES(?,?,?,?,?,?,?,?,?,?,?)",
                (received, message["t"], message["flags"], message["bpm_raw"],
                 message["rr_raw"], message["sqi_score"] if message["sqi_score"] is not None else 255,
                 message["sqi_status"], message["sqi_rule_mask"],
                 message["af_probability_raw"], message["af_score"], message["af_event_id"]),
            )
            flags = message["flags"]
            if flags & FLAG_AF_NEW:
                print(f"AF_SUSPECTED event={message['af_event_id']} p={message['af_probability_raw']:.3f} bpm={message['bpm_raw']}")
            elif flags & FLAG_BAD_SQI:
                print(f"BAD_SQI score={message['sqi_score']} rules=0x{message['sqi_rule_mask']:02x}")
            elif flags & FLAG_AF_CONFIRMED:
                print(f"AF_ACTIVE event={message['af_event_id']} p={message['af_probability_raw']:.3f}")
        database.commit()

    try:
        device = await find_device(args.name)
        print(f"Discovered {device.name} ({device.address})", flush=True)
        client_options = ble_client_options()
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
    parser.add_argument("--serve", action="store_true", help="run local HTTP/WebSocket API")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--mock", action="store_true", help="deterministic simulated BLE adapter")
    parser.add_argument("--origin", action="append", help="allowed browser origin (repeatable)")
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
    if args.serve:
        import uvicorn
        from live_gateway import create_app
        uvicorn.run(create_app(args.database, mock=args.mock, origins=args.origin),
                    host=args.host, port=args.port)
        return
    try:
        asyncio.run(run(args))
    except Exception as error:
        raise SystemExit(
            f"BLE gateway failed: {type(error).__name__}: {error}"
        ) from None


if __name__ == "__main__":
    main()
