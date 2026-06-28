"""edge-tts en subproceso aislado (se mata con timeout si se cuelga)."""

from __future__ import annotations

import asyncio
import io
import json
import os
import struct
import sys
import wave

import numpy as np

EDGE_VOICE = os.environ.get("EDGE_TTS_VOICE", "es-MX-DaliaNeural")
TTS_SAMPLE_RATE = int(os.environ.get("TTS_SAMPLE_RATE", "16000"))
TTS_PCM_GAIN = float(os.environ.get("TTS_PCM_GAIN", "0.55"))
EDGE_TIMEOUT_S = float(os.environ.get("EDGE_TTS_TIMEOUT_S", "6"))


def _pcm_to_wav(pcm: np.ndarray, sample_rate: int) -> bytes:
    pcm = np.clip(pcm.astype(np.float32) * TTS_PCM_GAIN, -32768, 32767).astype(np.int16)
    out = io.BytesIO()
    with wave.open(out, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm.tobytes())
    return out.getvalue()


def _resample_pcm(pcm: np.ndarray, src_rate: int, dst_rate: int) -> np.ndarray:
    if src_rate == dst_rate or len(pcm) == 0:
        return pcm
    n_out = int(len(pcm) * dst_rate / src_rate)
    x_out = np.linspace(0, len(pcm) - 1, n_out)
    return np.interp(x_out, np.arange(len(pcm)), pcm.astype(np.float32)).astype(np.int16)


def _sing_prosody(sing: bool, *, strong: bool = False) -> tuple[str, str, str]:
    if sing and strong:
        return "-45%", "+42Hz", "-12%"
    if sing:
        return "-22%", "+18Hz", "-8%"
    return "+0%", "+0Hz", "+0%"


async def _stream_edge_mp3(
    text: str, sing: bool, sing_strong: bool, voice: str, *, speed_rate: str = "+0%"
) -> bytes:
    import edge_tts

    rate, pitch, volume = _sing_prosody(sing, strong=sing_strong)
    if not sing and speed_rate != "+0%":
        rate = speed_rate
    communicate = edge_tts.Communicate(text, voice, rate=rate, pitch=pitch, volume=volume)
    mp3 = bytearray()
    async for chunk in communicate.stream():
        if chunk["type"] == "audio":
            mp3.extend(chunk["data"])
    if not mp3:
        raise RuntimeError("edge-tts returned no audio")
    return bytes(mp3)


def _decode_mp3_to_wav(mp3: bytes) -> bytes:
    import miniaudio

    decoded = miniaudio.decode(
        mp3,
        output_format=miniaudio.SampleFormat.SIGNED16,
        nchannels=1,
        sample_rate=TTS_SAMPLE_RATE,
    )
    samples = np.asarray(decoded.samples, dtype=np.int16)
    if decoded.nchannels > 1:
        samples = samples.reshape(-1, decoded.nchannels)[:, 0]
    if decoded.sample_rate != TTS_SAMPLE_RATE:
        samples = _resample_pcm(samples, decoded.sample_rate, TTS_SAMPLE_RATE)
    return _pcm_to_wav(samples, TTS_SAMPLE_RATE)


async def _synthesize(req: dict) -> bytes:
    text = req.get("text", "").strip()
    if not text:
        raise RuntimeError("missing text")
    sing = bool(req.get("sing", False))
    sing_strong = bool(req.get("sing_strong", False))
    voice = (req.get("voice") or EDGE_VOICE).strip() or EDGE_VOICE
    speed_rate = str(req.get("speed_rate", "+0%"))
    mp3 = await asyncio.wait_for(
        _stream_edge_mp3(text, sing, sing_strong, voice, speed_rate=speed_rate),
        timeout=EDGE_TIMEOUT_S,
    )
    return _decode_mp3_to_wav(mp3)


def main() -> None:
    req = json.loads(sys.stdin.read())
    wav = asyncio.run(_synthesize(req))
    sys.stdout.buffer.write(wav)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(str(e), file=sys.stderr)
        sys.exit(1)
