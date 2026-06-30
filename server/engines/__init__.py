"""Brain engines package."""

from .device_manager import device_manager, DeviceManager, VALID_DEV_EMOTIONS
from .event_bus import BrainEventBus, EventEnvelope
from .personality import with_device_context
from .behavior import enrich_converse_response, behavior_params_for_personality
from .vision import analyze_frame_stub
from .plugin_loader import brain_bus, plugin_manager, load_plugins
from .theme_service import list_themes, read_theme_file
from .plugins import PluginManager
from .notification import notify_device
from .scheduler import scheduler
from .automation import automation_available, list_entities
from .guardian import actuation_for_emergency, is_emergency_context

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
    "brain_bus",
    "plugin_manager",
    "load_plugins",
    "list_themes",
    "read_theme_file",
    "PluginManager",
    "notify_device",
    "scheduler",
    "automation_available",
    "list_entities",
    "actuation_for_emergency",
    "is_emergency_context",
]
