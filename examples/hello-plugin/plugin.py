"""Hello plugin — ejemplo mínimo para server/engines (Fase 6)."""

PLUGIN_ID = "hello"
PLUGIN_VERSION = "0.1.0"


def on_load(event_bus) -> None:
    event_bus.subscribe("device.poll", lambda e: None)


def handle_intent(text: str) -> dict | None:
    if "hola plugin" in text.lower():
        return {"reply": "Plugin hello activo.", "emotion": "happy", "speak": True}
    return None
