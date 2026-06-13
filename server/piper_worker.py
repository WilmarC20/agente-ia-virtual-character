"""Sintetiza WAV 16 kHz con Piper en proceso aparte (evita cuelgue con Whisper).

Modos:
  python piper_worker.py           — una petición (stdin JSON → stdout WAV)
  python piper_worker.py --daemon  — proceso persistente (carga ONNX una vez)
"""

from __future__ import annotations

import io
import json
import os
import struct
import sys
import wave
from pathlib import Path

import numpy as np

os.environ.setdefault("OMP_NUM_THREADS", "1")
os.environ.setdefault("ORT_INTRA_OP_NUM_THREADS", "1")
os.environ.setdefault("ORT_INTER_OP_NUM_THREADS", "1")

VOICES_DIR = Path(__file__).parent / "voices"
TTS_SAMPLE_RATE = int(os.environ.get("TTS_SAMPLE_RATE", "16000"))
TTS_PCM_GAIN = float(os.environ.get("TTS_PCM_GAIN", "0.55"))

_voice_cache: dict[str, object] = {}


def _resample_pcm(pcm: np.ndarray, src_rate: int, dst_rate: int) -> np.ndarray:
    if src_rate == dst_rate or len(pcm) == 0:
        return pcm
    n_out = int(len(pcm) * dst_rate / src_rate)
    x_out = np.linspace(0, len(pcm) - 1, n_out)
    return np.interp(x_out, np.arange(len(pcm)), pcm.astype(np.float32)).astype(np.int16)


def _pcm_to_wav(pcm: np.ndarray, sample_rate: int) -> bytes:
    pcm = np.clip(pcm.astype(np.float32) * TTS_PCM_GAIN, -32768, 32767).astype(np.int16)
    out = io.BytesIO()
    with wave.open(out, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm.tobytes())
    return out.getvalue()


def _load_voice(voice_name: str):
    if voice_name in _voice_cache:
        return _voice_cache[voice_name]

    from piper import PiperVoice

    model = VOICES_DIR / f"{voice_name}.onnx"
    if not model.exists():
        raise FileNotFoundError(f"Piper voice not found: {model}")

    print(f"piper_worker: loading {model.name}...", file=sys.stderr, flush=True)
    voice = PiperVoice.load(str(model))
    _voice_cache[voice_name] = voice
    return voice


def synthesize(text: str, voice_name: str, sing: bool) -> bytes:
    if sing:
        text = text.replace(". ", ", ").replace("! ", ", ")

    voice = _load_voice(voice_name)
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        voice.synthesize_wav(text, wf)
    buf.seek(0)
    with wave.open(buf, "rb") as wf:
        pcm = np.frombuffer(wf.readframes(wf.getnframes()), dtype=np.int16)
        src_rate = wf.getframerate()
    pcm = _resample_pcm(pcm, src_rate, TTS_SAMPLE_RATE)
    return _pcm_to_wav(pcm, TTS_SAMPLE_RATE)


def _handle_request(req: dict) -> bytes:
    text = str(req.get("text", "")).strip()
    if not text:
        raise ValueError("missing text")
    return synthesize(
        text=text,
        voice_name=req.get("voice", "es_MX-claude-high"),
        sing=bool(req.get("sing", False)),
    )


def run_daemon(default_voice: str) -> int:
    try:
        _load_voice(default_voice)
    except Exception as e:
        print(f"FATAL: {e}", file=sys.stderr, flush=True)
        return 1

    print("READY", file=sys.stderr, flush=True)
    stdin = sys.stdin.buffer
    stdout = sys.stdout.buffer

    while True:
        header = stdin.read(4)
        if not header:
            break
        (length,) = struct.unpack(">I", header)
        payload = stdin.read(length)
        if len(payload) < length:
            break
        try:
            req = json.loads(payload.decode("utf-8"))
            wav = _handle_request(req)
            stdout.write(struct.pack(">I", len(wav)))
            stdout.write(wav)
            stdout.flush()
        except Exception as e:
            err = json.dumps({"error": str(e)}).encode("utf-8")
            stdout.write(struct.pack(">I", len(err)))
            stdout.write(err)
            stdout.flush()
    return 0


def main() -> int:
    try:
        req = json.loads(sys.stdin.read())
        wav = _handle_request(req)
    except Exception as e:
        print(str(e), file=sys.stderr)
        return 1
    sys.stdout.buffer.write(wav)
    return 0


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--daemon":
        voice = os.environ.get("TTS_VOICE", "es_MX-claude-high")
        raise SystemExit(run_daemon(voice))
    raise SystemExit(main())
