"""WebSocket + long-poll transport for device commands."""

from __future__ import annotations

import json
import logging
from typing import Any

from fastapi import WebSocket, WebSocketDisconnect

from engines.device_manager import device_manager

log = logging.getLogger("agenteia.transport")


class DeviceHub:
    def __init__(self) -> None:
        self._clients: set[WebSocket] = set()

    async def connect(self, ws: WebSocket) -> None:
        await ws.accept()
        self._clients.add(ws)
        ctx = device_manager.with_device_context({"cmd": None, "transport": "websocket"})
        await ws.send_text(json.dumps(ctx))

    def disconnect(self, ws: WebSocket) -> None:
        self._clients.discard(ws)

    async def broadcast_context(self) -> None:
        if not self._clients:
            return
        payload = json.dumps(device_manager.with_device_context({"event": "context"}))
        dead: list[WebSocket] = []
        for ws in list(self._clients):
            try:
                await ws.send_text(payload)
            except Exception:
                dead.append(ws)
        for ws in dead:
            self.disconnect(ws)

    async def handle(self, ws: WebSocket) -> None:
        await self.connect(ws)
        try:
            while True:
                raw = await ws.receive_text()
                msg: dict[str, Any] = {}
                try:
                    msg = json.loads(raw) if raw.strip() else {}
                except json.JSONDecodeError:
                    msg = {"op": raw.strip()}
                op = str(msg.get("op", "poll")).lower()
                if op in ("ping", "heartbeat"):
                    await ws.send_text(json.dumps({"op": "pong"}))
                    continue
                if op == "poll":
                    payload = await device_manager.poll()
                    payload["transport"] = "websocket"
                    await ws.send_text(json.dumps(payload))
                    continue
                if op == "poll_wait":
                    timeout = float(msg.get("timeout", 25))
                    payload = await device_manager.poll_wait(timeout)
                    payload["transport"] = "websocket"
                    await ws.send_text(json.dumps(payload))
        except WebSocketDisconnect:
            pass
        finally:
            self.disconnect(ws)


device_hub = DeviceHub()
