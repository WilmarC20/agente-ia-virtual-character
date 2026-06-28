"""M4 Context System — active context shapes expressivity, rhythm, and idle behavior."""
import threading

# Per-context defaults. expressivity is the global scale for all face expressions.
# pre/post_response_ms: rhythm pacing (M5) sent to the ESP32 in /converse response.
CONTEXT_CONFIG: dict[str, dict] = {
    "idle":         {"expressivity": 0.50, "pre_response_ms": 400, "post_response_ms": 250, "emotion_default": "neutral"},
    "programming":  {"expressivity": 0.40, "pre_response_ms": 600, "post_response_ms": 350, "emotion_default": "thinking"},
    "compiling":    {"expressivity": 0.45, "pre_response_ms": 500, "post_response_ms": 300, "emotion_default": "thinking"},
    "music":        {"expressivity": 0.80, "pre_response_ms": 250, "post_response_ms": 150, "emotion_default": "happy"},
    "gaming":       {"expressivity": 0.90, "pre_response_ms": 150, "post_response_ms": 100, "emotion_default": "excited"},
    "domotics":     {"expressivity": 0.30, "pre_response_ms": 300, "post_response_ms": 200, "emotion_default": "neutral"},
    "camera_watch": {"expressivity": 0.25, "pre_response_ms": 700, "post_response_ms": 400, "emotion_default": "thinking"},
    "waiting":      {"expressivity": 0.20, "pre_response_ms": 800, "post_response_ms": 500, "emotion_default": "neutral"},
    "visitor":      {"expressivity": 0.75, "pre_response_ms": 300, "post_response_ms": 200, "emotion_default": "happy"},
    "night_mode":   {"expressivity": 0.25, "pre_response_ms": 900, "post_response_ms": 500, "emotion_default": "sleepy"},
    "emergency":    {"expressivity": 0.60, "pre_response_ms":  80, "post_response_ms":  50, "emotion_default": "angry"},
}

_lock = threading.Lock()
_current = "idle"


def get() -> str:
    with _lock:
        return _current


def set_context(name: str) -> str:
    global _current
    with _lock:
        if name in CONTEXT_CONFIG:
            _current = name
        return _current


def config(name: str | None = None) -> dict:
    ctx = name or get()
    return CONTEXT_CONFIG.get(ctx, CONTEXT_CONFIG["idle"])


def all_contexts() -> list[str]:
    return sorted(CONTEXT_CONFIG.keys())
