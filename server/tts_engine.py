"""TTS backends: sapi (Windows, rápido), edge-tts (neural), piper (lento en este PC)."""

from __future__ import annotations

import asyncio
import io
import json
import logging
import os
import re
import struct
import subprocess
import sys
import tempfile
import threading
import unicodedata
import wave
from pathlib import Path

import httpx
import numpy as np

import server_config as srv_cfg

log = logging.getLogger("brain.tts")

# edge = voz neural (~2s, requiere internet) con fallback rápido a sapi.
# sapi = Windows SAPI (~1-3s, offline). piper tarda 20+ min cargando onnx en este PC.
TTS_ENGINE = os.environ.get("TTS_ENGINE", "sapi").lower()
EDGE_VOICE = os.environ.get("EDGE_TTS_VOICE", "es-MX-DaliaNeural")
# Guia RVC: edge-tts neural (TTS_RVC_EDGE_VOICE) → Applio VoiceConverter local
TTS_RVC_EDGE_VOICE = os.environ.get("TTS_RVC_EDGE_VOICE", "es-MX-JorgeNeural")
TTS_RVC_EDGE_TIMEOUT_S = float(os.environ.get("TTS_RVC_EDGE_TIMEOUT_S", "15"))
# Timeout corto: si edge se cuelga, caemos a sapi rápido (evita -11 en la placa).
EDGE_TIMEOUT_S = float(os.environ.get("EDGE_TTS_TIMEOUT_S", "6"))
TTS_TOTAL_TIMEOUT_S = float(os.environ.get("TTS_TOTAL_TIMEOUT_S", "28"))
TTS_MAX_CHARS = int(os.environ.get("TTS_MAX_CHARS", "320"))
SAPI_TIMEOUT_S = float(os.environ.get("SAPI_TTS_TIMEOUT_S", "20"))
PIPER_VOICE = os.environ.get("TTS_VOICE", "es_MX-claude-high")
TTS_SAMPLE_RATE = int(os.environ.get("TTS_SAMPLE_RATE", "16000"))
TTS_PCM_GAIN = float(os.environ.get("TTS_PCM_GAIN", "0.55"))
VOICES_DIR = Path(__file__).parent / "voices"
PIPER_WORKER = Path(__file__).parent / "piper_worker.py"
EDGE_WORKER = Path(__file__).parent / "edge_worker.py"
SAPI_WORKER = Path(__file__).parent / "sapi_worker.py"
PIPER_TIMEOUT_S = float(os.environ.get("PIPER_TIMEOUT_S", "1800"))

_piper_proc: subprocess.Popen | None = None
_piper_lock = threading.Lock()

_EMOJI_RE = re.compile(
    "["
    "\U0001F1E0-\U0001F1FF"
    "\U0001F300-\U0001FAFF"
    "\U00002702-\U000027B0"
    "\U000024C2-\U0001F251"
    "\u200d"
    "\ufe0f"
    "]+",
    flags=re.UNICODE,
)
_SUBSCRIPT = "₀₁₂₃₄₅₆₇₈₉"
_SUPERSCRIPT = "⁰¹²³⁴⁵⁶⁷⁸⁹"


def _is_emoji_char(ch: str) -> bool:
    o = ord(ch)
    if o in (0xFE0F, 0x200D):
        return True
    if 0x1F300 <= o <= 0x1FAFF:
        return True
    if 0x2600 <= o <= 0x27BF:
        return True
    cat = unicodedata.category(ch)
    return cat == "So" and o > 0x2300


def sanitize_speech_text(text: str) -> str:
    """Texto listo para TTS: sin emojis, markdown ni símbolos que se leen mal."""
    text = unicodedata.normalize("NFC", text)
    text = "".join(ch for ch in text if not _is_emoji_char(ch))
    text = _EMOJI_RE.sub("", text)
    text = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", text)
    text = re.sub(r"`+([^`]+)`+", r"\1", text)
    text = re.sub(r"\*+([^*]+)\*+", r"\1", text)
    text = re.sub(r"_+([^_]+)_+", r"\1", text)
    text = re.sub(r"#+\s*", "", text)
    text = text.replace("…", ".").replace("–", "-").replace("—", "-")
    text = text.replace(""", '"').replace(""", '"').replace("'", "'").replace("'", "'")
    text = re.sub(r"(\d)\s*%", r"\1 por ciento", text)
    text = text.replace("&", " y ")
    text = text.translate(str.maketrans(_SUPERSCRIPT, "0123456789"))
    for ch in _SUBSCRIPT:
        text = text.replace(ch, "")
    text = re.sub(r"\^[\d]+", "", text)
    text = re.sub(r"https?://\S+", "", text)
    text = re.sub(r"\s+", " ", text).strip()
    return text


