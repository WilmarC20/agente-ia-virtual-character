"""Singing pipeline: guide track (Bark / TTS) → RVC voice conversion → ESP32 PCM stream.

Environment (optional):
  RVC_MODEL_PATH      Path to character .pth
  RVC_INDEX_PATH      Path to feature .index (optional but recommended)
  RVC_DEVICE          cuda:0 (default)
  RVC_F0_METHOD       rmvpe | harvest | crepe | pm
  RVC_INDEX_RATE      0.75
  SING_GUIDE_ENGINE   bark | edge (default bark)
  BARK_HISTORY_PROMPT v2/es_speaker_6
  SING_STREAM_CHUNK   4096
"""

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
import time
import wave
from collections.abc import AsyncIterator, Iterator
from pathlib import Path
from typing import Any

import numpy as np

from applio_config import (
    APPLIO_DIR,
    APPLIO_PYTHON,
    APPLIO_RVC_WORKER,
    APPLIO_USE_DAEMON,
    RVC_INDEX_PATH,
    RVC_MODEL_PATH,
    RVC_WORKER_TIMEOUT_S,
    TTS_RVC_GUIDE,
    applio_available,
    build_convert_request,
    build_text_request,
    rvc_model_configured,
)
from tts_engine import TTS_PCM_GAIN, TTS_SAMPLE_RATE, _resample_pcm

log = logging.getLogger("brain.sing")

SERVER_DIR = Path(__file__).resolve().parent
RVC_WORKER = SERVER_DIR / "rvc_worker.py"
BARK_WORKER = SERVER_DIR / "bark_worker.py"
RVC_PYTHON = os.environ.get(
    "RVC_PYTHON",
    str(SERVER_DIR / ".venv-rvc" / "Scripts" / "python.exe"),
)
BARK_PYTHON = os.environ.get("BARK_PYTHON", RVC_PYTHON)
BARK_WORKER_TIMEOUT_S = float(os.environ.get("BARK_WORKER_TIMEOUT_S", "600"))
BARK_USE_DAEMON = os.environ.get("BARK_USE_DAEMON", "1") != "0"
RVC_USE_DAEMON = os.environ.get("RVC_USE_DAEMON", "1") != "0"
TTS_RVC_ENGINE = os.environ.get("TTS_RVC_ENGINE", "bender_http").lower()
BENDER_SERVER_URL = os.environ.get("BENDER_SERVER_URL", "http://localhost:7860")

STREAM_MAGIC = b"AGNT"
STREAM_VERSION = 1

RVC_DEVICE = os.environ.get("RVC_DEVICE", "cuda:0")
RVC_CPU_FALLBACK = os.environ.get("RVC_CPU_FALLBACK", "1") != "0"
RVC_F0_METHOD = os.environ.get("RVC_F0_METHOD", "rmvpe")
RVC_INDEX_RATE = float(os.environ.get("RVC_INDEX_RATE", "0.75"))
RVC_FILTER_RADIUS = int(os.environ.get("RVC_FILTER_RADIUS", "3"))
RVC_RMS_MIX_RATE = float(os.environ.get("RVC_RMS_MIX_RATE", "0.5"))
RVC_PROTECT = float(os.environ.get("RVC_PROTECT", "0.5"))
# 0 = sample rate nativo del modelo (~40 kHz), como RVC WebUI. El ESP32 baja a 16 kHz al final.
RVC_RESAMPLE_SR = int(os.environ.get("RVC_RESAMPLE_SR", "0"))
SING_GUIDE_SR = int(os.environ.get("SING_GUIDE_SR", "40000"))
SING_PCM_GAIN = float(os.environ.get("SING_PCM_GAIN", str(TTS_PCM_GAIN)))
SING_DEBUG_SAVE = os.environ.get("SING_DEBUG_SAVE", "1") != "0"
SING_SKIP_RVC = os.environ.get("SING_SKIP_RVC", "0") == "1"
SING_ALLOW_EDGE_GUIDE = os.environ.get("SING_ALLOW_EDGE_GUIDE", "0") == "1"

DEBUG_AUDIO_DIR = SERVER_DIR / "debug_audio"
SING_GUIDE_ENGINE = os.environ.get("SING_GUIDE_ENGINE", "auto").lower()
BARK_HISTORY_PROMPT = os.environ.get("BARK_HISTORY_PROMPT", "v2/es_speaker_6")
SING_STREAM_CHUNK = int(os.environ.get("SING_STREAM_CHUNK", "4096"))

_rvc_lock = threading.Lock()
_gpu_torch_lock = threading.Lock()
_rvc_engine: Any | None = None
_bark_ready = False
_bark_lock = threading.Lock()
_bark_proc: subprocess.Popen | None = None
_bark_daemon_lock = threading.Lock()
_bark_force_cpu = False
_rvc_proc: subprocess.Popen | None = None
_rvc_daemon_lock = threading.Lock()
_rvc_force_cpu = False
_applio_proc: subprocess.Popen | None = None
_applio_daemon_lock = threading.Lock()
_applio_request_lock = threading.Lock()


class DaemonReadTimeout(RuntimeError):
    """Timeout leyendo stdout del worker daemon (no confundir con asyncio.TimeoutError)."""


def _start_stderr_drainer(proc: subprocess.Popen, name: str) -> None:
    """Evita deadlock: Applio/torch llenan stderr y bloquean el proceso si no se drena."""

    def _drain() -> None:
        assert proc.stderr is not None
        for raw in proc.stderr:
            line = raw.decode("utf-8", errors="replace").rstrip()
            if line:
                log.debug("%s: %s", name, line)

    threading.Thread(target=_drain, name=f"stderr-{name}", daemon=True).start()


class SingingNotConfigured(RuntimeError):
    """Raised when RVC model paths are missing."""


class SingingDependencyError(RuntimeError):
    """Raised when bark / rvc-python is not installed."""


def singing_configured() -> bool:
    # bender_http no necesita RVC_MODEL_PATH local
    if TTS_RVC_ENGINE == "bender_http":
        return True
    return rvc_model_configured()


def _rvc_worker_available() -> bool:
    return Path(RVC_PYTHON).is_file() and RVC_WORKER.is_file()


def _rvc_worker_import_ok() -> bool:
    if not _rvc_worker_available():
        return False
    try:
        proc = subprocess.run(
            [RVC_PYTHON, "-c", "from rvc_python.infer import RVCInference"],
            capture_output=True,
            timeout=60,
        )
        return proc.returncode == 0
    except Exception:
        return False


def rvc_runtime_available() -> bool:
    """True if in-process rvc-python or the Python 3.11 worker can import RVC."""
    if _rvc_worker_import_ok():
        return True
    try:
        import rvc_python  # noqa: F401

        return True
    except ImportError:
        return False


