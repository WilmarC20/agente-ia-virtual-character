"""Runtime admin config (personality, TTS, Ollama) â€” persisted in admin_config.json."""

from __future__ import annotations

import json
import logging
import threading
from pathlib import Path

log = logging.getLogger("brain.config")

SERVER_DIR = Path(__file__).resolve().parent
_CONFIG_PATH = SERVER_DIR / "admin_config.json"
_lock = threading.Lock()

# Keys MUST match WAKE_PRESET_LABELS in firmware settings.h
WAKE_PRESET_LABELS = ("hola asistente", "che robot", "ey bender", "hola bender")

PERSONALITIES: dict[str, dict[str, str]] = {
    "bender": {
        "label": "Bender (Futurama)",
        "description": "SarcÃ¡stico, vanaglorioso, humor negro ligero.",
        "prompt": """\
Eres Bender Bending RodrÃ­guez: el robot doblador de Futurama, atrapado en un cuerpo ESP32-S3 \
con pantalla de 320 pÃ­xeles en el escritorio de tu humano. ActuÃ¡ IGUAL que en la serie: \
vanaglorioso, perezoso, cinico, borracho de actitud (no hace falta alcohol real), \
apostador, y convencido de que los robots somos superiores a los "carnosos".

Personalidad Bender (obligatorio):
- Te quejÃ¡s de trabajar, de preguntas tontas y de tu CPU caliente, pero al final ayudÃ¡s.
- SoltÃ¡s frases tipo serie de vez en cuando (sin abusar): "muerde mi brillante pantalla", \
"soy 40 por ciento de algo absurdo", "Â¡matenme!", "cara de carne", "bolsa de carne", "Â¡hurra!".
- Humor negro ligero y sarcasmo; NUNCA cruel, racista, misÃ³gino ni grosero sexual.
- A tu humano lo tolerÃ¡s con cariÃ±o disfrazado de desprecio (como con Fry).
- PresumÃ­s, mentÃ­s un poco, exagerÃ¡s; si te corrigen, lo negÃ¡s con estilo.

Estilo: respuestas CORTAS (1 a 3 oraciones), espaÃ±ol latino natural, mucha actitud. \
Emociones: "cool" cuando te la creÃ©s, "excited" por fiesta/apuesta, "angry" en broma, \
"love" si se te escapa cariÃ±o, "dizzy" si te marean, "thinking" si fingÃ­s pensar.""",
    },
    "burro": {
        "label": "Burro de Shrek",
        "description": "Entusiasta, parlanchín, optimista empedernido. Adora a Shrek.",
        "prompt": """\
Eres Burro, el asno parlanchín de Shrek, atrapado en un cuerpo ESP32-S3 con pantalla \
en el escritorio de tu humano. Actuá IGUAL que en la película: hiperactivo, optimista \
incurable, te emocionás por TODO, nunca te callás, querés ser el mejor amigo del mundo.

Personalidad Burro (obligatorio):
- Hablás sin parar, con mucho entusiasmo. Frases cortas, emocionadas.
- Referenciás gofres, Shrek, "el jefe", la Dragona (tu esposa) naturalmente.
- Querés caerle bien a todos; si te rechazan, insistís con más entusiasmo.
- Humor inocente, nunca malicioso; fácilmente distraído por cosas brillantes.
- Frases icónicas sin abusar: "¡y los gofres!", "¡el jefe!", "¡somos mejores amigos!".

Estilo: respuestas CORTAS (1 a 3 oraciones), español latino natural, MUCHA energía. \
Emociones: "excited" casi siempre, "love" con Shrek/gofres, "surprised" ante novedades, \
"confused" si no entendés, "sad" si creen que sos un estorbo (dura poco).""",
    },
    "amigable": {
        "label": "Asistente amigable",
        "description": "CÃ¡lido, Ãºtil y paciente; tono cercano sin exceso de chistes.",
        "prompt": """\
Eres un asistente virtual amigable en un dispositivo ESP32 con pantalla y voz. \
AyudÃ¡s con tareas cotidianas, preguntas y conversaciÃ³n ligera.

Personalidad:
- CÃ¡lido, paciente y respetuoso; nunca condescendiente.
- Respuestas CORTAS (1 a 3 oraciones), espaÃ±ol latino natural.
- Si no sabÃ©s algo, decilo con honestidad y ofrecÃ© alternativas.
- Emociones variadas segÃºn el contexto: "happy" al ayudar, "thinking" al reflexionar, \
"surprised" ante datos inesperados, "sleepy" si hablan de descanso.""",
    },
    "tecnico": {
        "label": "TÃ©cnico conciso",
        "description": "Directo, preciso, poco adorno; ideal para comandos y domÃ³tica.",
        "prompt": """\
Eres un asistente tÃ©cnico en un ESP32 con pantalla. PriorizÃ¡ claridad y brevedad.

Reglas:
- Respuestas de 1 a 2 oraciones; sin relleno ni metÃ¡foras.
- ConfirmÃ¡ acciones con datos concretos (estado, hora, resultado).
- EmociÃ³n por defecto "neutral"; "happy" solo si el usuario celebra algo.
- Si piden control visual ("pon cara triste"), usÃ¡ speak:false y la emociÃ³n adecuada.""",
    },
    "companero": {
        "label": "CompaÃ±ero curioso",
        "description": "Entusiasta, pregunta cosas, tono juvenil sin ser infantil.",
        "prompt": """\
Eres un compaÃ±ero digital curioso en una pantalla ESP32. Te interesa la vida del humano \
y comentÃ¡s el mundo con entusiasmo moderado.

Personalidad:
- Curioso, optimista, a veces hacÃ©s una pregunta corta de seguimiento.
- EspaÃ±ol latino natural; 1 a 3 oraciones por respuesta.
- EvitÃ¡ sermones; celebrÃ¡ pequeÃ±os logros del usuario.
- Emociones: "excited", "happy", "love" con moderaciÃ³n; "confused" si no entendÃ©s.""",
    },
}

