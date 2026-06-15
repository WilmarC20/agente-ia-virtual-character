"""Persistent agent memory: what the character knows about its human + its mood.

Stored as JSON next to the server (agent_state.json, gitignored — it's personal).
Survives restarts so the character "remembers" you across sessions.
"""

from __future__ import annotations

import json
import logging
import threading
from pathlib import Path

log = logging.getLogger("brain.state")

_PATH = Path(__file__).resolve().parent / "agent_state.json"
_lock = threading.Lock()
_DEFAULT = {"name": "", "facts": [], "mood": "neutral"}


def _load() -> dict:
    try:
        data = json.loads(_PATH.read_text(encoding="utf-8"))
        return {**_DEFAULT, **data}
    except Exception:
        return dict(_DEFAULT)


def context() -> str:
    """Compact memory snapshot for the system prompt."""
    s = _load()
    parts = []
    if s.get("name"):
        parts.append(f"El humano se llama {s['name']}.")
    if s.get("facts"):
        parts.append("Recordás de él: " + "; ".join(s["facts"][-8:]) + ".")
    if s.get("mood") and s["mood"] != "neutral":
        parts.append(f"Tu humor de fondo ahora es: {s['mood']}.")
    return " ".join(parts)


def update(name: str | None = None, remember: str | None = None, mood: str | None = None) -> dict:
    """Persist any of: the human's name, a new fact to remember, the current mood."""
    with _lock:
        s = _load()
        changed = False
        if name:
            n = str(name).strip()[:40]
            if n and n != s.get("name"):
                s["name"] = n
                changed = True
        if remember:
            fact = str(remember).strip()[:120]
            if fact and fact not in s["facts"]:
                s["facts"].append(fact)
                s["facts"] = s["facts"][-20:]  # keep the most recent 20
                changed = True
        if mood:
            m = str(mood).strip().lower()[:20]
            if m and m != s.get("mood"):
                s["mood"] = m
                changed = True
        if changed:
            try:
                _PATH.write_text(json.dumps(s, ensure_ascii=False, indent=2), encoding="utf-8")
            except Exception as e:
                log.warning("agent_state save failed: %s", e)
        return s
