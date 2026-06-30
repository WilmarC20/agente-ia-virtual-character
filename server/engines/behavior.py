"""Behavior engine — M11 params and actuation hints from server_config."""

from __future__ import annotations

from typing import Any

import server_config as srv_cfg


def behavior_params_for_personality(personality_id: str | None = None) -> dict[str, Any]:
    pid = personality_id or srv_cfg.current_personality_id()
    p = srv_cfg.PERSONALITY_BEHAVIOR.get(pid, srv_cfg.PERSONALITY_BEHAVIOR.get("bender", {}))
    return {
        "microexp_rate": float(p.get("microexp_rate", 0.6)),
        "emotion_recovery_ms": int(p.get("emotion_recovery_ms", 8000)),
        "expressivity": float(p.get("expressivity", 0.5)),
    }


def enrich_converse_response(result: dict[str, Any]) -> dict[str, Any]:
    """Attach M11 behavior fields expected by firmware."""
    params = behavior_params_for_personality()
    result.setdefault("microexp_rate", params["microexp_rate"])
    result.setdefault("emotion_recovery_ms", params["emotion_recovery_ms"])
    result.setdefault("expressivity", params["expressivity"])
    return result
