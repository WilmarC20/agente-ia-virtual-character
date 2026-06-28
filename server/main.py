"""Brain server for the virtual character.

Run with: .\\start.ps1  (usa el venv, NO Python global)
"""

import asyncio
import concurrent.futures
import datetime
import io
import json
import logging
import os
import re
import tempfile
import threading
import time
import wave
from collections import deque
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Any

import httpx
from fastapi import FastAPI, File, Form, Query, Request, UploadFile
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse, Response, StreamingResponse

# Un solo hilo OpenMP evita cuelgues de faster-whisper en Windows.
os.environ["OMP_NUM_THREADS"] = "1"
os.environ["MKL_NUM_THREADS"] = "1"


def _enable_cuda_dlls() -> None:
    """Put the nvidia-*-cu12 pip DLLs (cuBLAS/cuDNN/cudart) on the search path so
    ctranslate2 finds them. On Windows ctranslate2 loads them by bare name via
    LoadLibrary, which only searches PATH — os.add_dll_directory is NOT enough.
    Must run before faster_whisper/ctranslate2 is imported (i.e. before get_whisper).
    """
    import glob
    import sysconfig

    bin_dirs = glob.glob(os.path.join(sysconfig.get_paths()["purelib"], "nvidia", "*", "bin"))
    if not bin_dirs:
        return
    os.environ["PATH"] = os.pathsep.join(bin_dirs) + os.pathsep + os.environ.get("PATH", "")
    if hasattr(os, "add_dll_directory"):
        for d in bin_dirs:
            try:
                os.add_dll_directory(d)
            except OSError:
                pass


_enable_cuda_dlls()

from tts_engine import (
    cap_speech_text,
    shutdown_piper_daemon,
    synthesize_wav_16k,
    sanitize_speech_text,
    warm_piper_daemon,
)
from text_encoding import normalize_heard, normalize_heard_ascii, prepare_spanish_text
import singing_pipeline as sing

SINGING_ENABLED = os.environ.get("ENABLE_SINGING", "0") == "1"
TTS_RVC_ENABLED = os.environ.get("ENABLE_TTS_RVC", "1") == "1"  # default ON cuando bender_server disponible
import ha_client as ha
import agent_state
import esp_registry as esp_reg
import server_config as srv_cfg
import music_service as music
import story_mode as story_mod
import context_manager
from emotional_memory import EmotionalMemory

SERVER_DIR = Path(__file__).resolve().parent
CAPTURE_DIR = SERVER_DIR / "debug_audio"
LAST_CONVERSE_WAV = CAPTURE_DIR / "last_converse.wav"
SAVE_AUDIO_CAPTURES = os.environ.get("SAVE_AUDIO_CAPTURES", "1") != "0"

OLLAMA_URL = os.environ.get("OLLAMA_URL", "http://127.0.0.1:11434")
OLLAMA_MODEL = os.environ.get("OLLAMA_MODEL", "qwen2.5:7b")
OLLAMA_TIMEOUT_S = float(os.environ.get("OLLAMA_TIMEOUT_S", "90"))
OLLAMA_KEEP_ALIVE = os.environ.get("OLLAMA_KEEP_ALIVE", "30m")
OLLAMA_NUM_PREDICT = int(os.environ.get("OLLAMA_NUM_PREDICT", "220"))
OLLAMA_HA_MAX_DEVICES = int(os.environ.get("OLLAMA_HA_MAX_DEVICES", "25"))
# "base" en CPU ~2-8s; "small" + vad_filter suele COLGARSE en Windows.
WHISPER_MODEL = os.environ.get("WHISPER_MODEL", "base")
WHISPER_DEVICE = os.environ.get("WHISPER_DEVICE", "cuda")
WHISPER_COMPUTE_TYPE = os.environ.get("WHISPER_COMPUTE_TYPE", "")
WHISPER_LANGUAGE = os.environ.get("WHISPER_LANGUAGE", "es")
WHISPER_TIMEOUT_S = float(os.environ.get("WHISPER_TIMEOUT_S", "45"))
WHISPER_BEAM_SIZE = int(os.environ.get("WHISPER_BEAM_SIZE", "2"))
WHISPER_BEST_OF = int(os.environ.get("WHISPER_BEST_OF", "1"))

WHISPER_CONVERSE_PROMPT = os.environ.get(
    "WHISPER_CONVERSE_PROMPT",
    "Comandos en español: enójate, ponte contento, haz cara de enojo, cara triste, "
    "cara sorprendida, cántame una canción, hola, cómo estás, qué hora es, Hi ESP.",
)

EMOTIONS = [
    "neutral", "happy", "sad", "angry", "surprised", "thinking", "sleepy",
    "love", "excited", "cool", "confused", "dizzy",
]
SOUND_EFFECTS = ["none", "beep", "laugh", "error", "yawn", "power_up", "glitch"]

_conversation_history: deque = deque(maxlen=6)
_memory = EmotionalMemory()


def build_system_prompt() -> str:
    return srv_cfg.build_system_prompt(SINGING_ENABLED)

# Appended to the system prompt only when Home Assistant is configured. Carries the
# live device list and teaches the model to emit an optional "actions" field.
HOME_PROMPT = """

CONTROL DEL HOGAR (Home Assistant). Podés encender, apagar o alternar dispositivos.
Cuando el usuario pida controlar algo, AGREGÁ al JSON un campo "actions" (lista):
"actions": [{{"entity_id": "<id EXACTO de la lista>", "command": "on" | "off" | "toggle"}}]
Reglas:
- Usá SOLO entity_id que estén en la lista de abajo; si no está, no lo inventes y avisá en "reply".
- Para escenas y scripts usá command "on".
- En "reply" confirmá en español, corto, con la actitud del personaje activo.
- Si preguntan por el estado de algo, respondé con los estados de la lista y NO incluyas "actions".
- Si el pedido NO es de domótica, no incluyas "actions".

Dispositivos disponibles (nombre (entity_id) = estado):
{devices}
"""

# Appended when a music backend is available — teaches the model to emit a "music" query.
MUSIC_PROMPT = """

MÚSICA (parlante inteligente). Podés reproducir canciones por el parlante.
Si el usuario pide poner/reproducir/escuchar una canción, un artista o un género
(ej. "reproducí Bohemian Rhapsody", "poné algo de Soda Stereo", "quiero escuchar cumbia"),
AGREGÁ al JSON el campo: "music": "<consulta de búsqueda: canción y/o artista>".
- En "reply" confirmá corto y con tu actitud (ej. "Dale, poniendo Soda Stereo.").
- Pasá como consulta lo que pidió; no inventes temas.
- Si el pedido NO es de música, NO incluyas "music".
"""


def _music_available() -> bool:
    try:
        return bool(music.has_youtube_api() or music.has_ytmusicapi() or music.has_yt_dlp())
    except Exception:
        return False


# Direct music-command detection (the small LLM often ignores the "music" instruction and
# just answers in character). Extracts the song/artist after a trigger word — works for ANY
# track, not a hardcoded list.
def music_command_query(text: str) -> str | None:
    t = normalize_heard(text or "")
    _filler = r"(?:(?:la|el|un|una)\s+)?(?:canci[oó]n|cancion|tema|temita|m[uú]sica|musica|algo)\s+(?:de\s+)?"
    # Strong music verbs (música-specific): the rest of the phrase is the query.
    m = re.search(r"\b(?:reproduc\w*|escuch\w*|play)\b\s+(.+)", t)
    if m:
        q = re.sub(r"^" + _filler, "", m.group(1), flags=re.IGNORECASE)
    else:
        # "poné/pon/ponme" is ambiguous (also home automation), so require a music marker
        # OR "ponme <artista>" without filler (common speech).
        m = re.search(r"\bp[oó]n(?:e|é|me|er|ga|gan)?\b\s+" + _filler + r"(.+)", t, flags=re.IGNORECASE)
        if not m:
            m = re.search(r"\bponme\b\s+(.+)", t, flags=re.IGNORECASE)
        if not m:
            return None
        q = m.group(1)
    q = re.sub(r"^(?:de|por)\s+", "", q, flags=re.IGNORECASE)
    q = q.strip(" .,¿?¡!\"'")
    return q if len(q) >= 2 else None


async def _resolve_music_track(query: str) -> dict | None:
    try:
        sr = await asyncio.to_thread(music.search, query, limit=1)
        tracks = (sr or {}).get("results") or []
        if not tracks:
            return None
        vid = tracks[0].get("id") or tracks[0].get("video_id") or ""
        return {"video_id": vid, "title": tracks[0].get("title") or query} if vid else None
    except Exception as e:
        log.warning("music search '%s' failed: %s", query, e)
        return None


async def synthesize_character_wav(text: str, *, sing_flag: bool = False, log_label: str = "TTS") -> bytes:
    """Misma pipeline que POST /tts: voz del personaje (RVC/Applio) o guía si falla."""
    t0 = time.monotonic()
    rvc_timeout = float(os.environ.get("TTS_RVC_TIMEOUT_S", "180"))
    use_applio_unified = (
        TTS_RVC_ENABLED
        and sing.singing_configured()
        and sing.tts_rvc_runtime_available()
        and os.environ.get("TTS_RVC_GUIDE", "edge").lower() == "edge"
        and sing.TTS_RVC_ENGINE == "applio"
    )
    if use_applio_unified:
        log.info("%s pipeline: Applio unificado (edge+RVC)", log_label)
        await asyncio.to_thread(release_ollama_vram)
        wav = await asyncio.wait_for(
            asyncio.to_thread(sing.render_tts_applio_from_text, text, timeout=rvc_timeout),
            timeout=rvc_timeout + 10,
        )
        log.info("%s Applio %.1fs (%d bytes)", log_label, time.monotonic() - t0, len(wav))
        return wav
    if TTS_RVC_ENABLED and sing.singing_configured():
        from tts_engine import synthesize_rvc_guide_wav

        wav = await asyncio.to_thread(synthesize_rvc_guide_wav, text, sing_flag)
        log.info(
            "%s guia %s %.1fs (%d bytes)",
            log_label,
            os.environ.get("TTS_RVC_GUIDE", "edge"),
            time.monotonic() - t0,
            len(wav),
        )
        if sing.tts_rvc_runtime_available():
            try:
                trvc = time.monotonic()
                if sing.TTS_RVC_ENGINE != "bender_http":
                    await asyncio.to_thread(release_ollama_vram)
                wav = await asyncio.wait_for(
                    sing.render_tts_with_rvc(wav, timeout=rvc_timeout),
                    timeout=rvc_timeout + 10,
                )
                log.info(
                    "%s RVC %.1fs (%d bytes) | total %.1fs",
                    log_label,
                    time.monotonic() - trvc,
                    len(wav),
                    time.monotonic() - t0,
                )
            except TimeoutError:
                log.warning("%s RVC timeout %.0fs — voz guia sin RVC", log_label, rvc_timeout)
            except Exception as e:
                log.warning("%s RVC fallo (%s) — voz guia sin RVC", log_label, e)
        return wav
    return await synthesize_wav_16k(text, sing=sing_flag)


async def _character_tts_wav(text: str) -> bytes:
    """Guion/story: voz del personaje (idéntica a /tts)."""
    safe = cap_speech_text(prepare_spanish_text(text), sing=False)
    return await synthesize_character_wav(safe, sing_flag=False, log_label="story")


def time_context() -> str:
    return srv_cfg.time_facts()["context"]


def try_quick_reply(user_text: str) -> dict | None:
    """Respuestas instantáneas sin Ollama (hora, GLM 5.2, etc.) en voz del personaje activo."""
    t = normalize_heard_ascii(user_text)
    if any(k in t for k in ("que hora", "hora es", "dime la hora", "que horas")):
        return srv_cfg.quick_time_reply()
    if asks_about_glm52(user_text):
        return srv_cfg.quick_glm52_reply()
    return None


def asks_about_glm52(user_text: str) -> bool:
    """Detecta preguntas sobre el modelo GLM 5.2 (tolerante a transcripción de voz)."""
    t = normalize_heard_ascii(user_text)
    compact = re.sub(r"[^a-z0-9]", "", t)
    if "glm" not in compact and "glm" not in t:
        return False
    if "52" in compact or "52" in t or "5 2" in t:
        return True
    if any(k in t for k in ("glm 5", "que es glm", "que es el glm", "explicame glm", "hablame de glm")):
        return True
    return False


