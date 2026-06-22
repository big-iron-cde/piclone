#!/usr/bin/env python3
"""
hardware_api.py — Host-side Hardware API for Pico-as-ROM.

Framed serial protocol:
    ENQ → STX → ACK → payload → EOT → ACK/NACK

Commands and responses are JSON.  ROM upload uses a second binary frame.
"""

from __future__ import annotations

import json
import sys
import time
from dataclasses import dataclass, field
from typing import Any

try:
    import serial
except ImportError:
    serial = None  # type: ignore

ENQ = 0x05
ACK = 0x06
STX = 0x02
EOT = 0x04
NACK = 0x15

DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_BAUD = 115200
ROM_SIZE = 0x8000


@dataclass
class CycleRecord:
    seq: int
    addr: str
    data: str
    rw: int


@dataclass
class ReadResult:
    ok: bool
    reason: str
    cycles: list[CycleRecord] = field(default_factory=list)
    stopped_addr: str = ""


class HardwareAPI:
    def __init__(self, port: str = DEFAULT_PORT, baud: int = DEFAULT_BAUD) -> None:
        if serial is None:
            raise RuntimeError("pyserial required: pip install pyserial")
        self.port = port
        self.baud = baud
        self._ser: serial.Serial | None = None

    def open(self) -> None:
        self._ser = serial.Serial(self.port, self.baud, timeout=0.2)
        time.sleep(0.3)
        self._ser.reset_input_buffer()

    def close(self) -> None:
        if self._ser:
            self._ser.close()
            self._ser = None

    def __enter__(self) -> HardwareAPI:
        self.open()
        return self

    def __exit__(self, *args: object) -> None:
        self.close()

    @property
    def ser(self) -> serial.Serial:
        if self._ser is None:
            raise RuntimeError("serial port not open")
        return self._ser

    def _read_byte(self, timeout: float = 3.0) -> int:
        deadline = time.time() + timeout
        while time.time() < deadline:
            b = self.ser.read(1)
            if b:
                return b[0]
        raise TimeoutError("timed out waiting for byte")

    def _write_byte(self, value: int) -> None:
        self.ser.write(bytes([value]))

    def _read_frame_payload(self) -> bytes:
        b = self._read_byte()
        if b != STX:
            raise ProtocolError(f"expected STX after ENQ, got 0x{b:02X}")

        self._write_byte(ACK)

        buf = bytearray()
        deadline = time.time() + 30.0
        while time.time() < deadline:
            chunk = self.ser.read(256)
            if not chunk:
                continue
            for byte in chunk:
                if byte == EOT:
                    self._write_byte(ACK)
                    return bytes(buf)
                buf.append(byte)
        raise TimeoutError("timed out waiting for EOT")

    def _send_frame(self, payload: bytes) -> None:
        self._write_byte(ENQ)
        self._write_byte(STX)
        ack = self._read_byte()
        if ack != ACK:
            raise ProtocolError(f"expected ACK from device, got 0x{ack:02X}")
        self.ser.write(payload)
        self._write_byte(EOT)
        resp = self._read_byte()
        if resp != ACK:
            raise ProtocolError(f"device NACK after EOT (0x{resp:02X})")

    def _exchange_json(self, command: dict[str, Any]) -> dict[str, Any]:
        payload = json.dumps(command, separators=(",", ":")).encode()
        self._send_frame(payload)

        b = self._read_byte()
        if b != ENQ:
            raise ProtocolError(f"expected ENQ for response, got 0x{b:02X}")
        raw = self._read_frame_payload()
        return json.loads(raw.decode())

    def _recv_json_frame(self) -> dict[str, Any]:
        b = self._read_byte()
        if b != ENQ:
            raise ProtocolError(f"expected ENQ, got 0x{b:02X}")
        raw = self._read_frame_payload()
        return json.loads(raw.decode())

    def reset(self, assert_reset: bool = True) -> dict[str, Any]:
        return self._exchange_json({"cmd": "reset", "assert": assert_reset})

    def request_addr(self) -> dict[str, Any]:
        return self._exchange_json({"cmd": "request_addr"})

    def upload_rom(self, data: bytes) -> dict[str, Any]:
        return self.upload_rom_json(data)

    def upload_rom_json(self, data: bytes) -> dict[str, Any]:
        if len(data) != ROM_SIZE:
            raise ValueError(f"ROM must be exactly {ROM_SIZE} bytes, got {len(data)}")

        pending = self._exchange_json({"cmd": "upload_rom", "size": ROM_SIZE})
        if not pending.get("ok"):
            raise ProtocolError(f"upload_rom rejected: {pending}")

        self._send_frame(data)
        b = self._read_byte()
        if b != ENQ:
            raise ProtocolError(f"expected ENQ for upload result, got 0x{b:02X}")
        raw = self._read_frame_payload()
        return json.loads(raw.decode())

    def read_until_stp(self, max_cycles: int = 10000) -> ReadResult:
        ack = self._exchange_json(
            {"cmd": "read", "until": "stp", "max_cycles": max_cycles}
        )
        if not ack.get("ok"):
            raise ProtocolError(f"read rejected: {ack}")

        cycles: list[CycleRecord] = []
        result = ReadResult(ok=False, reason="unknown")

        while True:
            msg = self._recv_json_frame()
            if msg.get("type") == "cycle":
                cycles.append(
                    CycleRecord(
                        seq=int(msg["seq"]),
                        addr=str(msg["addr"]),
                        data=str(msg["data"]),
                        rw=int(msg["rw"]),
                    )
                )
            elif msg.get("type") == "done":
                result = ReadResult(
                    ok=bool(msg.get("ok")),
                    reason=str(msg.get("reason", "")),
                    cycles=cycles,
                    stopped_addr=str(msg.get("addr", "")),
                )
                break
            else:
                raise ProtocolError(f"unexpected frame during read: {msg}")

        return result

    def monitor(self, enable: bool = True) -> dict[str, Any]:
        return self._exchange_json({"cmd": "monitor", "enable": enable})


class ProtocolError(Exception):
    pass


def main() -> None:
    """Quick CLI smoke test."""
    port = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_PORT
    with HardwareAPI(port) as api:
        print("request_addr:", api.request_addr())
        print("reset assert:", api.reset(assert_reset=True))
        print("reset release:", api.reset(assert_reset=False))


if __name__ == "__main__":
    main()
