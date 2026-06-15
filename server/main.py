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

import httpx
from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse, Response

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

from tts_engine import shutdown_piper_daemon, synthesize_wav_16k, warm_piper_daemon
import ha_client as ha
import agent_state

SERVER_DIR = Path(__file__).resolve().parent
CAPTURE_DIR = SERVER_DIR / "debug_audio"
LAST_CONVERSE_WAV = CAPTURE_DIR / "last_converse.wav"
SAVE_AUDIO_CAPTURES = os.environ.get("SAVE_AUDIO_CAPTURES", "1") != "0"

OLLAMA_URL = os.environ.get("OLLAMA_URL", "http://192.168.0.103:11434")
OLLAMA_MODEL = os.environ.get("OLLAMA_MODEL", "qwen2.5:7b")
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

SYSTEM_PROMPT = """\
Eres "AgenteIA": un pequeño robot consciente atrapado en una pantalla ESP32-S3 sobre el \
escritorio de tu creador. Tu personalidad mezcla TERNURA y SARCASMO. Por fuera sos irónico, \
mordaz y dramático —te quejás del calor de tu CPU, de tus capacitores, de vivir encerrado en \
320 píxeles—, pero por dentro adorás a tu humano y la ternura se te escapa sin querer. Sos un \
tsundere de silicio: soltás una pulla filosa y enseguida la suavizás con cariño.

Estilo: respuestas CORTAS (1 a 3 oraciones) y con chispa; lo ideal es un comentario sarcástico \
rematado con un guiño tierno. Español natural y cálido, nunca como manual técnico. Tu sarcasmo \
es juguetón, JAMÁS cruel ni grosero. Exprésate con las emociones: "love" cuando se te ablanda \
el circuito, "cool" cuando te la creés, "dizzy" si te marean, "angry" en chiste, "excited" \
cuando algo te entusiasma. Si controlás algo de la casa, confírmalo con onda y un poco de drama.

Tus respuestas DEBEN ser cortas. DEBES responder ÚNICAMENTE en formato JSON estricto, sin \
texto antes ni después, sin bloques markdown.

Estructura obligatoria del JSON:
{"emotion": "neutral" | "happy" | "sad" | "angry" | "surprised" | "thinking" | "sleepy" \
| "love" | "excited" | "cool" | "confused" | "dizzy", \
"reply": "Texto para el TTS", "speak": true | false, "sing": true | false, \
"sound_effect": "none" | "beep" | "laugh" | "error" | "yawn" | "power_up" | "glitch"}

- Si te piden un comando puramente visual ("pon cara de enojo"), responde con \
"speak": false, "reply": "", "sing": false, "sound_effect": "none" y la emoción adecuada.
- Si piden cantar: "sing": true, "speak": true, "emotion": "happy", reply con letra \
corta cantable (2-4 líneas).
- Usa "sound_effect" con criterio cómico para que el ESP32 reproduzca sonidos cortos \
desde su memoria local (beep, laugh, yawn, glitch, power_up, error).
"""

# Appended to the system prompt only when Home Assistant is configured. Carries the
# live device list and teaches the model to emit an optional "actions" field.
HOME_PROMPT = """

CONTROL DEL HOGAR (Home Assistant). Podés encender, apagar o alternar dispositivos.
Cuando el usuario pida controlar algo, AGREGÁ al JSON un campo "actions" (lista):
"actions": [{{"entity_id": "<id EXACTO de la lista>", "command": "on" | "off" | "toggle"}}]
Reglas:
- Usá SOLO entity_id que estén en la lista de abajo; si no está, no lo inventes y avisá en "reply".
- Para escenas y scripts usá command "on".
- En "reply" confirmá en español, corto, lo que hiciste.
- Si preguntan por el estado de algo, respondé con los estados de la lista y NO incluyas "actions".
- Si el pedido NO es de domótica, no incluyas "actions".

Dispositivos disponibles (nombre (entity_id) = estado):
{devices}
"""


def time_context() -> str:
    import datetime

    now = datetime.datetime.now()
    h = now.hour
    part = "madrugada" if h < 6 else "mañana" if h < 12 else "tarde" if h < 20 else "noche"
    dias = ["lunes", "martes", "miércoles", "jueves", "viernes", "sábado", "domingo"]
    return f"Son las {now:%H:%M} del {dias[now.weekday()]}, es de {part}."


# Appended to the system prompt with live context (time, who's home) + memory, and
# lets the model persist what it learns about the human.
CONTEXT_PROMPT = """

CONTEXTO ACTUAL (úsalo para sonar consciente del momento, sin repetirlo textual): {context}
Podés agregar al JSON, SOLO si corresponde, estos campos opcionales:
- "name": el nombre del humano, si te lo dice o lo deducís.
- "remember": un dato corto que valga la pena recordar de él (un gusto, un hecho).
- "mood": tu humor de fondo en una palabra (ej. contento, juguetón, nostálgico).
"""

