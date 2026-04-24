#!/usr/bin/env python3
"""Tiny bleak-based BLE central for bringing up TWANG firmware without the
Flutter app.

Scans for a device advertising name starting with ``TWANG-``, connects,
subscribes to telemetry notifications, and streams a synthetic control
packet at 40 Hz. The tilt sweeps sinusoidally and an "attack" fires every
2 seconds (i.e. attack_seq increments every 2 s).

Usage:
    pip install bleak
    python fake_client.py [--device TWANG-ABCD] [--seconds 30]
"""

from __future__ import annotations

import argparse
import asyncio
import math
import struct
import sys
import time
from typing import Optional

try:
    from bleak import BleakClient, BleakScanner
except ImportError:
    print("Install bleak first:  pip install bleak", file=sys.stderr)
    raise

SVC_UUID       = "5d2f0001-9e8f-4b8c-9d8a-5a6b7c8d9e10"
CHR_INFO       = "5d2f0002-9e8f-4b8c-9d8a-5a6b7c8d9e10"
CHR_CONTROL    = "5d2f0003-9e8f-4b8c-9d8a-5a6b7c8d9e10"
CHR_TELEMETRY  = "5d2f0004-9e8f-4b8c-9d8a-5a6b7c8d9e10"
CHR_MODE       = "5d2f0005-9e8f-4b8c-9d8a-5a6b7c8d9e10"

CONTROL_FMT   = "<BBhHHBBhHH"      # 16 bytes, little-endian
TELEMETRY_FMT = "<BBBBBBHHHHH" + "BBH" * 4   # 32 bytes, TelemetryPacket v3


def encode_control(*, flags: int, tilt_centideg: int, attack_seq: int,
                   seq: int, calibration_offset: int = 0) -> bytes:
    return struct.pack(
        CONTROL_FMT,
        1,                      # version
        flags & 0xFF,
        int(tilt_centideg),
        attack_seq & 0xFFFF,
        seq & 0xFFFF,
        0,                      # player_id
        0,                      # group_id
        int(calibration_offset),
        0,                      # reserved
        0,                      # reserved
    )


def decode_telemetry(data: bytes) -> dict:
    if len(data) != 32:
        return {"raw": data.hex(), "error": f"bad len {len(data)}"}
    fields = struct.unpack(TELEMETRY_FMT, data)
    (version, mode_id, player_count, your_player_id, global_flags, _r0,
     level, fps_x10, last_seq_ack, rtt_ms, _r1) = fields[:11]
    players = []
    for i in range(4):
        lives, flags, score_x10 = fields[11 + i * 3 : 11 + i * 3 + 3]
        players.append({"lives": lives, "flags": flags, "score_x10": score_x10})
    return {
        "version": version, "mode": mode_id, "player_count": player_count,
        "your_player_id": your_player_id, "global_flags": global_flags,
        "level": level, "fps": fps_x10 / 10.0,
        "last_seq": last_seq_ack, "rtt_ms": rtt_ms,
        "players": players,
    }


async def find_device(name_hint: Optional[str]) -> Optional[str]:
    print("[scan] searching for TWANG-* ...")
    devices = await BleakScanner.discover(timeout=5.0)
    for d in devices:
        if d.name is None:
            continue
        if name_hint is not None and d.name != name_hint:
            continue
        if d.name.startswith("TWANG-"):
            print(f"[scan] found {d.name} ({d.address})")
            return d.address
    return None


async def run(device_addr: str, seconds: float) -> None:
    async with BleakClient(device_addr) as client:
        print("[link] connected")

        info = await client.read_gatt_char(CHR_INFO)
        if len(info) >= 14:
            (proto, fw_major, fw_minor, fw_patch, board_id, caps) = struct.unpack(
                "<BHHHII", info
            )
            print(f"[info] proto={proto} fw={fw_major}.{fw_minor}.{fw_patch} "
                  f"board=0x{board_id:08X} caps=0x{caps:08X}")

        def on_tel(_handle: int, data: bytearray) -> None:
            snap = decode_telemetry(bytes(data))
            print(f"[tel] {snap}")

        await client.start_notify(CHR_TELEMETRY, on_tel)

        started = time.monotonic()
        seq = 0
        attack_seq = 0
        next_attack_at = started + 2.0
        period = 1 / 40.0
        try:
            while (time.monotonic() - started) < seconds:
                t = time.monotonic() - started
                tilt_centideg = int(4500 * math.sin(t * 0.5))
                flags = 0
                now = time.monotonic()
                if now >= next_attack_at:
                    attack_seq = (attack_seq + 1) & 0xFFFF
                    flags |= 0x01
                    next_attack_at = now + 2.0
                pkt = encode_control(
                    flags=flags, tilt_centideg=tilt_centideg,
                    attack_seq=attack_seq, seq=seq,
                )
                await client.write_gatt_char(CHR_CONTROL, pkt, response=False)
                seq = (seq + 1) & 0xFFFF
                await asyncio.sleep(period)
        finally:
            await client.stop_notify(CHR_TELEMETRY)
            print("[link] disconnecting")


def main() -> int:
    p = argparse.ArgumentParser(description="TWANG BLE smoke test client")
    p.add_argument("--device", help="exact advertised name, e.g. TWANG-ABCD")
    p.add_argument("--seconds", type=float, default=30.0)
    args = p.parse_args()

    addr = asyncio.run(find_device(args.device))
    if addr is None:
        print("No TWANG device found in scan window", file=sys.stderr)
        return 1
    asyncio.run(run(addr, args.seconds))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