def _normalize_tts_text(text: str) -> str:
    text = sanitize_speech_text(text)
    text = " ".join(text.replace("\n", " ").replace("\r", " ").split())
    if len(text) > TTS_MAX_CHARS:
        text = text[: TTS_MAX_CHARS - 1].rstrip() + "."
    return text


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


def _sanitize_for_sapi(text: str) -> str:
    """PowerShell Speak() rompe con comillas raras y saltos."""
    return (
        text.replace("'", " ")
        .replace('"', " ")
        .replace("`", " ")
        .replace("\n", " ")
        .replace("\r", " ")
    )


def _sing_prosody(sing: bool, *, strong: bool = False) -> tuple[str, str, str]:
    if sing and strong:
        return "-45%", "+42Hz", "-12%"
    if sing:
        return "-22%", "+18Hz", "-8%"
    return "+0%", "+0Hz", "+0%"


def _synthesize_sapi(text: str, sing: bool) -> bytes:
    """Windows SAPI via subproceso aislado."""
    text = _sanitize_for_sapi(_normalize_tts_text(text))
    if not text.strip():
        text = "Ok."
    payload = json.dumps({"text": text, "sing": sing}, ensure_ascii=False).encode("utf-8")
    proc = subprocess.run(
        [sys.executable, str(SAPI_WORKER)],
        input=payload,
        capture_output=True,
        timeout=int(SAPI_TIMEOUT_S) + 5,
        cwd=str(SAPI_WORKER.parent),
    )
    if proc.returncode != 0:
        err = proc.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(err or f"sapi_worker exit {proc.returncode}")
    if not proc.stdout:
        raise RuntimeError("sapi_worker returned empty audio")
    return proc.stdout


def _ensure_piper_voice() -> Path:
    VOICES_DIR.mkdir(exist_ok=True)
    model = VOICES_DIR / f"{PIPER_VOICE}.onnx"
    if model.exists():
        return model
    lang, name, quality = PIPER_VOICE.split("-")
    base = (
        "https://huggingface.co/rhasspy/piper-voices/resolve/main/"
        f"{lang.split('_')[0]}/{lang}/{name}/{quality}/{PIPER_VOICE}.onnx"
    )
    log.info("Downloading Piper voice '%s'...", PIPER_VOICE)
    for url, dest in ((base, model), (base + ".json", model.with_suffix(".onnx.json"))):
        r = httpx.get(url, follow_redirects=True, timeout=300)
        r.raise_for_status()
        dest.write_bytes(r.content)
    return model


def _piper_env() -> dict[str, str]:
    env = os.environ.copy()
    env["OMP_NUM_THREADS"] = "1"
    env["ORT_INTRA_OP_NUM_THREADS"] = "1"
    env["ORT_INTER_OP_NUM_THREADS"] = "1"
    env["TTS_VOICE"] = PIPER_VOICE
    return env


def _read_exact(stream, nbytes: int, timeout_s: float) -> bytes:
    import time

    deadline = time.monotonic() + timeout_s
    chunks: list[bytes] = []
    got = 0
    while got < nbytes:
        if time.monotonic() > deadline:
            raise TimeoutError(f"Piper daemon read timeout ({timeout_s}s)")
        chunk = stream.read(nbytes - got)
        if not chunk:
            raise RuntimeError("Piper daemon closed stdout")
        chunks.append(chunk)
        got += len(chunk)
    return b"".join(chunks)


