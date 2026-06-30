"""Guardian engine — emergency / safety context."""

from __future__ import annotations

from typing import Any


def actuation_for_emergency(reason: str = "") -> dict[str, Any]:
    return {
        "face": {"emotion": "angry", "intensity": 0.85},
        "scene": "emergency",
        "context": "emergency",
        "micro": ["shake"],
        "timing": {"pre_ms": 0, "post_ms": 5000},
        "reason": reason[:200],
    }


def is_emergency_context(context: str) -> bool:
    return (context or "").strip().lower() == "emergency"
