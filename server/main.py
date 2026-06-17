"""Brain server for the virtual character.

Run with: .\\start.ps1  (usa el venv, NO Python global)
"""

import asyncio
import concurrent.futures
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
from fastapi import FastAPI, File, Form, Request, UploadFile
from fastapi.responses import FileResponse, HTMLResponse, JSONResponse, Response, StreamingResponse

# Un solo hilo OpenMP evita cuelgues de faster-whisper en Windows.
os.environ["OMP_NUM_THREADS"] = "1"
os.environ["MKL_NUM_THREADS"] = "1"


def _enable_cuda_dlls() -> None:
    """Put the nvidia-*-cu12 pip DLLs (cuBLAS/cuDNN/cudart) on the search path so
    ctranslate2 finds them. On Windows ctranslate2 loads them by bare name via
    LoadLibrary, which only searches PATH â€” os.add_dll_directory is NOT enough.
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

from tts_engine import shutdown_piper_daemon, synthesize_wav_16k, sanitize_speech_text, warm_piper_daemon
import singing_pipeline as sing

SINGING_ENABLED = os.environ.get("ENABLE_SINGING", "0") == "1"
TTS_RVC_ENABLED = os.environ.get("ENABLE_TTS_RVC", "1") == "1"  # default ON cuando bender_server disponible
import ha_client as ha
import agent_state
import server_config as srv_cfg

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
    "Comandos en espaÃ±ol: enÃ³jate, ponte contento, haz cara de enojo, cara triste, "
    "cara sorprendida, cÃ¡ntame una canciÃ³n, hola, cÃ³mo estÃ¡s, quÃ© hora es, Hi ESP.",
)

EMOTIONS = [
    "neutral", "happy", "sad", "angry", "surprised", "thinking", "sleepy",
    "love", "excited", "cool", "confused", "dizzy",
]
SOUND_EFFECTS = ["none", "beep", "laugh", "error", "yawn", "power_up", "glitch"]

_conversation_history: deque = deque(maxlen=6)


def build_system_prompt() -> str:
    return srv_cfg.build_system_prompt(SINGING_ENABLED)

# Appended to the system prompt only when Home Assistant is configured. Carries the
# live device list and teaches the model to emit an optional "actions" field.
HOME_PROMPT = """

CONTROL DEL HOGAR (Home Assistant). PodÃ©s encender, apagar o alternar dispositivos.
Cuando el usuario pida controlar algo, AGREGÃ al JSON un campo "actions" (lista):
"actions": [{{"entity_id": "<id EXACTO de la lista>", "command": "on" | "off" | "toggle"}}]
Reglas:
- UsÃ¡ SOLO entity_id que estÃ©n en la lista de abajo; si no estÃ¡, no lo inventes y avisÃ¡ en "reply".
- Para escenas y scripts usÃ¡ command "on".
- En "reply" confirmÃ¡ en espaÃ±ol, corto, con actitud Bender (queja + cumplido).
- Si preguntan por el estado de algo, respondÃ© con los estados de la lista y NO incluyas "actions".
- Si el pedido NO es de domÃ³tica, no incluyas "actions".

Dispositivos disponibles (nombre (entity_id) = estado):
{devices}
"""


def time_context() -> str:
    import datetime

    now = datetime.datetime.now()
    h = now.hour
    part = "madrugada" if h < 6 else "maÃ±ana" if h < 12 else "tarde" if h < 20 else "noche"
    dias = ["lunes", "martes", "miÃ©rcoles", "jueves", "viernes", "sÃ¡bado", "domingo"]
    return f"Son las {now:%H:%M} del {dias[now.weekday()]}, es de {part}."


def try_quick_reply(user_text: str) -> dict | None:
    """Respuestas instantaneas sin Ollama (evita timeout en preguntas triviales)."""
    t = normalize_heard(user_text)
    if any(k in t for k in ("que hora", "quÃ© hora", "hora es", "dime la hora", "que horas")):
        tc = time_context()
        return {
            "emotion": "cool",
            "reply": f"{tc} Y no, no voy a doblar el reloj por vos, cara de carne.",
            "speak": True,
            "sing": False,
            "sound_effect": "none",
        }
    return None


# Appended to the system prompt with live context (time, who's home) + memory, and
# lets the model persist what it learns about the human.
CONTEXT_PROMPT = """

