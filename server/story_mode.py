"""Modo historia: concatena TTS por beat y devuelve timeline at_ms + emoción."""
from __future__ import annotations

import logging
from typing import Any

import numpy as np

from tts_engine import TTS_SAMPLE_RATE, synthesize_wav_16k

log = logging.getLogger(__name__)

VALID_EMOTIONS = frozenset({
    "neutral", "happy", "sad", "angry", "surprised", "thinking", "sleepy",
    "love", "excited", "cool", "confused", "dizzy", "vibing",
})


def _pcm_to_wav(pcm: np.ndarray, sample_rate: int = TTS_SAMPLE_RATE) -> bytes:
    import io
    import wave

    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm.astype(np.int16).tobytes())
    return buf.getvalue()


def _wav_pcm(wav: bytes) -> np.ndarray:
    if len(wav) <= 44:
        return np.array([], dtype=np.int16)
    return np.frombuffer(wav[44:], dtype=np.int16).copy()


def _trim_silence(pcm: np.ndarray, threshold: int = 180) -> np.ndarray:
    """Quita padding silencioso del TTS/RVC que suele causar clics al concatenar."""
    if pcm.size == 0:
        return pcm
    start = 0
    while start < pcm.size and abs(int(pcm[start])) <= threshold:
        start += 1
    end = pcm.size
    while end > start and abs(int(pcm[end - 1])) <= threshold:
        end -= 1
    if end <= start:
        return pcm
    return pcm[start:end].copy()


def _fade_edges(pcm: np.ndarray, fade_ms: int = 25) -> np.ndarray:
    """Fade in/out corto para evitar discontinuidades (golpes) entre beats."""
    n = int(fade_ms * TTS_SAMPLE_RATE / 1000)
    if n < 8 or pcm.size < n * 3:
        return pcm
    out = pcm.astype(np.float32)
    ramp_in = np.linspace(0.0, 1.0, n, dtype=np.float32)
    ramp_out = np.linspace(1.0, 0.0, n, dtype=np.float32)
    out[:n] *= ramp_in
    out[-n:] *= ramp_out
    return np.clip(out, -32768, 32767).astype(np.int16)


async def build_story_wav(
    beats: list[dict[str, Any]],
    *,
    gap_ms: int = 350,
    synth: Any = None,
) -> tuple[bytes, list[dict[str, Any]], int]:
    """Sintetiza cada beat, concatena PCM y calcula at_ms real por segmento.

    synth: async callable (text) -> wav bytes. Por defecto la voz guía; pasale la voz
    del personaje (RVC) para que el guion suene con la misma voz que el resto.
    """
    if not beats:
        raise ValueError("beats vacío")
    if len(beats) > 24:
        raise ValueError("máximo 24 beats")

    timeline: list[dict[str, Any]] = []
    chunks: list[np.ndarray] = []
    offset_ms = 0
    gap_samples = max(0, gap_ms * TTS_SAMPLE_RATE // 1000)

    for i, beat in enumerate(beats):
        text = str(beat.get("text") or "").strip()
        if not text:
            raise ValueError(f"beat {i + 1}: text vacío")
        emotion = str(beat.get("emotion") or "neutral").strip().lower()
        if emotion not in VALID_EMOTIONS:
            raise ValueError(f"beat {i + 1}: emoción inválida: {emotion}")

        timeline.append({"at_ms": offset_ms, "emotion": emotion, "text": text[:120]})
        log.info("story beat %d @%dms [%s]: %s", i + 1, offset_ms, emotion, text[:60])

        wav = await synth(text) if synth else await synthesize_wav_16k(text, sing=False)
        pcm = _wav_pcm(wav)
        if pcm.size == 0:
            raise ValueError(f"beat {i + 1}: TTS sin audio")
        pcm = _trim_silence(pcm)
        pcm = _fade_edges(pcm, fade_ms=25)
        if pcm.size == 0:
            raise ValueError(f"beat {i + 1}: TTS sin audio tras trim")
        chunks.append(pcm)
        dur_ms = int(pcm.size * 1000 / TTS_SAMPLE_RATE)
        offset_ms += dur_ms
        if gap_samples > 0 and i + 1 < len(beats):
            chunks.append(np.zeros(gap_samples, dtype=np.int16))
            offset_ms += gap_ms

    pcm_all = np.concatenate(chunks) if chunks else np.array([], dtype=np.int16)
    if pcm_all.size > 0:
        tail = min(int(35 * TTS_SAMPLE_RATE / 1000), pcm_all.size // 2)
        if tail >= 8:
            out = pcm_all.astype(np.float32)
            out[-tail:] *= np.linspace(1.0, 0.0, tail, dtype=np.float32)
            pcm_all = np.clip(out, -32768, 32767).astype(np.int16)
    wav_out = _pcm_to_wav(pcm_all, TTS_SAMPLE_RATE)
    return wav_out, timeline, offset_ms
