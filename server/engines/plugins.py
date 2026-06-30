"""Plugin manager stub — loads community plugins (Fase 6)."""

from __future__ import annotations

from typing import Any, Callable

from .event_bus import BrainEventBus, EventEnvelope


class PluginManager:
    def __init__(self, bus: BrainEventBus) -> None:
        self._bus = bus
        self._plugins: dict[str, Any] = {}

    def register(self, plugin_id: str, module: Any) -> None:
        self._plugins[plugin_id] = module
        on_load = getattr(module, "on_load", None)
        if callable(on_load):
            on_load(self._bus)

    def try_intent(self, text: str) -> dict[str, Any] | None:
        for mod in self._plugins.values():
            handler: Callable[[str], dict | None] | None = getattr(mod, "handle_intent", None)
            if handler:
                result = handler(text)
                if result:
                    return result
        return None