# Appended to the system prompt with live context (time, who's home) + memory, and
# lets the model persist what it learns about the human.
CONTEXT_PROMPT = """

CONTEXTO ACTUAL (úsalo para sonar consciente del momento, sin repetirlo textual): {context}
Podés agregar al JSON, SOLO si corresponde, estos campos opcionales:
- "name": el nombre del humano, si te lo dice o lo deducís.
- "remember": un dato corto que valga la pena recordar de él (un gusto, un hecho).
- "mood": tu humor de fondo en una palabra (ej. vago, borracho, presumido, leal).
"""

# The character talks on its own after a while idle (firmware calls /idle).
# Texto real: srv_cfg.get_idle_user_prompt() según personalidad activa.
IDLE_USER_PROMPT = ""

# PC-side wake phrase (Whisper-based, via /wake-check). Spanish "Hola asistente"
# with common Whisper spellings. Override the whole list with env WAKE_PHRASES
# (comma-separated) to use a different phrase without touching code.
WAKE_PHRASES = tuple(
    p.strip()
    for p in os.environ.get(
        "WAKE_PHRASES",
        "hola asistente,ola asistente,hola asistent,ola asistent,hola sistente,"
        "hola acistente,olas istente",
    ).lower().split(",")
    if p.strip()
)

# Per-preset matching for the device's CONFIGURABLE wake word. The firmware sends the
# chosen label in the X-Wake-Phrase header; we match the transcription against that
# preset's spellings + fuzzy cores. Keys MUST match WAKE_PRESET_LABELS in the firmware
# (settings.h). Unknown/empty header falls back to the default WAKE_PHRASES behaviour.
WAKE_PRESETS = {
    "hola asistente": {
        "spellings": ("hola asistente", "ola asistente", "hola asistent", "ola asistent"),
        "cores": ("asistente", "asistent", "acistente", "sistente", "asustente"),
    },
    "che robot": {
        "spellings": ("che robot", "che robó", "cherobot", "che robots"),
        "cores": ("robot", "robó", "rrobot", "chrobot"),
    },
    "ey bender": {
        "spellings": ("ey bender", "hey bender", "ei bender", "ey vender"),
        "cores": ("bender", "vender", "bénder", "pender"),
    },
    "hola bender": {
        "spellings": ("hola bender", "ola bender", "hola vender"),
        "cores": ("bender", "vender", "bénder"),
    },
}

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
logging.getLogger("httpx").setLevel(logging.WARNING)
log = logging.getLogger("brain")

_whisper = None
_whisper_lock = threading.Lock()
_ollama_client: httpx.AsyncClient | None = None
# Un solo worker: Whisper no es thread-safe; evita 3 transcribe colgadas en paralelo.
_transcribe_pool = concurrent.futures.ThreadPoolExecutor(max_workers=1, thread_name_prefix="whisper")

_HA_INTENT_CUES = (
    "enciende", "encender", "apaga", "apagar", "alterna", "toggle",
    "luz", "luces", "lampara", "lámpara", "interruptor", "switch",
    "persiana", "cortina", "ventilador", "aire", "clima", "termostato",
    "escena", "script", "casa", "hogar", "habitacion", "habitación",
    "encendida", "encendido", "apagada", "apagado", "estado de",
    "home assistant", "domotica", "domótica",
)


def _wants_ha_context(user_text: str) -> bool:
    t = normalize_heard(user_text)
    return any(c in t for c in _HA_INTENT_CUES)


def get_ollama_client() -> httpx.AsyncClient:
    global _ollama_client
    if _ollama_client is None:
        _ollama_client = httpx.AsyncClient(timeout=_ollama_http_timeout())
    return _ollama_client


async def close_ollama_client() -> None:
    global _ollama_client
    if _ollama_client is not None:
        await _ollama_client.aclose()
        _ollama_client = None


def whisper_compute_type() -> str:
    if WHISPER_COMPUTE_TYPE:
        return WHISPER_COMPUTE_TYPE
    return "float32" if WHISPER_DEVICE == "cpu" else "float16"


def _load_whisper(device: str, compute: str):
    from faster_whisper import WhisperModel

    return WhisperModel(
        WHISPER_MODEL, device=device, compute_type=compute, cpu_threads=4, num_workers=1
    )


def _cuda_encode_works(model) -> bool:
    """faster-whisper loads on CUDA even when cuBLAS/cuDNN DLLs are missing — the
    failure only surfaces at encode(). Run a tiny encode now to detect it."""
    import numpy as np

    try:
        segments, _ = model.transcribe(
            np.zeros(16000, dtype=np.float32), beam_size=1, language="es"
        )
        list(segments)  # force the encoder to actually run
        return True
    except Exception as e:
        log.warning("CUDA encode self-test failed (%s)", e)
        return False


def get_whisper():
    global _whisper
    with _whisper_lock:
        if _whisper is None:
            compute = whisper_compute_type()
            device = WHISPER_DEVICE
            log.info("Loading Whisper '%s' device=%s compute=%s...", WHISPER_MODEL, device, compute)

            try:
                model = _load_whisper(device, compute)
            except Exception as e:
                if device == "cpu":
                    raise
                log.warning("Whisper load on %s failed (%s), using CPU", device, e)
                device = "cpu"
                model = _load_whisper("cpu", "float32")

            # CUDA can load but fail at encode (missing cuBLAS/cuDNN). Verify, else CPU.
            if device == "cuda" and not _cuda_encode_works(model):
                log.warning("CUDA unusable (missing cuBLAS/cuDNN DLLs) — rebuilding on CPU")
                del model
                device = "cpu"
                model = _load_whisper("cpu", "float32")

            _whisper = model
            log.info("Whisper ready (device=%s)", device)
        return _whisper


def _wav_stats(wav_bytes: bytes) -> str:
    peak, rms, dur = _wav_metrics(wav_bytes)
    if dur <= 0:
        return "empty wav"
    return (
        f"{dur:.2f}s sr=16000 peak={peak} rms={rms:.0f} bytes={len(wav_bytes)}"
    )


def _wav_metrics(wav_bytes: bytes) -> tuple[float, int, float]:
    """Duración (s), pico absoluto, RMS del PCM mono 16-bit."""
    import numpy as np

    try:
        with wave.open(io.BytesIO(wav_bytes), "rb") as wf:
            sr = wf.getframerate()
            pcm = np.frombuffer(wf.readframes(wf.getnframes()), dtype=np.int16)
    except (wave.Error, ValueError):
        return 0.0, 0, 0.0

    if len(pcm) == 0 or sr <= 0:
        return 0.0, 0, 0.0
    fpcm = pcm.astype(np.float32)
    rms = float(np.sqrt(np.mean(fpcm * fpcm)))
    peak = int(np.max(np.abs(pcm)))
    return len(pcm) / sr, peak, rms


MIN_WAKE_PEAK = int(os.environ.get("MIN_WAKE_PEAK", "2800"))
MIN_WAKE_RMS = int(os.environ.get("MIN_WAKE_RMS", "500"))
MIN_CONVERSE_PEAK = int(os.environ.get("MIN_CONVERSE_PEAK", "2000"))
MIN_CONVERSE_RMS = int(os.environ.get("MIN_CONVERSE_RMS", "400"))


def wav_has_speech(wav_bytes: bytes, *, min_peak: int, min_rms: float) -> bool:
    _, peak, rms = _wav_metrics(wav_bytes)
    return peak >= min_peak and rms >= min_rms


def save_last_capture(wav_bytes: bytes, label: str = "converse") -> Path | None:
    if not SAVE_AUDIO_CAPTURES:
        return None
    CAPTURE_DIR.mkdir(parents=True, exist_ok=True)
    if label == "converse":
        path = LAST_CONVERSE_WAV
    else:
        path = CAPTURE_DIR / f"last_{label}.wav"
    path.write_bytes(wav_bytes)
    log.info("audio capture -> %s | %s", path, _wav_stats(wav_bytes))
    return path


def normalize_wav_bytes(wav_bytes: bytes, target_peak: int = 12000) -> bytes:
    """Boost quiet mic captures so Whisper can hear speech."""
    import numpy as np

    try:
        with wave.open(io.BytesIO(wav_bytes), "rb") as wf:
            if wf.getnchannels() != 1 or wf.getsampwidth() != 2:
                return wav_bytes
            sr = wf.getframerate()
            pcm = np.frombuffer(wf.readframes(wf.getnframes()), dtype=np.int16)
    except (wave.Error, ValueError):
        return wav_bytes

    if len(pcm) == 0:
        return wav_bytes
    peak = int(np.max(np.abs(pcm)))
    if peak < 800:
        log.warning("audio very quiet peak=%d — skip normalize", peak)
        return wav_bytes
    if peak < MIN_CONVERSE_PEAK:
        log.info("audio below speech peak=%d — skip normalize boost", peak)
        return wav_bytes
    if peak >= target_peak:
        return wav_bytes

    gain = target_peak / peak
    boosted = np.clip(pcm.astype(np.float32) * gain, -32768, 32767).astype(np.int16)
    log.info("audio normalize gain=%.1f peak %d -> %d", gain, peak, int(np.max(np.abs(boosted))))

    out = io.BytesIO()
    with wave.open(out, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sr)
        wf.writeframes(boosted.tobytes())
    return out.getvalue()


def _wav_bytes_to_float32(wav_bytes: bytes):
    import numpy as np

    with wave.open(io.BytesIO(wav_bytes), "rb") as wf:
        if wf.getnchannels() != 1 or wf.getsampwidth() != 2:
            raise ValueError("expected 16-bit mono WAV")
        frames = wf.readframes(wf.getnframes())
    return np.frombuffer(frames, dtype=np.int16).astype(np.float32) / 32768.0


def _transcribe_wav_sync(
    wav_bytes: bytes,
    *,
    language: str | None = None,
    initial_prompt: str | None = None,
    beam_size: int | None = None,
) -> str:
    """Blocking STT — una sola instancia a la vez."""
    t0 = time.monotonic()
    model = get_whisper()
    log.info("transcribe: start (%d bytes)", len(wav_bytes))

    try:
        audio = _wav_bytes_to_float32(wav_bytes)
    except (ValueError, wave.Error) as e:
        log.warning("wav parse failed (%s), retry via temp file", e)
        audio = None

    beam = beam_size if beam_size is not None else WHISPER_BEAM_SIZE
    best_of = min(WHISPER_BEST_OF, beam)

    try:
        if audio is not None:
            segments, info = model.transcribe(
                audio,
                language=language or WHISPER_LANGUAGE,
                initial_prompt=initial_prompt,
                vad_filter=False,
                beam_size=beam,
                best_of=best_of,
                condition_on_previous_text=False,
                temperature=0.0,
            )
        else:
            tmp_path = None
            try:
                with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
                    tmp.write(wav_bytes)
                    tmp_path = tmp.name
                segments, info = model.transcribe(
                    tmp_path,
                    language=language or WHISPER_LANGUAGE,
                    initial_prompt=initial_prompt,
                    vad_filter=False,
                    beam_size=beam,
                    best_of=best_of,
                    condition_on_previous_text=False,
                    temperature=0.0,
                )
            finally:
                if tmp_path and os.path.exists(tmp_path):
                    try:
                        os.unlink(tmp_path)
                    except OSError:
                        pass

        parts = [seg.text for seg in segments]
        text = " ".join(parts).strip()
        log.info(
            "transcribe: done in %.1fs lang=%s(%.0f%%) beam=%d best_of=%d text=%r",
            time.monotonic() - t0,
            info.language,
            (info.language_probability or 0) * 100,
            beam,
            best_of,
            text[:80],
        )
        return text
    except Exception:
        log.exception("transcribe failed after %.1fs", time.monotonic() - t0)
        raise


async def transcribe_wav(
    wav_bytes: bytes,
    *,
    language: str | None = None,
    initial_prompt: str | None = None,
    beam_size: int | None = None,
) -> str:
    loop = asyncio.get_running_loop()
    try:
        raw = await asyncio.wait_for(
            loop.run_in_executor(
                _transcribe_pool,
                lambda: _transcribe_wav_sync(
                    wav_bytes,
                    language=language,
                    initial_prompt=initial_prompt,
                    beam_size=beam_size,
                ),
            ),
            timeout=WHISPER_TIMEOUT_S,
        )
        return fix_transcription(raw)
    except asyncio.TimeoutError:
        log.error("transcribe: TIMEOUT after %.0fs — reinicia el servidor si persiste", WHISPER_TIMEOUT_S)
        return ""


