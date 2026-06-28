"""M7 Emotional Memory — sliding window of session events with exponential decay.

Stores only metadata (event type + severity). No user text, no commands.
Events decay with a 20-minute half-life so recent history matters more.
"""
import math
import threading
import time
from dataclasses import dataclass

DECAY_MINUTES = 20.0

VALID_EVENTS = frozenset({
    "voice_interaction",
    "emotion_peak",
    "music_played",
    "long_wait",
    "context_switch",
    "compile_error",
    "compile_ok",
})


@dataclass
class _Event:
    event_type: str
    timestamp: float
    severity: float


class EmotionalMemory:
    MAX_EVENTS = 50
    WINDOW_SECS = 3600.0

    def __init__(self) -> None:
        self._events: list[_Event] = []
        self._lock = threading.Lock()

    def log(self, event_type: str, severity: float = 0.5) -> None:
        if event_type not in VALID_EVENTS:
            return
        severity = max(0.0, min(1.0, float(severity)))
        with self._lock:
            now = time.time()
            cutoff = now - self.WINDOW_SECS
            self._events = [e for e in self._events if e.timestamp > cutoff]
            if len(self._events) >= self.MAX_EVENTS:
                self._events.pop(0)
            self._events.append(_Event(event_type, now, severity))

    def weighted_sum(self, event_type: str) -> float:
        """Decay-weighted sum of severity for event_type events in the last hour."""
        with self._lock:
            now = time.time()
            total = 0.0
            for e in self._events:
                if e.event_type != event_type:
                    continue
                age_min = (now - e.timestamp) / 60.0
                total += e.severity * math.exp(-age_min / DECAY_MINUTES)
            return total

    def get_modifiers(self) -> dict:
        """Intensity and expressivity boosts derived from session history."""
        intensity_boost = 0.0
        expressivity_boost = 0.0

        music = self.weighted_sum("music_played")
        if music > 0.3:
            expressivity_boost += min(0.10, music * 0.10)

        peaks = self.weighted_sum("emotion_peak")
        if peaks > 0.5:
            intensity_boost += min(0.05, peaks * 0.05)

        return {
            "intensity_boost": round(intensity_boost, 3),
            "expressivity_boost": round(expressivity_boost, 3),
        }

    def snapshot(self) -> list[dict]:
        """Debug view — recent events with timestamps."""
        with self._lock:
            return [
                {"type": e.event_type, "ts": round(e.timestamp), "sev": e.severity}
                for e in self._events
            ]