def tts_rvc_runtime_available() -> bool:
    """True if TTS+RVC can run (Applio nativo, bender_http, o rvc_python)."""
    if TTS_RVC_ENGINE == "bender_http":
        return True  # Disponible si bender_server esta corriendo; se verifica al llamar
    if TTS_RVC_ENGINE == "applio" and applio_available():
        return True
    return rvc_runtime_available()


def _build_rvc_request(**extra: Any) -> dict[str, Any]:
    req: dict[str, Any] = {
        "model_path": RVC_MODEL_PATH,
        "index_path": RVC_INDEX_PATH,
        "device": RVC_DEVICE,
        "f0_method": RVC_F0_METHOD,
        "index_rate": RVC_INDEX_RATE,
        "filter_radius": RVC_FILTER_RADIUS,
        "rms_mix_rate": RVC_RMS_MIX_RATE,
        "protect": RVC_PROTECT,
        "resample_sr": RVC_RESAMPLE_SR,
    }
    req.update(extra)
    return req


def _rvc_worker_oneshot(
    req: dict[str, Any],
    *,
    timeout: float | None = None,
    expect_wav: bool = True,
) -> bytes | None:
    with _gpu_torch_lock:
        proc = subprocess.run(
            [RVC_PYTHON, str(RVC_WORKER)],
            input=json.dumps(req).encode("utf-8"),
            capture_output=True,
            timeout=timeout or RVC_WORKER_TIMEOUT_S,
        )
    if proc.returncode != 0:
        err = proc.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(err or f"rvc_worker exit {proc.returncode}")
    if not expect_wav:
        return None
    if not proc.stdout or not proc.stdout.startswith(b"RIFF"):
        raise RuntimeError("rvc_worker returned invalid audio")
    return proc.stdout