def _start_piper_daemon() -> subprocess.Popen:
    log.info("Starting Piper daemon (voice=%s)...", PIPER_VOICE)
    proc = subprocess.Popen(
        [sys.executable, str(PIPER_WORKER), "--daemon"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=_piper_env(),
        cwd=str(PIPER_WORKER.parent),
        bufsize=0,
    )
    assert proc.stderr is not None
    line = proc.stderr.readline().decode("utf-8", errors="replace").strip()
    if line != "READY":
        err = proc.stderr.read().decode("utf-8", errors="replace")
        proc.kill()
        raise RuntimeError(f"Piper daemon failed to start: {line or err}")
    log.info("Piper daemon ready")
    return proc


def _get_piper_daemon() -> subprocess.Popen:
    global _piper_proc
    with _piper_lock:
        if _piper_proc is None or _piper_proc.poll() is not None:
            _piper_proc = _start_piper_daemon()
        return _piper_proc


def _daemon_request(req: dict) -> bytes:
    proc = _get_piper_daemon()
    payload = json.dumps(req).encode("utf-8")
    assert proc.stdin is not None and proc.stdout is not None
    proc.stdin.write(struct.pack(">I", len(payload)))
    proc.stdin.write(payload)
    proc.stdin.flush()
    header = _read_exact(proc.stdout, 4, PIPER_TIMEOUT_S)
    (length,) = struct.unpack(">I", header)
    body = _read_exact(proc.stdout, length, PIPER_TIMEOUT_S)
    if body[:1] == b"{" and b"error" in body[:80]:
        try:
            raise RuntimeError(json.loads(body.decode("utf-8"))["error"])
        except (json.JSONDecodeError, KeyError):
            pass
    if not body.startswith(b"RIFF"):
        raise RuntimeError(f"Piper daemon returned invalid audio ({len(body)} bytes)")
    return body


def _synthesize_piper_once(text: str, sing: bool) -> bytes:
    payload = json.dumps({"text": text, "sing": sing, "voice": PIPER_VOICE})
    proc = subprocess.run(
        [sys.executable, str(PIPER_WORKER)],
        input=payload.encode("utf-8"),
        capture_output=True,
        timeout=PIPER_TIMEOUT_S,
        env=_piper_env(),
        cwd=str(PIPER_WORKER.parent),
    )
    if proc.returncode != 0:
        err = proc.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(err or f"piper_worker exit {proc.returncode}")
    if not proc.stdout:
        raise RuntimeError("piper_worker returned empty audio")
    return proc.stdout


def _synthesize_piper(text: str, sing: bool) -> bytes:
    _ensure_piper_voice()
    try:
        return _daemon_request({"text": text, "sing": sing, "voice": PIPER_VOICE})
    except Exception as e:
        log.warning("Piper daemon failed (%s), one-shot fallback", e)
        global _piper_proc
        with _piper_lock:
            if _piper_proc is not None:
                try:
                    _piper_proc.kill()
                except OSError:
                    pass
                _piper_proc = None
        return _synthesize_piper_once(text, sing)


async def _stream_edge_mp3(text: str, sing: bool, *, sing_strong: bool = False) -> bytes:
    import edge_tts

    rate, pitch, volume = _sing_prosody(sing, strong=sing_strong)
    communicate = edge_tts.Communicate(text, EDGE_VOICE, rate=rate, pitch=pitch, volume=volume)
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


async def _synthesize_edge(text: str, sing: bool, *, sing_strong: bool = False, voice: str | None = None) -> bytes:
    """edge-tts en subproceso: timeout mata el proceso si se cuelga (Windows)."""
    edge_voice = voice or EDGE_VOICE

    def _edge_subprocess() -> bytes:
        payload = json.dumps(
            {"text": text, "sing": sing, "sing_strong": sing_strong, "voice": edge_voice},
            ensure_ascii=False,
        ).encode("utf-8")
        proc = subprocess.run(
            [sys.executable, str(EDGE_WORKER)],
            input=payload,
            capture_output=True,
            timeout=int(EDGE_TIMEOUT_S + 8),
            cwd=str(EDGE_WORKER.parent),
        )
        if proc.returncode != 0:
            err = proc.stderr.decode("utf-8", errors="replace").strip()
            raise RuntimeError(err or f"edge_worker exit {proc.returncode}")
        if not proc.stdout:
            raise RuntimeError("edge_worker returned empty audio")
        return proc.stdout

    return await asyncio.to_thread(_edge_subprocess)


def _edge_rate_from_speed_percent() -> str:
    pct = int(os.environ.get("TTS_SPEED_PERCENT", "5"))
    sign = "+" if pct >= 0 else ""
    return f"{sign}{pct}%"


def _ensure_wav_bytes(data: bytes) -> bytes:
    """edge-tts entrega MP3 aunque el archivo diga .wav — convertir a PCM WAV."""
    if data.startswith(b"RIFF"):
        return data
    import miniaudio

    guide_sr = int(os.environ.get("TTS_GUIDE_SAMPLE_RATE", "48000"))
    decoded = miniaudio.decode(
        data,
        output_format=miniaudio.SampleFormat.SIGNED16,
        nchannels=1,
        sample_rate=guide_sr,
    )
    samples = np.asarray(decoded.samples, dtype=np.int16)
    if decoded.nchannels > 1:
        samples = samples.reshape(-1, decoded.nchannels)[:, 0]
    out = io.BytesIO()
    with wave.open(out, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(decoded.sample_rate)
        wf.writeframes(samples.tobytes())
    wav = out.getvalue()
    if not wav.startswith(b"RIFF"):
        raise RuntimeError("no se pudo decodificar guia edge a WAV")
    return wav


async def _synthesize_edge_rvc_guide(text: str) -> bytes:
    """Guia TTS para RVC: edge-tts (gratis, sin API key) → WAV PCM."""
    import edge_tts

    text = _normalize_tts_text(text)
    if not text.strip():
        text = "Ok."
    rate = _edge_rate_from_speed_percent()
    communicate = edge_tts.Communicate(text, TTS_RVC_EDGE_VOICE, rate=rate)

    async def _collect_mp3() -> bytes:
        mp3 = bytearray()
        async for chunk in communicate.stream():
            if chunk["type"] == "audio":
                mp3.extend(chunk["data"])
        return bytes(mp3)

    mp3 = await asyncio.wait_for(_collect_mp3(), timeout=TTS_RVC_EDGE_TIMEOUT_S)
    if not mp3:
        raise RuntimeError("edge-tts returned no audio")
    return _ensure_wav_bytes(mp3)


def synthesize_rvc_guide_wav(text: str, sing: bool = False) -> bytes:
    """Guia para RVC: edge (neural) o sapi si TTS_RVC_GUIDE=sapi."""
    guide = os.environ.get("TTS_RVC_GUIDE", "edge").lower()
    if guide == "edge":
        return asyncio.run(_synthesize_edge_rvc_guide(text))
    return _synthesize_sapi(text, sing)


async def synthesize_wav_16k(text: str, sing: bool = False, *, sing_strong: bool = False) -> bytes:
    text = _normalize_tts_text(text)
    if not text.strip():
        text = "Ok."

    async def _run() -> bytes:
        engine = srv_cfg.get_tts_engine(TTS_ENGINE)
        edge_voice = srv_cfg.get_edge_voice(EDGE_VOICE)
        if engine == "edge":
            try:
                wav = await _synthesize_edge(text, sing, sing_strong=sing_strong, voice=edge_voice)
                log.info("TTS engine: edge (%s), %d bytes", edge_voice, len(wav))
                return wav
            except Exception as e:
                log.warning("edge-tts failed (%s), falling back to sapi", e)

        if engine == "piper":
            try:
                wav = await asyncio.wait_for(
                    asyncio.to_thread(_synthesize_piper, text, sing),
                    timeout=30,
                )
                log.info("TTS engine: piper (%s), %d bytes", PIPER_VOICE, len(wav))
                return wav
            except Exception as e:
                log.warning("piper failed/timeout (%s), falling back to sapi", e)
                shutdown_piper_daemon()

        wav = await asyncio.to_thread(_synthesize_sapi, text, sing)
        log.info("TTS engine: sapi, %d bytes", len(wav))
        return wav

    try:
        return await asyncio.wait_for(_run(), timeout=TTS_TOTAL_TIMEOUT_S)
    except asyncio.TimeoutError:
        log.warning("TTS total timeout %.0fs — respuesta corta por sapi", TTS_TOTAL_TIMEOUT_S)
        short = "Perdon, tarde mucho. Repetilo?"
        return _synthesize_sapi(short, False)


def warm_piper_daemon() -> None:
    if TTS_ENGINE != "piper":
        return
    try:
        _synthesize_piper("Hola", sing=False)
        log.info("Piper warm-up done")
    except Exception as e:
        log.warning("Piper warm-up failed: %s", e)


def shutdown_piper_daemon() -> None:
    global _piper_proc
    with _piper_lock:
        if _piper_proc is not None:
            try:
                _piper_proc.kill()
            except OSError:
                pass
            _piper_proc = None
