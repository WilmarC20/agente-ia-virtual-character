"""Converse helpers — STT/LLM pipeline pieces (main.py still owns the route)."""

from __future__ import annotations

from typing import Any, Awaitable, Callable

from engines.conversation import build_converse_response
from engines.emotion import emotion_state, normalize_emotion
from engines.personality import with_device_context
from engines.plugin_loader import plugin_manager


async def run_plugin_or_llm(
    text: str,
    *,
    try_quick_reply: Callable[[str], dict[str, Any] | None],
    ask_ollama: Callable[[str, bool], Awaitable[dict[str, Any]]],
    cap_reply: Callable[[str], str],
) -> dict[str, Any]:
    plugin_hit = plugin_manager.try_intent(text)
    if plugin_hit:
        plugin_hit.setdefault("heard", text)
        plugin_hit.setdefault("emotion", "happy")
        plugin_hit.setdefault("speak", bool(plugin_hit.get("reply")))
        plugin_hit.setdefault("sing", False)
        plugin_hit.setdefault("sound_effect", "none")
        if plugin_hit.get("reply"):
            plugin_hit["reply"] = cap_reply(plugin_hit["reply"])
        state = emotion_state(
            plugin_hit.get("emotion", "happy"),
            intensity=float(plugin_hit.get("intensity", 0.75)),
        )
        plugin_hit.update(state)
        return with_device_context(plugin_hit)
    return await build_converse_response(
        text,
        try_quick_reply=try_quick_reply,
        ask_ollama=ask_ollama,
        cap_reply=cap_reply,
    )


def silent_converse_response() -> dict[str, Any]:
    return {
        "emotion": "neutral",
        "reply": "",
        "heard": "",
        "sing": False,
        "speak": False,
        "sound_effect": "none",
    }
