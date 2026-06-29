#!/usr/bin/env python3
"""Thin wrapper — delegates to Romulan in ~/Downloads/romulan."""

from __future__ import annotations

from romulan_path import ensure_romulan_on_path

ensure_romulan_on_path()

from romulan.hardware_api import (  # noqa: E402
    CaptureResult,
    HardwareAPI,
    HardwareAPIError,
    READ_FRAME_TIMEOUT,
    ROM_SIZE,
)
from romulan.protocol_v1 import (  # noqa: E402
    CHUNK_RAW_MAX,
    CycleEvent,
    ProtocolV1Error,
    ReadResult,
    StatusResponse,
)

ProtocolError = HardwareAPIError

__all__ = [
    "CaptureResult",
    "CHUNK_RAW_MAX",
    "CycleRecord",
    "HardwareAPI",
    "HardwareAPIError",
    "ProtocolError",
    "ProtocolV1Error",
    "READ_FRAME_TIMEOUT",
    "ROM_SIZE",
    "ReadResult",
    "StatusResponse",
]

CycleRecord = CycleEvent

DEFAULT_PORT = "/dev/ttyACM0"
DEFAULT_BAUD = 115200