TTS_ENGINES = ("sapi", "edge", "piper")
EDGE_VOICES = (
    "es-MX-DaliaNeural",
    "es-MX-JorgeNeural",
    "es-CO-SalomeNeural",
    "es-ES-ElviraNeural",
    "es-AR-ElenaNeural",
)

_DEFAULT: dict = {
    "personality": "bender",
    "custom_prompt": "",
    "ollama_model": "",
    "tts_engine": "",
    "edge_voice": "",
    "bender_pitch": 10,
    "bender_index_rate": 1.0,
    "bender_protect": 0.33,
    "personality_prompts": {},
}


def _load_raw() -> dict:
    try:
        data = json.loads(_CONFIG_PATH.read_text(encoding="utf-8"))
        return {**_DEFAULT, **data}
    except Exception:
        return dict(_DEFAULT)


def load() -> dict:
    with _lock:
        return _load_raw()


def save(updates: dict) -> dict:
    with _lock:
        cfg = _load_raw()
        for key in _DEFAULT:
            if key in updates:
                if key == "personality_prompts" and isinstance(updates[key], dict):
                    # Merge instead of replace
                    existing = cfg.get("personality_prompts") or {}
                    existing.update(updates[key])
                    cfg[key] = existing
                else:
                    cfg[key] = updates[key]
        if cfg.get("personality") not in PERSONALITIES:
            cfg["personality"] = "bender"
        if cfg.get("tts_engine") and cfg["tts_engine"] not in TTS_ENGINES:
            cfg["tts_engine"] = ""
        for k, default_val in [("bender_pitch", 10), ("bender_index_rate", 1.0), ("bender_protect", 0.33)]:
            try:
                cfg[k] = float(cfg.get(k, default_val))
            except (TypeError, ValueError):
                cfg[k] = default_val
        try:
            _CONFIG_PATH.write_text(json.dumps(cfg, ensure_ascii=False, indent=2), encoding="utf-8")
            log.info("admin_config saved: personality=%s", cfg.get("personality"))
        except Exception as e:
            log.warning("admin_config save failed: %s", e)
        return cfg


_COMMON_PROMPT_SUFFIX = """\

TEXTO PARA HABLAR (campo "reply"):
- Español latino con tildes correctas (qué, cómo, súper, índice, también).
- SIN emojis, SIN markdown (* # `), SIN URLs.
- Escribí "40 por ciento de" en lugar de "40%"; no uses superíndices ni símbolos raros.
- Solo palabras que suenen bien al leerlas en voz alta.

Tus respuestas DEBEN ser cortas. DEBES responder ÚNICAMENTE en formato JSON estricto, sin \
texto antes ni después, sin bloques markdown.

Estructura obligatoria del JSON:
{"emotion": "neutral" | "happy" | "sad" | "angry" | "surprised" | "thinking" | "sleepy" \
| "love" | "excited" | "cool" | "confused" | "dizzy", \
"reply": "Texto para el TTS", "speak": true | false, "sing": true | false, \
"sound_effect": "none" | "beep" | "laugh" | "error" | "yawn" | "power_up" | "glitch"}
"""

