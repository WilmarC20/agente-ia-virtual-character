"""Notification engine — maps agent events to device commands."""

from __future__ import annotations

from typing import Any

from .device_manager import NOTIFY_KIND_EMOTIONS, device_manager


async def notify_device(body: dict[str, Any], *, cap_speech_text, prepare_spanish_text) -> dict[str, Any]:
    return await device_manager.notify(
        body, cap_speech_text=cap_speech_text, prepare_spanish_text=prepare_spanish_text,
    )


def emotion_for_kind(kind: str) -> str:
    return NOTIFY_KIND_EMOTIONS.get(kind, "thinking")