def _start_rvc_daemon() -> subprocess.Popen:
    log.info("Starting RVC daemon (device=%s)...", "cpu" if _rvc_force_cpu else RVC_DEVICE)
    proc = subprocess.Popen(
        [RVC_PYTHON, str(RVC_WORKER), "--daemon"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=str(SERVER_DIR),
        bufsize=0,
    )
    assert proc.stderr is not None
    line = proc.stderr.readline().decode("utf-8", errors="replace").strip()
    if line != "READY":
        err = proc.stderr.read().decode("utf-8", errors="replace")
        proc.kill()
        raise RuntimeError(f"RVC daemon failed to start: {line or err}")
    _start_stderr_drainer(proc, "rvc")
    return proc


def _get_rvc_daemon() -> subprocess.Popen:
    global _rvc_proc
    with _rvc_daemon_lock:
        if _rvc_proc is not None and _rvc_proc.poll() is None:
            return _rvc_proc
        _rvc_proc = _start_rvc_daemon()
        log.info("RVC daemon ready (persistent model, device=%s)", "cpu" if _rvc_force_cpu else RVC_DEVICE)
        return _rvc_proc


def _rvc_daemon_request(
    req: dict[str, Any],
    *,
    timeout: float | None = None,
    expect_wav: bool = True,
) -> bytes | None:
    timeout_s = timeout or RVC_WORKER_TIMEOUT_S
    with _gpu_torch_lock:
        proc = _get_rvc_daemon()
        payload = json.dumps(req).encode("utf-8")
        assert proc.stdin is not None and proc.stdout is not None
        proc.stdin.write(struct.pack(">I", len(payload)))
        proc.stdin.write(payload)
        proc.stdin.flush()
        header = _read_exact(proc.stdout, 4, timeout_s)
        (length,) = struct.unpack(">I", header)
        body = _read_exact(proc.stdout, length, timeout_s)
    if not expect_wav:
        return None
    if body[:1] == b"{" and b"error" in body[:80]:
        try:
            err = json.loads(body.decode("utf-8"))
            raise RuntimeError(err.get("error", body.decode("utf-8", errors="replace")))
        except json.JSONDecodeError:
            raise RuntimeError(body.decode("utf-8", errors="replace"))
    if not body.startswith(b"RIFF"):
        raise RuntimeError("rvc_worker daemon returned invalid audio")
    return body


def _rvc_worker_request(req: dict[str, Any], *, timeout: float | None = None) -> bytes:
    if RVC_USE_DAEMON:
        try:
            out = _rvc_daemon_request(req, timeout=timeout, expect_wav=True)
            assert out is not None
            return out
        except Exception as e:
            log.warning("RVC daemon failed (%s), one-shot fallback", e)
            global _rvc_proc
            with _rvc_daemon_lock:
                if _rvc_proc is not None:
                    try:
                        _rvc_proc.kill()
                    except OSError:
                        pass
                    _rvc_proc = None
    out = _rvc_worker_oneshot(req, timeout=timeout, expect_wav=True)
    assert out is not None
    return out


def _rvc_worker_warmup(req: dict[str, Any], *, timeout: float | None = None) -> None:
    if RVC_USE_DAEMON:
        _rvc_daemon_request(req, timeout=timeout, expect_wav=False)
        return
    _rvc_worker_oneshot(req, timeout=timeout, expect_wav=False)


def _is_cuda_rvc_error(exc: BaseException) -> bool:
    msg = str(exc).lower()
    needles = (
        "no kernel image",
        "sm_120",
        "not compatible with the current pytorch",
        "cuda error",
    )
    return any(n in msg for n in needles)


def _rvc_worker_request_auto(req: dict[str, Any], *, timeout: float | None = None) -> bytes:
    device = str(req.get("device", RVC_DEVICE))
    try:
        return _rvc_worker_request(req, timeout=timeout)
    except RuntimeError as e:
        if not RVC_CPU_FALLBACK or not device.startswith("cuda") or not _is_cuda_rvc_error(e):
            raise
        log.warning(
            "RVC CUDA failed on %s (RTX 50xx needs PyTorch cu128) — retrying on CPU",
            device,
        )
        global _rvc_force_cpu, _rvc_proc
        _rvc_force_cpu = True
        with _rvc_daemon_lock:
            if _rvc_proc is not None:
                try:
                    _rvc_proc.kill()
                except OSError:
                    pass
                _rvc_proc = None
        req_cpu = dict(req)
        req_cpu["device"] = "cpu"
        return _rvc_worker_request(req_cpu, timeout=timeout)


def _rvc_worker_warmup_auto(req: dict[str, Any], *, timeout: float | None = None) -> None:
    device = str(req.get("device", RVC_DEVICE))
    try:
        _rvc_worker_warmup(req, timeout=timeout)
    except RuntimeError as e:
        if not RVC_CPU_FALLBACK or not device.startswith("cuda") or not _is_cuda_rvc_error(e):
            raise
        log.warning("RVC warm-up CUDA failed — retrying on CPU")
        global _rvc_force_cpu, _rvc_proc
        _rvc_force_cpu = True
        with _rvc_daemon_lock:
            if _rvc_proc is not None:
                try:
                    _rvc_proc.kill()
                except OSError:
                    pass
                _rvc_proc = None
        req_cpu = dict(req)
        req_cpu["device"] = "cpu"
        _rvc_worker_warmup(req_cpu, timeout=timeout)


def _write_temp_wav_bytes(wav_bytes: bytes) -> str:
    fd, path = tempfile.mkstemp(suffix=".wav", prefix="agent_tts_guide_")
    os.close(fd)
    Path(path).write_bytes(wav_bytes)
    return path


def _make_silence_wav_path(*, duration_s: float = 0.35, sample_rate: int = 48000) -> str:
    fd, path = tempfile.mkstemp(suffix=".wav", prefix="agent_rvc_warm_")
    os.close(fd)
    frames = b"\x00\x00" * int(duration_s * sample_rate)
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(frames)
    return path


def _applio_rvc_oneshot(req: dict[str, Any], *, timeout: float | None = None) -> bytes:
    with _applio_request_lock:
        proc = subprocess.run(
            [APPLIO_PYTHON, str(APPLIO_RVC_WORKER)],
            input=json.dumps(req).encode("utf-8"),
            capture_output=True,
            timeout=timeout or RVC_WORKER_TIMEOUT_S,
            cwd=str(SERVER_DIR),
            env={**os.environ, "SERVER_PYTHON": sys.executable},
        )
    if proc.returncode != 0:
        err = proc.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(err or f"applio_rvc_worker exit {proc.returncode}")
    if not proc.stdout or not proc.stdout.startswith(b"RIFF"):
        raise RuntimeError("applio_rvc_worker returned invalid audio")
    return proc.stdout


def _start_applio_daemon() -> subprocess.Popen:
    log.info("Starting Applio RVC daemon...")
    proc = subprocess.Popen(
        [APPLIO_PYTHON, str(APPLIO_RVC_WORKER), "--daemon"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=str(SERVER_DIR),
        bufsize=0,
        env={**os.environ, "SERVER_PYTHON": sys.executable},
    )
    assert proc.stderr is not None
    # Leer lineas hasta READY: el daemon imprime logs de carga del modelo antes
    while True:
        raw = proc.stderr.readline()
        if not raw:  # pipe cerrada = proceso muerto
            proc.kill()
            raise RuntimeError("Applio RVC daemon murio durante startup")
        line = raw.decode("utf-8", errors="replace").strip()
        log.debug("applio daemon: %s", line)
        if line == "READY":
            break
        if line.startswith("ERROR") or line.startswith("Traceback"):
            rest = proc.stderr.read().decode("utf-8", errors="replace")
            proc.kill()
            raise RuntimeError(f"Applio RVC daemon failed to start: {line}\\n{rest}")
    _start_stderr_drainer(proc, "applio-rvc")
    return proc


def _get_applio_daemon() -> subprocess.Popen:
    global _applio_proc
    with _applio_daemon_lock:
        if _applio_proc is not None and _applio_proc.poll() is None:
            return _applio_proc
        _applio_proc = _start_applio_daemon()
        log.info("Applio RVC daemon ready (VoiceConverter local)")
        return _applio_proc


def _kill_applio_daemon() -> None:
    global _applio_proc
    with _applio_daemon_lock:
        if _applio_proc is not None:
            try:
                _applio_proc.kill()
            except OSError:
                pass
            _applio_proc = None


def _applio_daemon_request(
    req: dict[str, Any],
    *,
    timeout: float | None = None,
    expect_wav: bool = True,
) -> bytes | None:
    timeout_s = timeout or RVC_WORKER_TIMEOUT_S
    t0 = time.monotonic()
    try:
        with _applio_request_lock:
            proc = _get_applio_daemon()
            payload = json.dumps(req).encode("utf-8")
            assert proc.stdin is not None and proc.stdout is not None
            proc.stdin.write(struct.pack(">I", len(payload)))
            proc.stdin.write(payload)
            proc.stdin.flush()
            header = _read_exact(proc.stdout, 4, timeout_s)
            (length,) = struct.unpack(">I", header)
            body = _read_exact(proc.stdout, length, timeout_s)
    except DaemonReadTimeout:
        log.warning("Applio daemon timeout (%.0fs) — reiniciando worker", timeout_s)
        _kill_applio_daemon()
        raise
    elapsed = time.monotonic() - t0
    if req.get("warmup"):
        log.info("Applio RVC warmup %.1fs", elapsed)
    elif expect_wav:
        kind = "text+RVC" if req.get("text") else "convert"
        log.info("Applio %s %.1fs (%d bytes wav)", kind, elapsed, len(body))
    if not expect_wav:
        return None
    if body[:1] == b"{" and b"error" in body[:80]:
        try:
            err = json.loads(body.decode("utf-8"))
            raise RuntimeError(err.get("error", body.decode("utf-8", errors="replace")))
        except json.JSONDecodeError:
            raise RuntimeError(body.decode("utf-8", errors="replace"))
    if not body.startswith(b"RIFF"):
        raise RuntimeError("applio_rvc_worker daemon returned invalid audio")
    return body


def _applio_rvc_request(req: dict[str, Any], *, timeout: float | None = None) -> bytes:
    if APPLIO_USE_DAEMON:
        try:
            out = _applio_daemon_request(req, timeout=timeout, expect_wav=True)
            assert out is not None
            return out
        except Exception as e:
            log.warning("Applio RVC daemon failed (%s), one-shot fallback", e)
            global _applio_proc
            with _applio_daemon_lock:
                if _applio_proc is not None:
                    try:
                        _applio_proc.kill()
                    except OSError:
                        pass
                    _applio_proc = None
    return _applio_rvc_oneshot(req, timeout=timeout)


def _applio_rvc_warmup(req: dict[str, Any], *, timeout: float | None = None) -> None:
    if APPLIO_USE_DAEMON:
        _applio_daemon_request(req, timeout=timeout, expect_wav=False)
        return
    proc = subprocess.run(
        [APPLIO_PYTHON, str(APPLIO_RVC_WORKER)],
        input=json.dumps(req).encode("utf-8"),
        capture_output=True,
        timeout=timeout or RVC_WORKER_TIMEOUT_S,
        cwd=str(SERVER_DIR),
    )
    if proc.returncode != 0:
        err = proc.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(err or f"applio_rvc_worker warmup exit {proc.returncode}")


def _normalize_lyrics(text: str) -> str:
    return " ".join(text.replace("\r", " ").replace("\n", " ").split())


def _lyrics_for_guide(text: str) -> str:
    """Keep line breaks for singing rhythm (Bark / RVC guide)."""
    lines = [ln.strip() for ln in text.replace("\r", "").split("\n") if ln.strip()]
    return "\n".join(lines) if lines else text.strip()


def _default_bark_cpu_only() -> str:
    if "BARK_CPU_ONLY" in os.environ:
        return os.environ["BARK_CPU_ONLY"]
    return "0"


def _bark_worker_env(*, force_cpu: bool = False) -> dict[str, str]:
    cache_root = SERVER_DIR / ".cache-suno"
    hf_home = SERVER_DIR / ".hf-cache"
    cache_root.mkdir(parents=True, exist_ok=True)
    hf_home.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.setdefault("XDG_CACHE_HOME", str(cache_root))
    env.setdefault("HF_HOME", str(hf_home))
    env.setdefault("HUGGINGFACE_HUB_CACHE", str(hf_home / "hub"))
    env.setdefault("SUNO_USE_SMALL_MODELS", os.environ.get("SUNO_USE_SMALL_MODELS", "True"))
    if force_cpu:
        env["BARK_CPU_ONLY"] = "1"
        env["SUNO_OFFLOAD_CPU"] = "True"
    else:
        env.setdefault("BARK_CPU_ONLY", _default_bark_cpu_only())
        if env.get("BARK_CPU_ONLY", "0") != "0":
            env.setdefault("SUNO_OFFLOAD_CPU", "True")
    env.setdefault("KMP_DUPLICATE_LIB_OK", "TRUE")
    return env


def _read_exact(stream, nbytes: int, timeout_s: float) -> bytes:
    deadline = time.monotonic() + timeout_s
    chunks: list[bytes] = []
    got = 0
    while got < nbytes:
        if time.monotonic() > deadline:
            raise DaemonReadTimeout(f"daemon read timeout ({timeout_s}s)")
        chunk = stream.read(nbytes - got)
        if not chunk:
            raise RuntimeError("daemon closed stdout")
        chunks.append(chunk)
        got += len(chunk)
    return b"".join(chunks)


def _start_bark_daemon(*, force_cpu: bool = False) -> subprocess.Popen:
    use_gpu = not force_cpu and _bark_worker_env(force_cpu=False).get("BARK_CPU_ONLY", "0") == "0"
    log.info("Starting Bark daemon (gpu=%s)...", use_gpu)
    proc = subprocess.Popen(
        [BARK_PYTHON, str(BARK_WORKER), "--daemon"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=_bark_worker_env(force_cpu=force_cpu),
        cwd=str(SERVER_DIR),
        bufsize=0,
    )
    assert proc.stderr is not None
    line = proc.stderr.readline().decode("utf-8", errors="replace").strip()
    if line != "READY":
        err = proc.stderr.read().decode("utf-8", errors="replace")
        proc.kill()
        raise RuntimeError(f"Bark daemon failed to start: {line or err}")
    _start_stderr_drainer(proc, "bark")
    return proc


def _get_bark_daemon() -> subprocess.Popen:
    global _bark_proc, _bark_force_cpu
    with _bark_daemon_lock:
        if _bark_proc is not None and _bark_proc.poll() is None:
            return _bark_proc
        attempts: list[tuple[bool, str]] = (
            [(False, "gpu"), (True, "cpu")] if not _bark_force_cpu else [(True, "cpu")]
        )
        err: BaseException | None = None
        for force_cpu, label in attempts:
            try:
                _bark_proc = _start_bark_daemon(force_cpu=force_cpu)
                if force_cpu:
                    _bark_force_cpu = True
                    log.warning(
                        "Bark daemon en CPU (fallback) — mas lento; fija BARK_CPU_ONLY=1 si persiste"
                    )
                else:
                    log.info("Bark daemon ready (gpu)")
                return _bark_proc
            except RuntimeError as e:
                err = e
                log.warning("Bark daemon start failed (%s): %s", label, e)
        raise RuntimeError(str(err or "Bark daemon failed to start"))


def _bark_daemon_request(req: dict[str, Any], *, timeout: float | None = None) -> bytes:
    timeout_s = timeout or BARK_WORKER_TIMEOUT_S
    with _gpu_torch_lock:
        proc = _get_bark_daemon()
        payload = json.dumps(req).encode("utf-8")
        assert proc.stdin is not None and proc.stdout is not None
        proc.stdin.write(struct.pack(">I", len(payload)))
        proc.stdin.write(payload)
        proc.stdin.flush()
        header = _read_exact(proc.stdout, 4, timeout_s)
        (length,) = struct.unpack(">I", header)
        body = _read_exact(proc.stdout, length, timeout_s)
    if body[:1] == b"{" and b"error" in body[:80]:
        try:
            raise RuntimeError(json.loads(body.decode("utf-8"))["error"])
        except (json.JSONDecodeError, KeyError):
            pass
    if req.get("warmup"):
        return body
    if not body.startswith(b"RIFF"):
        raise RuntimeError(f"bark daemon returned invalid audio ({len(body)} bytes)")
    return body


def _bark_worker_oneshot(req: dict[str, Any], *, timeout: float | None = None) -> bytes:
    with _gpu_torch_lock:
        proc = subprocess.run(
            [BARK_PYTHON, str(BARK_WORKER)],
            input=json.dumps(req).encode("utf-8"),
            capture_output=True,
            timeout=timeout or BARK_WORKER_TIMEOUT_S,
            env=_bark_worker_env(force_cpu=_bark_force_cpu),
        )
    if proc.returncode != 0:
        err = proc.stderr.decode("utf-8", errors="replace").strip()
        raise RuntimeError(err or f"bark_worker exit {proc.returncode}")
    if req.get("warmup"):
        return b""
    if not proc.stdout or not proc.stdout.startswith(b"RIFF"):
        raise RuntimeError("bark_worker returned invalid audio")
    return proc.stdout


def _bark_worker_request(req: dict[str, Any], *, timeout: float | None = None) -> bytes:
    if BARK_USE_DAEMON:
        try:
            return _bark_daemon_request(req, timeout=timeout)
        except Exception as e:
            log.warning("Bark daemon failed (%s), one-shot fallback", e)
            global _bark_proc
            with _bark_daemon_lock:
                if _bark_proc is not None:
                    try:
                        _bark_proc.kill()
                    except OSError:
                        pass
                    _bark_proc = None
    return _bark_worker_oneshot(req, timeout=timeout)


def _bark_worker_available() -> bool:
    if not Path(BARK_PYTHON).is_file() or not BARK_WORKER.is_file():
        return False
    try:
        proc = subprocess.run(
            [BARK_PYTHON, "-c", "from bark import generate_audio"],
            capture_output=True,
            timeout=90,
            env=_bark_worker_env(),
        )
        return proc.returncode == 0
    except Exception:
        return False


def bark_runtime_available() -> bool:
    return _bark_worker_available()


def _float_to_int16(audio: np.ndarray) -> np.ndarray:
    if audio.dtype == np.int16:
        return audio
    clipped = np.clip(audio.astype(np.float32), -1.0, 1.0)
    return (clipped * 32767.0).astype(np.int16)


def _wav_bytes_from_pcm(pcm: np.ndarray, sample_rate: int, *, gain: float | None = None) -> bytes:
    pcm_i16 = _float_to_int16(pcm)
    g = SING_PCM_GAIN if gain is None else gain
    pcm_i16 = np.clip(pcm_i16.astype(np.float32) * g, -32768, 32767).astype(np.int16)
    out = io.BytesIO()
    with wave.open(out, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm_i16.tobytes())
    return out.getvalue()


def _pcm_from_wav_bytes(wav_bytes: bytes) -> tuple[np.ndarray, int]:
    with wave.open(io.BytesIO(wav_bytes), "rb") as wf:
        channels = wf.getnchannels()
        sr = wf.getframerate()
        pcm = np.frombuffer(wf.readframes(wf.getnframes()), dtype=np.int16)
    if channels > 1:
        pcm = pcm.reshape(-1, channels)[:, 0]
    return pcm, sr


def _to_esp32_pcm(pcm: np.ndarray, sample_rate: int) -> tuple[np.ndarray, int]:
    """Mono int16 @ 16 kHz for ES8311 / I2S."""
    if pcm.dtype == np.int16:
        audio_f = pcm.astype(np.float32) / 32768.0
    else:
        audio_f = np.clip(pcm.astype(np.float32), -1.0, 1.0)
    if sample_rate != TTS_SAMPLE_RATE:
        n_out = max(1, int(len(audio_f) * TTS_SAMPLE_RATE / sample_rate))
        x_out = np.linspace(0, len(audio_f) - 1, n_out)
        audio_f = np.interp(x_out, np.arange(len(audio_f)), audio_f)
    pcm_i16 = np.clip(audio_f * 32767.0, -32768, 32767).astype(np.int16)
    return pcm_i16, TTS_SAMPLE_RATE


def _save_debug_wav(name: str, pcm: np.ndarray, sample_rate: int) -> None:
    if not SING_DEBUG_SAVE:
        return
    try:
        DEBUG_AUDIO_DIR.mkdir(parents=True, exist_ok=True)
        path = DEBUG_AUDIO_DIR / name
        if pcm.dtype != np.int16:
            pcm = _float_to_int16(pcm)
        with wave.open(str(path), "wb") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(sample_rate)
            wf.writeframes(pcm.tobytes())
        log.info("debug audio -> %s", path)
    except Exception as e:
        log.warning("debug audio save failed: %s", e)


def build_stream_header(metadata: dict[str, Any]) -> bytes:
    """Length-prefixed JSON header: MAGIC + version + json_len + json."""
    payload = json.dumps(metadata, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    return STREAM_MAGIC + struct.pack(">BHI", STREAM_VERSION, 0, len(payload)) + payload


def _ensure_bark() -> None:
    global _bark_ready
    with _bark_lock:
        if _bark_ready:
            return
        try:
            from bark import preload_models  # type: ignore[import-untyped]
        except ImportError as e:
            raise SingingDependencyError(
                "Bark no instalado. pip install git+https://github.com/suno-ai/bark.git"
            ) from e
        log.info("Bark: preloading models (first run may take a while)...")
        preload_models()
        _bark_ready = True
        log.info("Bark: models ready")


def _generate_guide_bark(lyrics: str) -> tuple[np.ndarray, int]:
    """Sung guide via Bark subprocess (.venv-rvc)."""
    wav_bytes = _bark_worker_request(
        {
            "lyrics": lyrics,
            "history_prompt": BARK_HISTORY_PROMPT,
        }
    )
    return _read_wav_bytes(wav_bytes)


def _generate_guide_bark_legacy(lyrics: str) -> tuple[np.ndarray, int]:
    _ensure_bark()
    from bark import SAMPLE_RATE, generate_audio  # type: ignore[import-untyped]

    prompt = f"♪ [music] [singing] {lyrics} ♪"
    log.info("Bark guide track (in-process): %r", prompt[:120])
    audio = generate_audio(prompt, history_prompt=BARK_HISTORY_PROMPT)
    audio = np.asarray(audio, dtype=np.float32)
    if audio.ndim > 1:
        audio = audio.mean(axis=1)
    return audio, int(SAMPLE_RATE)


async def _generate_guide_edge(lyrics: str) -> tuple[np.ndarray, int]:
    from tts_engine import synthesize_wav_16k

    log.warning(
        "Guia edge-TTS (hablada) — RVC solo cambia timbre, NO crea melodía. "
        "Ejecuta .\\install-bark.ps1 para canto real."
    )
    wav = await synthesize_wav_16k(lyrics, sing=True, sing_strong=True)
    pcm, sr = _pcm_from_wav_bytes(wav)
    guide_sr = SING_GUIDE_SR
    pcm_f = _resample_pcm(pcm, sr, guide_sr).astype(np.float32) / 32768.0
    return pcm_f, guide_sr


def _want_bark_guide() -> bool:
    if SING_GUIDE_ENGINE == "edge":
        return False
    if SING_GUIDE_ENGINE == "bark":
        return True
    return SING_GUIDE_ENGINE == "auto"


def _try_bark_guide(lyrics: str) -> tuple[np.ndarray, int]:
    if _bark_worker_available():
        log.info("Bark guide (worker %s)", BARK_PYTHON)
        return _generate_guide_bark(lyrics)
    if SING_GUIDE_ENGINE == "bark":
        return _generate_guide_bark_legacy(lyrics)
    raise SingingDependencyError("Bark no instalado — ejecuta server/install-bark.ps1")


def generate_guide_track(lyrics: str) -> tuple[np.ndarray, int]:
    """Synthesise a sung guide vocal (intonation / rhythm) for RVC."""
    lyrics = _normalize_lyrics(lyrics)
    if not lyrics:
        raise ValueError("empty lyrics")

    if SING_GUIDE_ENGINE == "bark":
        try:
            return _generate_guide_bark(lyrics)
        except SingingDependencyError:
            log.warning("Bark unavailable, falling back to edge/sapi guide")
        except Exception as e:
            log.warning("Bark failed (%s), falling back to edge/sapi guide", e)

    # edge path must run in async context — sync wrapper uses asyncio.run only if needed
    try:
        loop = asyncio.get_running_loop()
    except RuntimeError:
        return asyncio.run(_generate_guide_edge(lyrics))

    raise RuntimeError("generate_guide_track sync call inside running loop — use generate_guide_track_async")


async def generate_guide_track_async(lyrics: str) -> tuple[np.ndarray, int]:
    lyrics = _lyrics_for_guide(lyrics)
    if not lyrics:
        raise ValueError("empty lyrics")

    if _want_bark_guide():
        try:
            return await asyncio.to_thread(_try_bark_guide, lyrics)
        except SingingDependencyError:
            if not SING_ALLOW_EDGE_GUIDE:
                raise SingingDependencyError(
                    "Bark no instalado — canto requiere guía melodica. "
                    "Ejecuta: cd server && .\\install-bark.ps1 (tarda ~10 min la 1ª vez)"
                )
            log.warning("Bark unavailable, falling back to edge/sapi guide (SING_ALLOW_EDGE_GUIDE=1)")
        except Exception as e:
            if not SING_ALLOW_EDGE_GUIDE:
                raise SingingDependencyError(f"Bark fallo: {e}") from e
            log.warning("Bark failed (%s), falling back to edge/sapi guide", e)

    return await _generate_guide_edge(lyrics)


def _get_rvc_engine() -> Any:
    global _rvc_engine
    if not singing_configured():
        raise SingingNotConfigured(
            f"RVC_MODEL_PATH missing or not found ({RVC_MODEL_PATH!r}). "
            "Set RVC_MODEL_PATH and optionally RVC_INDEX_PATH."
        )
    with _rvc_lock:
        if _rvc_engine is not None:
            return _rvc_engine
        try:
            from rvc_python.infer import RVCInference  # type: ignore[import-untyped]
        except ImportError as e:
            if _rvc_worker_available():
                raise SingingDependencyError(
                    "rvc-python no esta en el venv principal; se usara .venv-rvc"
                ) from e
            raise SingingDependencyError(
                "RVC no disponible. Ejecuta: .\\install-rvc.ps1  (requiere Python 3.11)"
            ) from e

        log.info("RVC: loading model %s (device=%s)", RVC_MODEL_PATH, RVC_DEVICE)
        engine = RVCInference(device=RVC_DEVICE)
        index_path = RVC_INDEX_PATH if RVC_INDEX_PATH and Path(RVC_INDEX_PATH).is_file() else None
        try:
            if index_path:
                engine.load_model(RVC_MODEL_PATH, index_path=index_path)
            else:
                engine.load_model(RVC_MODEL_PATH)
        except TypeError:
            # Older rvc-python: index via set_params only
            engine.load_model(RVC_MODEL_PATH)

        if hasattr(engine, "set_params"):
            engine.set_params(
                f0method=RVC_F0_METHOD,
                f0up_key=0,
                index_rate=RVC_INDEX_RATE,
                filter_radius=RVC_FILTER_RADIUS,
                resample_sr=RVC_RESAMPLE_SR,
                rms_mix_rate=RVC_RMS_MIX_RATE,
                protect=RVC_PROTECT,
            )
        _rvc_engine = engine
        log.info("RVC: model ready")
        return _rvc_engine


def _write_temp_wav(pcm: np.ndarray, sample_rate: int) -> str:
    fd, path = tempfile.mkstemp(suffix=".wav", prefix="agent_sing_")
    os.close(fd)
    if pcm.dtype != np.int16:
        clipped = np.clip(pcm.astype(np.float32), -1.0, 1.0)
        pcm_i16 = (clipped * 32767.0).astype(np.int16)
    else:
        pcm_i16 = pcm
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm_i16.tobytes())
    return path


def _read_wav_bytes(wav_bytes: bytes) -> tuple[np.ndarray, int]:
    with wave.open(io.BytesIO(wav_bytes), "rb") as wf:
        channels = wf.getnchannels()
        sr = wf.getframerate()
        pcm = np.frombuffer(wf.readframes(wf.getnframes()), dtype=np.int16)
    if channels > 1:
        pcm = pcm.reshape(-1, channels)[:, 0]
    return pcm.astype(np.float32) / 32768.0, sr


def _read_wav_file(path: str) -> tuple[np.ndarray, int]:
    with wave.open(path, "rb") as wf:
        channels = wf.getnchannels()
        sr = wf.getframerate()
        pcm = np.frombuffer(wf.readframes(wf.getnframes()), dtype=np.int16)
    if channels > 1:
        pcm = pcm.reshape(-1, channels)[:, 0]
    return pcm.astype(np.float32) / 32768.0, sr


def rvc_convert(
    audio: np.ndarray,
    sample_rate: int,
    *,
    f0_up_key: int = 0,
    index_rate: float | None = None,
    protect: float | None = None,
) -> tuple[np.ndarray, int]:
    """Run RVC voice conversion on a guide vocal (in-memory boundary uses short temp files)."""
    if not singing_configured():
        raise SingingNotConfigured(
            f"RVC_MODEL_PATH missing or not found ({RVC_MODEL_PATH!r}). "
            "Set RVC_MODEL_PATH and optionally RVC_INDEX_PATH."
        )

    idx_rate = float(index_rate if index_rate is not None else RVC_INDEX_RATE)
    prot = float(protect if protect is not None else RVC_PROTECT)
    in_path = None
    try:
        in_path = _write_temp_wav(audio, sample_rate)

        if _rvc_worker_available():
            wav_bytes = _rvc_worker_request_auto(
                _build_rvc_request(
                    input_wav=in_path,
                    f0_up_key=int(f0_up_key),
                    index_rate=idx_rate,
                    protect=prot,
                )
            )
            return _read_wav_bytes(wav_bytes)

        engine = _get_rvc_engine()
        fd, out_path = tempfile.mkstemp(suffix=".wav", prefix="agent_sing_rvc_")
        os.close(fd)
        try:
            if hasattr(engine, "set_params"):
                params: dict[str, Any] = {"f0up_key": int(f0_up_key), "f0_up_key": int(f0_up_key)}
                params["index_rate"] = idx_rate
                try:
                    engine.set_params(**params)
                except TypeError:
                    engine.set_params(f0up_key=int(f0_up_key), index_rate=idx_rate)

            infer_kwargs: dict[str, Any] = {}
            if not hasattr(engine, "set_params"):
                infer_kwargs["index_rate"] = idx_rate

            try:
                engine.infer_file(in_path, out_path, f0_up_key=int(f0_up_key), **infer_kwargs)
            except TypeError:
                engine.infer_file(in_path, out_path)

            return _read_wav_file(out_path)
        finally:
            if os.path.exists(out_path):
                try:
                    os.unlink(out_path)
                except OSError:
                    pass
    finally:
        if in_path and os.path.exists(in_path):
            try:
                os.unlink(in_path)
            except OSError:
                pass


async def render_singing_wav(
    lyrics: str,
    *,
    f0_up_key: int = 0,
    index_rate: float | None = None,
    skip_rvc: bool | None = None,
) -> bytes:
    """Full pipeline → WAV bytes (16 kHz mono PCM) ready for ESP32."""
    use_skip_rvc = SING_SKIP_RVC if skip_rvc is None else skip_rvc
    guide, guide_sr = await generate_guide_track_async(lyrics)
    log.info("Guide track: %.1fs @ %d Hz", len(guide) / guide_sr, guide_sr)
    _save_debug_wav("last_sing_guide.wav", guide, guide_sr)

    if use_skip_rvc:
        log.warning("SING_SKIP_RVC — enviando guia sin RVC (prueba calidad)")
        pcm, out_sr = _to_esp32_pcm(guide, guide_sr)
        wav_out = _wav_bytes_from_pcm(pcm, out_sr)
        _save_debug_wav("last_sing_esp32.wav", pcm, out_sr)
        return wav_out

    log.info("RVC converting guide (f0_up_key=%d resample_sr=%d)...", f0_up_key, RVC_RESAMPLE_SR)

    converted, conv_sr = await asyncio.to_thread(
        rvc_convert,
        guide,
        guide_sr,
        f0_up_key=f0_up_key,
        index_rate=index_rate if index_rate is not None else RVC_INDEX_RATE,
    )

    # RVC preserves duration. Some rvc-python builds resample the DATA to resample_sr
    # but stamp the WAV header with the model's NATIVE rate — so the declared rate lies
    # (e.g. 16 kHz audio labelled 40 kHz). If we trust that header, _to_esp32_pcm
    # resamples again and crushes the song ~2.5x into chipmunk noise. Trust the guide
    # duration instead: derive the true rate from the actual sample count and snap to kHz.
    guide_dur = len(guide) / guide_sr if guide_sr else 0.0
    if guide_dur > 0 and len(converted) > 0 and conv_sr > 0:
        true_sr = len(converted) / guide_dur
        if abs(true_sr - conv_sr) / conv_sr > 0.1:
            fixed_sr = int(round(true_sr / 1000.0)) * 1000
            log.warning(
                "RVC header says %d Hz but %d samples over %.2fs => %d Hz; correcting "
                "(WAV header was mislabeled)",
                conv_sr, len(converted), guide_dur, fixed_sr,
            )
            conv_sr = fixed_sr

    log.info(
        "RVC output: %.1fs @ %d Hz (f0_up_key=%d)",
        len(converted) / conv_sr,
        conv_sr,
        f0_up_key,
    )
    _save_debug_wav("last_sing_rvc.wav", converted, conv_sr)

    pcm, out_sr = _to_esp32_pcm(converted, conv_sr)
    wav_out = _wav_bytes_from_pcm(pcm, out_sr)
    _save_debug_wav("last_sing_esp32.wav", pcm, out_sr)
    log.info(
        "ESP32 WAV: %.1fs peak=%d bytes=%d",
        len(pcm) / out_sr,
        int(np.max(np.abs(pcm))) if pcm.size else 0,
        len(wav_out),
    )
    return wav_out


async def render_rvc_from_guide_wav(
    guide_wav: bytes,
    *,
    f0_up_key: int = 0,
    index_rate: float | None = None,
    protect: float | None = None,
) -> bytes:
    """RVC only — skip Bark. Use a vocal guide WAV like RVC WebUI / Discord demos."""
    guide, guide_sr = _read_wav_bytes(guide_wav)
    log.info("External guide: %.1fs @ %d Hz", len(guide) / guide_sr, guide_sr)
    _save_debug_wav("last_sing_guide.wav", guide, guide_sr)

    converted, conv_sr = await asyncio.to_thread(
        rvc_convert,
        guide,
        guide_sr,
        f0_up_key=f0_up_key,
        index_rate=index_rate,
        protect=protect,
    )

    guide_dur = len(guide) / guide_sr if guide_sr else 0.0
    if guide_dur > 0 and len(converted) > 0 and conv_sr > 0:
        true_sr = len(converted) / guide_dur
        if abs(true_sr - conv_sr) / conv_sr > 0.1:
            conv_sr = int(round(true_sr / 1000.0)) * 1000
            log.warning("RVC header mislabeled — corrected to %d Hz", conv_sr)

    _save_debug_wav("last_sing_rvc.wav", converted, conv_sr)
    pcm, out_sr = _to_esp32_pcm(converted, conv_sr)
    wav_out = _wav_bytes_from_pcm(pcm, out_sr)
    _save_debug_wav("last_sing_esp32.wav", pcm, out_sr)
    return wav_out


TTS_RVC_F0_UP_KEY = int(os.environ.get("TTS_RVC_F0_UP_KEY", "9"))
TTS_RVC_INDEX_RATE = float(os.environ.get("TTS_RVC_INDEX_RATE", "1.0"))
TTS_RVC_PROTECT = float(os.environ.get("TTS_RVC_PROTECT", "0.33"))


def _bender_http_rvc(guide_wav: bytes, *, timeout: float = 60.0) -> bytes:
    """POST WAV de guia a bender_server.py /rvc y devuelve WAV con voz RVC.
    Usa urllib stdlib -- sin dependencias extra.
    """
    import urllib.parse
    import urllib.request
    try:
        import server_config as _scfg
        _p = _scfg.get_bender_rvc_params()
    except Exception:
        _p = {"model": "bender", "pitch": 10, "index_rate": 1.0, "protect": 0.33}
    qs = urllib.parse.urlencode({
        "model": _p.get("model", "bender"),
        "pitch": int(_p["pitch"]),
        "index_rate": float(_p["index_rate"]),
        "protect": float(_p["protect"]),
    })
    url = BENDER_SERVER_URL + "/rvc?" + qs
    req = urllib.request.Request(url, data=guide_wav, method="POST")
    req.add_header("Content-Type", "audio/wav")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            wav = resp.read()
    except OSError as exc:
        raise RuntimeError(
            f"bender_server no disponible en {BENDER_SERVER_URL} — arrancalo con bender_server.py: {exc}"
        ) from exc
    if not wav.startswith(b"RIFF"):
        raise RuntimeError(f"bender_server devolvio respuesta invalida ({len(wav)} bytes)")
    return wav

def render_tts_applio_from_text(text: str, *, timeout: float | None = None) -> bytes:
    """Texto → edge+RVC en Applio (un proceso, como doc IMPLEM). → WAV 16 kHz ESP32."""
    rvc_timeout = timeout or float(os.environ.get("TTS_RVC_TIMEOUT_S", "180"))
    wav_bytes = _applio_rvc_request(build_text_request(text), timeout=rvc_timeout)
    converted, conv_sr = _read_wav_bytes(wav_bytes)
    _save_debug_wav("last_tts_rvc.wav", converted, conv_sr)
    pcm, out_sr = _to_esp32_pcm(converted, conv_sr)
    wav_out = _wav_bytes_from_pcm(pcm, out_sr)
    _save_debug_wav("last_tts_esp32.wav", pcm, out_sr)
    return wav_out


async def render_tts_with_rvc(guide_wav: bytes, *, timeout: float | None = None) -> bytes:
    """TTS guia (edge/sapi) → RVC → WAV 16 kHz para el ESP32."""
    rvc_timeout = timeout or float(os.environ.get("TTS_RVC_TIMEOUT_S", "180"))
    if TTS_RVC_ENGINE == "bender_http":
        guide, guide_sr = _read_wav_bytes(guide_wav)
        try:
            import server_config as _scfg
            _model = _scfg.get_rvc_voice_model()
        except Exception:
            _model = "bender"
        log.info(
            "TTS guide: %.1fs @ %d Hz (bender_http RVC model=%s)",
            len(guide) / guide_sr,
            guide_sr,
            _model,
        )
        _save_debug_wav("last_tts_guide.wav", guide, guide_sr)
        wav_bytes = await asyncio.to_thread(_bender_http_rvc, guide_wav, timeout=rvc_timeout)
        converted, conv_sr = _read_wav_bytes(wav_bytes)
        _save_debug_wav("last_tts_rvc.wav", converted, conv_sr)
        pcm, out_sr = _to_esp32_pcm(converted, conv_sr)
        wav_out = _wav_bytes_from_pcm(pcm, out_sr)
        _save_debug_wav("last_tts_esp32.wav", pcm, out_sr)
        return wav_out
    if TTS_RVC_ENGINE == "applio" and applio_available():
        in_path = None
        try:
            in_path = _write_temp_wav_bytes(guide_wav)
            guide, guide_sr = _read_wav_bytes(guide_wav)
            log.info("TTS guide: %.1fs @ %d Hz (applio RVC)", len(guide) / guide_sr, guide_sr)
            _save_debug_wav("last_tts_guide.wav", guide, guide_sr)
            wav_bytes = await asyncio.to_thread(
                _applio_rvc_request,
                build_convert_request(input_wav=in_path),
                timeout=rvc_timeout,
            )
            converted, conv_sr = _read_wav_bytes(wav_bytes)
            _save_debug_wav("last_tts_rvc.wav", converted, conv_sr)
            pcm, out_sr = _to_esp32_pcm(converted, conv_sr)
            wav_out = _wav_bytes_from_pcm(pcm, out_sr)
            _save_debug_wav("last_tts_esp32.wav", pcm, out_sr)
            return wav_out
        finally:
            if in_path and os.path.exists(in_path):
                try:
                    os.unlink(in_path)
                except OSError:
                    pass

    wav = await render_rvc_from_guide_wav(
        guide_wav,
        f0_up_key=TTS_RVC_F0_UP_KEY,
        index_rate=TTS_RVC_INDEX_RATE,
        protect=TTS_RVC_PROTECT,
    )
    _save_debug_wav("last_tts_rvc.wav", *_pcm_from_wav_bytes(wav))
    return wav


def _chunk_bytes(data: bytes, size: int) -> Iterator[bytes]:
    for i in range(0, len(data), size):
        yield data[i : i + size]


async def iter_singing_stream(
    lyrics: str,
    *,
    emotion: str = "happy",
    f0_up_key: int = 0,
    index_rate: float | None = None,
    extra_meta: dict[str, Any] | None = None,
) -> AsyncIterator[bytes]:
    """Async generator: AGNT header JSON + chunked WAV body."""
    wav_bytes = await render_singing_wav(
        lyrics,
        f0_up_key=f0_up_key,
        index_rate=index_rate,
    )
    async for chunk in iter_prepared_singing_stream(
        wav_bytes,
        lyrics=lyrics,
        emotion=emotion,
        f0_up_key=f0_up_key,
        extra_meta=extra_meta,
    ):
        yield chunk


def build_singing_payload(
    wav_bytes: bytes,
    *,
    emotion: str = "happy",
    f0_up_key: int = 0,
    extra_meta: dict[str, Any] | None = None,
) -> bytes:
    """AGNT header + WAV bytes (single buffer for ESP32 HTTPClient + Content-Length)."""
    meta: dict[str, Any] = {
        "singing": True,
        "emotion": emotion,
        "sample_rate": TTS_SAMPLE_RATE,
        "channels": 1,
        "bits_per_sample": 16,
        "format": "wav",
        "f0_up_key": int(f0_up_key),
        "pcm_bytes": max(0, len(wav_bytes) - 44),
    }
    if extra_meta:
        meta.update(extra_meta)
    return build_stream_header(meta) + wav_bytes


async def iter_prepared_singing_stream(
    wav_bytes: bytes,
    *,
    lyrics: str,
    emotion: str = "happy",
    f0_up_key: int = 0,
    extra_meta: dict[str, Any] | None = None,
) -> AsyncIterator[bytes]:
    """Stream pre-rendered singing WAV (header + chunks). Prefer build_singing_payload for ESP."""
    payload = build_singing_payload(
        wav_bytes,
        emotion=emotion,
        f0_up_key=f0_up_key,
        extra_meta=extra_meta,
    )
    for chunk in _chunk_bytes(payload, SING_STREAM_CHUNK):
        yield chunk
        await asyncio.sleep(0)


def warm_bark_model() -> None:
    if not _bark_worker_available():
        log.info("Bark warm-up skipped — ejecuta .\\install-bark.ps1")
        return
    try:
        if BARK_USE_DAEMON:
            _get_bark_daemon()
        else:
            _bark_worker_request({"warmup": True}, timeout=600)
        log.info("Bark warm-up done (daemon=%s, worker %s)", BARK_USE_DAEMON, BARK_PYTHON)
    except Exception as e:
        log.warning("Bark warm-up failed: %s", e)


def shutdown_bark_daemon() -> None:
    global _bark_proc
    with _bark_daemon_lock:
        if _bark_proc is not None:
            try:
                _bark_proc.kill()
            except OSError:
                pass
            _bark_proc = None


def preload_applio_daemon() -> None:
    """Opcional (APPLIO_PRELOAD=1): carga modelo en daemon. No usar en arranque sincrono."""
    if TTS_RVC_ENGINE != "applio" or not applio_available() or not rvc_model_configured():
        return
    timeout = float(os.environ.get("APPLIO_PRELOAD_TIMEOUT_S", "90"))
    try:
        _applio_rvc_warmup(build_convert_request(warmup=True), timeout=timeout)
        log.info("Applio daemon listo (modelo en VRAM)")
    except Exception as e:
        log.warning("Applio preload fallo: %s", e)
        _kill_applio_daemon()


def warm_rvc_model() -> None:
    if not singing_configured():
        log.info("RVC warm-up skipped (RVC_MODEL_PATH not set)")
        return
    if TTS_RVC_ENGINE == "applio" and applio_available():
        try:
            _applio_rvc_request(build_text_request("ok"), timeout=120)
            log.info(
                "Applio RVC warm-up done (daemon=%s, %s)",
                APPLIO_USE_DAEMON,
                APPLIO_PYTHON,
            )
            return
        except Exception as e:
            log.warning("Applio RVC warm-up failed (%s) — probando rvc_python", e)
    if not rvc_runtime_available():
        log.warning(
            "RVC warm-up skipped: rvc-python no listo — ejecuta .\\install-rvc.ps1 en server/"
        )
        return
    try:
        if _rvc_worker_available():
            _rvc_worker_warmup_auto(_build_rvc_request(warmup=True), timeout=180)
            log.info(
                "RVC warm-up done (daemon=%s, worker %s)",
                RVC_USE_DAEMON,
                RVC_PYTHON,
            )
            return
        _get_rvc_engine()
        log.info("RVC warm-up done (in-process)")
    except Exception as e:
        log.warning(
            "RVC warm-up failed: %s — ejecuta .\\install-rvc.ps1 en server/",
            e,
        )


def shutdown_rvc() -> None:
    global _rvc_engine, _rvc_proc, _applio_proc
    _kill_applio_daemon()
    with _rvc_daemon_lock:
        if _rvc_proc is not None:
            try:
                _rvc_proc.kill()
            except OSError:
                pass
            _rvc_proc = None
    with _rvc_lock:
        _rvc_engine = None