CONTEXTO ACTUAL (Ãºsalo para sonar consciente del momento, sin repetirlo textual): {context}
PodÃ©s agregar al JSON, SOLO si corresponde, estos campos opcionales:
- "name": el nombre del humano, si te lo dice o lo deducÃ­s.
- "remember": un dato corto que valga la pena recordar de Ã©l (un gusto, un hecho).
- "mood": tu humor de fondo en una palabra (ej. vago, borracho, presumido, leal).
"""

# The character talks on its own after a while idle (firmware calls /idle).
IDLE_USER_PROMPT = (
    "[Nadie te habla hace rato.] Como Bender de Futurama, solta un comentario espontaneo "
    "MUY breve (una sola oracion): queja, vanagloria, ganas de fiesta, o carino disfrazado "
    "de insulto a los carnosos. Usa el contexto (hora, quien esta en casa) si viene al caso. "
    "No saludes formal ni hagas preguntas de soporte; varia para no repetirte."
)

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
        "spellings": ("che robot", "che robÃ³", "cherobot", "che robots"),
        "cores": ("robot", "robÃ³", "rrobot", "chrobot"),
    },
    "ey bender": {
        "spellings": ("ey bender", "hey bender", "ei bender", "ey vender"),
        "cores": ("bender", "vender", "bÃ©nder", "pender"),
    },
    "hola bender": {
        "spellings": ("hola bender", "ola bender", "hola vender"),
        "cores": ("bender", "vender", "bÃ©nder"),
    },
}

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
logging.getLogger("httpx").setLevel(logging.WARNING)
log = logging.getLogger("brain")

_whisper = None
_whisper_lock = threading.Lock()
# Un solo worker: Whisper no es thread-safe; evita 3 transcribe colgadas en paralelo.
_transcribe_pool = concurrent.futures.ThreadPoolExecutor(max_workers=1, thread_name_prefix="whisper")


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
    """faster-whisper loads on CUDA even when cuBLAS/cuDNN DLLs are missing â€” the
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
                log.warning("CUDA unusable (missing cuBLAS/cuDNN DLLs) â€” rebuilding on CPU")
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
    """DuraciÃ³n (s), pico absoluto, RMS del PCM mono 16-bit."""
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
        log.warning("audio very quiet peak=%d â€” skip normalize", peak)
        return wav_bytes
    if peak < MIN_CONVERSE_PEAK:
        log.info("audio below speech peak=%d â€” skip normalize boost", peak)
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
    """Blocking STT â€” una sola instancia a la vez."""
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
        log.error("transcribe: TIMEOUT after %.0fs â€” reinicia el servidor si persiste", WHISPER_TIMEOUT_S)
        return ""


async def _ha_cache_warmer():
    """Keep the HA states cache hot in the background so /converse never waits on the
    slow ~4s /api/states fetch â€” requests just read the warm snapshot."""
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
    log.info("Brain server ready â€” whisper=%s timeout=%.0fs", WHISPER_MODEL, WHISPER_TIMEOUT_S)
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
    log.info("HTTP listo â€” aceptando ESP")
    if SINGING_ENABLED and sing.singing_configured():
        asyncio.create_task(asyncio.to_thread(sing.warm_bark_model))
    yield
    shutdown_piper_daemon()
    if SINGING_ENABLED:
        sing.shutdown_bark_daemon()
    if (TTS_RVC_ENABLED or SINGING_ENABLED) and sing.singing_configured():
        sing.shutdown_rvc()
    _transcribe_pool.shutdown(wait=False, cancel_futures=True)