async def _ha_cache_warmer():
    """Keep the HA states cache hot in the background so /converse never waits on the
    slow ~4s /api/states fetch — requests just read the warm snapshot."""
    interval_s = float(os.environ.get("HA_CACHE_WARM_S", "60"))
    while True:
        try:
            states = await asyncio.to_thread(ha.get_states, True)
            log.debug("HA cache warm: %d entities", len(states))
        except Exception as e:
            log.warning("HA cache warm failed: %s", e)
        await asyncio.sleep(interval_s)


@asynccontextmanager
async def lifespan(app: FastAPI):
    log.info("Preloading Whisper (model=%s)...", WHISPER_MODEL)
    await asyncio.to_thread(get_whisper)
    log.info("Brain server ready — whisper=%s timeout=%.0fs", WHISPER_MODEL, WHISPER_TIMEOUT_S)
    if os.environ.get("TTS_ENGINE", "sapi").lower() == "piper":
        asyncio.create_task(asyncio.to_thread(warm_piper_daemon))
    if ha.ha_enabled():
        asyncio.create_task(_ha_cache_warmer())

    if (
        TTS_RVC_ENABLED
        and sing.singing_configured()
        and sing.tts_rvc_runtime_available()
        and os.environ.get("APPLIO_PRELOAD", "0") == "1"
    ):
        asyncio.create_task(asyncio.to_thread(sing.preload_applio_daemon))
        log.info("Applio RVC: precarga en segundo plano (no bloquea HTTP)")
    elif TTS_RVC_ENABLED and sing.singing_configured():
        log.info("Applio RVC: carga en primer /tts (arranque rapido)")

    asyncio.create_task(asyncio.to_thread(warm_ollama_model))
    if music.has_yt_dlp() and music.has_ffmpeg() and music.should_warm_ytdlp_ejs():
        log.info("yt-dlp: precalentando EJS (~1-3 min, bloquea arranque — necesario con pytubefix=off)")
        await asyncio.to_thread(music.warm_ytdlp_ejs)
    elif music.has_yt_dlp() and music.has_ffmpeg():
        log.info("yt-dlp EJS warm omitido al arranque")
    asyncio.create_task(asyncio.to_thread(music.warm_deps_cache))
    if music.has_youtube_api():
        log.info("YouTube Data API v3: activa (busqueda oficial)")
    elif music.has_pytubefix():
        log.info("Musica: sin YOUTUBE_API_KEY — busqueda por ytmusic/yt-dlp")
    log.info("HTTP listo — aceptando ESP")
    if SINGING_ENABLED and sing.singing_configured():
        asyncio.create_task(asyncio.to_thread(sing.warm_bark_model))
    yield
    await close_ollama_client()
    shutdown_piper_daemon()
    if SINGING_ENABLED:
        sing.shutdown_bark_daemon()
    if (TTS_RVC_ENABLED or SINGING_ENABLED) and sing.singing_configured():
        sing.shutdown_rvc()
    _transcribe_pool.shutdown(wait=False, cancel_futures=True)
    music.cancel_prefetch_kill()


app = FastAPI(title="agenteIA brain", lifespan=lifespan)


@app.middleware("http")
async def track_esp_devices(request: Request, call_next):
    path = request.url.path
    if esp_reg.should_track_path(path):
        ip = request.client.host if request.client else ""
        esp_reg.note_request(
            ip=ip,
            mac_header=request.headers.get("X-Device-MAC", ""),
            path=path,
        )
    return await call_next(request)


# normalize_heard -> text_encoding (conserva tildes; normalize_heard_ascii para comparar comandos)


# Whisper base a veces alucina en clips cortos; corregir alias conocidos.
_TRANSCRIPTION_ALIASES: dict[str, str] = {
    "y no rete": "enojate",
    "y no reté": "enojate",
    "ino hate": "enojate",
    "enohate": "enojate",
    "enojate": "enojate",
    "ponte contenta": "ponte contento",
    "ponte contentos": "ponte contento",
}


def fix_transcription(text: str) -> str:
    if not text:
        return text
    text = prepare_spanish_text(text)
    norm = normalize_heard_ascii(text)
    if norm in _TRANSCRIPTION_ALIASES:
        fixed = _TRANSCRIPTION_ALIASES[norm]
        log.info("transcribe: alias %r -> %r", text, fixed)
        return fixed
    if "rete" in norm and "no" in norm:
        log.info("transcribe: guess %r -> enojate", text)
        return "enojate"
    if norm.startswith("enoj") or "enoja" in norm:
        return "enojate"
    return text


def is_wake_phrase(text: str, phrase: str = "") -> bool:
    norm = normalize_heard(text)
    if not norm:
        return False
    compact = norm.replace(" ", "")

    preset = WAKE_PRESETS.get(phrase.strip().lower())
    if preset:
        for sp in preset["spellings"]:
            if sp.replace(" ", "") in compact or sp in norm:
                return True
        for core in preset["cores"]:
            if core in compact:
                return True
        return False

    # No/unknown preset -> default "Hola asistente" matching.
    for p in WAKE_PHRASES:
        if p.replace(" ", "") in compact or p in norm:
            return True
    # Fuzzy: any "asistente"-like transcription counts as the wake phrase.
    for core in ("asistente", "asistent", "acistente", "sistente", "asustente"):
        if core in compact:
            return True
    return False


def strip_wake_phrase(text: str, phrase: str = "") -> str:
    """Quita la frase de activación del inicio; devuelve el comando (p. ej. 'que hora es')."""
    norm = normalize_heard(text)
    if not norm:
        return ""

    spellings: list[str] = []
    preset = WAKE_PRESETS.get(phrase.strip().lower())
    if preset:
        spellings.extend(preset["spellings"])
    else:
        spellings.extend(WAKE_PHRASES)

    for sp in sorted(set(spellings), key=len, reverse=True):
        sp = sp.strip()
        if not sp:
            continue
        if norm.startswith(sp):
            return norm[len(sp) :].lstrip(" ,.!?…")
        sp_c = sp.replace(" ", "")
        norm_c = norm.replace(" ", "")
        if norm_c.startswith(sp_c) and len(norm_c) > len(sp_c):
            sp_words = len(sp.split())
            words = norm.split()
            if len(words) > sp_words:
                return " ".join(words[sp_words:]).strip()

    words = norm.split()
    if len(words) >= 2:
        cores: tuple[str, ...]
        if preset:
            cores = preset["cores"]
        else:
            cores = ("asistente", "asistent", "acistente", "sistente", "asustente")
        w0, w1 = words[0], words[1]
        w1c = w1.replace(" ", "")
        if w0 in ("hola", "ola", "ey", "hey", "ei", "che") and any(c in w1c for c in cores):
            if len(words) > 2:
                return " ".join(words[2:]).strip()
            return ""

    return ""


def wants_sing(user_text: str) -> bool:
    t = normalize_heard(user_text)
    return any(w in t for w in ("canta", "cantame", "cancion", "canción", "melodia", "melodía", "tararea"))


def wants_face_only(user_text: str) -> bool:
    t = normalize_heard(user_text)
    if not t:
        return False
    cues = (
        "cara de",
        "haz cara",
        "hace cara",
        "pon cara",
        "ponte",
        "ponme cara",
        "expresion",
        "solo cara",
        "solo la cara",
        "no hables",
        "no digas",
        "sin hablar",
        "mirada de",
        "gesto de",
        "hazme cara",
    )
    return any(c in t for c in cues)


def emotion_from_face_command(user_text: str) -> str | None:
    t = normalize_heard(user_text)
    rules = (
        (("enoj", "furios", "brav", "rabia"), "angry"),
        (("trist", "tristeza", "llor"), "sad"),
        (("content", "feliz", "alegre", "happ"), "happy"),
        (("sorprend", "sorpresa"), "surprised"),
        (("pens", "pensando"), "thinking"),
        (("dorm", "sleepy", "cansad", "suen"), "sleepy"),
        (("neutral", "seri", "normal"), "neutral"),
        (("amor", "enamor", "quer"), "love"),
        (("emocion", "emociona", "eufor"), "excited"),
        (("genial", "guay", "chido", "cool"), "cool"),
        (("confund", "no entiendo", "perdid"), "confused"),
        (("maread", "vertig", "dizzy"), "dizzy"),
    )
    for keys, emotion in rules:
        if any(k in t for k in keys):
            return emotion
    return None


def parse_ollama_json(content: str) -> dict:
    text = content.strip()
    if text.startswith("```"):
        text = re.sub(r"^```(?:json)?\s*", "", text, flags=re.IGNORECASE)
        text = re.sub(r"\s*```\s*$", "", text)
    return json.loads(text)


def ollama_fallback(raw: str = "", *, kind: str = "default") -> dict:
    if raw.strip():
        reply = sanitize_speech_text(prepare_spanish_text(raw.strip()[:200]))
    else:
        reply = prepare_spanish_text(srv_cfg.ollama_error_reply(kind))
    return {
        "emotion": "surprised",
        "reply": reply,
        "speak": bool(reply),
        "sing": False,
        "sound_effect": "error",
    }


def _ollama_http_timeout() -> httpx.Timeout:
    return httpx.Timeout(connect=5.0, read=OLLAMA_TIMEOUT_S, write=15.0, pool=5.0)


def _ollama_chat_payload(messages: list[dict], *, json_format: bool = True, model_override: str | None = None) -> dict[str, Any]:
    payload: dict[str, Any] = {
        "model": model_override if model_override else srv_cfg.get_ollama_model(OLLAMA_MODEL),
        "stream": False,
        "keep_alive": OLLAMA_KEEP_ALIVE,
        "messages": messages,
        "options": {"num_predict": OLLAMA_NUM_PREDICT},
    }
    if json_format:
        payload["format"] = "json"
    return payload


def warm_ollama_model() -> None:
    """Carga el modelo en RAM/VRAM; sin esto la 1ª petición tarda ~40s y hace timeout."""
    payload = _ollama_chat_payload(
        [{"role": "user", "content": "responde ok"}],
        json_format=False,
    )
    payload["options"] = {"num_predict": 16}
    try:
        t0 = time.monotonic()
        with httpx.Client(timeout=_ollama_http_timeout()) as client:
            r = client.post(f"{OLLAMA_URL}/api/chat", json=payload)
            r.raise_for_status()
        log.info(
            "Ollama warm-up listo model=%s en %.1fs (keep_alive=%s)",
            OLLAMA_MODEL,
            time.monotonic() - t0,
            OLLAMA_KEEP_ALIVE,
        )
    except Exception as e:
        log.warning("Ollama warm-up fallo (%s): %s", OLLAMA_URL, e)


def release_ollama_vram() -> None:
    """Libera VRAM de Ollama antes de RVC (misma GPU)."""
    try:
        with httpx.Client(timeout=httpx.Timeout(8.0)) as client:
            client.post(
                f"{OLLAMA_URL}/api/chat",
                json={
                    "model": OLLAMA_MODEL,
                    "stream": False,
                    "keep_alive": 0,
                    "messages": [{"role": "user", "content": "."}],
                    "options": {"num_predict": 1},
                },
            )
        log.info("Ollama VRAM liberada (keep_alive=0) antes de RVC")
    except Exception as e:
        log.debug("Ollama release VRAM: %s", e)


_VALID_TONES = frozenset({
    "neutral", "ironic", "worried", "proud", "curious",
    "flat", "excited", "empathetic", "sarcastic", "urgent",
})


