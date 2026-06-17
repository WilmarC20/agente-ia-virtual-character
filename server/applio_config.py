"""Configuración RVC local (Applio VoiceConverter).

Pipeline autocontenido en agenteIA — NO usa bender_server ni HTTP :7860.
Solo necesitas:
  - Applio instalado (APPLIO_DIR + APPLIO_PYTHON)
  - Modelo inference .pth (~50 MB) + .index
Ver secrets.local.ps1.example
"""

from __future__ import annotations

import os
from pathlib import Path

SERVER_DIR = Path(__file__).resolve().parent

# Raíz de Applio (contiene rvc/infer/infer.py). Obligatorio si ENABLE_TTS_RVC=1.
APPLIO_DIR = os.environ.get("APPLIO_DIR", "").strip()
APPLIO_PYTHON = os.environ.get("APPLIO_PYTHON", "").strip()
if not APPLIO_PYTHON and APPLIO_DIR:
    APPLIO_PYTHON = str(Path(APPLIO_DIR) / "env" / "python.exe")

RVC_MODEL_PATH = os.environ.get("RVC_MODEL_PATH", "").strip()
RVC_INDEX_PATH = os.environ.get("RVC_INDEX_PATH", "").strip()

# Parámetros RVC (mismos que Applio WebUI / doc IMPLEM)
RVC_F0_METHOD = os.environ.get("RVC_F0_METHOD", "rmvpe")
TTS_RVC_F0_UP_KEY = int(os.environ.get("TTS_RVC_F0_UP_KEY", "0"))
TTS_RVC_INDEX_RATE = float(os.environ.get("TTS_RVC_INDEX_RATE", "0.75"))
TTS_RVC_PROTECT = float(os.environ.get("TTS_RVC_PROTECT", "0.33"))
TTS_RVC_HOP_LENGTH = int(os.environ.get("TTS_RVC_HOP_LENGTH", "128"))
TTS_RVC_EMBEDDER = os.environ.get("TTS_RVC_EMBEDDER", "contentvec")

TTS_RVC_EDGE_VOICE = os.environ.get("TTS_RVC_EDGE_VOICE", "es-MX-JorgeNeural")
TTS_RVC_GUIDE = os.environ.get("TTS_RVC_GUIDE", "edge").lower()

APPLIO_RVC_WORKER = SERVER_DIR / "applio_rvc_worker.py"
APPLIO_USE_DAEMON = os.environ.get("APPLIO_USE_DAEMON", "1") != "0"
RVC_WORKER_TIMEOUT_S = float(os.environ.get("RVC_WORKER_TIMEOUT_S", "300"))


def applio_available() -> bool:
    return bool(APPLIO_DIR and Path(APPLIO_PYTHON).is_file() and APPLIO_RVC_WORKER.is_file())


def rvc_model_configured() -> bool:
    return bool(RVC_MODEL_PATH and Path(RVC_MODEL_PATH).is_file())


def build_convert_request(**extra: object) -> dict:
    """Payload JSON para applio_rvc_worker (stdin o daemon)."""
    req: dict = {
        "model_path": RVC_MODEL_PATH,
        "index_path": RVC_INDEX_PATH,
        "f0_method": RVC_F0_METHOD,
        "pitch": TTS_RVC_F0_UP_KEY,
        "f0_up_key": TTS_RVC_F0_UP_KEY,
        "index_rate": TTS_RVC_INDEX_RATE,
        "protect": TTS_RVC_PROTECT,
        "hop_length": TTS_RVC_HOP_LENGTH,
        "embedder_model": TTS_RVC_EMBEDDER,
    }
    req.update(extra)
    return req


def build_text_request(text: str, **extra: object) -> dict:
    """Texto → edge + RVC en Applio (pipeline unificado, rapido como doc IMPLEM)."""
    pct = int(os.environ.get("TTS_SPEED_PERCENT", "5"))
    sign = "+" if pct >= 0 else ""
    req = build_convert_request(**extra)
    req["text"] = text
    req["edge_voice"] = TTS_RVC_EDGE_VOICE
    req["edge_rate"] = f"{sign}{pct}%"
    return req
