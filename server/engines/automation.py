"""Automation engine — Home Assistant bridge."""

from __future__ import annotations

from typing import Any

import ha_client as ha


def automation_available() -> bool:
    return ha.ha_enabled()


async def list_entities(limit: int = 25) -> list[dict[str, Any]]:
    if not ha.ha_enabled():
        return []
    states = ha.get_states()
    return states[:limit] if states else []
