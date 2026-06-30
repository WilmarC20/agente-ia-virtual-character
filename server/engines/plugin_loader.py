"""Load built-in and community plugins at server startup."""

from __future__ import annotations

import importlib.util
import logging
from pathlib import Path
from typing import Any

from .event_bus import BrainEventBus
from .plugins import PluginManager

log = logging.getLogger("agenteia.plugins")

REPO_DIR = Path(__file__).resolve().parents[2]

brain_bus = BrainEventBus()
plugin_manager = PluginManager(brain_bus)


def _load_module(path: Path, module_name: str) -> Any | None:
    if not path.is_file():
        return None
    spec = importlib.util.spec_from_file_location(module_name, path)
    if not spec or not spec.loader:
        return None
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def load_plugins() -> int:
    """Register plugins from examples/hello-plugin and plugins/*/plugin.py."""
    loaded = 0
    candidates = [
        ("hello", REPO_DIR / "examples" / "hello-plugin" / "plugin.py"),
    ]
    plugins_root = REPO_DIR / "plugins"
    if plugins_root.is_dir():
        for child in sorted(plugins_root.iterdir()):
            if child.is_dir():
                candidates.append((child.name, child / "plugin.py"))

    for plugin_id, path in candidates:
        mod = _load_module(path, f"agenteia_plugin_{plugin_id}")
        if mod is None:
            continue
        plugin_manager.register(plugin_id, mod)
        loaded += 1
        log.info("plugin loaded: %s (%s)", plugin_id, path)

    return loaded
