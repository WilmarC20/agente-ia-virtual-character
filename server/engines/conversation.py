"""Conversation engine — wraps Ollama/history helpers used by /converse."""

from __future__ import annotations

from collections import deque
from typing import Any, Callable, Awaitable

from .behavior import enrich_converse_response
from .personality import with_device_context

_history: deque = deque(maxlen=6)


def conversation_history() -> deque:
    return _history


def clear_history() -> int:
    _history.clear()
    return 0


async def build_converse_response(
    text: str,
    *,
    try_quick_reply: Callable[[str], dict[str, Any] | None],
    ask_ollama: Callable[[str, bool], Awaitable[dict[str, Any]]],
    cap_reply: Callable[[str], str],
) -> dict[str, Any]:
    result = try_quick_reply(text)
    if result is not None:
        result["reply"] = cap_reply(result["reply"])
    else:
        result = await ask_ollama(text, use_history=True)
    result["heard"] = text
    enrich_converse_response(result)
    return with_device_context(result)
