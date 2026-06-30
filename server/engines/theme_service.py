"""Theme packages served to ESP (SPIFFS sync)."""

from __future__ import annotations

import json
import re
from pathlib import Path

REPO_DIR = Path(__file__).resolve().parents[2]
THEMES_DIR = REPO_DIR / "themes"

_SAFE_ID = re.compile(r"^[a-z0-9_-]{1,32}$")
_ALLOWED_FILES = frozenset({
    "theme.json",
    "colors.json",
    "layout.json",
    "labels.json",
    "widgets.json",
    "animations.json",
    "labels_atlas.json",
    "labels_atlas.bin",
})


def list_themes() -> list[dict]:
    out: list[dict] = []
    if not THEMES_DIR.is_dir():
        return out
    for d in sorted(THEMES_DIR.iterdir()):
        if not d.is_dir():
            continue
        manifest = d / "theme.json"
        if not manifest.is_file():
            continue
        try:
            data = json.loads(manifest.read_text(encoding="utf-8"))
        except json.JSONDecodeError:
            data = {"id": d.name}
        data.setdefault("id", d.name)
        out.append(data)
    return out


def read_theme_file(theme_id: str, filename: str) -> tuple[bytes, str] | None:
    if not _SAFE_ID.match(theme_id):
        return None
    if filename not in _ALLOWED_FILES:
        return None
    path = THEMES_DIR / theme_id / filename
    if not path.is_file():
        return None
    data = path.read_bytes()
    if filename.endswith(".json"):
        return data, "application/json"
    return data, "application/octet-stream"
