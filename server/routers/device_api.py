"""Device API router — poll, queue, status."""

from __future__ import annotations

from fastapi import APIRouter, Query, Request, WebSocket
from fastapi.responses import JSONResponse

from engines.device_manager import device_manager, VALID_DEV_EMOTIONS
from transport.device_hub import device_hub

router = APIRouter(tags=["device"])


@router.get("/api/dev/poll")
async def dev_poll():
    return await device_manager.poll()


@router.get("/api/dev/poll-wait")
async def dev_poll_wait(timeout: float = Query(25.0, ge=0.5, le=60.0)):
    return await device_manager.poll_wait(timeout)


@router.websocket("/ws/device")
async def ws_device(websocket: WebSocket):
    await device_hub.handle(websocket)


@router.get("/api/dev/status")
async def dev_status():
    return await device_manager.status()


@router.post("/api/dev/clear")
async def dev_clear():
    n = await device_manager.clear()
    return {"ok": True, "cleared": n}


@router.post("/api/dev/scene")
async def dev_scene(request: Request):
    try:
        body = await request.json()
    except Exception:
        return JSONResponse(status_code=400, content={"error": "invalid json"})
    cmd, qlen, err = await device_manager.queue_scene(body)
    if err:
        return JSONResponse(status_code=400, content={"error": err})
    return {"ok": True, "queued": qlen, "cmd": cmd}


__all__ = ["router", "VALID_DEV_EMOTIONS"]
