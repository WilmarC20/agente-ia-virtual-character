"""Reparación UTF-8 / mojibake para texto en español (pantalla + TTS)."""

from __future__ import annotations

import re
import unicodedata

_MOJIBAKE_MARKERS = ("Ã", "Â", "â€", "ï¿½")

_MANUAL_FIXES: tuple[tuple[str, str], ...] = (
    ("\u00e2\u20ac\u201d", "\u2014"),
    ("\u00e2\u20ac\u201c", "\u2013"),
    ("\u00e2\u20ac\u00a6", "\u2026"),
    ("\u00e2\u20ac\u2122", "'"),
    ("\u00e2\u20ac\u0153", '"'),
    ("\u00e2\u20ac\u009d", '"'),
)

# Fragmentos con bytes UTF-8 mal interpretados (no tocar texto ya correcto en la misma línea).
_MOJIBAKE_CHUNK = re.compile(
    r"[^\s\"'`]*(?:Ã|Â|â€|ï¿½)[^\s\"'`]*",
    re.UNICODE,
)


def _repair_chunk(chunk: str) -> str:
    for enc in ("cp1252", "latin-1"):
        try:
            fixed = chunk.encode(enc).decode("utf-8")
            if fixed and "\ufffd" not in fixed:
                return fixed
        except (UnicodeEncodeError, UnicodeDecodeError):
            continue
    out = chunk
    for bad, good in _MANUAL_FIXES:
        out = out.replace(bad, good)
    return out


def repair_mojibake(text: str) -> str:
    """Recupera tildes/ñ en trozos corruptos sin romper UTF-8 válido adyacente."""
    if not text:
        return text
    if not any(m in text for m in _MOJIBAKE_MARKERS):
        return unicodedata.normalize("NFC", text)

    def _sub(m: re.Match[str]) -> str:
        return _repair_chunk(m.group(0))

    out = _MOJIBAKE_CHUNK.sub(_sub, text)
    for bad, good in _MANUAL_FIXES:
        out = out.replace(bad, good)
    return unicodedata.normalize("NFC", out)


def prepare_spanish_text(text: str) -> str:
    """Texto listo para mostrar o enviar al TTS (UTF-8 NFC, sin mojibake)."""
    return repair_mojibake(text or "")


def normalize_heard(text: str) -> str:
    """Minúsculas, NFC, solo letras/números españoles (conserva tildes)."""
    t = prepare_spanish_text(text).lower()
    t = re.sub(r"[^a-z0-9áéíóúüñ\s]", " ", t)
    return re.sub(r"\s+", " ", t).strip()


def normalize_heard_ascii(text: str) -> str:
    """Como normalize_heard pero sin tildes (para comparar comandos de voz)."""
    t = normalize_heard(text)
    t = unicodedata.normalize("NFD", t)
    return "".join(c for c in t if unicodedata.category(c) != "Mn")


# Whisper suele confundir «emisora» con «misora», «emisor», etc.
_RADIO_MISHEARINGS: tuple[tuple[str, str], ...] = (
    (
        r"(?i)\b(reproduc\w*|escuch\w*|pon(?:e|é|me|er|ga|gan)?|sintoniz\w*)\s+en\s+(?:la\s+)?(?:misora|mizora|emisora)\b",
        r"\1 la emisora",
    ),
    (r"(?i)\ben\s+misora\b", "en emisora"),
    (r"(?i)\bla\s+misora\b", "la emisora"),
    (r"(?i)\bmisoras?\b", "emisora"),
    (r"(?i)\bmizoras?\b", "emisora"),
    (r"(?i)\bemisoro\b", "emisora"),
    (r"(?i)\bemissora\b", "emisora"),
    (r"(?i)\bhe\s+misora\b", "emisora"),
    (r"(?i)\bhemisora\b", "emisora"),
    (r"(?i)\bemisor\b", "emisora"),
    (r"(?i)\ben\s+mizora\b", "en emisora"),
    (r"(?i)\bla\s+mizora\b", "la emisora"),
)


def fix_radio_transcription(text: str) -> str:
    """Corrige en texto crudo las variantes típicas de Whisper para emisora/radio."""
    if not text:
        return text
    out = prepare_spanish_text(text)
    for pat, repl in _RADIO_MISHEARINGS:
        out = re.sub(pat, repl, out)
    return out


def normalize_radio_speech(text: str) -> str:
    """Texto normalizado listo para detectar comandos de radio/emisora."""
    return normalize_heard(fix_radio_transcription(text or ""))
