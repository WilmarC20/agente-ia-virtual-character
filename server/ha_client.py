"""Home Assistant integration: read state and control devices via the REST API.

The token comes from the HA_TOKEN env var (set in secrets.local.ps1, gitignored) —
never hardcode it. If HA_URL/HA_TOKEN are unset the whole module is a graceful no-op,
so the assistant keeps working without home automation.
"""

from __future__ import annotations

import logging
import os
import time

import httpx

log = logging.getLogger("brain.ha")

HA_URL = os.environ.get("HA_URL", "").rstrip("/")
HA_TOKEN = os.environ.get("HA_TOKEN", "")
HA_TIMEOUT = float(os.environ.get("HA_TIMEOUT_S", "6"))

# Domains the assistant can switch on/off/toggle generically.
CONTROLLABLE = (
    "light", "switch", "cover", "fan", "climate",
    "scene", "script", "input_boolean", "media_player",
)

_COMMAND_SERVICE = {"on": "turn_on", "off": "turn_off", "toggle": "toggle"}

_cache: dict = {"ts": 0.0, "data": None}
_CACHE_TTL = 5.0


def ha_enabled() -> bool:
    return bool(HA_URL and HA_TOKEN)


def _headers() -> dict:
    return {"Authorization": f"Bearer {HA_TOKEN}", "Content-Type": "application/json"}


def get_states(force: bool = False) -> list[dict]:
    """All HA entity states, cached briefly. Returns [] if HA is unavailable."""
    if not ha_enabled():
        return []
    now = time.monotonic()
    if not force and _cache["data"] is not None and now - _cache["ts"] < _CACHE_TTL:
        return _cache["data"]
    try:
        r = httpx.get(f"{HA_URL}/api/states", headers=_headers(), timeout=HA_TIMEOUT)
        r.raise_for_status()
        _cache["data"] = r.json()
        _cache["ts"] = now
        return _cache["data"]
    except Exception as e:
        log.warning("HA get_states failed: %s", e)
        return _cache["data"] or []


def controllable_devices() -> list[dict]:
    """Available controllable entities as {entity_id, name, state}."""
    out = []
    for s in get_states():
        eid = s.get("entity_id", "")
        if eid.split(".")[0] in CONTROLLABLE and s.get("state") not in ("unavailable", "unknown", None):
            out.append({
                "entity_id": eid,
                "name": s.get("attributes", {}).get("friendly_name", eid),
                "state": s.get("state"),
            })
    return out


def devices_prompt(max_items: int = 60) -> str:
    """Compact device list to inject into the LLM system prompt."""
    devs = controllable_devices()[:max_items]
    return "\n".join(f"- {d['name']} ({d['entity_id']}) = {d['state']}" for d in devs)


def valid_entity_ids() -> set[str]:
    return {d["entity_id"] for d in controllable_devices()}


def presence_context() -> str:
    """Who is home + day/night, for situational awareness in the prompt."""
    if not ha_enabled():
        return ""
    states = get_states()
    parts = []
    home = [
        s.get("attributes", {}).get("friendly_name", s["entity_id"])
        for s in states
        if s.get("entity_id", "").startswith("person.") and s.get("state") == "home"
    ]
    if home:
        parts.append("En casa ahora: " + ", ".join(home) + ".")
    sun = next((s for s in states if s.get("entity_id") == "sun.sun"), None)
    if sun:
        parts.append("Afuera es de " + ("día" if sun.get("state") == "above_horizon" else "noche") + ".")
    return " ".join(parts)


def execute_actions(actions) -> list[dict]:
    """actions: [{entity_id, command}] with command in on/off/toggle. Calls HA."""
    if not ha_enabled() or not actions:
        return []
    results = []
    valid = valid_entity_ids()
    for a in actions:
        eid = str(a.get("entity_id", "")).strip()
        cmd = str(a.get("command", "")).strip().lower()
        service = _COMMAND_SERVICE.get(cmd)
        if eid not in valid:
            results.append({"entity_id": eid, "ok": False, "error": "unknown entity"})
            continue
        if not service:
            results.append({"entity_id": eid, "ok": False, "error": f"bad command '{cmd}'"})
            continue
        try:
            r = httpx.post(
                f"{HA_URL}/api/services/homeassistant/{service}",
                headers=_headers(),
                json={"entity_id": eid},
                timeout=HA_TIMEOUT,
            )
            r.raise_for_status()
            results.append({"entity_id": eid, "ok": True, "command": cmd})
            log.info("HA %s -> %s OK", service, eid)
        except Exception as e:
            results.append({"entity_id": eid, "ok": False, "error": str(e)})
            log.warning("HA %s -> %s FAILED: %s", service, eid, e)
    _cache["ts"] = 0.0  # bust cache so the next read reflects the change
    return results
