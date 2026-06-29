#!/usr/bin/env python3
"""HTTP REST API wrapping the Pico Hardware API v1 (Romulan serial client)."""

from __future__ import annotations

import asyncio
import json
import os
import sys
import uuid
from contextlib import asynccontextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, AsyncIterator

from romulan_path import ensure_romulan_on_path

ensure_romulan_on_path()

from fastapi import FastAPI, HTTPException, Request  # noqa: E402
from fastapi.responses import JSONResponse, StreamingResponse  # noqa: E402
from pydantic import BaseModel, ConfigDict, Field  # noqa: E402

from romulan.hardware_api import HardwareAPI, HardwareAPIError, ROM_SIZE  # noqa: E402

PICO_PORT = os.environ.get("PICO_PORT", "/dev/ttyACM0")
PICO_BAUD = int(os.environ.get("PICO_BAUD", "115200"))


class ResetBody(BaseModel):
    model_config = ConfigDict(populate_by_name=True)
    assert_reset: bool = Field(alias="assert")


class MonitorBody(BaseModel):
    enable: bool


class ReadStartBody(BaseModel):
    max_cycles: int = 10000


@dataclass
class ReadJob:
    job_id: str
    max_cycles: int
    events: asyncio.Queue[dict[str, Any] | None] = field(default_factory=asyncio.Queue)
    done: asyncio.Event = field(default_factory=asyncio.Event)
    error: str | None = None


_api: HardwareAPI | None = None
_lock = asyncio.Lock()
_jobs: dict[str, ReadJob] = {}


@asynccontextmanager
async def lifespan(_app: FastAPI) -> AsyncIterator[None]:
    global _api
    _api = HardwareAPI(PICO_PORT, baudrate=PICO_BAUD)
    try:
        yield
    finally:
        if _api is not None:
            _api.close()
            _api = None


app = FastAPI(title="PICO-ROM Hardware API", version="1.0.0", lifespan=lifespan)


def _api_or_500() -> HardwareAPI:
    if _api is None:
        raise HTTPException(status_code=503, detail="serial not connected")
    return _api


async def _run_sync(fn: Any, *args: Any, **kwargs: Any) -> Any:
    async with _lock:
        return await asyncio.to_thread(fn, *args, **kwargs)


@app.get("/v1/status")
async def get_status() -> dict[str, Any]:
    api = _api_or_500()

    def _status() -> dict[str, Any]:
        st = api.status()
        return {
            "connected": True,
            "port": api.port,
            "protocol_version": 1,
            "phi2_hz": st.phi2_hz,
            "rom_active": st.rom_active,
            "reset_asserted": st.reset_asserted,
            "last_addr": st.last_addr,
            "read_active": st.read_active,
            "monitor_enabled": st.monitor_enabled,
            "upload_active": st.upload_active,
        }

    try:
        return await _run_sync(_status)
    except HardwareAPIError as exc:
        raise HTTPException(status_code=502, detail=str(exc)) from exc


@app.post("/v1/reset")
async def post_reset(body: ResetBody) -> dict[str, Any]:
    api = _api_or_500()
    try:
        await _run_sync(api.reset, body.assert_reset)
        return {"ok": True, "asserted": body.assert_reset}
    except HardwareAPIError as exc:
        raise HTTPException(status_code=502, detail=str(exc)) from exc


@app.post("/v1/monitor")
async def post_monitor(body: MonitorBody) -> dict[str, Any]:
    api = _api_or_500()
    try:
        await _run_sync(api.monitor, body.enable)
        return {"ok": True, "enable": body.enable}
    except HardwareAPIError as exc:
        raise HTTPException(status_code=502, detail=str(exc)) from exc


@app.get("/v1/addr")
async def get_addr() -> dict[str, Any]:
    api = _api_or_500()
    try:
        addr = await _run_sync(api.request_addr)
        return {"ok": True, "addr": f"{addr:04X}"}
    except HardwareAPIError as exc:
        raise HTTPException(status_code=502, detail=str(exc)) from exc


@app.post("/v1/rom")
async def post_rom(request: Request) -> dict[str, Any]:
    api = _api_or_500()
    data = await request.body()
    if len(data) != ROM_SIZE:
        raise HTTPException(
            status_code=400,
            detail=f"ROM body must be exactly {ROM_SIZE} bytes, got {len(data)}",
        )
    try:
        result = await _run_sync(api.upload_rom, data)
        return result
    except HardwareAPIError as exc:
        raise HTTPException(status_code=502, detail=str(exc)) from exc


@app.post("/v1/read")
async def post_read(body: ReadStartBody) -> dict[str, str]:
    job_id = uuid.uuid4().hex
    job = ReadJob(job_id=job_id, max_cycles=body.max_cycles)
    _jobs[job_id] = job
    asyncio.create_task(_capture_job(job))
    return {"job_id": job_id}


async def _capture_job(job: ReadJob) -> None:
    api = _api_or_500()

    def _capture() -> None:
        result = api.read_until_stp(max_cycles=job.max_cycles)
        for cyc in result.cycles:
            job.events.put_nowait({"type": "cycle", **cyc})
        job.events.put_nowait(
            {
                "type": "done",
                "ok": True,
                "reason": result.reason,
                "cycles": len(result.cycles),
            }
        )

    try:
        async with _lock:
            await asyncio.to_thread(_capture)
    except Exception as exc:  # noqa: BLE001
        job.error = str(exc)
        job.events.put_nowait({"type": "done", "ok": False, "reason": "error", "detail": str(exc)})
    finally:
        job.done.set()
        job.events.put_nowait(None)


@app.get("/v1/read/{job_id}")
async def stream_read(job_id: str) -> StreamingResponse:
    job = _jobs.get(job_id)
    if job is None:
        raise HTTPException(status_code=404, detail="unknown job_id")

    async def event_stream() -> AsyncIterator[str]:
        while True:
            item = await job.events.get()
            if item is None:
                break
            yield f"data: {json.dumps(item, separators=(',', ':'))}\n\n"

    return StreamingResponse(event_stream(), media_type="text/event-stream")


@app.exception_handler(HardwareAPIError)
async def hardware_api_error_handler(_request: Request, exc: HardwareAPIError) -> JSONResponse:
    return JSONResponse(status_code=502, content={"ok": False, "detail": str(exc)})
