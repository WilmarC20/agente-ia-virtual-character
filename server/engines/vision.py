"""Vision engine stub — ESP-CAM / frame analysis (Fase 5)."""

from __future__ import annotations

from typing import Any


def analyze_frame_stub(image_bytes: bytes, *, prompt: str = "") -> dict[str, Any]:
    """Placeholder until ESP-CAM pipeline is wired."""
    return {
        "ok": True,
        "stub": True,
        "description": "vision engine not configured",
        "bytes": len(image_bytes),
        "prompt": prompt[:200],
        "objects": [],
        "emotion_hint": "neutral",
    }
