"""Registro en memoria de ESP32 que llaman al cerebro (IP, MAC, última petición)."""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass
from datetime import datetime
from typing import Any

ESP_TRACK_EXACT = frozenset({
    "/converse",
    "/tts",
    "/idle",
    "/wake-check",
    "/api/dev/poll",
    "/agent/sing",
})
ESP_TRACK_PREFIXES = ("/music/", "/api/dev/")


def should_track_path(path: str) -> bool:
    if path in ESP_TRACK_EXACT:
        return True
    return any(path.startswith(p) for p in ESP_TRACK_PREFIXES)


def _normalize_mac(raw: str) -> str:
    s = (raw or "").strip().upper().replace("-", ":")
    if not s or s == "00:00:00:00:00:00":
        return ""
    return s


def _fmt_ts(ts: float) -> str:
    return datetime.fromtimestamp(ts).strftime("%Y-%m-%d %H:%M:%S")


@dataclass
class EspDevice:
    mac: str
    ip: str
    last_seen: float
    last_path: str
    first_seen: float
    request_count: int = 1


_lock = threading.Lock()
_devices: dict[str, EspDevice] = {}


def note_request(*, ip: str, mac_header: str, path: str) -> None:
    mac = _normalize_mac(mac_header)
    key = mac if mac else f"ip:{ip or 'unknown'}"
    now = time.time()
    with _lock:
        rec = _devices.get(key)
        if rec:
            if ip:
                rec.ip = ip
            rec.last_seen = now
            rec.last_path = path
            rec.request_count += 1
            if mac:
                rec.mac = mac
        else:
            _devices[key] = EspDevice(
                mac=mac,
                ip=ip or "",
                last_seen=now,
                last_path=path,
                first_seen=now,
            )


def list_devices() -> list[dict[str, Any]]:
    with _lock:
        items = list(_devices.values())
    items.sort(key=lambda d: d.last_seen, reverse=True)
    return [
        {
            "mac": d.mac or None,
            "ip": d.ip,
            "last_seen": _fmt_ts(d.last_seen),
            "last_seen_ts": d.last_seen,
            "last_path": d.last_path,
            "first_seen": _fmt_ts(d.first_seen),
            "request_count": d.request_count,
        }
        for d in items
    ]
