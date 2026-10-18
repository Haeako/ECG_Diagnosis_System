#!/usr/bin/env python3
"""
ECG BLE Client / Controller
---------------------------
Ket noi toi thiet bi ESP32 (NimBLE) chay ecg_ble.c, subscribe notify tren
characteristic ECG_DATA va giai ma goi tin 16-byte thanh cac truong ro rang.
App cung co the gui lenh BLE toi ECG_COMMAND de doi state thay cho nut bam.

Yeu cau:
    pip install bleak

Chay:
    python app.py                           # scan, subscribe, mo prompt lenh
    python app.py --address AA:BB:CC:DD:EE:FF
    python app.py --csv out.csv             # ghi them ra file CSV
    python app.py --command start           # gui mot lenh roi thoat
"""

import argparse
import asyncio
import csv
import struct
import sys
import time
from dataclasses import dataclass

from bleak import BleakClient, BleakScanner

# Phai khop voi ecg_ble.c
SERVICE_UUID = "7b9a0001-6e6f-4e69-8d65-636700000001"
DATA_CHAR_UUID = "7b9a0002-6e6f-4e69-8d65-636700000001"
COMMAND_CHAR_UUID = "7b9a0003-6e6f-4e69-8d65-636700000001"
VALID_COMMANDS = {"start", "stop"}

# '<' = little-endian (khop voi ESP32 / __attribute__((packed)))
#   I  uint32_t timestamp_ms
#   h  int16_t  filtered
#   h  int16_t  corrected
#   H  uint16_t raw_adc
#   H  uint16_t bpm
#   H  uint16_t rr_interval_ms
#   B  uint8_t  is_peak
#   B  uint8_t  SQI score (0..100, 255 = insufficient data)
PACKET_FMT = "<IhhHHHBB"
PACKET_SIZE = struct.calcsize(PACKET_FMT)  # = 16 bytes


@dataclass
class EcgSample:
    timestamp_ms: int
    filtered: int
    corrected: int
    raw_adc: int
    bpm: int
    rr_interval_ms: int
    is_peak: bool
    sqi_score: int

    @classmethod
    def from_bytes(cls, data: bytes) -> "EcgSample":
        if len(data) != PACKET_SIZE:
            raise ValueError(
                f"Goi tin sai kich thuoc: nhan {len(data)} byte, "
                f"can {PACKET_SIZE} byte"
            )
        (ts, filtered, corrected, raw_adc, bpm,
         rr, is_peak, sqi_score) = struct.unpack(PACKET_FMT, data)
        return cls(ts, filtered, corrected, raw_adc, bpm, rr, bool(is_peak), sqi_score)


class EcgBleClient:
    def __init__(self, address: str | None = None,
                 name_prefix: str = "ECG",
                 csv_path: str | None = None,
                 command: str | None = None):
        self.address = address
        self.name_prefix = name_prefix
        self.command = command
        self.csv_writer = None
        self.csv_file = None
        if csv_path:
            self.csv_file = open(csv_path, "w", newline="")
            self.csv_writer = csv.writer(self.csv_file)
            self.csv_writer.writerow(
                ["recv_time", "timestamp_ms", "filtered", "corrected",
                 "raw_adc", "bpm", "rr_interval_ms", "is_peak", "sqi_score"]
            )

    async def discover(self) -> str:
        """Quet BLE va tim thiet bi theo service UUID hoac ten."""
        print("Dang quet BLE...")
        devices = await BleakScanner.discover(timeout=6.0, return_adv=True)
        for device, adv in devices.values():
            uuids = [u.lower() for u in (adv.service_uuids or [])]
            if SERVICE_UUID in uuids:
                print(f"Tim thay theo service UUID: {device.name} ({device.address})")
                return device.address
            if device.name and device.name.startswith(self.name_prefix):
                print(f"Tim thay theo ten: {device.name} ({device.address})")
                return device.address
        raise RuntimeError("Khong tim thay thiet bi ECG. Kiem tra da bat quang cao BLE chua.")

    def _on_notify(self, _handle, data: bytearray):
        try:
            sample = EcgSample.from_bytes(bytes(data))
        except ValueError as e:
            print(f"[canh bao] {e}", file=sys.stderr)
            return

        marker = " <-- R-PEAK" if sample.is_peak else ""
        print(f"t={sample.timestamp_ms:>10} ms  filtered={sample.filtered:>6} "
              f"corrected={sample.corrected:>6}  raw_adc={sample.raw_adc:>5}  "
              f"bpm={sample.bpm:>3}  rr={sample.rr_interval_ms:>5} ms  "
              f"sqi={sample.sqi_score:>3}{marker}")

        if self.csv_writer:
            self.csv_writer.writerow([
                time.time(), sample.timestamp_ms, sample.filtered,
                sample.corrected, sample.raw_adc, sample.bpm,
                sample.rr_interval_ms, int(sample.is_peak), sample.sqi_score,
            ])
            self.csv_file.flush()

    async def _write_command(self, client: BleakClient, command: str):
        command = command.strip().lower()
        if command not in VALID_COMMANDS:
            print(f"Lenh khong hop le: {command}")
            print("Lenh hop le: start, stop, quit")
            return

        services = client.services
        command_chr = services.get_characteristic(COMMAND_CHAR_UUID)
        if command_chr is None:
            raise RuntimeError("Khong tim thay characteristic ECG_COMMAND tren thiet bi")

        await client.write_gatt_char(command_chr, command.encode("ascii"), response=True)
        print(f"Da gui lenh: {command}")

    async def _command_prompt(self, client: BleakClient):
        print("Lenh: start | stop | quit")
        while True:
            command = await asyncio.to_thread(input, "ecg> ")
            command = command.strip().lower()
            if command in {"q", "quit", "exit"}:
                break
            if command:
                await self._write_command(client, command)

    async def run(self):
        address = self.address or await self.discover()

        async with BleakClient(address) as client:
            print(f"Da ket noi: {address}")

            services = client.services
            chr_obj = services.get_characteristic(DATA_CHAR_UUID)
            if chr_obj is None:
                raise RuntimeError("Khong tim thay characteristic ECG_DATA tren thiet bi")

            if self.command:
                await self._write_command(client, self.command)
                return

            # Doc mot lan de kiem tra (READ flag co ho tro trong firmware)
            initial = await client.read_gatt_char(chr_obj)
            if len(initial) == PACKET_SIZE:
                self._on_notify(None, initial)

            await client.start_notify(chr_obj, self._on_notify)
            print("Da subscribe notify.\n")

            try:
                await self._command_prompt(client)
            except asyncio.CancelledError:
                pass
            finally:
                await client.stop_notify(chr_obj)

    def close(self):
        if self.csv_file:
            self.csv_file.close()


async def main():
    parser = argparse.ArgumentParser(description="ECG BLE notify client")
    parser.add_argument("--address", help="Dia chi MAC BLE cua thiet bi (bo qua de tu scan)")
    parser.add_argument("--name-prefix", default="ECG",
                         help="Tien to ten thiet bi khi tu scan (mac dinh: ECG)")
    parser.add_argument("--csv", help="Duong dan file CSV de ghi log")
    parser.add_argument("--command", choices=sorted(VALID_COMMANDS),
                        help="Gui mot lenh BLE roi thoat")
    args = parser.parse_args()

    client = EcgBleClient(address=args.address, name_prefix=args.name_prefix,
                           csv_path=args.csv, command=args.command)
    try:
        await client.run()
    except KeyboardInterrupt:
        pass
    finally:
        client.close()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nDa dung.")
