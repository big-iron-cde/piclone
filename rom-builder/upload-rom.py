#!/usr/bin/env python3
"""
upload-rom.py — push a 32 KB ROM image via the Hardware API.

Usage:
    python3 upload-rom.py [PORT] [BIN]

Defaults:
    PORT = /dev/ttyACM0
    BIN  = bin/rom.bin

Protocol (framed serial):
    ENQ → STX → ACK → JSON/binary payload → EOT → ACK/NACK

Steps:
    1. {"cmd":"upload_rom","size":32768}
    2. binary frame with 32768 raw bytes
    3. optional read-until-STP capture
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

from hardware_api import HardwareAPI, ROM_SIZE


def upload(port: str, path: Path, read_stp: bool = False) -> None:
    data = path.read_bytes()
    if len(data) != ROM_SIZE:
        sys.exit(
            f"ERROR: {path} is {len(data)} bytes, expected exactly "
            f"{ROM_SIZE} ({ROM_SIZE // 1024} KB)"
        )

    print(f"Opening {port} ...")
    with HardwareAPI(port) as api:
        print(">> upload_rom")
        result = api.upload_rom_json(data)
        print(json.dumps(result, indent=2))

        if read_stp:
            print("\n>> read until STP")
            capture = api.read_until_stp(max_cycles=500)
            print(f"   reason={capture.reason}  cycles={len(capture.cycles)}")
            for cyc in capture.cycles[:20]:
                print(f"   {cyc.seq:3d}  {cyc.addr}  {cyc.data}  rw={cyc.rw}")
            if len(capture.cycles) > 20:
                print(f"   ... {len(capture.cycles) - 20} more")

    print("\nDone.")


def main() -> None:
    args = sys.argv[1:]
    read_stp = "--read-stp" in args
    if read_stp:
        args.remove("--read-stp")

    port = args[0] if len(args) >= 1 else "/dev/ttyACM0"
    binp = Path(args[1]) if len(args) >= 2 else Path(__file__).parent / "bin" / "rom.bin"
    upload(port, binp, read_stp=read_stp)


if __name__ == "__main__":
    main()
