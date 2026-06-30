"""Event bus central del cerebro (Fase 4)."""

from dataclasses import dataclass, field
from typing import Any, Callable, Dict, List


@dataclass
class EventEnvelope:
    id: str
    type: str
    source: str
    timestamp_ms: int
    payload: Dict[str, Any] = field(default_factory=dict)


class BrainEventBus:
  def __init__(self) -> None:
    self._subs: Dict[str, List[Callable[[EventEnvelope], None]]] = {}

  def subscribe(self, event_type: str, handler: Callable[[EventEnvelope], None]) -> None:
    self._subs.setdefault(event_type, []).append(handler)

  def publish(self, event: EventEnvelope) -> None:
    for handler in self._subs.get(event.type, []):
      handler(event)
    for handler in self._subs.get("*", []):
      handler(event)