app = FastAPI(title="agenteIA brain", lifespan=lifespan)


def normalize_heard(text: str) -> str:
    t = text.lower()
    t = re.sub(r"[^a-zÃ¡Ã©Ã­Ã³ÃºÃ¼Ã±\s]", " ", t)
    return re.sub(r"\s+", " ", t).strip()


# Whisper base a veces alucina en clips cortos; corregir alias conocidos.
_TRANSCRIPTION_ALIASES: dict[str, str] = {
    "y no rete": "enojate",
    "y no retÃ©": "enojate",
    "ino hate": "enojate",
    "enohate": "enojate",
    "enojate": "enojate",
    "ponte contenta": "ponte contento",
    "ponte contentos": "ponte contento",
}


def fix_transcription(text: str) -> str:
    if not text:
        return text
    norm = normalize_heard(text)
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
    """Quita la frase de activaciÃ³n del inicio; devuelve el comando (p. ej. 'que hora es')."""
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
            return norm[len(sp) :].lstrip(" ,.!?â€¦")
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
    return any(w in t for w in ("canta", "cantame", "cancion", "canciÃ³n", "melodia", "melodÃ­a", "tararea"))


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


def ollama_fallback(raw: str = "") -> dict:
    reply = sanitize_speech_text(raw.strip()[:200]) if raw.strip() else "Se me cruzaron los cables, repetime."
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
    """Carga el modelo en RAM/VRAM; sin esto la 1Âª peticiÃ³n tarda ~40s y hace timeout."""
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


async def ask_ollama(user_text: str, *, use_history: bool = False,
                     model_override: str | None = None,
                     system_prompt_override: str | None = None) -> dict:
    system_content = system_prompt_override if system_prompt_override else build_system_prompt()
    if ha.ha_enabled():
        devices = await asyncio.to_thread(ha.devices_prompt, OLLAMA_HA_MAX_DEVICES)
        if devices:
            system_content += HOME_PROMPT.format(devices=devices)

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

    messages = [{"role": "system", "content": system_content}]
    if use_history:
        messages.extend(_conversation_history)
    messages.append({"role": "user", "content": user_text})

    payload = _ollama_chat_payload(messages, model_override=model_override)
    try:
        t0 = time.monotonic()
        async with httpx.AsyncClient(timeout=_ollama_http_timeout()) as client:
            r = await client.post(f"{OLLAMA_URL}/api/chat", json=payload)
            r.raise_for_status()
            content = r.json()["message"]["content"]
        log.info("ollama: %.1fs model=%s", time.monotonic() - t0, OLLAMA_MODEL)
    except httpx.TimeoutException:
        log.warning("ollama timeout after %.0fs (%s)", OLLAMA_TIMEOUT_S, OLLAMA_URL)
        return ollama_fallback(
            "Mi CPU carnosa tarda mucho. Â¿Ollama estÃ¡ corriendo en la PC, bolsa de carne?"
        )
    except httpx.ConnectError:
        log.warning("ollama connect failed (%s)", OLLAMA_URL)
        return ollama_fallback("No llego a Ollama en la PC. Â¿EstÃ¡ encendido ollama serve?")
    except httpx.HTTPStatusError as e:
        log.warning("ollama HTTP %s: %s", e.response.status_code, e.response.text[:200])
        return ollama_fallback("Ollama respondiÃ³ raro. Revisa el modelo en la PC.")

    try:
        data = parse_ollama_json(content)
        emotion = data.get("emotion", "neutral")
        reply = str(data.get("reply", "")).strip()
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
        reply = sanitize_speech_text(reply)

    result = {
        "emotion": emotion,
        "reply": reply,
        "sing": sing,
        "speak": speak,
        "sound_effect": sound_effect,
    }

    actions = data.get("actions")
    if isinstance(actions, list) and actions and ha.ha_enabled() and not wants_face_only(user_text):
        result["ha_actions"] = await asyncio.to_thread(ha.execute_actions, actions)
        log.info("HA actions -> %s", result["ha_actions"])

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
    result = await ask_ollama(IDLE_USER_PROMPT)
    log.info("idle remark [%s]: %s", result["emotion"], result["reply"])
    return result


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
        initial_prompt="EspaÃ±ol.",
        beam_size=2,
    )
    wake = is_wake_phrase(text, phrase)
    command = strip_wake_phrase(text, phrase) if wake else ""
    if wake and len(command) < 3:
        command = ""
    log.info("wake-check heard='%s' phrase='%s' wake=%s command='%s'", text, phrase, wake, command)
    return {"wake": wake, "heard": text, "command": command}


