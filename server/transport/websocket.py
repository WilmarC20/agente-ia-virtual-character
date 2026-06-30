"""WebSocket event stream (Fase 3) — stub; poll remains fallback."""

from typing import Any, Dict, Optional


class WebSocketTransport:
  def __init__(self) -> None:
    self._connected = False

  @property
  def connected(self) -> bool:
    return self._connected

  async def push_event(self, event: Dict[str, Any]) -> bool:
    (event)
    return False

  async def recv_command(self) -> Optional[Dict[str, Any]]:
    return None
