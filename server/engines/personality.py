"""Personality + presentation context for device sync."""

from __future__ import annotations

from typing import Any

import server_config as srv_cfg


def with_device_context(payload: dict[str, Any]) -> dict[str, Any]:
    payload["personality"] = srv_cfg.current_personality_id()
    payload["presentation"] = srv_cfg.get_presentation()
    return payload


def current_personality_id() -> str:
    return srv_cfg.current_personality_id()


def get_presentation() -> dict[str, Any]:
    return srv_cfg.get_presentation()
