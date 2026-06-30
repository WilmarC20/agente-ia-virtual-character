"""Brain engines package."""

from .device_manager import device_manager, DeviceManager, VALID_DEV_EMOTIONS
from .event_bus import BrainEventBus, EventEnvelope
from .personality import with_device_context
from .behavior import enrich_converse_response, behavior_params_for_personality
from .vision import analyze_frame_stub

__all__ = [
    "device_manager",
    "DeviceManager",
    "VALID_DEV_EMOTIONS",
    "BrainEventBus",
    "EventEnvelope",
    "with_device_context",
    "enrich_converse_response",
    "behavior_params_for_personality",
    "analyze_frame_stub",
]
