"""Voice engine — TTS entry points (delegates to tts_engine / singing_pipeline)."""

from __future__ import annotations

import asyncio
import os
from typing import Any

import singing_pipeline as sing


async def synthesize_character_wav(
    text: str,
    *,
    synthesize_rvc_guide_wav,
    synthesize_wav_16k,
    release_ollama_vram,
    log,
    sing_flag: bool = False,
    log_label: str = "TTS",
) -> bytes:
    """Same pipeline as POST /tts — character voice with RVC fallback."""
    import time

    t0 = time.monotonic()
    rvc_timeout = float(os.environ.get("TTS_RVC_TIMEOUT_S", "180"))
    tts_rvc_enabled = os.environ.get("ENABLE_TTS_RVC", "1") == "1"
    use_applio = (
        tts_rvc_enabled
        and sing.singing_configured()
        and sing.tts_rvc_runtime_available()
        and os.environ.get("TTS_RVC_GUIDE", "edge").lower() == "edge"
        and sing.TTS_RVC_ENGINE == "applio"
    )
    if use_applio:
        log.info("%s pipeline: Applio unificado (edge+RVC)", log_label)
        await asyncio.to_thread(release_ollama_vram)
        wav = await asyncio.wait_for(
            asyncio.to_thread(sing.render_tts_applio_from_text, text, timeout=rvc_timeout),
            timeout=rvc_timeout + 10,
        )
        log.info("%s Applio %.1fs (%d bytes)", log_label, time.monotonic() - t0, len(wav))
        return wav
    if tts_rvc_enabled and sing.singing_configured():
        await asyncio.to_thread(release_ollama_vram)
        wav = await asyncio.wait_for(
            asyncio.to_thread(synthesize_rvc_guide_wav, text),
            timeout=rvc_timeout + 10,
        )
        log.info("%s RVC %.1fs (%d bytes)", log_label, time.monotonic() - t0, len(wav))
        return wav
    wav = await asyncio.to_thread(synthesize_wav_16k, text)
    log.info("%s edge %.1fs (%d bytes)", log_label, time.monotonic() - t0, len(wav))
    return wav
