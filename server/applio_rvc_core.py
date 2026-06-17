"""Nucleo RVC — corre SOLO bajo APPLIO_PYTHON (entorno Applio).

Importa VoiceConverter tras sys.path + chdir (obligatorio en Applio).
No importar desde el venv del brain server.
"""

from __future__ import annotations

import asyncio
import os
import sys
import tempfile
import time
import uuid
from pathlib import Path
from typing import Any

APPLIO_DIR = os.environ.get("APPLIO_DIR", "").strip()
if not APPLIO_DIR:
    raise RuntimeError("APPLIO_DIR no configurado")

sys.path.insert(0, APPLIO_DIR)
os.chdir(APPLIO_DIR)

_vc: Any | None = None
_loaded_model: str | None = None


def get_voice_converter() -> Any:
    global _vc
    if _vc is None:
        from rvc.infer.infer import VoiceConverter  # type: ignore[import-untyped]

        _vc = VoiceConverter()
    return _vc


def load_model(model_path: str) -> None:
    vc = get_voice_converter()
    global _loaded_model
    if _loaded_model != model_path:
        vc.get_vc(model_path, sid=0)
        _loaded_model = model_path


def run_edge_tts(text: str, output_path: str, *, voice: str, rate: str) -> None:
    """Edge-TTS directo con asyncio.run() -- igual que bender_server.py en :7860.

    No usa subprocess ni timeout artificial. El mismo Applio Python corre edge-tts
    con un event loop fresco (asyncio.run crea loop nuevo cada vez), igual que
    el servidor Flask que funciona en produccion.
    """
    import edge_tts

    async def _go() -> None:
        await edge_tts.Communicate(text, voice, rate=rate).save(output_path)

    asyncio.run(_go())

def convert_guide_wav(
    *,
    input_wav: str,
    model_path: str,
    index_path: str = "",
    pitch: int = 0,
    index_rate: float = 0.75,
    protect: float = 0.33,
    f0_method: str = "rmvpe",
    hop_length: int = 128,
    embedder_model: str = "contentvec",
) -> bytes:
    """Guia WAV ? voz personaje WAV (bytes RIFF)."""
    if not Path(input_wav).is_file():
        raise ValueError(f"input_wav invalido: {input_wav}")

    load_model(model_path)
    vc = get_voice_converter()
    out_path = os.path.join(tempfile.gettempdir(), f"agente_rvc_{uuid.uuid4().hex[:8]}.wav")
    try:
        t0 = time.monotonic()
        vc.convert_audio(
            audio_input_path=input_wav,
            audio_output_path=out_path,
            model_path=model_path,
            index_path=index_path,
            pitch=pitch,
            f0_method=f0_method,
            index_rate=index_rate,
            volume_envelope=1.0,
            protect=protect,
            hop_length=hop_length,
            embedder_model=embedder_model,
            export_format="WAV",
        )
        elapsed = time.monotonic() - t0
        data = Path(out_path).read_bytes()
        if not data.startswith(b"RIFF"):
            raise RuntimeError("RVC produjo audio invalido (sin RIFF)")
        print(f"applio_rvc: convert {elapsed:.1f}s", file=sys.stderr, flush=True)
        return data
    finally:
        try:
            os.unlink(out_path)
        except OSError:
            pass


def text_to_character_wav(
    text: str,
    *,
    model_path: str,
    index_path: str = "",
    voice: str = "es-MX-JorgeNeural",
    rate: str = "+0%",
    pitch: int = 0,
    index_rate: float = 0.75,
    protect: float = 0.33,
    f0_method: str = "rmvpe",
    hop_length: int = 128,
    embedder_model: str = "contentvec",
) -> bytes:
    """Texto ? edge TTS ? RVC (mismo pipeline que :7860, en un solo proceso)."""
    text = text.strip() or "Ok."
    tts_path = os.path.join(tempfile.gettempdir(), f"agente_tts_{uuid.uuid4().hex[:8]}.wav")
    try:
        t0 = time.monotonic()
        run_edge_tts(text, tts_path, voice=voice, rate=rate)
        print(f"applio_tts: edge {time.monotonic() - t0:.1f}s", file=sys.stderr, flush=True)
        return convert_guide_wav(
            input_wav=tts_path,
            model_path=model_path,
            index_path=index_path,
            pitch=pitch,
            index_rate=index_rate,
            protect=protect,
            f0_method=f0_method,
            hop_length=hop_length,
            embedder_model=embedder_model,
        )
    finally:
        try:
            os.unlink(tts_path)
        except OSError:
            pass
