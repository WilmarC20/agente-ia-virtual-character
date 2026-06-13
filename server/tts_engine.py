"""TTS backends: sapi (Windows, rápido), edge-tts (neural), piper (lento en este PC)."""

from __future__ import annotations

import asyncio
import io
import json
import logging
import os
import struct
import subprocess
import sys
import tempfile
import threading
import wave
from pathlib import Path

import httpx
import numpy as np

log = logging.getLogger("brain.tts")

# edge = voz neural (~2s, requiere internet) con fallback rápido a sapi.
# sapi = Windows SAPI (~1-3s, offline). piper tarda 20+ min cargando onnx en este PC.
TTS_ENGINE = os.environ.get("TTS_ENGINE", "edge").lower()
EDGE_VOICE = os.environ.get("EDGE_TTS_VOICE", "es-MX-DaliaNeural")
# Timeout corto: si edge se cuelga, caemos a sapi rápido (evita -11 en la placa).
EDGE_TIMEOUT_S = float(os.environ.get("EDGE_TTS_TIMEOUT_S", "8"))
PIPER_VOICE = os.environ.get("TTS_VOICE", "es_MX-claude-high")
TTS_SAMPLE_RATE = int(os.environ.get("TTS_SAMPLE_RATE", "16000"))
TTS_PCM_GAIN = float(os.environ.get("TTS_PCM_GAIN", "0.55"))
VOICES_DIR = Path(__file__).parent / "voices"
PIPER_WORKER = Path(__file__).parent / "piper_worker.py"
PIPER_TIMEOUT_S = float(os.environ.get("PIPER_TIMEOUT_S", "1800"))

_piper_proc: subprocess.Popen | None = None
_piper_lock = threading.Lock()


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


def _normalize_tts_text(text: str) -> str:
    return " ".join(text.replace("\n", " ").replace("\r", " ").split())


def _sing_prosody(sing: bool) -> tuple[str, str, str]:
    if sing:
        return "-22%", "+18Hz", "-8%"
    return "+0%", "+0Hz", "+0%"


def _synthesize_sapi(text: str, sing: bool) -> bytes:
    """Windows SAPI en subproceso PowerShell (evita cuelgues COM en uvicorn)."""
    text = _normalize_tts_text(text).replace("'", "''")
    rate = 2 if sing else 0

    with tempfile.TemporaryDirectory() as td:
        wav_path = Path(td) / "tts_out.wav"
        ps = f"""
Add-Type -AssemblyName System.Speech
$s = New-Object System.Speech.Synthesis.SpeechSynthesizer
foreach ($v in $s.GetInstalledVoices()) {{
  if ($v.VoiceInfo.Culture.Name -like 'es*') {{ $s.SelectVoice($v.VoiceInfo.Name); break }}
}}
$s.Rate = {rate}
$s.SetOutputToWaveFile('{wav_path}')
$s.Speak('{text}')
$s.Dispose()
"""
        proc = subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", ps],
            capture_output=True,
            timeout=60,
        )
        if proc.returncode != 0:
            err = proc.stderr.decode("utf-8", errors="replace").strip()
            raise RuntimeError(err or f"PowerShell SAPI exit {proc.returncode}")
        if not wav_path.exists() or wav_path.stat().st_size < 44:
            raise RuntimeError("SAPI produced empty WAV")

        with wave.open(str(wav_path), "rb") as wf:
            pcm = np.frombuffer(wf.readframes(wf.getnframes()), dtype=np.int16)
            src_rate = wf.getframerate()
            channels = wf.getnchannels()
        if channels > 1:
            pcm = pcm.reshape(-1, channels)[:, 0]
        pcm = _resample_pcm(pcm, src_rate, TTS_SAMPLE_RATE)
        return _pcm_to_wav(pcm, TTS_SAMPLE_RATE)


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


async def _stream_edge_mp3(text: str, sing: bool) -> bytes:
    import edge_tts

    rate, pitch, volume = _sing_prosody(sing)
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


async def _synthesize_edge(text: str, sing: bool) -> bytes:
    mp3 = await asyncio.wait_for(_stream_edge_mp3(text, sing), timeout=EDGE_TIMEOUT_S)
    return await asyncio.to_thread(_decode_mp3_to_wav, mp3)


async def synthesize_wav_16k(text: str, sing: bool = False) -> bytes:
    text = _normalize_tts_text(text)

    if TTS_ENGINE == "edge":
        try:
            wav = await _synthesize_edge(text, sing)
            log.info("TTS engine: edge (%s), %d bytes", EDGE_VOICE, len(wav))
            return wav
        except Exception as e:
            log.warning("edge-tts failed (%s), falling back to sapi", e)

    if TTS_ENGINE == "piper":
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

    # default: sapi (rápido en Windows)
    wav = await asyncio.to_thread(_synthesize_sapi, text, sing)
    log.info("TTS engine: sapi, %d bytes", len(wav))
    return wav


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