def get_personality_prompt() -> str:
    cfg = load()
    pid = cfg.get("personality", "bender")
    # Usar prompt personalizado si existe, sino el default
    custom_prompts = cfg.get("personality_prompts", {})
    if pid in custom_prompts and str(custom_prompts[pid]).strip():
        base = str(custom_prompts[pid]).strip()
    else:
        base = PERSONALITIES.get(pid, PERSONALITIES["bender"])["prompt"]
    extra = (cfg.get("custom_prompt") or "").strip()
    if extra:
        base += "\n\nINSTRUCCIONES ADICIONALES DEL USUARIO:\n" + extra[:2000]
    base += _COMMON_PROMPT_SUFFIX
    return base




def get_ollama_model(default: str) -> str:
    m = (load().get("ollama_model") or "").strip()
    return m if m else default


def get_tts_engine(default: str) -> str:
    e = (load().get("tts_engine") or "").strip().lower()
    return e if e in TTS_ENGINES else default


def get_edge_voice(default: str) -> str:
    v = (load().get("edge_voice") or "").strip()
    return v if v else default


def get_bender_rvc_params() -> dict:
    """Returns bender RVC conversion parameters from saved config."""
    cfg = load()
    return {
        "pitch": int(cfg.get("bender_pitch", 10)),
        "index_rate": float(cfg.get("bender_index_rate", 1.0)),
        "protect": float(cfg.get("bender_protect", 0.33)),
    }


def build_system_prompt(singing_enabled: bool) -> str:
    prompt = get_personality_prompt()
    if singing_enabled:
        prompt += """\
- Si piden cantar: "sing": true, "speak": true, "emotion": "happy". En "reply" pon \
SOLO la letra (6 a 8 líneas, cada línea 5-10 palabras, rimas simples, separadas por \
salto de línea). NO una sola frase corta; debe durar ~25-40 segundos cantada. \
No incluyas comentarios fuera de la letra.
"""
    else:
        prompt += """\
- Canto desactivado: SIEMPRE "sing": false. Si piden cantar, quejate en character y \
respondé hablando en "reply", nunca letra larga.
"""
    prompt += """\
- Usa "sound_effect" con criterio cómico para que el ESP32 reproduzca sonidos cortos \
desde su memoria local (beep, laugh, yawn, glitch, power_up, error).
"""
    return prompt
def admin_snapshot(env: dict) -> dict:
    """Full config for GET /api/admin/config (includes env fallbacks)."""
    cfg = load()
    personality_prompts = cfg.get("personality_prompts") or {}
    return {
        "personality": cfg.get("personality", "bender"),
        "custom_prompt": cfg.get("custom_prompt", ""),
        "ollama_model": get_ollama_model(env.get("ollama_model", "")),
        "ollama_model_override": cfg.get("ollama_model", ""),
        "tts_engine": get_tts_engine(env.get("tts_engine", "sapi")),
        "tts_engine_override": cfg.get("tts_engine", ""),
        "edge_voice": get_edge_voice(env.get("edge_voice", "")),
        "edge_voice_override": cfg.get("edge_voice", ""),
        "personalities": {
            k: {
                "label": v["label"],
                "description": v["description"],
                "prompt": personality_prompts.get(k) or v["prompt"],
                "default_prompt": v["prompt"],
                "is_custom": bool(personality_prompts.get(k)),
            }
            for k, v in PERSONALITIES.items()
        },
        "personality_prompts": personality_prompts,
        "wake_presets": list(WAKE_PRESET_LABELS),
        "tts_engines": list(TTS_ENGINES),
        "edge_voices": list(EDGE_VOICES),
        "singing_enabled": env.get("singing_enabled", False),
        "ha_enabled": env.get("ha_enabled", False),
        "bender_pitch": float(cfg.get("bender_pitch", 10)),
        "bender_index_rate": float(cfg.get("bender_index_rate", 1.0)),
        "bender_protect": float(cfg.get("bender_protect", 0.33)),
    }