async def ask_ollama(user_text: str, *, use_history: bool = False,
                     model_override: str | None = None,
                     system_prompt_override: str | None = None) -> dict:
    ctx_name = context_manager.get()
    ctx_cfg = context_manager.config(ctx_name)

    # M11: personality behavior config
    pid = srv_cfg.load().get("personality", "bender")
    beh = srv_cfg.get_behavior_config(pid)

    # M11: night_mode_auto — switch idle context to night_mode during quiet hours
    if beh.get("night_mode_auto") and ctx_name == "idle":
        hour = datetime.datetime.now().hour
        if hour >= 22 or hour < 7:
            ctx_name = context_manager.set_context("night_mode")
            ctx_cfg = context_manager.config(ctx_name)
    system_content = system_prompt_override if system_prompt_override else build_system_prompt()
    if ha.ha_enabled() and _wants_ha_context(user_text):
        devices = await asyncio.to_thread(ha.devices_prompt, OLLAMA_HA_MAX_DEVICES)
        if devices:
            system_content += HOME_PROMPT.format(devices=devices)
    if _music_available():
        system_content += MUSIC_PROMPT

    # Situational awareness + memory: time, who's home, what we remember of the human.
    ctx_parts = [time_context()]
    if ha.ha_enabled():
        presence = await asyncio.to_thread(ha.presence_context)
        if presence:
            ctx_parts.append(presence)
    mem = agent_state.context()
    if mem:
        ctx_parts.append(mem)
    system_content += CONTEXT_PROMPT.format(context=" ".join(p for p in ctx_parts if p))
    if ctx_name != "idle":
        system_content += f"\n\nCONTEXTO DE COMPORTAMIENTO: contexto activo '{ctx_name}'. Ajustá tu energía, ritmo y tono a ese ambiente."

    messages = [{"role": "system", "content": system_content}]
    if use_history:
        messages.extend(_conversation_history)
    messages.append({"role": "user", "content": user_text})

    payload = _ollama_chat_payload(messages, model_override=model_override)
    try:
        t0 = time.monotonic()
        r = await get_ollama_client().post(f"{OLLAMA_URL}/api/chat", json=payload)
        r.raise_for_status()
        content = r.json()["message"]["content"]
        log.info("ollama: %.1fs model=%s", time.monotonic() - t0, OLLAMA_MODEL)
    except httpx.TimeoutException:
        log.warning("ollama timeout after %.0fs (%s)", OLLAMA_TIMEOUT_S, OLLAMA_URL)
        return ollama_fallback(kind="timeout")
    except httpx.ConnectError:
        log.warning("ollama connect failed (%s)", OLLAMA_URL)
        return ollama_fallback(kind="connect")
    except httpx.HTTPStatusError as e:
        log.warning("ollama HTTP %s: %s", e.response.status_code, e.response.text[:200])
        return ollama_fallback(kind="http")

    try:
        data = parse_ollama_json(content)
        emotion = data.get("emotion", "neutral")
        try:
            intensity = float(data.get("intensity", 0.7))
        except (TypeError, ValueError):
            intensity = 0.7
        intensity = max(0.0, min(1.0, intensity))
        tone_raw = str(data.get("tone", "neutral")).strip().lower()
        tone = tone_raw if tone_raw in _VALID_TONES else "neutral"
        reply = prepare_spanish_text(str(data.get("reply", "")).strip())
        sing = bool(data.get("sing", False))
        speak = bool(data.get("speak", True))
        sound_effect = str(data.get("sound_effect", "none")).strip().lower()
    except json.JSONDecodeError:
        log.warning("ollama JSON parse failed, using fallback")
        return ollama_fallback(content)

    if emotion not in EMOTIONS:
        emotion = "neutral"
    if sound_effect not in SOUND_EFFECTS:
        sound_effect = "none"

    if wants_face_only(user_text):
        speak = False
        reply = ""
        sing = False
        sound_effect = "none"
        hinted = emotion_from_face_command(user_text)
        if hinted:
            emotion = hinted
    elif wants_sing(user_text) and SINGING_ENABLED:
        speak = True
        sing = True
        if emotion == "neutral":
            emotion = "happy"
    elif not reply:
        speak = False

    if not SINGING_ENABLED:
        sing = False

    if sing and reply:
        reply = await expand_sing_lyrics(reply, user_text)

    if reply:
        reply = cap_speech_text(reply, sing=sing)

    # M11: emotion_biases — per-personality intensity nudge per emotion
    biases = beh.get("emotion_biases", {})
    if emotion in biases:
        intensity = max(0.0, min(1.0, intensity + biases[emotion]))

    # M7: apply session memory modifiers
    mods = _memory.get_modifiers()
    intensity = max(0.0, min(1.0, intensity + mods["intensity_boost"]))

    # M11 + M7: expressivity = personality-anchored blend + memory boost
    expressivity = min(1.0, context_manager.get_effective_expressivity(beh, ctx_name) + mods["expressivity_boost"])

    # M5 Rhythm: pre/post response timing from context, adjusted by tone and intensity
    pre_response_ms = ctx_cfg["pre_response_ms"]
    if tone in ("urgent", "excited") and pre_response_ms > 150:
        pre_response_ms = max(80, pre_response_ms // 2)
    post_response_ms = ctx_cfg["post_response_ms"]
    if intensity > 0.85:  # M8: post_emotion_peak silence
        post_response_ms = max(post_response_ms, 600)
    if len(reply) > 200:  # M8: post_complex_answer silence
        post_response_ms = int(post_response_ms * 1.4)

    # M7: log memory events
    _memory.log("voice_interaction", severity=0.4)
    if intensity > 0.85:
        _memory.log("emotion_peak", severity=intensity)

    result = {
        "emotion": emotion,
        "intensity": round(intensity, 3),
        "tone": tone,
        "context": ctx_name,
        "expressivity": round(expressivity, 3),
        "pre_response_ms": pre_response_ms,
        "post_response_ms": post_response_ms,
        "reply": reply,
        "sing": sing,
        "speak": speak,
        "sound_effect": sound_effect,
        "emotion_recovery_ms": beh.get("emotion_recovery_ms", 8000),
        "microexp_rate": round(float(beh.get("microexp_rate", 0.6)), 3),
    }

    actions = data.get("actions")
    if isinstance(actions, list) and actions and ha.ha_enabled() and not wants_face_only(user_text):
        result["ha_actions"] = await asyncio.to_thread(ha.execute_actions, actions)
        log.info("HA actions -> %s", result["ha_actions"])

    # Smart speaker: resolve the model's music request to a playable track for the device.
    music_query = data.get("music")
    if isinstance(music_query, str) and music_query.strip() and _music_available() and not wants_face_only(user_text):
        track = await _resolve_music_track(music_query.strip()[:120])
        if track:
            result["music"] = track
            _memory.log("music_played", severity=0.7)
            log.info("music intent (llm) '%s' -> %s (%s)", music_query.strip(), track["title"], track["video_id"])
        else:
            result["reply"] = cap_speech_text("No encontré esa canción.", sing=False)
            result["speak"] = True

    # Persist anything the character learned about the human (name, facts, mood).
    if data.get("name") or data.get("remember") or data.get("mood"):
        await asyncio.to_thread(
            agent_state.update,
            name=data.get("name"),
            remember=data.get("remember"),
            mood=data.get("mood"),
        )

    if use_history:
        _conversation_history.append({"role": "user", "content": user_text})
        _conversation_history.append(
            {"role": "assistant", "content": reply or f"[{emotion}]"}
        )

    return result


@app.get("/health")
async def health():
    """Siempre responde si el servidor esta arriba (no espera Ollama/RVC)."""
    payload: dict = {
        "status": "ok",
        "whisper": WHISPER_MODEL,
        "tts_rvc": {
            "enabled": TTS_RVC_ENABLED,
            "configured": sing.singing_configured() if TTS_RVC_ENABLED else False,
            "applio": sing.tts_rvc_runtime_available() if TTS_RVC_ENABLED else False,
            "guide": os.environ.get("TTS_RVC_GUIDE", "edge"),
            "rvc_model": sing.RVC_MODEL_PATH or None,
        },
        "home_assistant": "enabled" if ha.ha_enabled() else "disabled",
    }
    try:
        async with httpx.AsyncClient(timeout=3.0) as client:
            r = await client.get(f"{OLLAMA_URL}/api/tags")
            r.raise_for_status()
        payload["ollama"] = "ok"
        payload["model"] = OLLAMA_MODEL
        if ha.ha_enabled():
            ha_devices = await asyncio.to_thread(ha.controllable_devices)
            payload["ha_devices"] = len(ha_devices)
    except Exception as e:
        payload["ollama"] = f"unavailable: {e}"
    return payload


@app.post("/chat")
async def chat(body: dict):
    text = body.get("text", "").strip()
    if not text:
        return JSONResponse(status_code=400, content={"error": "missing 'text'"})
    model_ov = (body.get("model") or "").strip() or None
    sysprompt_ov = (body.get("system_prompt") or "").strip() or None
    log.info("chat: model_ov=%s text=%s", model_ov, text)
    result = await ask_ollama(text, model_override=model_ov, system_prompt_override=sysprompt_ov)
    log.info(
        "reply [%s] speak=%s sing=%s sfx=%s: %s",
        result["emotion"],
        result["speak"],
        result["sing"],
        result.get("sound_effect", "none"),
        result["reply"],
    )
    return result


@app.post("/idle")
async def idle():
    """Spontaneous in-character remark (the board calls this after a while idle)."""
    result = await ask_ollama(srv_cfg.get_idle_user_prompt())
    log.info("idle remark [%s]: %s", result["emotion"], result["reply"])
    return result


_VALID_DEV_EMOTIONS = frozenset({
    "neutral", "happy", "sad", "angry", "surprised", "thinking", "sleepy",
    "love", "excited", "cool", "confused", "dizzy", "vibing",
})


@app.get("/dev", response_class=HTMLResponse)
async def dev_panel_page():
    """Panel para probar caras y TTS en el robot desde el PC."""
    if not DEV_PANEL_HTML.is_file():
        return HTMLResponse("<h1>dev_panel.html not found</h1>", status_code=404)
    return HTMLResponse(DEV_PANEL_HTML.read_text(encoding="utf-8"))


@app.get("/gestures-preview", response_class=HTMLResponse)
async def gestures_preview_page():
    """Vista previa estática de todos los gestos (referencia de diseño)."""
    if not GESTURES_PREVIEW_HTML.is_file():
        return HTMLResponse("<h1>gestures-preview.html not found</h1>", status_code=404)
    return HTMLResponse(GESTURES_PREVIEW_HTML.read_text(encoding="utf-8"))


@app.get("/static/face_preview.js")
async def face_preview_js():
    if not FACE_PREVIEW_JS.is_file():
        return JSONResponse(status_code=404, content={"error": "face_preview.js not found"})
    return FileResponse(FACE_PREVIEW_JS, media_type="application/javascript")


@app.get("/api/dev/poll")
async def dev_poll():
    """El ESP32 consulta cada ~2 s; devuelve el siguiente comando en cola o cmd=null."""
    async with _dev_lock:
        if not _dev_queue:
            return {"cmd": None}
        return {"cmd": _dev_queue.popleft()}


@app.post("/api/dev/face")
async def dev_face(request: Request):
    """Encola una emoción para que el robot la muestre (sin hablar)."""
    try:
        body = await request.json()
    except Exception:
        return JSONResponse(status_code=400, content={"error": "invalid json"})
    emotion = str(body.get("emotion", "neutral")).strip().lower()
    if emotion not in _VALID_DEV_EMOTIONS:
        return JSONResponse(status_code=400, content={"error": f"unknown emotion: {emotion}"})
    bored = bool(body.get("bored", False))
    hold_ms = max(1000, min(120000, int(body.get("hold_ms", 8000))))
    cmd = {"type": "face", "emotion": emotion, "bored": bored, "hold_ms": hold_ms}
    vmic = body.get("vibing_mic")
    if vmic is not None:
        vmic = max(50, min(300, int(vmic)))
        cmd["vibing_mic"] = vmic
    vflo = body.get("vibing_floor")
    if vflo is not None:
        cmd["vibing_floor"] = max(0, min(500, int(vflo)))
    vcei = body.get("vibing_ceil")
    if vcei is not None:
        cmd["vibing_ceil"] = max(200, min(900, int(vcei)))
    async with _dev_lock:
        _dev_queue.append(cmd)
        qlen = len(_dev_queue)
    log.info("dev face queued: %s bored=%s hold=%dms (queue=%d)", emotion, bored, hold_ms, qlen)
    return {"ok": True, "queued": qlen, "cmd": cmd}


@app.post("/api/dev/speak")
async def dev_speak(request: Request):
    """Encola texto para que el robot lo diga con TTS (y muestre la emoción)."""
    try:
        body = await request.json()
    except Exception:
        return JSONResponse(status_code=400, content={"error": "invalid json"})
    text = str(body.get("text", "")).strip()
    if not text:
        return JSONResponse(status_code=400, content={"error": "empty text"})
    if len(text) > 500:
        text = text[:497] + "..."
    emotion = str(body.get("emotion", "happy")).strip().lower()
    if emotion not in _VALID_DEV_EMOTIONS:
        emotion = "happy"
    cmd = {"type": "speak", "text": text, "emotion": emotion}
    async with _dev_lock:
        _dev_queue.append(cmd)
        qlen = len(_dev_queue)
    log.info("dev speak queued [%s]: %s (queue=%d)", emotion, text[:80], qlen)
    return {"ok": True, "queued": qlen, "cmd": cmd}


_story_cache: dict[str, dict[str, Any]] = {}
_STORY_CACHE_MAX = 8


def _story_cache_put(story_id: str, wav: bytes, timeline: list, title: str, duration_ms: int) -> None:
    while len(_story_cache) >= _STORY_CACHE_MAX:
        oldest = min(_story_cache.items(), key=lambda kv: kv[1].get("created", 0.0))[0]
        _story_cache.pop(oldest, None)
    _story_cache[story_id] = {
        "wav": wav,
        "timeline": timeline,
        "title": title,
        "duration_ms": duration_ms,
        "created": time.time(),
    }


@app.post("/api/dev/story")
async def dev_story(request: Request):
    """Genera audio TTS concatenado + timeline de emociones; encola reproducción en el ESP."""
    try:
        body = await request.json()
    except Exception:
        return JSONResponse(status_code=400, content={"error": "invalid json"})
    beats = body.get("beats")
    if not isinstance(beats, list) or not beats:
        return JSONResponse(status_code=400, content={"error": "beats vacío"})
    title = str(body.get("title", "Historia")).strip()[:120] or "Historia"
    gap_ms = max(0, min(2000, int(body.get("gap_ms", 350))))
    priority = bool(body.get("priority", True))
    try:
        wav, timeline, dur_ms = await story_mod.build_story_wav(
            beats, gap_ms=gap_ms, synth=_character_tts_wav)
    except ValueError as e:
        return JSONResponse(status_code=400, content={"error": str(e)})
    except Exception as e:
        log.exception("dev/story build failed")
        return JSONResponse(status_code=500, content={"error": str(e)})

    import uuid

    story_id = uuid.uuid4().hex[:12]
    _story_cache_put(story_id, wav, timeline, title, dur_ms)
    cmd: dict[str, Any] = {
        "type": "story",
        "story_id": story_id,
        "title": title,
        "timeline": timeline,
        "duration_ms": dur_ms,
    }
    async with _dev_lock:
        if priority:
            _dev_queue.appendleft(cmd)
        else:
            _dev_queue.append(cmd)
        qlen = len(_dev_queue)
    log.info(
        "dev story queued id=%s title=%r beats=%d dur=%dms (queue=%d)",
        story_id,
        title,
        len(beats),
        dur_ms,
        qlen,
    )
    return {
        "ok": True,
        "story_id": story_id,
        "title": title,
        "duration_ms": dur_ms,
        "timeline": timeline,
        "queued": qlen,
        "cmd": cmd,
    }


async def _story_stream_response(story_id: str):
    entry = _story_cache.get(story_id)
    if not entry:
        return JSONResponse(status_code=404, content={"error": "story not found"})
    wav = entry["wav"]
    if len(wav) <= 44:
        return JSONResponse(status_code=503, content={"error": "story wav vacío"})
    pcm = wav[44:]
    title = str(entry.get("title") or story_id)
    log.info("story/play id=%s title=%r bytes=%d", story_id, title, len(pcm))
    # Usamos Response (no StreamingResponse) para que FastAPI incluya Content-Length
    # automáticamente. Esto evita chunked transfer encoding en HTTP/1.1, que corrompía
    # el stream PCM cuando el ESP leía bytes raw sin decodificar los chunk headers.
    return Response(
        content=pcm,
        media_type="application/octet-stream",
        headers={
            "Cache-Control": "no-store",
            "X-Story-Title": title[:120],
            "X-Story-Duration-Ms": str(entry.get("duration_ms", 0)),
        },
    )


@app.get("/story/play")
async def story_play_esp(id: str = Query("", alias="id")):
    """PCM 16 kHz mono en streaming (modo historia / guion de video)."""
    story_id = (id or "").strip()
    if not story_id:
        return JSONResponse(status_code=400, content={"error": "missing id"})
    return await _story_stream_response(story_id)


@app.get("/api/dev/status")
async def dev_status():
    async with _dev_lock:
        pending = list(_dev_queue)
    return {"ok": True, "pending": len(pending), "queue": pending}


@app.post("/api/dev/clear")
async def dev_clear():
    async with _dev_lock:
        n = len(_dev_queue)
        _dev_queue.clear()
    return {"ok": True, "cleared": n}


_NOTIFY_KIND_EMOTIONS: dict[str, str] = {
    "agent_blocked": "thinking",
    "ask_question": "confused",
    "subagent_done": "happy",
    "ci_failed": "sad",
    "approval_needed": "surprised",
    "stop_failure": "sad",
    "elicitation": "confused",
    "task_completed": "happy",
    "agent": "thinking",
}
_notify_last: dict[str, float] = {}
_NOTIFY_COOLDOWN_S = 45.0


@app.post("/api/dev/notify")
async def dev_notify(request: Request):
    """Aviso al robot (cara + TTS) cuando un agente de Cursor/Claude necesita intervención."""
    try:
        body = await request.json()
    except Exception:
        return JSONResponse(status_code=400, content={"error": "invalid json"})
    kind = str(body.get("kind", "agent")).strip().lower()[:64] or "agent"
    context = body.get("context") if isinstance(body.get("context"), dict) else {}
    message = str(body.get("message", "")).strip()
    generated = None
    if not message:
        generated = srv_cfg.notify_reply(kind, context=context)
        message = generated["reply"]
    if not message:
        return JSONResponse(status_code=400, content={"error": "empty message"})
    message = cap_speech_text(prepare_spanish_text(message), sing=False)
    emotion = str(body.get("emotion", "")).strip().lower()
    if not emotion or emotion not in _VALID_DEV_EMOTIONS:
        if generated:
            emotion = generated["emotion"]
        else:
            emotion = _NOTIFY_KIND_EMOTIONS.get(kind, "thinking")
    if emotion not in _VALID_DEV_EMOTIONS:
        emotion = "thinking"
    speak = bool(body.get("speak", True))
    priority = bool(body.get("priority", kind in (
        "agent_blocked", "approval_needed", "ask_question", "stop_failure", "elicitation",
    )))
    pid = srv_cfg.current_personality_id()
    dedupe_key = str(body.get("dedupe_key", f"{kind}:{pid}"))
    now = time.time()
    last = _notify_last.get(dedupe_key, 0.0)
    if now - last < _NOTIFY_COOLDOWN_S:
        return {"ok": True, "skipped": True, "reason": "cooldown", "kind": kind}
    _notify_last[dedupe_key] = now

    cmd: dict[str, Any] = {"type": "speak", "text": message, "emotion": emotion} if speak else {
        "type": "face",
        "emotion": emotion,
        "hold_ms": max(3000, min(120000, int(body.get("hold_ms", 15000)))),
    }
    async with _dev_lock:
        if priority:
            _dev_queue.appendleft(cmd)
        else:
            _dev_queue.append(cmd)
        qlen = len(_dev_queue)
    log.info(
        "dev notify [%s] personality=%s %s speak=%s priority=%s (queue=%d)",
        kind, pid, message[:80], speak, priority, qlen,
    )
    return {"ok": True, "queued": qlen, "kind": kind, "personality": pid, "cmd": cmd}


@app.post("/converse/reset")
async def converse_reset():
    _conversation_history.clear()
    log.info("conversation history cleared")
    return {"status": "ok", "history_len": 0}


@app.post("/wake-check")
async def wake_check(request: Request):
    wav_bytes = await request.body()
    if len(wav_bytes) < 400:
        return {"wake": False, "heard": ""}
    phrase = request.headers.get("X-Wake-Phrase", "")
    dur, peak, rms = _wav_metrics(wav_bytes)
    if not wav_has_speech(wav_bytes, min_peak=MIN_WAKE_PEAK, min_rms=MIN_WAKE_RMS):
        log.info(
            "wake-check rejected quiet audio dur=%.1fs peak=%d rms=%.0f (need peak>=%d rms>=%d)",
            dur,
            peak,
            rms,
            MIN_WAKE_PEAK,
            MIN_WAKE_RMS,
        )
        return {"wake": False, "heard": "", "command": ""}

    # Sin prompt sesgado a "hola asistente": en silencio Whisper alucinaba la wake phrase.
    text = await transcribe_wav(
        wav_bytes,
        language="es",
        initial_prompt="Español.",
        beam_size=2,
    )
    wake = is_wake_phrase(text, phrase)
    command = strip_wake_phrase(text, phrase) if wake else ""
    if wake and len(command) < 3:
        command = ""
    log.info("wake-check heard='%s' phrase='%s' wake=%s command='%s'", text, phrase, wake, command)
    return {"wake": wake, "heard": text, "command": command}


SING_LYRICS_PROMPT = (
    "Escribe SOLO la letra de una canción en español para cantar en voz alta.\n"
    "Requisitos: 6 a 8 líneas; cada línea 5-10 palabras; rimas o ritmo claros; "
    "tono alegre robot tierno-sarcástico; sin título ni explicación; "
    "cada línea en una línea nueva."
)

SING_LYRICS_MIN_CHARS = int(os.environ.get("SING_LYRICS_MIN_CHARS", "140"))


async def _ollama_plain_text(prompt: str, *, timeout: float | None = None) -> str:
    messages = [
        {
            "role": "system",
            "content": "Respondes únicamente con el texto pedido, sin markdown ni JSON.",
        },
        {"role": "user", "content": prompt},
    ]
    payload = _ollama_chat_payload(messages, json_format=False)
    read_timeout = timeout if timeout is not None else OLLAMA_TIMEOUT_S
    http_timeout = httpx.Timeout(connect=5.0, read=read_timeout, write=15.0, pool=5.0)
    client = httpx.AsyncClient(timeout=http_timeout)
    try:
        r = await client.post(f"{OLLAMA_URL}/api/chat", json=payload)
        r.raise_for_status()
        text = r.json()["message"]["content"].strip()
    finally:
        await client.aclose()
    if text.startswith("```"):
        text = re.sub(r"^```(?:\w+)?\s*", "", text)
        text = re.sub(r"\s*```\s*$", "", text)
    return text.strip()


def _lyrics_line_count(text: str) -> int:
    parts = re.split(r"[\n.!?;]+", text)
    return len([p for p in parts if len(p.strip()) >= 4])


async def expand_sing_lyrics(lyrics: str, topic: str = "") -> str:
    """Ensure enough lines/chars for a ~25-40s sung guide track."""
    lyrics = lyrics.strip()
    if len(lyrics) >= SING_LYRICS_MIN_CHARS and _lyrics_line_count(lyrics) >= 5:
        return lyrics
    seed = (topic or lyrics or "robots y humanos").strip()
    prompt = f"{SING_LYRICS_PROMPT}\n\nTema: {seed}"
    if lyrics:
        prompt += f"\nIdea inicial (expándela, no la dejes en una sola frase): {lyrics}"
    try:
        expanded = await _ollama_plain_text(prompt)
        if len(expanded) > len(lyrics) and _lyrics_line_count(expanded) >= 3:
            log.info("sing lyrics expanded %d -> %d chars", len(lyrics), len(expanded))
            return expanded
    except Exception as e:
        log.warning("sing lyrics expansion failed: %s", e)
    return lyrics


async def _resolve_sing_lyrics(body: dict) -> tuple[str, str]:
    """Return (lyrics, emotion). Generates via Ollama when only topic is given."""
    lyrics = (body.get("lyrics") or body.get("text") or "").strip()
    topic = (body.get("topic") or "").strip()
    emotion = (body.get("emotion") or "happy").strip().lower()
    if emotion not in EMOTIONS:
        emotion = "happy"

    if not lyrics and topic:
        result = await ask_ollama(f"{SING_LYRICS_PROMPT}\nTema: {topic}")
        lyrics = (result.get("reply") or "").strip()
        if result.get("emotion") in EMOTIONS:
            emotion = result["emotion"]

    if lyrics and body.get("expand", True):
        lyrics = await expand_sing_lyrics(lyrics, topic or lyrics)

    return lyrics, emotion


SERVER_DIR = Path(__file__).resolve().parent
REPO_DIR = SERVER_DIR.parent
SING_TEST_HTML = SERVER_DIR / "sing_test.html"
ADMIN_HTML = SERVER_DIR / "admin.html"
DEV_PANEL_HTML = SERVER_DIR / "dev_panel.html"
FACE_PREVIEW_JS = SERVER_DIR / "face_preview.js"
GESTURES_PREVIEW_HTML = REPO_DIR / "firmware" / "agente-ia" / "gestures-preview.html"

# Cola de comandos de prueba (cara / hablar) que el ESP consume con GET /api/dev/poll.
_dev_queue: deque[dict[str, Any]] = deque()
_dev_lock = asyncio.Lock()
_DEBUG_AUDIO_NAMES = frozenset(
    {
        "last_sing_guide.wav",
        "last_sing_rvc.wav",
        "last_sing_esp32.wav",
        "last_converse.wav",
    }
)


async def _render_agent_sing_wav(body: dict) -> tuple[str, str, bytes]:
    """Shared sing pipeline for ESP stream and browser preview."""
    lyrics, emotion = await _resolve_sing_lyrics(body)
    if not lyrics:
        raise ValueError("missing 'lyrics'/'text' or 'topic'")

    if not sing.singing_configured():
        raise sing.SingingNotConfigured(
            "RVC_MODEL_PATH missing — set RVC_MODEL_PATH (and RVC_INDEX_PATH)"
        )
    if not sing.rvc_runtime_available() and not body.get("skip_rvc"):
        raise sing.SingingDependencyError(
            "RVC runtime missing — run server/install-rvc.ps1 (Python 3.11 venv)"
        )

    f0_up_key = int(body.get("f0_up_key", body.get("pitch_shift", 0)))
    index_rate = body.get("index_rate")
    index_rate_f = float(index_rate) if index_rate is not None else None
    skip_rvc = bool(body.get("skip_rvc", False))

    wav_bytes = await sing.render_singing_wav(
        lyrics,
        f0_up_key=f0_up_key,
        index_rate=index_rate_f,
        skip_rvc=skip_rvc,
    )
    return lyrics, emotion, wav_bytes


@app.get("/admin", response_class=HTMLResponse)
async def admin_page():
    """Panel de administración: personalidad, TTS, enlace al ESP32."""
    if not ADMIN_HTML.is_file():
        return HTMLResponse("<h1>admin.html not found</h1>", status_code=404)
    return HTMLResponse(ADMIN_HTML.read_text(encoding="utf-8"))


@app.get("/api/context")
async def get_context_endpoint():
    """Returns the active behavior context and its config (M4)."""
    return {
        "context": context_manager.get(),
        "config": context_manager.config(),
        "available": context_manager.all_contexts(),
    }


@app.post("/api/context")
async def set_context_endpoint(request: Request):
    """Set the active behavior context (M4). Affects expressivity, rhythm, and LLM tone."""
    try:
        body = await request.json()
    except Exception:
        return JSONResponse(status_code=400, content={"error": "invalid json"})
    name = str(body.get("context", "")).strip().lower()
    if name not in context_manager.CONTEXT_CONFIG:
        return JSONResponse(status_code=400, content={
            "error": f"unknown context: {name!r}",
            "available": context_manager.all_contexts(),
        })
    result = context_manager.set_context(name)
    _memory.log("context_switch", severity=0.3)
    log.info("context -> %s", result)
    return {"context": result, "config": context_manager.config(result)}


@app.get("/api/memory/snapshot")
async def memory_snapshot():
    """Debug endpoint — recent emotional memory events (M7)."""
    return {"events": _memory.snapshot(), "modifiers": _memory.get_modifiers()}


@app.get("/api/admin/rvc-models")
async def admin_rvc_models():
    """Lista modelos RVC desde bender_server (:7860/models) para el panel admin."""
    url = sing.BENDER_SERVER_URL.rstrip("/") + "/models"
    try:
        async with httpx.AsyncClient(timeout=httpx.Timeout(5.0)) as client:
            r = await client.get(url)
            r.raise_for_status()
            models = r.json()
        return {
            "models": models,
            "bender_server_url": sing.BENDER_SERVER_URL,
            "ok": True,
        }
    except Exception as e:
        log.warning("rvc-models fetch failed (%s): %s", url, e)
        return JSONResponse(
            status_code=503,
            content={
                "ok": False,
                "error": str(e),
                "models": [],
                "bender_server_url": sing.BENDER_SERVER_URL,
            },
        )


def _esp_device_url(ip: str) -> str:
    ip = (ip or "").strip().rstrip("/")
    if not ip:
        raise ValueError("missing ip")
    if "://" not in ip:
        ip = "http://" + ip
    return ip.rstrip("/")


_ESP_TIMEOUT = httpx.Timeout(connect=2.0, read=3.0, write=3.0, pool=3.0)


@app.get("/api/admin/devices")
async def admin_devices():
    """ESP32 vistos por el cerebro (última petición, IP, MAC), más reciente primero."""
    return {"ok": True, "devices": esp_reg.list_devices()}


@app.get("/api/admin/device/settings")
async def admin_device_settings_get(ip: str = Query("")):
    """Proxy: lee /api/settings del ESP32 (volumen, etc.) evitando CORS del navegador."""
    try:
        base = _esp_device_url(ip)
    except ValueError:
        return JSONResponse(status_code=400, content={"error": "missing ip"})
    try:
        async with httpx.AsyncClient(timeout=_ESP_TIMEOUT) as client:
            r = await client.get(f"{base}/api/settings")
            r.raise_for_status()
            return r.json()
    except Exception as e:
        log.warning("device settings GET failed (%s): %s", ip, e)
        return JSONResponse(status_code=502, content={"error": str(e)})


@app.post("/api/admin/device/settings")
async def admin_device_settings_post(request: Request):
    """Proxy: escribe ajustes en el ESP32 (p. ej. volumen)."""
    try:
        body = await request.json()
    except Exception:
        return JSONResponse(status_code=400, content={"error": "invalid json"})
    ip = str(body.get("ip", "")).strip()
    try:
        base = _esp_device_url(ip)
    except ValueError:
        return JSONResponse(status_code=400, content={"error": "missing ip"})
    payload: dict[str, Any] = {}
    if "volume" in body:
        v = int(body["volume"])
        payload["volume"] = max(0, min(100, v))
    if not payload:
        return JSONResponse(status_code=400, content={"error": "nothing to update"})
    try:
        async with httpx.AsyncClient(timeout=httpx.Timeout(8.0)) as client:
            r = await client.post(
                f"{base}/api/settings",
                json=payload,
                headers={"Content-Type": "application/json"},
            )
            r.raise_for_status()
            return r.json()
    except Exception as e:
        log.warning("device settings POST failed (%s): %s", ip, e)
        return JSONResponse(status_code=502, content={"error": str(e)})


@app.post("/api/admin/device/dev/face")
async def admin_device_dev_face(request: Request):
    """Proxy: envía gesto al ESP32 directamente (sin cola del cerebro)."""
    try:
        body = await request.json()
    except Exception:
        return JSONResponse(status_code=400, content={"error": "invalid json"})
    ip = str(body.get("ip", "")).strip()
    emotion = str(body.get("emotion", "neutral")).strip().lower()
    if emotion not in _VALID_DEV_EMOTIONS:
        return JSONResponse(status_code=400, content={"error": f"unknown emotion: {emotion}"})
    try:
        base = _esp_device_url(ip)
    except ValueError:
        return JSONResponse(status_code=400, content={"error": "missing ip"})
    payload = {
        "emotion": emotion,
        "bored": bool(body.get("bored", False)),
        "hold_ms": max(1000, min(120000, int(body.get("hold_ms", 8000)))),
    }
    if body.get("vibing_mic") is not None:
        payload["vibing_mic"] = max(50, min(300, int(body.get("vibing_mic"))))
    if body.get("vibing_floor") is not None:
        payload["vibing_floor"] = max(0, min(500, int(body.get("vibing_floor"))))
    if body.get("vibing_ceil") is not None:
        payload["vibing_ceil"] = max(200, min(900, int(body.get("vibing_ceil"))))
    try:
        async with httpx.AsyncClient(timeout=_ESP_TIMEOUT) as client:
            r = await client.post(f"{base}/api/dev/face", json=payload)
            r.raise_for_status()
            return r.json()
    except Exception as e:
        log.warning("device dev/face failed (%s): %s", ip, e)
        return JSONResponse(status_code=502, content={"error": str(e)})


@app.post("/api/admin/device/dev/speak")
async def admin_device_dev_speak(request: Request):
    """Proxy: TTS de prueba directo en el ESP32."""
    try:
        body = await request.json()
    except Exception:
        return JSONResponse(status_code=400, content={"error": "invalid json"})
    ip = str(body.get("ip", "")).strip()
    text = str(body.get("text", "")).strip()
    if not text:
        return JSONResponse(status_code=400, content={"error": "empty text"})
    if len(text) > 500:
        text = text[:497] + "..."
    emotion = str(body.get("emotion", "happy")).strip().lower()
    if emotion not in _VALID_DEV_EMOTIONS:
        emotion = "happy"
    try:
        base = _esp_device_url(ip)
    except ValueError:
        return JSONResponse(status_code=400, content={"error": "missing ip"})
    payload = {"text": text, "emotion": emotion}
    try:
        async with httpx.AsyncClient(timeout=httpx.Timeout(connect=3.0, read=120.0, write=10.0, pool=5.0)) as client:
            r = await client.post(f"{base}/api/dev/speak", json=payload)
            r.raise_for_status()
            return r.json()
    except Exception as e:
        log.warning("device dev/speak failed (%s): %s", ip, e)
        return JSONResponse(status_code=502, content={"error": str(e)})


@app.get("/api/admin/config")
async def admin_config_get():
    env = {
        "ollama_model": OLLAMA_MODEL,
        "tts_engine": os.environ.get("TTS_ENGINE", "sapi"),
        "edge_voice": os.environ.get("EDGE_TTS_VOICE", "es-MX-DaliaNeural"),
        "singing_enabled": SINGING_ENABLED,
        "ha_enabled": ha.ha_enabled(),
    }
    return srv_cfg.admin_snapshot(env)


@app.post("/api/admin/config")
async def admin_config_post(request: Request):
    try:
        body = await request.json()
    except Exception:
        return JSONResponse(status_code=400, content={"error": "invalid json"})
    allowed = {
        "personality", "custom_prompt", "ollama_model", "tts_engine", "edge_voice",
        "bender_pitch", "bender_index_rate", "bender_protect", "rvc_voice_model",
        "personality_prompts",
    }
    updates = {k: body[k] for k in allowed if k in body}
    cfg = srv_cfg.save(updates)
    return {**srv_cfg.admin_snapshot({
        "ollama_model": OLLAMA_MODEL,
        "tts_engine": os.environ.get("TTS_ENGINE", "sapi"),
        "edge_voice": os.environ.get("EDGE_TTS_VOICE", "es-MX-DaliaNeural"),
        "singing_enabled": SINGING_ENABLED,
        "ha_enabled": ha.ha_enabled(),
    }), "ok": True, "saved": cfg}


@app.post("/api/admin/reset-memory")
async def admin_reset_memory():
    path = agent_state._PATH
    try:
        if path.is_file():
            path.unlink()
        return {"ok": True}
    except Exception as e:
        return JSONResponse(status_code=500, content={"error": str(e)})


@app.get("/api/admin/music/status")
async def admin_music_status():
    """Estado YT Music: cuenta (cookies) vs anónimo, deps."""
    return await asyncio.to_thread(music.playback_ready, light=True)


@app.post("/api/admin/music/search")
async def admin_music_search(request: Request):
    try:
        body = await request.json()
    except Exception:
        return JSONResponse(status_code=400, content={"error": "invalid json"})
    query = str(body.get("query", "")).strip()
    limit = body.get("limit")
    try:
        lim = int(limit) if limit is not None else None
    except (TypeError, ValueError):
        lim = None
    try:
        return await asyncio.wait_for(
            asyncio.to_thread(music.search, query, limit=lim),
            timeout=20.0,
        )
    except ValueError as e:
        return JSONResponse(status_code=400, content={"error": str(e)})
    except (TimeoutError, asyncio.TimeoutError):
        log.warning("music search timeout query=%r", query)
        return JSONResponse(status_code=504, content={"error": "timeout buscando — reintentá"})
    except RuntimeError as e:
        return JSONResponse(status_code=503, content={"error": str(e)})
    except Exception as e:
        log.exception("music search failed")
        return JSONResponse(status_code=500, content={"error": str(e)})


@app.get("/api/admin/music/audio")
async def admin_music_audio(video_id: str = Query("", alias="id")):
    """Extrae audio MP3 de un video_id (YouTube / YT Music)."""
    vid = (video_id or "").strip()
    if not vid:
        return JSONResponse(status_code=400, content={"error": "missing id"})
    try:
        data, meta = await asyncio.to_thread(music.extract_audio_mp3, vid)
    except ValueError as e:
        return JSONResponse(status_code=400, content={"error": str(e)})
    except RuntimeError as e:
        return JSONResponse(status_code=503, content={"error": str(e)})
    except Exception as e:
        log.exception("music audio failed id=%s", vid)
        return JSONResponse(status_code=500, content={"error": str(e)})
    title = (meta.get("title") or vid).replace('"', "'")[:120]
    return Response(
        content=data,
        media_type="audio/mpeg",
        headers={
            "Content-Length": str(len(data)),
            "Cache-Control": "no-store",
            "Content-Disposition": f'inline; filename="{vid}.mp3"',
            "X-Track-Title": title,
            "X-Track-Artist": (meta.get("artist") or "")[:120],
        },
    )


@app.post("/api/admin/music/cookies")
async def admin_music_cookies_upload(file: UploadFile = File(...)):
    """Sube cookies.txt (Netscape) exportadas del navegador con sesión YT Music."""
    try:
        raw = await file.read()
        if not raw:
            raise ValueError("archivo vacío")
        await asyncio.to_thread(music.save_cookies, raw)
        st = await asyncio.to_thread(music.auth_status, light=True)
        return {**st, "ok": True}
    except ValueError as e:
        return JSONResponse(status_code=400, content={"error": str(e)})
    except Exception as e:
        log.exception("music cookies upload failed")
        return JSONResponse(status_code=500, content={"error": str(e)})


@app.delete("/api/admin/music/cookies")
async def admin_music_cookies_delete():
    """Cierra sesión YT Music (borra cookies locales)."""
    removed = music.delete_cookies()
    st = await asyncio.to_thread(music.auth_status, light=True)
    return {**st, "ok": True, "removed": removed}


@app.get("/music/prefetch/status")
async def music_prefetch_status(id: str = Query("", alias="id")):
    """El ESP consulta si el audio ya está listo antes de GET /music/play."""
    video_id = (id or "").strip()
    if not video_id:
        return JSONResponse(status_code=400, content={"error": "missing id"})
    st = await asyncio.to_thread(music.prefetch_status, video_id)
    if st.get("status") == "none":
        music.prefetch_pcm_stream(video_id)
        return JSONResponse(status_code=202, content={"status": "loading", "ready": False})
    if st.get("ready"):
        return st
    if st.get("status") == "error":
        return JSONResponse(status_code=503, content=st)
    return JSONResponse(status_code=202, content=st)


@app.get("/music/play")
async def music_play_esp_get(request: Request, id: str = Query("", alias="id")):
    """PCM 16 kHz mono en streaming (GET — el ESP no lee bien POST chunked)."""
    video_id = (id or "").strip()
    if not video_id:
        return JSONResponse(status_code=400, content={"error": "missing id"})
    return await _music_stream_response(video_id, request)


@app.post("/music/play")
async def music_play_esp(request: Request):
    """PCM 16 kHz mono en streaming (POST legacy)."""
    try:
        body = await request.json()
    except Exception:
        return JSONResponse(status_code=400, content={"error": "invalid json"})
    video_id = str(body.get("video_id") or body.get("id") or "").strip()
    if not video_id:
        return JSONResponse(status_code=400, content={"error": "missing video_id"})
    return await _music_stream_response(video_id, request)


async def _music_stream_response(video_id: str, request: Request | None = None):
    t0 = time.monotonic()
    open_timeout = float(os.environ.get("YTMUSIC_ESP_OPEN_TIMEOUT", "180"))
    if music.skip_pytubefix():
        open_timeout = max(open_timeout, 180.0)
    pace_pcm = os.environ.get("YTMUSIC_PCM_PACE", "1").strip().lower() in ("1", "true", "yes")
    pcm_bytes_per_sec = 16000 * 2
    pcm_pace_step = int(os.environ.get("YTMUSIC_PCM_PACE_STEP", "2048"))
    pace_next = time.monotonic()

    async def _yield_pcm(data: bytes):
        nonlocal pace_next
        if not data:
            return
        if not pace_pcm:
            yield data
            return
        off = 0
        while off < len(data):
            piece = data[off : off + pcm_pace_step]
            off += len(piece)
            now = time.monotonic()
            if now < pace_next:
                await asyncio.sleep(pace_next - now)
            yield piece
            pace_next += len(piece) / pcm_bytes_per_sec

    try:
        stream_it, first_chunk = await asyncio.wait_for(
            asyncio.to_thread(music.open_pcm_stream, video_id),
            timeout=open_timeout,
        )
    except TimeoutError:
        music.abort_client_open(video_id)
        log.warning("music/play open timeout id=%s %.0fs", video_id, open_timeout)
        return JSONResponse(
            status_code=503,
            content={"error": f"timeout ({open_timeout:.0f}s) — probá otra canción o subí cookies YT"},
        )
    except asyncio.CancelledError:
        music.abort_client_open(video_id)
        raise
    except Exception as e:
        music.abort_client_open(video_id)
        log.warning("music/play open failed id=%s: %s", video_id, e)
        return JSONResponse(status_code=503, content={"error": str(e)})

    try:
        meta = await asyncio.to_thread(music.probe_track_meta, video_id)
    except Exception as e:
        log.warning("music/play probe skipped id=%s: %s", video_id, e)
        meta = {"title": video_id, "artist": ""}

    log.info(
        "music/play stream ready id=%s title=%r first=%d bytes open=%.1fs",
        video_id,
        meta.get("title"),
        len(first_chunk),
        time.monotonic() - t0,
    )

    # Acumulamos todo el PCM antes de responder para que FastAPI pueda incluir
    # Content-Length automáticamente. Esto evita chunked transfer encoding en
    # HTTP/1.1, que corrompía el stream PCM en el ESP (los chunk headers se
    # interpretaban como muestras de audio → golpe rítmico cada 16 KB).
    # PCM 16 kHz mono 16-bit ≈ 2 MB/min — manejable en RAM del servidor.
    chunks: list[bytes] = [first_chunk]
    try:
        while True:
            chunk = await asyncio.to_thread(music._next_chunk, stream_it)
            if chunk is None:
                break
            chunks.append(chunk)
    finally:
        music.shutdown_all_streams()

    pcm_all = b"".join(chunks)
    log.info(
        "music/play ready id=%s %.1fs %d bytes",
        video_id,
        time.monotonic() - t0,
        len(pcm_all),
    )
    return Response(
        content=pcm_all,
        media_type="application/octet-stream",
        headers={
            "Cache-Control": "no-store",
            "X-Audio-Format": "pcm_s16le;rate=16000;channels=1",
            "X-Track-Title": (meta.get("title") or "")[:120],
            "X-Track-Artist": (meta.get("artist") or "")[:120],
        },
    )


@app.post("/api/admin/device/music/play")
async def admin_device_music_play(request: Request):
    """Encola reproducción en el ESP (proxy a /api/music/play del firmware)."""
    try:
        body = await request.json()
    except Exception:
        return JSONResponse(status_code=400, content={"error": "invalid json"})
    ip = str(body.get("ip", "")).strip()
    video_id = str(body.get("video_id") or body.get("id") or "").strip()
    title = str(body.get("title", "")).strip()
    if not ip:
        return JSONResponse(status_code=400, content={"error": "missing ip"})
    if not video_id:
        return JSONResponse(status_code=400, content={"error": "missing video_id"})
    try:
        base = _esp_device_url(ip)
    except ValueError:
        return JSONResponse(status_code=400, content={"error": "missing ip"})
    music.prefetch_pcm_stream(video_id)
    wait_sec = float(os.environ.get("YTMUSIC_PREFETCH_WAIT_SEC", "180"))
    try:
        await asyncio.wait_for(
            asyncio.to_thread(music.wait_prefetch_ready, video_id, timeout_sec=wait_sec),
            timeout=wait_sec + 15.0,
        )
        log.info("prefetch listo -> ESP id=%s", video_id)
    except (TimeoutError, asyncio.TimeoutError):
        music.cancel_prefetch_kill(video_id)
        log.warning("prefetch wait timeout id=%s %.0fs", video_id, wait_sec)
        return JSONResponse(
            status_code=503,
            content={
                "error": (
                    f"El audio no estuvo listo en {wait_sec:.0f}s. "
                    "Reiniciá el servidor (EJS warm) o probá otra canción."
                ),
                "ip": ip,
                "video_id": video_id,
            },
        )
    except RuntimeError as e:
        music.cancel_prefetch_kill(video_id)
        log.warning("prefetch failed id=%s: %s", video_id, e)
        return JSONResponse(
            status_code=503,
            content={"error": str(e), "ip": ip, "video_id": video_id},
        )
    payload = {"video_id": video_id, "title": title}
    target = f"{base}/api/music/play"
    log.info("device music play -> ESP ip=%s id=%s", ip, video_id)
    try:
        async with httpx.AsyncClient(timeout=httpx.Timeout(15.0)) as client:
            r = await client.post(
                target,
                json=payload,
                headers={"Content-Type": "application/json"},
            )
            if r.status_code >= 400:
                body_preview = (r.text or "")[:240]
                log.warning(
                    "device music play HTTP %s %s: %s",
                    ip,
                    r.status_code,
                    body_preview,
                )
                if r.status_code == 404:
                    return JSONResponse(
                        status_code=502,
                        content={
                            "error": (
                                "El ESP no tiene /api/music/play. "
                                "Flasheá el firmware agente-ia más reciente."
                            ),
                            "ip": ip,
                            "status": r.status_code,
                        },
                    )
                return JSONResponse(
                    status_code=502,
                    content={
                        "error": f"ESP respondió {r.status_code}",
                        "ip": ip,
                        "detail": body_preview or None,
                    },
                )
            try:
                data = r.json()
            except Exception:
                data = {"ok": True, "queued": r.status_code in (200, 202)}
            return {**data, "prefetch": "started", "ip": ip, "video_id": video_id}
    except httpx.ConnectError as e:
        log.warning("device music play connect failed (%s): %s", ip, e)
        return JSONResponse(
            status_code=502,
            content={
                "error": (
                    f"No se pudo conectar al ESP en {ip}. "
                    "Revisá la IP en Dispositivo y que la placa esté en la misma red WiFi."
                ),
                "ip": ip,
                "detail": str(e),
            },
        )
    except httpx.TimeoutException as e:
        log.warning("device music play timeout (%s): %s", ip, e)
        return JSONResponse(
            status_code=502,
            content={
                "error": (
                    f"Timeout al contactar el ESP en {ip}. "
                    "¿Está ocupado o el web admin no responde?"
                ),
                "ip": ip,
                "detail": str(e),
            },
        )
    except Exception as e:
        log.warning("device music play failed (%s): %s", ip, e)
        return JSONResponse(
            status_code=502,
            content={"error": str(e), "ip": ip, "target": target},
        )


@app.get("/api/admin/device/music/status")
async def admin_device_music_status(ip: str = Query("")):
    """Estado de reproducción musical en el ESP."""
    try:
        base = _esp_device_url(ip)
    except ValueError:
        return JSONResponse(status_code=400, content={"error": "missing ip"})
    try:
        async with httpx.AsyncClient(timeout=_ESP_TIMEOUT) as client:
            r = await client.get(f"{base}/api/music/status")
            r.raise_for_status()
            return r.json()
    except httpx.TimeoutException:
        return {"playing": False, "esp_busy": True, "error": "timeout ESP"}
    except Exception as e:
        log.warning("device music status failed (%s): %s", ip, e)
        return JSONResponse(status_code=502, content={"error": str(e), "playing": False})


@app.post("/api/admin/device/music/stop")
async def admin_device_music_stop(request: Request):
    """Detiene reproducción musical en el ESP y cancela prefetch en el servidor."""
    try:
        body = await request.json()
    except Exception:
        return JSONResponse(status_code=400, content={"error": "invalid json"})
    ip = str(body.get("ip", "")).strip()
    video_id = str(body.get("video_id") or body.get("id") or "").strip()
    if not ip:
        return JSONResponse(status_code=400, content={"error": "missing ip"})
    if video_id:
        music.cancel_prefetch_kill(video_id)
    else:
        music.cancel_prefetch_kill()
    try:
        base = _esp_device_url(ip)
    except ValueError:
        return JSONResponse(status_code=400, content={"error": "missing ip"})
    try:
        async with httpx.AsyncClient(timeout=httpx.Timeout(8.0)) as client:
            r = await client.post(f"{base}/api/music/stop", json={})
            r.raise_for_status()
            try:
                return r.json()
            except Exception:
                return {"ok": True, "stop": True}
    except Exception as e:
        log.warning("device music stop failed (%s): %s", ip, e)
        return JSONResponse(status_code=502, content={"error": str(e), "ip": ip})


@app.get("/sing-test", response_class=HTMLResponse)
async def sing_test_page():
    """Browser UI to test /agent/sing without the ESP32."""
    if not SINGING_ENABLED:
        return HTMLResponse(
            "<h1>Canto desactivado</h1><p>ENABLE_SINGING=0 — solo TTS. "
            "Reinicia con <code>$env:ENABLE_SINGING=\"1\"</code> si lo necesitas.</p>",
            status_code=503,
        )
    if not SING_TEST_HTML.is_file():
        return HTMLResponse("<h1>sing_test.html not found</h1>", status_code=404)
    return HTMLResponse(SING_TEST_HTML.read_text(encoding="utf-8"))


@app.get("/debug_audio/{filename}")
async def debug_audio_file(filename: str):
    if filename not in _DEBUG_AUDIO_NAMES:
        return JSONResponse(status_code=404, content={"error": "unknown file"})
    path = SERVER_DIR / "debug_audio" / filename
    if not path.is_file():
        return JSONResponse(status_code=404, content={"error": "not generated yet — run Cantar first"})
    return FileResponse(path, media_type="audio/wav", filename=filename)


@app.post("/agent/sing/preview-guide")
async def agent_sing_preview_guide(
    guide: UploadFile = File(..., description="Vocal guide WAV (mono/stereo, any SR)"),
    f0_up_key: int = Form(0),
    index_rate: float = Form(0.75),
):
    """RVC only with uploaded guide — same workflow as RVC WebUI / Discord demos."""
    if not SINGING_ENABLED:
        return JSONResponse(status_code=503, content={"error": "singing disabled (ENABLE_SINGING=0)"})
    if not sing.singing_configured():
        return JSONResponse(
            status_code=503,
            content={"error": "RVC_MODEL_PATH missing"},
        )
    if not sing.rvc_runtime_available():
        return JSONResponse(
            status_code=503,
            content={"error": "RVC runtime missing — install-rvc.ps1"},
        )
    try:
        raw = await guide.read()
        if not raw or not raw.startswith(b"RIFF"):
            return JSONResponse(status_code=400, content={"error": "invalid WAV file"})
        wav_bytes = await sing.render_rvc_from_guide_wav(
            raw,
            f0_up_key=f0_up_key,
            index_rate=index_rate,
        )
    except Exception as e:
        log.exception("agent/sing/preview-guide failed")
        return JSONResponse(status_code=503, content={"error": str(e)[-800:]})

    peak = 0
    try:
        with wave.open(io.BytesIO(wav_bytes), "rb") as wf:
            pcm = wf.readframes(wf.getnframes())
            peak = max(abs(int.from_bytes(pcm[i : i + 2], "little", signed=True)) for i in range(0, len(pcm), 2)) if pcm else 0
            dur_s = wf.getnframes() / float(wf.getframerate())
    except Exception:
        dur_s = 0.0

    return Response(
        content=wav_bytes,
        media_type="audio/wav",
        headers={
            "X-Sing-Duration-S": f"{dur_s:.2f}",
            "X-Sing-Peak": str(peak),
            "X-Sing-Mode": "guide-upload",
        },
    )


@app.post("/agent/sing/preview")
async def agent_sing_preview(body: dict):
    """Plain WAV for browser testing (no AGNT header)."""
    if not SINGING_ENABLED:
        return JSONResponse(status_code=503, content={"error": "singing disabled (ENABLE_SINGING=0)"})
    try:
        lyrics, emotion, wav_bytes = await _render_agent_sing_wav(body)
    except ValueError as e:
        return JSONResponse(status_code=400, content={"error": str(e)})
    except sing.SingingDependencyError as e:
        return JSONResponse(status_code=503, content={"error": str(e)})
    except sing.SingingNotConfigured as e:
        return JSONResponse(status_code=503, content={"error": str(e)})
    except RuntimeError as e:
        log.error("agent/sing/preview RVC failed: %s", e)
        return JSONResponse(
            status_code=503,
            content={"error": "RVC conversion failed", "detail": str(e)[-800:]},
        )
    except Exception as e:
        log.exception("agent/sing/preview failed")
        return JSONResponse(status_code=500, content={"error": str(e)})

    import numpy as np

    pcm = np.frombuffer(wav_bytes[44:], dtype=np.int16) if len(wav_bytes) > 44 else np.array([], dtype=np.int16)
    peak = int(np.max(np.abs(pcm))) if pcm.size else 0
    duration_s = len(pcm) / sing.TTS_SAMPLE_RATE if pcm.size else 0.0

    log.info(
        "agent/sing/preview emotion=%s lyrics=%r %.1fs peak=%d",
        emotion,
        lyrics[:60],
        duration_s,
        peak,
    )

    return Response(
        content=wav_bytes,
        media_type="audio/wav",
        headers={
            "Cache-Control": "no-store",
            "X-Sing-Emotion": emotion,
            "X-Sing-Duration-S": f"{duration_s:.2f}",
            "X-Sing-Peak": str(peak),
        },
    )


@app.post("/agent/sing")
async def agent_sing(body: dict):
    """RVC singing pipeline: guide vocal â†’ voice conversion â†’ streamed ESP32 WAV.

    Body:
      lyrics | text   — song lyrics (from Ollama or client)
      topic           — if no lyrics, Ollama writes short lyrics for this theme
      emotion         — face animation hint (default happy)
      f0_up_key       — semitone pitch shift for RVC (alias: pitch_shift)
      index_rate      — RVC feature index strength (optional)

    Response: application/octet-stream
      [AGNT header][JSON metadata][WAV chunks]
    Initial JSON includes {"singing": true, "emotion": "..."}.
    """
    if not SINGING_ENABLED:
        return JSONResponse(status_code=503, content={"error": "singing disabled (ENABLE_SINGING=0)"})
    try:
        lyrics, emotion, wav_bytes = await _render_agent_sing_wav(body)
    except ValueError as e:
        return JSONResponse(status_code=400, content={"error": str(e)})
    except sing.SingingDependencyError as e:
        log.error("Singing deps missing: %s", e)
        return JSONResponse(status_code=503, content={"error": str(e)})
    except sing.SingingNotConfigured as e:
        return JSONResponse(status_code=503, content={"error": str(e)})
    except RuntimeError as e:
        log.error("agent/sing RVC failed: %s", e)
        return JSONResponse(
            status_code=503,
            content={"error": "RVC conversion failed", "detail": str(e)[-800:]},
        )
    except Exception as e:
        log.exception("agent/sing failed")
        return JSONResponse(status_code=500, content={"error": str(e)})

    f0_up_key = int(body.get("f0_up_key", body.get("pitch_shift", 0)))

    log.info(
        "agent/sing emotion=%s f0_up_key=%d lyrics=%r",
        emotion,
        f0_up_key,
        lyrics[:80],
    )

    payload = sing.build_singing_payload(
        wav_bytes,
        emotion=emotion,
        f0_up_key=f0_up_key,
    )
    log.info(
        "agent/sing ready stream=%d bytes (%.1fs audio)",
        len(payload),
        (len(wav_bytes) - 44) / (2 * sing.TTS_SAMPLE_RATE),
    )

    return Response(
        content=payload,
        media_type="application/octet-stream",
        headers={
            "Cache-Control": "no-store",
            "X-Agent-Sing": "1",
            "X-Agent-Emotion": emotion,
        },
    )


@app.post("/tts")
async def tts(body: dict):
    text = body.get("text", "").strip()
    if not text:
        return JSONResponse(status_code=400, content={"error": "missing 'text'"})
    sing_flag = bool(body.get("sing", False))
    text = cap_speech_text(text, sing=sing_flag)
    log.info("tts sing=%s (%d chars): %s", sing_flag, len(text), text)
    try:
        wav = await synthesize_character_wav(text, sing_flag=sing_flag, log_label="TTS")
    except ModuleNotFoundError as e:
        log.error("TTS dependency missing: %s", e)
        return JSONResponse(status_code=503, content={"error": "TTS deps missing — run server/start.ps1"})
    except TimeoutError:
        log.warning("TTS timeout")
        return JSONResponse(status_code=504, content={"error": "TTS timeout"})
    except Exception as e:
        log.exception("TTS failed")
        return JSONResponse(status_code=500, content={"error": str(e)})

    return Response(
        content=wav,
        media_type="audio/wav",
        headers={
            "Content-Length": str(len(wav)),
            "Connection": "close",
            "Cache-Control": "no-store",
        },
    )


@app.post("/converse")
async def converse(request: Request):
    t_total = time.monotonic()
    wav_bytes = await request.body()
    if len(wav_bytes) < 1000:
        return JSONResponse(status_code=400, content={"error": "audio too short"})
    log.info("converse: received %d bytes of audio", len(wav_bytes))
    save_last_capture(wav_bytes, "converse")

    dur, peak, rms = _wav_metrics(wav_bytes)
    if not wav_has_speech(wav_bytes, min_peak=MIN_CONVERSE_PEAK, min_rms=MIN_CONVERSE_RMS):
        log.info(
            "converse: silent/quiet capture ignored dur=%.1fs peak=%d rms=%.0f",
            dur,
            peak,
            rms,
        )
        return {
            "emotion": "neutral",
            "reply": "",
            "heard": "",
            "sing": False,
            "speak": False,
            "sound_effect": "none",
        }

    wav_for_stt = normalize_wav_bytes(wav_bytes)
    if wav_for_stt is not wav_bytes:
        norm_path = CAPTURE_DIR / "last_converse_normalized.wav"
        norm_path.write_bytes(wav_for_stt)
        log.info("audio normalized copy -> %s", norm_path)

    t_stt = time.monotonic()
    text = await transcribe_wav(
        wav_for_stt,
        language=WHISPER_LANGUAGE,
        initial_prompt=WHISPER_CONVERSE_PROMPT,
        beam_size=WHISPER_BEAM_SIZE,
    )
    stt_s = time.monotonic() - t_stt
    log.info("transcribed (%.1fs): %s", stt_s, text)

    if not text:
        log.info("converse: empty transcription — ignored (no TTS)")
        return {
            "emotion": "neutral",
            "reply": "",
            "heard": "",
            "sing": False,
            "speak": False,
            "sound_effect": "none",
        }

    if is_wake_phrase(text):
        stripped = strip_wake_phrase(text)
        if stripped:
            log.info("stripped wake phrase -> command: %s", stripped)
            text = stripped
        elif len(normalize_heard(text)) < 24:
            wake = srv_cfg.wake_only_reply()
            wake["reply"] = cap_speech_text(prepare_spanish_text(wake["reply"]), sing=False)
            wake["heard"] = text
            return wake

    # Smart speaker: a direct "reproducí/poné X" command -> resolve + play (the small LLM
    # tends to ignore the music instruction, so we handle it here, fast and reliable).
    if _music_available():
        mq = music_command_query(text)
        if mq:
            track = await _resolve_music_track(mq)
            if track:
                log.info("music command '%s' -> %s (%s)", mq, track["title"], track["video_id"])
                return {
                    "emotion": "happy", "reply": "", "heard": text, "sing": False,
                    "speak": False, "sound_effect": "none", "music": track,
                }
            log.info("music command '%s' -> sin resultados", mq)
            return {
                "emotion": "confused",
                "reply": cap_speech_text("No encontré esa canción.", sing=False),
                "heard": text, "sing": False, "speak": True, "sound_effect": "none",
            }

    result = try_quick_reply(text)
    if result is not None:
        result["reply"] = cap_speech_text(prepare_spanish_text(result["reply"]), sing=False)
    if result is None:
        result = await ask_ollama(text, use_history=True)
    result["heard"] = text
    log.info(
        "reply [%s] speak=%s sing=%s sfx=%s: %s",
        result["emotion"],
        result["speak"],
        result["sing"],
        result.get("sound_effect", "none"),
        result["reply"],
    )
    return result

