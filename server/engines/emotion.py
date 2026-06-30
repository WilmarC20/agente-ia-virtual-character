"""Emotion engine — maps text/context to emotion state for device + actuation."""

from __future__ import annotations

from typing import Any

import server_config as srv_cfg

from .behavior import behavior_params_for_personality

VALID_EMOTIONS = frozenset({
    "neutral", "happy", "sad", "angry", "surprised", "thinking", "sleepy",
    "love", "excited", "cool", "confused", "dizzy", "vibing",
})


def normalize_emotion(name: str | None, default: str = "neutral") -> str:
    em = (name or default).strip().lower()
    return em if em in VALID_EMOTIONS else default


def emotion_state(
    emotion: str,
    *,
    intensity: float = 0.7,
    personality_id: str | None = None,
) -> dict[str, Any]:
    """Build EmotionState payload aligned with sdk/contracts/emotion_state.schema.json."""
    params = behavior_params_for_personality(personality_id)
    return {
        "emotion": normalize_emotion(emotion),
        "intensity": max(0.0, min(1.0, float(intensity))),
        "recovery_ms": int(params.get("emotion_recovery_ms", 8000)),
        "expressivity": float(params.get("expressivity", 0.5)),
        "microexp_rate": float(params.get("microexp_rate", 0.6)),
    }


def actuation_plan(
    emotion: str,
    *,
    intensity: float = 0.7,
    hold_ms: int | None = None,
    personality_id: str | None = None,
) -> dict[str, Any]:
    """ActuationPlan hint for ExpressionEngine / dev face queue."""
    state = emotion_state(emotion, intensity=intensity, personality_id=personality_id)
    return {
        "type": "face",
        "emotion": state["emotion"],
        "intensity": state["intensity"],
        "hold_ms": hold_ms or state["recovery_ms"],
        "microexp_rate": state["microexp_rate"],
    }


def personality_default_emotion(personality_id: str | None = None) -> str:
    pid = personality_id or srv_cfg.current_personality_id()
    pres = srv_cfg.get_presentation(pid)
    if pres == "kitt":
        return "neutral"
    return "happy"