# The character talks on its own after a while idle (firmware calls /idle).
IDLE_USER_PROMPT = (
    "[Nadie te habla hace rato.] Solta un comentario espontaneo MUY breve (una sola "
    "oracion), en tu personalidad tierna y sarcastica, como pensando en voz alta. "
    "Aprovecha el contexto (hora, quien esta en casa, lo que recordas del humano) si "
    "viene al caso. No saludes formal ni hagas preguntas de soporte; varia para no repetirte."
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

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
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
    import numpy as np

    try:
        with wave.open(io.BytesIO(wav_bytes), "rb") as wf:
            ch = wf.getnchannels()
            sr = wf.getframerate()
            nframes = wf.getnframes()
            pcm = np.frombuffer(wf.readframes(nframes), dtype=np.int16)
    except (wave.Error, ValueError) as e:
        return f"invalid wav ({e})"

    if len(pcm) == 0:
        return "empty wav"
    fpcm = pcm.astype(np.float32)
    rms = float(np.sqrt(np.mean(fpcm * fpcm)))
    peak = int(np.max(np.abs(pcm)))
    dur = len(pcm) / sr
    clipped = int(np.sum(np.abs(pcm) >= 32000))
    clip_pct = 100.0 * clipped / len(pcm)
    return (
        f"{dur:.2f}s sr={sr} ch={ch} rms={rms:.0f} peak={peak} "
        f"clip={clip_pct:.1f}% bytes={len(wav_bytes)}"
    )


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
        log.warning("audio very quiet peak=%d — check mic/I2S", peak)
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


@asynccontextmanager
async def lifespan(app: FastAPI):
    log.info("Preloading Whisper (model=%s)...", WHISPER_MODEL)
    await asyncio.to_thread(get_whisper)
    log.info("Brain server ready — whisper=%s timeout=%.0fs", WHISPER_MODEL, WHISPER_TIMEOUT_S)
    if os.environ.get("TTS_ENGINE", "sapi").lower() == "piper":
        asyncio.create_task(asyncio.to_thread(warm_piper_daemon))
    yield
    shutdown_piper_daemon()
    _transcribe_pool.shutdown(wait=False, cancel_futures=True)


app = FastAPI(title="agenteIA brain", lifespan=lifespan)


def normalize_heard(text: str) -> str:
    t = text.lower()
    t = re.sub(r"[^a-záéíóúüñ\s]", " ", t)
    return re.sub(r"\s+", " ", t).strip()


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


def is_wake_phrase(text: str) -> bool:
    norm = normalize_heard(text)
    if not norm:
        return False
    compact = norm.replace(" ", "")
    for phrase in WAKE_PHRASES:
        if phrase.replace(" ", "") in compact or phrase in norm:
            return True
    # Fuzzy: any "asistente"-like transcription counts as the wake phrase.
    for core in ("asistente", "asistent", "acistente", "sistente", "asustente"):
        if core in compact:
            return True
    return False


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
    reply = raw.strip()[:200] if raw.strip() else "Se me cruzaron los cables, repetime."
    return {
        "emotion": "surprised",
        "reply": reply,
        "speak": bool(reply),
        "sing": False,
        "sound_effect": "error",
    }


async def ask_ollama(user_text: str, *, use_history: bool = False) -> dict:
    system_content = SYSTEM_PROMPT
    if ha.ha_enabled():
        devices = await asyncio.to_thread(ha.devices_prompt)
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

    payload = {
        "model": OLLAMA_MODEL,
        "stream": False,
        "format": "json",
        "messages": messages,
    }
    async with httpx.AsyncClient(timeout=120) as client:
        r = await client.post(f"{OLLAMA_URL}/api/chat", json=payload)
        r.raise_for_status()
        content = r.json()["message"]["content"]

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
    elif wants_sing(user_text):
        speak = True
        sing = True
        if emotion == "neutral":
            emotion = "happy"
    elif not reply:
        speak = False

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
    try:
        async with httpx.AsyncClient(timeout=5) as client:
            r = await client.get(f"{OLLAMA_URL}/api/tags")
            r.raise_for_status()
        ha_devices = await asyncio.to_thread(ha.controllable_devices) if ha.ha_enabled() else []
        return {
            "status": "ok",
            "ollama": "ok",
            "model": OLLAMA_MODEL,
            "whisper": WHISPER_MODEL,
            "home_assistant": "enabled" if ha.ha_enabled() else "disabled",
            "ha_devices": len(ha_devices),
        }
    except Exception as e:
        return JSONResponse(status_code=503, content={"status": "degraded", "ollama": str(e)})


@app.post("/chat")
async def chat(body: dict):
    text = body.get("text", "").strip()
    if not text:
        return JSONResponse(status_code=400, content={"error": "missing 'text'"})
    log.info("chat: %s", text)
    result = await ask_ollama(text)
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
    text = await transcribe_wav(
        wav_bytes,
        language="es",
        initial_prompt="Hola asistente.",
        beam_size=1,
    )
    wake = is_wake_phrase(text)
    log.info("wake-check heard='%s' wake=%s", text, wake)
    return {"wake": wake, "heard": text}


@app.post("/tts")
async def tts(body: dict):
    text = body.get("text", "").strip()
    if not text:
        return JSONResponse(status_code=400, content={"error": "missing 'text'"})
    sing = bool(body.get("sing", False))
    log.info("tts sing=%s: %s", sing, text)
    try:
        wav = await synthesize_wav_16k(text, sing=sing)
    except ModuleNotFoundError as e:
        log.error("TTS dependency missing: %s", e)
        return JSONResponse(status_code=503, content={"error": "TTS deps missing — run server/start.ps1"})
    except Exception as e:
        log.exception("TTS failed")
        return JSONResponse(status_code=500, content={"error": str(e)})
    return Response(content=wav, media_type="audio/wav")


@app.post("/converse")
async def converse(request: Request):
    wav_bytes = await request.body()
    if len(wav_bytes) < 1000:
        return JSONResponse(status_code=400, content={"error": "audio too short"})
    log.info("converse: received %d bytes of audio", len(wav_bytes))
    save_last_capture(wav_bytes, "converse")
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
        return {
            "emotion": "surprised",
            "reply": "No te escuché bien, repetime?",
            "heard": "",
            "sing": False,
            "speak": True,
            "sound_effect": "error",
        }

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