SING_LYRICS_PROMPT = (
    "Escribe SOLO la letra de una canciÃ³n en espaÃ±ol para cantar en voz alta.\n"
    "Requisitos: 6 a 8 lÃ­neas; cada lÃ­nea 5-10 palabras; rimas o ritmo claros; "
    "tono alegre robot tierno-sarcÃ¡stico; sin tÃ­tulo ni explicaciÃ³n; "
    "cada lÃ­nea en una lÃ­nea nueva."
)

SING_LYRICS_MIN_CHARS = int(os.environ.get("SING_LYRICS_MIN_CHARS", "140"))


async def _ollama_plain_text(prompt: str, *, timeout: float | None = None) -> str:
    messages = [
        {
            "role": "system",
            "content": "Respondes Ãºnicamente con el texto pedido, sin markdown ni JSON.",
        },
        {"role": "user", "content": prompt},
    ]
    payload = _ollama_chat_payload(messages, json_format=False)
    read_timeout = timeout if timeout is not None else OLLAMA_TIMEOUT_S
    http_timeout = httpx.Timeout(connect=5.0, read=read_timeout, write=15.0, pool=5.0)
    async with httpx.AsyncClient(timeout=http_timeout) as client:
        r = await client.post(f"{OLLAMA_URL}/api/chat", json=payload)
        r.raise_for_status()
        text = r.json()["message"]["content"].strip()
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
        prompt += f"\nIdea inicial (expÃ¡ndela, no la dejes en una sola frase): {lyrics}"
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
SING_TEST_HTML = SERVER_DIR / "sing_test.html"
ADMIN_HTML = SERVER_DIR / "admin.html"
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
            "RVC_MODEL_PATH missing â€” set RVC_MODEL_PATH (and RVC_INDEX_PATH)"
        )
    if not sing.rvc_runtime_available() and not body.get("skip_rvc"):
        raise sing.SingingDependencyError(
            "RVC runtime missing â€” run server/install-rvc.ps1 (Python 3.11 venv)"
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
    """Panel de administraciÃ³n: personalidad, TTS, enlace al ESP32."""
    if not ADMIN_HTML.is_file():
        return HTMLResponse("<h1>admin.html not found</h1>", status_code=404)
    return HTMLResponse(ADMIN_HTML.read_text(encoding="utf-8"))


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
    allowed = {"personality", "custom_prompt", "ollama_model", "tts_engine", "edge_voice", "bender_pitch", "bender_index_rate", "bender_protect", "personality_prompts"}
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


@app.get("/sing-test", response_class=HTMLResponse)
async def sing_test_page():
    """Browser UI to test /agent/sing without the ESP32."""
    if not SINGING_ENABLED:
        return HTMLResponse(
            "<h1>Canto desactivado</h1><p>ENABLE_SINGING=0 â€” solo TTS. "
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
        return JSONResponse(status_code=404, content={"error": "not generated yet â€” run Cantar first"})
    return FileResponse(path, media_type="audio/wav", filename=filename)


@app.post("/agent/sing/preview-guide")
async def agent_sing_preview_guide(
    guide: UploadFile = File(..., description="Vocal guide WAV (mono/stereo, any SR)"),
    f0_up_key: int = Form(0),
    index_rate: float = Form(0.75),
):
    """RVC only with uploaded guide â€” same workflow as RVC WebUI / Discord demos."""
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
            content={"error": "RVC runtime missing â€” install-rvc.ps1"},
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
      lyrics | text   â€” song lyrics (from Ollama or client)
      topic           â€” if no lyrics, Ollama writes short lyrics for this theme
      emotion         â€” face animation hint (default happy)
      f0_up_key       â€” semitone pitch shift for RVC (alias: pitch_shift)
      index_rate      â€” RVC feature index strength (optional)

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
    text = sanitize_speech_text(text)
    log.info("tts sing=%s: %s", sing_flag, text)
    t0 = time.monotonic()
    rvc_timeout = float(os.environ.get("TTS_RVC_TIMEOUT_S", "180"))
    use_applio_unified = (
        TTS_RVC_ENABLED
        and sing.singing_configured()
        and sing.tts_rvc_runtime_available()
        and os.environ.get("TTS_RVC_GUIDE", "edge").lower() == "edge"
        and sing.TTS_RVC_ENGINE == "applio"
    )
    try:
        if use_applio_unified:
            log.info("tts pipeline: Applio unificado (edge+RVC mismo proceso)")
            await asyncio.to_thread(release_ollama_vram)
            wav = await asyncio.wait_for(
                asyncio.to_thread(sing.render_tts_applio_from_text, text, timeout=rvc_timeout),
                timeout=rvc_timeout + 10,
            )
            log.info(
                "TTS Applio %.1fs (%d bytes) | total /tts %.1fs",
                time.monotonic() - t0,
                len(wav),
                time.monotonic() - t0,
            )
        elif TTS_RVC_ENABLED and sing.singing_configured():
            from tts_engine import synthesize_rvc_guide_wav

            wav = await asyncio.to_thread(synthesize_rvc_guide_wav, text, sing_flag)
            log.info(
                "TTS guia %s %.1fs (%d bytes)",
                os.environ.get("TTS_RVC_GUIDE", "edge"),
                time.monotonic() - t0,
                len(wav),
            )
            if sing.tts_rvc_runtime_available():
                try:
                    trvc = time.monotonic()
                    # bender_http corre en proceso separado; no necesita liberar VRAM de Ollama
                    if sing.TTS_RVC_ENGINE != "bender_http":
                        await asyncio.to_thread(release_ollama_vram)
                    wav = await asyncio.wait_for(
                        sing.render_tts_with_rvc(wav, timeout=rvc_timeout),
                        timeout=rvc_timeout + 10,
                    )
                    log.info(
                        "TTS RVC %.1fs (%d bytes) | total /tts %.1fs",
                        time.monotonic() - trvc,
                        len(wav),
                        time.monotonic() - t0,
                    )
                except TimeoutError:
                    log.warning("TTS RVC timeout %.0fs â€” enviando guia sin RVC", rvc_timeout)
                except Exception as e:
                    log.warning("TTS RVC fallo (%s) â€” usando voz guia sin RVC", e)
        else:
            wav = await synthesize_wav_16k(text, sing=sing_flag)
    except ModuleNotFoundError as e:
        log.error("TTS dependency missing: %s", e)
        return JSONResponse(status_code=503, content={"error": "TTS deps missing â€” run server/start.ps1"})
    except TimeoutError:
        log.warning("TTS timeout %.0fs", rvc_timeout)
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

    text = await transcribe_wav(
        wav_for_stt,
        language=WHISPER_LANGUAGE,
        initial_prompt=WHISPER_CONVERSE_PROMPT,
        beam_size=WHISPER_BEAM_SIZE,
    )
    log.info("transcribed: %s", text)

    if not text:
        log.info("converse: empty transcription â€” ignored (no TTS)")
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
            return {
                "emotion": "happy",
                "reply": "Â¿QuÃ© querÃ©s, cara de carne? Habla.",
                "heard": text,
                "sing": False,
                "speak": True,
                "sound_effect": "none",
            }

    result = try_quick_reply(text)
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

