"""Diagnose Windows BLE address-type/cache behavior for ECG-Monitor."""
from __future__ import annotations

import asyncio

from bleak import BleakClient, BleakScanner


async def main() -> None:
    found = await BleakScanner.find_device_by_filter(
        lambda device, adv: device.name == "ECG-Monitor"
        or adv.local_name == "ECG-Monitor",
        timeout=10.0,
    )
    if found is None:
        raise SystemExit("ECG-Monitor advertisement not found")
    print(f"found name={found.name} address={found.address} details={found.details!r}")
    raw_args = found.details.adv or found.details.scan
    if raw_args is not None:
        print(
            "raw_address=", raw_args.bluetooth_address,
            "raw_type=", getattr(raw_args, "bluetooth_address_type", None),
        )
    errors = []
    for address_type in ("public", "random", None):
        options = {"use_cached_services": False}
        if address_type is not None:
            options["address_type"] = address_type
        client = BleakClient(found, timeout=10.0, winrt=options)
        try:
            await client.connect()
            print(f"CONNECTED address_type={address_type or 'default'}")
            print("services=", [str(service.uuid) for service in client.services])
            await client.disconnect()
            return
        except Exception as error:  # diagnostic tool deliberately records all backends
            errors.append((address_type or "default", type(error).__name__, str(error)))
            print(f"failed address_type={address_type or 'default'}: {error!r}")
            try:
                await client.disconnect()
            except Exception:
                pass
    raise SystemExit(f"all connection strategies failed: {errors!r}")


if __name__ == "__main__":
    asyncio.run(main())
