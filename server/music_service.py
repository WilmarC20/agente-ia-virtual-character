"""YouTube Music — búsqueda y extracción de audio para el panel admin.

- Búsqueda: YouTube Data API v3 (oficial) si hay YOUTUBE_API_KEY.
- Respaldo búsqueda: ytmusicapi / yt-dlp.
- Stream audio: pytubefix (rápido, sin EJS) → ytmusic / yt-dlp / piped.
Requiere ffmpeg en PATH para PCM al ESP.
"""

from __future__ import annotations

import logging
import os
import queue
import re
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from collections.abc import Iterator
from concurrent.futures import ThreadPoolExecutor, TimeoutError as FuturesTimeoutError
from pathlib import Path
from typing import Any

log = logging.getLogger("brain.music")

SERVER_DIR = Path(__file__).resolve().parent
CACHE_DIR = SERVER_DIR / ".cache-music"
COOKIES_PATH = Path(os.environ.get("YTMUSIC_COOKIES_PATH", str(SERVER_DIR / "ytmusic_cookies.txt")))
DEFAULT_SEARCH_LIMIT = int(os.environ.get("YTMUSIC_SEARCH_LIMIT", "12"))
YTDLP_SEARCH_TIMEOUT_SEC = int(os.environ.get("YTMUSIC_YTDLP_SEARCH_TIMEOUT", "25"))
STREAM_START_TIMEOUT_SEC = int(os.environ.get("YTMUSIC_STREAM_START_TIMEOUT", "120"))
STREAM_URL_TIMEOUT_SEC = int(os.environ.get("YTMUSIC_STREAM_URL_TIMEOUT", "120"))
STREAM_OPEN_TIMEOUT_SEC = int(os.environ.get("YTMUSIC_STREAM_OPEN_TIMEOUT", "240"))
ALLOW_YTDLP_PIPE = os.environ.get("YTMUSIC_ALLOW_YTDLP_PIPE", "1").strip().lower() in ("1", "true", "yes")
TRY_URL_PATH = os.environ.get("YTMUSIC_TRY_URL_PATH", "1").strip().lower() in ("1", "true", "yes")
YTDLP_EJS_REMOTE = (os.environ.get("YTMUSIC_YTDLP_EJS", "") or "").strip().lower()
YTDLP_PLAYER_CLIENT = os.environ.get(
    "YTMUSIC_YTDLP_PLAYER_CLIENT", "android_sdkless,android,tv"
).strip()
REQUIRE_COOKIES_FOR_PLAY = os.environ.get("YTMUSIC_REQUIRE_COOKIES", "0").strip().lower() in (
    "1",
    "true",
    "yes",
)
YOUTUBE_API_KEY = (
    os.environ.get("YOUTUBE_API_KEY") or os.environ.get("GOOGLE_YOUTUBE_API_KEY") or ""
).strip()
YOUTUBE_API_SEARCH_TIMEOUT_SEC = float(os.environ.get("YOUTUBE_API_SEARCH_TIMEOUT", "12"))
PYTUBEFIX_URL_TIMEOUT_SEC = int(os.environ.get("YTMUSIC_PYTUBEFIX_TIMEOUT", "20"))
SKIP_PYTUBEFIX = os.environ.get("YTMUSIC_SKIP_PYTUBEFIX", "0").strip().lower() in ("1", "true", "yes")
PYTUBEFIX_CLIENTS = [
    c.strip()
    for c in os.environ.get("YTMUSIC_PYTUBEFIX_CLIENTS", "ANDROID,WEB,TV_EMBED").split(",")
    if c.strip()
]
def skip_pytubefix() -> bool:
    return os.environ.get("YTMUSIC_SKIP_PYTUBEFIX", "0").strip().lower() in ("1", "true", "yes")


def warm_ytdlp_ejs_enabled() -> bool:
    return os.environ.get("YTMUSIC_WARM_EJS", "").strip().lower() not in ("0", "false", "no")


WARM_YTDLP_EJS = warm_ytdlp_ejs_enabled()
LIGHT_OPEN_RACE = os.environ.get("YTMUSIC_LIGHT_OPEN", "1").strip().lower() in ("1", "true", "yes")
PIPED_INSTANCES_URL = os.environ.get(
    "YTMUSIC_PIPED_INSTANCES_URL", "https://piped-instances.kavin.rocks/"
)
PIPED_FALLBACK_API_BASES = [
    "https://pipedapi.kavin.rocks",
    "https://pipedapi.adminforge.de",
    "https://pipedapi.in.projectsegfau.lt",
    "https://pipedapi.syncpundit.io",
    "https://pipedapi.moomoo.me",
    "https://pipedapi.leptons.xyz",
]

_ytdlp_warmed = False

_lock = threading.Lock()
_proc_lock = threading.Lock()
_active_procs: set[subprocess.Popen[Any]] = set()
_prefetch_lock = threading.Lock()
_prefetch: dict[str, dict[str, Any]] = {}
_prefetch_procs: dict[str, list[subprocess.Popen[Any]]] = {}
_ytm_client: Any | None = None
_ytm_client_ok: bool | None = None
_ytm_public: Any | None = None
_deps_cache: dict[str, bool] | None = None


def warm_deps_cache() -> None:
    """Importa yt_dlp/pytubefix en hilo worker — no bloquear el event loop de FastAPI."""
    global _deps_cache
    if _deps_cache is not None:
        return
    _deps_cache = {
        "yt_dlp": has_yt_dlp(),
        "ffmpeg": has_ffmpeg(),
        "ytmusicapi": has_ytmusicapi(),
        "pytubefix": has_pytubefix(),
        "ytdlp_ejs": has_ytdlp_ejs(),
    }
    log.info(
        "music deps: ffmpeg=%s pytubefix=%s yt-dlp=%s ytmusicapi=%s",
        _deps_cache["ffmpeg"],
        _deps_cache["pytubefix"],
        _deps_cache["yt_dlp"],
        _deps_cache["ytmusicapi"],
    )


def _dep(name: str) -> bool:
    if _deps_cache is not None and name in _deps_cache:
        return bool(_deps_cache[name])
    if name == "yt_dlp":
        return has_yt_dlp()
    if name == "ffmpeg":
        return has_ffmpeg()
    if name == "ytmusicapi":
        return has_ytmusicapi()
    if name == "pytubefix":
        return has_pytubefix()
    if name == "ytdlp_ejs":
        return has_ytdlp_ejs()
    return False


def _ensure_cache() -> None:
    CACHE_DIR.mkdir(parents=True, exist_ok=True)


def cookies_path() -> Path | None:
    p = COOKIES_PATH
    return p if p.is_file() and p.stat().st_size > 32 else None


def has_ffmpeg() -> bool:
    return shutil.which("ffmpeg") is not None


def has_yt_dlp() -> bool:
    try:
        import importlib.util

        return importlib.util.find_spec("yt_dlp") is not None
    except Exception:
        return False


def has_ytmusicapi() -> bool:
    try:
        import importlib.util

        return importlib.util.find_spec("ytmusicapi") is not None
    except Exception:
        return False


def has_youtube_api() -> bool:
    return bool(YOUTUBE_API_KEY)


def has_pytubefix() -> bool:
    try:
        import importlib.util

        return importlib.util.find_spec("pytubefix") is not None
    except Exception:
        return False


def _parse_iso8601_duration(raw: str | None) -> int | None:
    if not raw:
        return None
    m = re.fullmatch(r"PT(?:(\d+)H)?(?:(\d+)M)?(?:(\d+)S)?", raw.strip())
    if not m:
        return None
    h, mi, s = m.groups()
    return int(h or 0) * 3600 + int(mi or 0) * 60 + int(s or 0)


def _format_duration(seconds: int | None) -> str:
    if not seconds or seconds < 0:
        return ""
    m, s = divmod(int(seconds), 60)
    h, m = divmod(m, 60)
    if h:
        return f"{h}:{m:02d}:{s:02d}"
    return f"{m}:{s:02d}"


def _normalize_track(entry: dict[str, Any], *, source: str) -> dict[str, Any] | None:
    vid = entry.get("videoId") or entry.get("id")
    if not vid and entry.get("url"):
        url = str(entry["url"])
        if "v=" in url:
            vid = url.split("v=", 1)[1].split("&")[0]
    if not vid:
        return None
    title = (entry.get("title") or entry.get("name") or "").strip()
    if not title:
        return None
    artists = entry.get("artists") or []
    if isinstance(artists, list) and artists:
        if isinstance(artists[0], dict):
            artist = ", ".join(a.get("name", "") for a in artists if a.get("name"))
        else:
            artist = ", ".join(str(a) for a in artists)
    else:
        artist = (entry.get("artist") or entry.get("uploader") or entry.get("channel") or "").strip()
    dur = entry.get("duration_seconds")
    if dur is None and entry.get("duration"):
        try:
            dur = int(entry["duration"])
        except (TypeError, ValueError):
            dur = None
    if dur is None and isinstance(entry.get("duration"), str) and ":" in entry["duration"]:
        parts = [int(x) for x in entry["duration"].split(":")]
        if len(parts) == 2:
            dur = parts[0] * 60 + parts[1]
        elif len(parts) == 3:
            dur = parts[0] * 3600 + parts[1] * 60 + parts[2]
    thumb = entry.get("thumbnails")
    thumbnail = ""
    if isinstance(thumb, list) and thumb:
        thumbnail = thumb[-1].get("url", "") if isinstance(thumb[-1], dict) else ""
    elif isinstance(entry.get("thumbnail"), str):
        thumbnail = entry["thumbnail"]
    return {
        "id": str(vid),
        "title": title,
        "artist": artist,
        "duration": _format_duration(dur),
        "duration_sec": dur,
        "thumbnail": thumbnail,
        "source": source,
    }


def _get_ytm_client() -> Any | None:
    global _ytm_client, _ytm_client_ok
    path = cookies_path()
    if not path or not has_ytmusicapi():
        _ytm_client = None
        _ytm_client_ok = False
        return None
    if _ytm_client is not None and _ytm_client_ok:
        return _ytm_client
    try:
        from ytmusicapi import YTMusic

        client = YTMusic(str(path))
        _ytm_client = client
        _ytm_client_ok = True
        log.info("YTMusic: cookies cargadas (%s)", path.name)
        return client
    except Exception as e:
        log.warning("YTMusic no pudo iniciar con cookies: %s", e)
        _ytm_client = None
        _ytm_client_ok = False
        return None


def invalidate_session() -> None:
    global _ytm_client, _ytm_client_ok, _ytm_public
    _ytm_client = None
    _ytm_client_ok = None
    _ytm_public = None


def _get_ytm_public_client() -> Any | None:
    """API pública de YT Music (sin cookies); búsqueda rápida y fiable."""
    global _ytm_public
    if not has_ytmusicapi():
        return None
    if _ytm_public is not None:
        return _ytm_public
    try:
        from ytmusicapi import YTMusic

        _ytm_public = YTMusic()
        log.info("YTMusic: cliente público (sin cookies)")
        return _ytm_public
    except Exception as e:
        log.warning("YTMusic público no pudo iniciar: %s", e)
        return None


def has_ytdlp_ejs() -> bool:
    try:
        import importlib.util

        return bool(importlib.util.find_spec("yt_dlp_ejs"))
    except Exception:
        return False


def playback_ready(*, light: bool = True) -> dict[str, Any]:
    """Estado para reproducir."""
    st = auth_status(light=light)
    has_cookies = st.get("account_ok") or st.get("cookies_present")
    can_play = st.get("ready") and (has_cookies or not REQUIRE_COOKIES_FOR_PLAY)
    return {
        **st,
        "ytdlp_ejs_installed": _dep("ytdlp_ejs"),
        "playback_ok": can_play,
        "needs_cookies": REQUIRE_COOKIES_FOR_PLAY and not has_cookies,
        "hint": (
            "Subí cookies de music.youtube.com (admin → Música)."
            if REQUIRE_COOKIES_FOR_PLAY and not has_cookies
            else (
                "Configurá YOUTUBE_API_KEY en secrets.local.ps1 para búsqueda oficial."
                if not st.get("youtube_api")
                else ""
            )
        ),
    }


def ensure_playback_allowed() -> None:
    if not _dep("ffmpeg"):
        raise RuntimeError("ffmpeg no está en PATH — instalalo para reproducir música")
    if not _dep("pytubefix") and not _dep("yt_dlp") and not _dep("ytmusicapi"):
        raise RuntimeError(
            "Sin extractor de audio — pip install pytubefix yt-dlp"
        )


def auth_status(*, light: bool = False) -> dict[str, Any]:
    """Estado deps/auth. light=True evita init YTMusic (no bloquear HTTP)."""
    cp = cookies_path()
    has_client = False
    if not light and not has_youtube_api() and cp:
        has_client = _get_ytm_client() is not None
    node = _node_exe()
    if light and has_youtube_api():
        stream_ok = True
        yt_dlp_ok = _dep("yt_dlp")
        pytubefix_ok = _dep("pytubefix")
        ytm_ok = _dep("ytmusicapi")
        ffmpeg_ok = _dep("ffmpeg")
    else:
        stream_ok = _dep("pytubefix") or _dep("yt_dlp") or _dep("ytmusicapi")
        yt_dlp_ok = _dep("yt_dlp")
        pytubefix_ok = _dep("pytubefix")
        ytm_ok = _dep("ytmusicapi")
        ffmpeg_ok = _dep("ffmpeg")
    return {
        "auth_mode": (
            "youtube_api"
            if has_youtube_api()
            else ("account" if (cp and has_client) else "anonymous")
        ),
        "youtube_api": has_youtube_api(),
        "pytubefix": pytubefix_ok,
        "cookies_present": cp is not None,
        "cookies_path": str(cp) if cp else "",
        "account_ok": bool(cp and has_client),
        "yt_dlp": yt_dlp_ok,
        "ffmpeg": ffmpeg_ok,
        "ytmusicapi": ytm_ok,
        "node_js": bool(node),
        "node_path": node or "",
        "ytdlp_pipe_fallback": ALLOW_YTDLP_PIPE,
        "ytdlp_ejs": YTDLP_EJS_REMOTE or ("local" if _dep("ytdlp_ejs") else "off"),
        "ytdlp_ejs_installed": _dep("ytdlp_ejs"),
        "ytdlp_player_client": YTDLP_PLAYER_CLIENT,
        "ready": ffmpeg_ok
        and stream_ok
        and (has_youtube_api() or yt_dlp_ok or ytm_ok),
    }


def save_cookies(content: bytes) -> None:
    _ensure_cache()
    text = content.decode("utf-8", errors="replace").strip()
    if "youtube" not in text.lower() and ".youtube.com" not in text:
        raise ValueError("El archivo no parece cookies de YouTube (formato Netscape)")
    COOKIES_PATH.write_text(text + "\n", encoding="utf-8")
    invalidate_session()
    log.info("Cookies YT Music guardadas en %s", COOKIES_PATH)


def delete_cookies() -> bool:
    invalidate_session()
    if COOKIES_PATH.is_file():
        COOKIES_PATH.unlink()
        log.info("Cookies YT Music eliminadas")
        return True
    return False


def _ytm_search_with(client: Any, query: str, limit: int) -> list[dict[str, Any]]:
    out: list[dict[str, Any]] = []
    raw = client.search(query, filter="songs", limit=limit)
    for item in raw or []:
        if not isinstance(item, dict):
            continue
        track = _normalize_track(item, source="ytmusic")
        if track:
            out.append(track)
    return out


def _search_youtube_api(query: str, limit: int) -> list[dict[str, Any]]:
    """YouTube Data API v3 — búsqueda oficial (requiere YOUTUBE_API_KEY)."""
    if not has_youtube_api():
        return []
    import httpx

    try:
        with httpx.Client(timeout=YOUTUBE_API_SEARCH_TIMEOUT_SEC) as client:
            r = client.get(
                "https://www.googleapis.com/youtube/v3/search",
                params={
                    "part": "snippet",
                    "type": "video",
                    "videoCategoryId": "10",
                    "maxResults": max(1, min(25, limit)),
                    "q": query,
                    "key": YOUTUBE_API_KEY,
                },
            )
            r.raise_for_status()
            payload = r.json()
            items = payload.get("items") or []
            video_ids: list[str] = []
            snippets: dict[str, dict[str, Any]] = {}
            for item in items:
                if not isinstance(item, dict):
                    continue
                vid = (item.get("id") or {}).get("videoId")
                snip = item.get("snippet") or {}
                if vid:
                    video_ids.append(vid)
                    snippets[vid] = snip
            if not video_ids:
                return []

            r2 = client.get(
                "https://www.googleapis.com/youtube/v3/videos",
                params={
                    "part": "contentDetails,snippet",
                    "id": ",".join(video_ids),
                    "key": YOUTUBE_API_KEY,
                },
            )
            r2.raise_for_status()
            details = {v["id"]: v for v in (r2.json().get("items") or []) if v.get("id")}

        out: list[dict[str, Any]] = []
        for vid in video_ids:
            det = details.get(vid) or {}
            snip = det.get("snippet") or snippets.get(vid) or {}
            dur_sec = _parse_iso8601_duration((det.get("contentDetails") or {}).get("duration"))
            thumbs = snip.get("thumbnails") or {}
            thumb_url = ""
            if isinstance(thumbs, dict) and thumbs:
                best = thumbs.get("medium") or thumbs.get("default") or next(iter(thumbs.values()))
                if isinstance(best, dict):
                    thumb_url = best.get("url") or ""
            track = _normalize_track(
                {
                    "id": vid,
                    "title": snip.get("title") or "",
                    "artist": snip.get("channelTitle") or "",
                    "duration_seconds": dur_sec,
                    "thumbnail": thumb_url,
                },
                source="youtube_api",
            )
            if track:
                out.append(track)
        if out:
            log.info("YouTube API search OK query=%r n=%d", query, len(out))
        return out
    except Exception as e:
        log.warning("YouTube API search failed query=%r: %s", query, e)
        return []


def _search_ytmusic(query: str, limit: int) -> list[dict[str, Any]]:
    """Cuenta (cookies) primero; si no hay, API pública sin login."""
    for getter in (_get_ytm_client, _get_ytm_public_client):
        client = getter()
        if not client:
            continue
        try:
            results = _ytm_search_with(client, query, limit)
            if results:
                return results
        except Exception as e:
            log.warning("YTMusic search failed (%s): %s", getter.__name__, e)
            if getter is _get_ytm_client:
                invalidate_session()
            else:
                global _ytm_public
                _ytm_public = None
    return []


def _search_ytdlp_impl(query: str, limit: int) -> list[dict[str, Any]]:
    import yt_dlp

    opts = {
        "quiet": True,
        "no_warnings": True,
        "extract_flat": True,
        "skip_download": True,
        "socket_timeout": 15,
    }
    cp = cookies_path()
    if cp:
        opts["cookiefile"] = str(cp)
    out: list[dict[str, Any]] = []
    with yt_dlp.YoutubeDL(opts) as ydl:
        info = ydl.extract_info(f"ytsearch{limit}:{query}", download=False)
    for entry in (info or {}).get("entries") or []:
        if not isinstance(entry, dict):
            continue
        track = _normalize_track(entry, source="youtube")
        if track:
            out.append(track)
    return out


def _search_ytdlp(query: str, limit: int) -> list[dict[str, Any]]:
    """Respaldo lento; ytsearch a veces cuelga — límite de tiempo estricto."""
    with ThreadPoolExecutor(max_workers=1) as pool:
        fut = pool.submit(_search_ytdlp_impl, query, limit)
        try:
            return fut.result(timeout=YTDLP_SEARCH_TIMEOUT_SEC)
        except FuturesTimeoutError:
            log.warning("yt-dlp search timeout (%ss) query=%r", YTDLP_SEARCH_TIMEOUT_SEC, query)
            fut.cancel()
            return []
        except Exception as e:
            log.warning("yt-dlp search failed: %s", e)
            return []


def search(query: str, *, limit: int | None = None) -> dict[str, Any]:
    q = (query or "").strip()
    if not q:
        raise ValueError("query vacía")
    lim = max(1, min(25, limit or DEFAULT_SEARCH_LIMIT))

    if has_youtube_api():
        if not shutil.which("ffmpeg"):
            raise RuntimeError("ffmpeg no está en PATH — instalalo para reproducir música")
        results = _search_youtube_api(q, lim)
        st = auth_status(light=True)
        return {
            **st,
            "auth_mode": "youtube_api" if results else st["auth_mode"],
            "query": q,
            "results": results[:lim],
        }

    if not _dep("ffmpeg"):
        raise RuntimeError("ffmpeg no está en PATH — instalalo para reproducir música")
    if not _dep("ytmusicapi") and not _dep("yt_dlp"):
        raise RuntimeError(
            "Configurá YOUTUBE_API_KEY (API oficial) o instalá yt-dlp / ytmusicapi"
        )
    status = auth_status(light=True)
    results: list[dict[str, Any]] = []
    auth_mode = status["auth_mode"]
    results = _search_ytmusic(q, lim)
    if results:
        auth_mode = status["auth_mode"]
    if not results and _dep("yt_dlp"):
        results = _search_ytdlp(q, lim)
        auth_mode = "anonymous" if not status["account_ok"] else status["auth_mode"]
    return {
        **status,
        "auth_mode": auth_mode,
        "query": q,
        "results": results[:lim],
    }


def _video_url(video_id: str) -> str:
    vid = (video_id or "").strip()
    if not vid or len(vid) > 32:
        raise ValueError("video_id inválido")
    return f"https://www.youtube.com/watch?v={vid}"


def extract_audio_mp3(video_id: str) -> tuple[bytes, dict[str, Any]]:
    """Descarga audio y devuelve MP3 + metadatos."""
    import yt_dlp

    if not has_ffmpeg():
        raise RuntimeError("ffmpeg no está en PATH — instalalo para reproducir música")
    url = _video_url(video_id)
    _ensure_cache()
    with tempfile.TemporaryDirectory(prefix="ytmusic_", dir=CACHE_DIR) as td:
        outtmpl = str(Path(td) / "track.%(ext)s")
        opts: dict[str, Any] = {
            "quiet": True,
            "no_warnings": True,
            "format": "bestaudio/best",
            "outtmpl": outtmpl,
            "postprocessors": [
                {
                    "key": "FFmpegExtractAudio",
                    "preferredcodec": "mp3",
                    "preferredquality": "192",
                }
            ],
            "noplaylist": True,
        }
        cp = cookies_path()
        if cp:
            opts["cookiefile"] = str(cp)
        with _lock:
            with yt_dlp.YoutubeDL(opts) as ydl:
                info = ydl.extract_info(url, download=True)
        files = list(Path(td).glob("track.*"))
        if not files:
            raise RuntimeError("yt-dlp no generó archivo de audio")
        data = files[0].read_bytes()
        if len(data) > MAX_AUDIO_BYTES:
            raise RuntimeError(f"audio demasiado grande (>{MAX_AUDIO_BYTES // (1024*1024)} MB)")
        meta = {
            "id": video_id,
            "title": (info or {}).get("title") or "",
            "artist": (info or {}).get("artist") or (info or {}).get("uploader") or "",
            "duration_sec": (info or {}).get("duration"),
        }
        return data, meta


def _mp3_bytes_to_wav_16k_mono(mp3_bytes: bytes) -> bytes:
    import subprocess

    with tempfile.TemporaryDirectory(prefix="ytwav_", dir=CACHE_DIR) as td:
        mp3_path = Path(td) / "track.mp3"
        wav_path = Path(td) / "track.wav"
        mp3_path.write_bytes(mp3_bytes)
        proc = subprocess.run(
            [
                "ffmpeg",
                "-y",
                "-loglevel",
                "error",
                "-i",
                str(mp3_path),
                "-ar",
                "16000",
                "-ac",
                "1",
                str(wav_path),
            ],
            capture_output=True,
            timeout=600,
        )
        if proc.returncode != 0:
            err = proc.stderr.decode("utf-8", errors="replace").strip()
            raise RuntimeError(err or f"ffmpeg exit {proc.returncode}")
        if not wav_path.is_file() or wav_path.stat().st_size < 44:
            raise RuntimeError("ffmpeg no generó WAV válido")
        return wav_path.read_bytes()


def extract_audio_wav_16k(video_id: str) -> tuple[bytes, dict[str, Any]]:
    """Audio listo para el ESP32: WAV PCM 16 kHz mono (mismo formato que /tts)."""
    mp3, meta = extract_audio_mp3(video_id)
    wav = _mp3_bytes_to_wav_16k_mono(mp3)
    return wav, meta


def _track_meta_from_info(video_id: str, info: dict[str, Any] | None) -> dict[str, Any]:
    info = info or {}
    return {
        "id": video_id,
        "title": info.get("title") or "",
        "artist": info.get("artist") or info.get("uploader") or "",
        "duration_sec": info.get("duration"),
    }


def probe_track_meta(video_id: str) -> dict[str, Any]:
    """Metadatos sin descargar audio (YouTube API / ytmusicapi / yt-dlp)."""
    vid = (video_id or "").strip()
    if not vid:
        raise ValueError("video_id inválido")

    if has_youtube_api():
        meta = _probe_track_meta_youtube_api(vid)
        if meta and meta.get("title"):
            return meta

    for getter in (_get_ytm_client, _get_ytm_public_client):
        client = getter()
        if not client:
            continue
        try:
            song = client.get_song(vid)
            vd = song.get("videoDetails") or {}
            title = (vd.get("title") or "").strip()
            if title:
                dur = vd.get("lengthSeconds")
                try:
                    dur_sec = int(dur) if dur is not None else None
                except (TypeError, ValueError):
                    dur_sec = None
                return {
                    "id": vid,
                    "title": title,
                    "artist": (vd.get("author") or "").strip(),
                    "duration_sec": dur_sec,
                }
        except Exception as e:
            log.warning("YTMusic probe failed (%s): %s", getter.__name__, e)

    if not has_yt_dlp():
        raise RuntimeError("no se pudo resolver el video (YouTube API / ytmusicapi / yt-dlp)")
    with ThreadPoolExecutor(max_workers=1) as pool:
        fut = pool.submit(_probe_track_meta_ytdlp, vid)
        try:
            return fut.result(timeout=25)
        except FuturesTimeoutError:
            raise RuntimeError("timeout obteniendo metadatos del video") from None


def _probe_track_meta_youtube_api(video_id: str) -> dict[str, Any] | None:
    import httpx

    try:
        with httpx.Client(timeout=12.0) as client:
            r = client.get(
                "https://www.googleapis.com/youtube/v3/videos",
                params={
                    "part": "contentDetails,snippet",
                    "id": video_id,
                    "key": YOUTUBE_API_KEY,
                },
            )
            r.raise_for_status()
            items = r.json().get("items") or []
            if not items:
                return None
            item = items[0]
            snip = item.get("snippet") or {}
            dur_sec = _parse_iso8601_duration((item.get("contentDetails") or {}).get("duration"))
            return {
                "id": video_id,
                "title": (snip.get("title") or "").strip(),
                "artist": (snip.get("channelTitle") or "").strip(),
                "duration_sec": dur_sec,
            }
    except Exception as e:
        log.warning("YouTube API probe failed id=%s: %s", video_id, e)
        return None


def _probe_track_meta_ytdlp(video_id: str) -> dict[str, Any]:
    import yt_dlp

    url = _video_url(video_id)
    opts: dict[str, Any] = {
        "quiet": True,
        "no_warnings": True,
        "skip_download": True,
        "noplaylist": True,
        "socket_timeout": 15,
        "extractor_args": {"youtube": {"player_client": YTDLP_PLAYER_CLIENT.split(",")}},
    }
    if YTDLP_EJS_REMOTE:
        opts["remote_components"] = [f"ejs:{YTDLP_EJS_REMOTE}"]
    cp = cookies_path()
    if cp:
        opts["cookiefile"] = str(cp)
    with yt_dlp.YoutubeDL(opts) as ydl:
        info = ydl.extract_info(url, download=False)
    if not info:
        raise RuntimeError("video no encontrado")
    return _track_meta_from_info(video_id, info)


def _register_proc(proc: subprocess.Popen[Any]) -> subprocess.Popen[Any]:
    with _proc_lock:
        _active_procs.add(proc)
    return proc


def _unregister_proc(proc: subprocess.Popen[Any]) -> None:
    with _proc_lock:
        _active_procs.discard(proc)


def shutdown_all_streams() -> None:
    """Mata yt-dlp/ffmpeg activos (Ctrl+C, timeout, cliente desconectado)."""
    with _proc_lock:
        procs = list(_active_procs)
        _active_procs.clear()
    with _prefetch_lock:
        for vid, procs in list(_prefetch_procs.items()):
            _kill_procs(procs)
        _prefetch_procs.clear()
    for proc in procs:
        _terminate_proc(proc, unregister=False)
    if procs:
        log.info("music: %d proceso(s) de stream terminados", len(procs))


def _terminate_proc(proc: subprocess.Popen[Any] | None, *, unregister: bool = True) -> None:
    if proc is None:
        return
    if unregister:
        _unregister_proc(proc)
    pid = proc.pid
    try:
        if proc.poll() is None:
            if sys.platform == "win32" and pid:
                subprocess.run(
                    ["taskkill", "/F", "/T", "/PID", str(pid)],
                    capture_output=True,
                    timeout=8,
                )
            else:
                proc.kill()
        proc.wait(timeout=3)
    except Exception:
        try:
            if proc.poll() is None:
                proc.kill()
        except Exception:
            pass


def _node_exe() -> str | None:
    return shutil.which("node")


def _ytdlp_cli_base() -> list[str]:
    cmd = [sys.executable, "-m", "yt_dlp"]
    node = _node_exe()
    if node:
        cmd.extend(["--js-runtimes", f"node:{node}"])
    elif not has_ytdlp_ejs():
        log.warning("Node.js no está en PATH — yt-dlp puede tardar mucho en YouTube")
    if YTDLP_EJS_REMOTE:
        cmd.extend(["--remote-components", f"ejs:{YTDLP_EJS_REMOTE}"])
    elif not has_ytdlp_ejs():
        # Sin paquete local: permitir descarga EJS (necesario para YouTube anónimo).
        cmd.extend(["--remote-components", "ejs:github"])
    cmd.extend(
        [
            "--no-warnings",
            "--socket-timeout",
            "15",
            "--extractor-args",
            f"youtube:player_client={YTDLP_PLAYER_CLIENT}",
        ]
    )
    cp = cookies_path()
    if cp:
        cmd.extend(["--cookies", str(cp)])
    return cmd


def _popen_tracked(*args: Any, **kwargs: Any) -> subprocess.Popen[bytes]:
    kwargs.setdefault("creationflags", subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
    return _register_proc(subprocess.Popen(*args, **kwargs))


def _popen_local(procs: list[subprocess.Popen[Any]], *args: Any, **kwargs: Any) -> subprocess.Popen[bytes]:
    kwargs.setdefault("creationflags", subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0)
    proc = _register_proc(subprocess.Popen(*args, **kwargs))
    procs.append(proc)
    return proc


def _ytdlp_python_opts() -> dict[str, Any]:
    opts: dict[str, Any] = {
        "quiet": True,
        "no_warnings": True,
        "socket_timeout": 15,
        "extractor_args": {"youtube": {"player_client": YTDLP_PLAYER_CLIENT.split(",")}},
    }
    if YTDLP_EJS_REMOTE:
        opts["remote_components"] = [f"ejs:{YTDLP_EJS_REMOTE}"]
    elif not has_ytdlp_ejs():
        opts["remote_components"] = ["ejs:github"]
    node = _node_exe()
    if node:
        opts["js_runtimes"] = {"node": {"path": node}}
    cp = cookies_path()
    if cp:
        opts["cookiefile"] = str(cp)
    return opts


def _proc_error_tail(proc: subprocess.Popen[Any] | None, *, limit: int = 800) -> str:
    if proc is None or not proc.stderr:
        return ""
    try:
        raw = proc.stderr.read(limit)
        return raw.decode("utf-8", errors="replace").strip()
    except Exception:
        return ""


def _ytdlp_pipe_cmd(url: str) -> list[str]:
    return _ytdlp_cli_base() + [
        "-f",
        "ba/b",
        "--no-playlist",
        "--no-part",
        "-o",
        "-",
        url,
    ]


def _cached_audio_paths(video_id: str) -> list[Path]:
    base = CACHE_DIR / video_id
    return [Path(str(base) + ext) for ext in (".m4a", ".webm", ".opus", ".mp3", ".mkv", ".ogg")]


def _find_cached_audio(video_id: str) -> Path | None:
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    for path in sorted(CACHE_DIR.glob(f"{video_id}.*")):
        if not path.is_file():
            continue
        if path.suffix.lower() in (".part", ".tmp", ".json"):
            continue
        try:
            if path.stat().st_size > 12_000:
                return path
        except OSError:
            continue
    return None


def _download_audio_ytdlp(video_id: str) -> Path:
    """Descarga audio a .cache-music/ — más fiable que pipe stdout en Windows."""
    if not has_yt_dlp():
        raise RuntimeError("yt-dlp no instalado")
    hit = _find_cached_audio(video_id)
    if hit:
        log.info("music cache hit id=%s path=%s", video_id, hit.name)
        return hit
    CACHE_DIR.mkdir(parents=True, exist_ok=True)
    out_base = str(CACHE_DIR / video_id)
    import yt_dlp

    opts = dict(_ytdlp_python_opts())
    opts.update(
        {
            "format": "bestaudio/best",
            "outtmpl": out_base + ".%(ext)s",
            "noplaylist": True,
            "overwrites": True,
            "retries": 3,
            "fragment_retries": 3,
        }
    )
    url = _video_url(video_id)
    log.info("music download id=%s (yt-dlp → cache)", video_id)
    with yt_dlp.YoutubeDL(opts) as ydl:
        info = ydl.extract_info(url, download=True)
    hit = _find_cached_audio(video_id)
    if not hit and info:
        ext = (info.get("ext") or "").strip()
        if ext:
            guess = CACHE_DIR / f"{video_id}.{ext}"
            if guess.is_file() and guess.stat().st_size > 12_000:
                hit = guess
    if not hit:
        raise RuntimeError(
            f"yt-dlp no dejó archivo de audio en cache ({CACHE_DIR / video_id}.*)"
        )
    log.info("music download ok id=%s bytes=%d", video_id, hit.stat().st_size)
    return hit


def _read_first_chunk(
    stream: Any,
    nbytes: int,
    timeout_sec: int,
    procs: list[subprocess.Popen[Any]] | None = None,
) -> bytes:
    with ThreadPoolExecutor(max_workers=1) as pool:
        fut = pool.submit(stream.read, nbytes)
        try:
            data = fut.result(timeout=timeout_sec)
        except FuturesTimeoutError:
            if procs:
                _kill_procs(procs)
            raise RuntimeError(
                f"sin audio en {timeout_sec}s — yt-dlp/ffmpeg no arrancó (1ª vez puede tardar 2 min)"
            ) from None
    return data or b""


def _pick_audio_url(info: dict[str, Any]) -> str | None:
    direct = info.get("url")
    if direct:
        return str(direct)
    best_url: str | None = None
    best_br = -1.0
    for fmt in info.get("formats") or []:
        if not isinstance(fmt, dict):
            continue
        url = fmt.get("url")
        if not url:
            continue
        if fmt.get("vcodec") not in (None, "none") and fmt.get("acodec") in (None, "none"):
            continue
        br = float(fmt.get("abr") or fmt.get("tbr") or 0)
        if br >= best_br:
            best_br = br
            best_url = str(url)
    return best_url


def _kill_procs(procs: list[subprocess.Popen[Any]]) -> None:
    for proc in procs:
        _terminate_proc(proc)


def _register_procs(procs: list[subprocess.Popen[Any]]) -> None:
    for proc in procs:
        _register_proc(proc)


def _piped_api_bases() -> list[str]:
    """Instancias Piped públicas (sin cookies)."""
    cached = getattr(_piped_api_bases, "_cache", None)
    if cached is not None:
        return cached
    bases: list[str] = list(PIPED_FALLBACK_API_BASES)
    try:
        import httpx

        r = httpx.get(PIPED_INSTANCES_URL, timeout=10)
        for inst in r.json() or []:
            api = str((inst or {}).get("api_url") or "").rstrip("/")
            if api and api not in bases:
                bases.insert(0, api)
    except Exception as e:
        log.debug("piped instances list failed: %s", e)
    _piped_api_bases._cache = bases  # type: ignore[attr-defined]
    return bases


def _resolve_stream_url_piped(video_id: str) -> str | None:
    """Audio vía API Piped (anónimo, sin yt-dlp)."""
    try:
        import httpx
    except ImportError:
        return None
    for api in _piped_api_bases()[:15]:
        try:
            r = httpx.get(
                f"{api}/streams/{video_id}",
                timeout=12,
                headers={"User-Agent": "Mozilla/5.0"},
            )
            if r.status_code != 200 or not r.content or r.content[:1] != b"{":
                continue
            data = r.json()
            streams = data.get("audioStreams") or []
            if not streams:
                continue
            best = max(streams, key=lambda s: int(s.get("bitrate") or 0))
            url = best.get("url")
            if url and str(url).startswith("http"):
                log.info("music url piped id=%s api=%s", video_id, api)
                return str(url)
        except Exception as e:
            log.debug("piped %s id=%s: %s", api, video_id, e)
    return None


def _resolve_stream_url_ytmusic(video_id: str) -> str | None:
    """URL googlevideo vía innertube (rápido con cookies; ~1s)."""
    for getter in (_get_ytm_client, _get_ytm_public_client):
        client = getter()
        if not client:
            continue
        try:
            song = client.get_song(video_id)
            sd = song.get("streamingData") or {}
            formats = sd.get("adaptiveFormats") or sd.get("formats") or []
            best_url: str | None = None
            best_br = -1
            for fmt in formats:
                if not isinstance(fmt, dict):
                    continue
                mime = str(fmt.get("mimeType") or "")
                if not mime.startswith("audio/"):
                    continue
                url = fmt.get("url")
                if url and str(url).startswith("http"):
                    br = int(fmt.get("bitrate") or 0)
                    if br >= best_br:
                        best_br = br
                        best_url = str(url)
            if best_url:
                log.info("music url ytmusic (%s) id=%s", getter.__name__, video_id)
                return best_url
        except Exception as e:
            log.warning("ytmusic url failed (%s) id=%s: %s", getter.__name__, video_id, e)
    return None


def _resolve_stream_url_pytubefix(video_id: str, client: str | None = None) -> str | None:
    """URL de audio vía pytubefix (innertube, sin EJS de yt-dlp)."""
    if not has_pytubefix():
        return None
    yt_client = (client or PYTUBEFIX_CLIENTS[0] if PYTUBEFIX_CLIENTS else "ANDROID").strip()

    def _work() -> str | None:
        from pytubefix import YouTube

        yt = YouTube(
            _video_url(video_id),
            client=yt_client,
            use_oauth=False,
            allow_oauth_cache=False,
        )
        stream = yt.streams.filter(only_audio=True).order_by("abr").desc().first()
        if stream is None:
            stream = yt.streams.get_audio_only()
        if stream and stream.url and str(stream.url).startswith("http"):
            return str(stream.url)
        return None

    with ThreadPoolExecutor(max_workers=1) as pool:
        fut = pool.submit(_work)
        try:
            url = fut.result(timeout=PYTUBEFIX_URL_TIMEOUT_SEC)
        except FuturesTimeoutError:
            log.warning(
                "pytubefix url timeout (%ss) id=%s client=%s",
                PYTUBEFIX_URL_TIMEOUT_SEC,
                video_id,
                yt_client,
            )
            return None
        except Exception as e:
            log.warning("pytubefix url failed id=%s client=%s: %s", video_id, yt_client, e)
            return None
    if url:
        log.info("music url pytubefix id=%s client=%s", video_id, yt_client)
    return url


def _resolve_stream_url_ytdlp(video_id: str, procs: list[subprocess.Popen[Any]] | None = None) -> str:
    """URL directa vía yt-dlp CLI (killable, usa Node/EJS si está en PATH)."""
    url = _video_url(video_id)
    cmd = _ytdlp_cli_base() + ["-g", "-f", "bestaudio/best", "--no-playlist", url]
    if procs is not None:
        proc = _popen_local(procs, cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    else:
        proc = _popen_tracked(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        out, err = proc.communicate(timeout=STREAM_URL_TIMEOUT_SEC)
    except subprocess.TimeoutExpired:
        _terminate_proc(proc)
        raise RuntimeError("yt-dlp timeout resolviendo URL") from None
    if proc.returncode != 0:
        msg = err.decode("utf-8", errors="replace").strip()
        log.warning("yt-dlp -g stderr id=%s: %s", video_id, (msg or "sin stderr")[:400])
        raise RuntimeError(msg or f"yt-dlp exit {proc.returncode}")
    line = out.decode("utf-8", errors="replace").strip().splitlines()
    if not line or not line[0].startswith("http"):
        raise RuntimeError("yt-dlp no devolvió URL de audio")
    return line[0]


def _resolve_stream_url(video_id: str) -> str | None:
    url = _resolve_stream_url_pytubefix(video_id)
    if url:
        return url
    try:
        return _resolve_stream_url_ytdlp(video_id)
    except FuturesTimeoutError:
        log.warning("resolve stream url timeout (%ss) id=%s", STREAM_URL_TIMEOUT_SEC, video_id)
        return None
    except Exception as e:
        log.warning("resolve stream url failed id=%s: %s", video_id, e)
        return None


def _ffmpeg_pcm_cmd(
    *, input_arg: str, from_pipe: bool, realtime: bool = True, local_file: bool = False
) -> list[str]:
    """PCM s16le 16 kHz mono. -re en URL directa evita ráfagas; archivos locales solo -re."""
    ffmpeg_bin = shutil.which("ffmpeg") or "ffmpeg"
    cmd = [ffmpeg_bin, "-nostdin", "-loglevel", "error"]
    if not from_pipe:
        if realtime:
            cmd.append("-re")
        if realtime and not local_file:
            cmd.extend(
                [
                    "-reconnect",
                    "1",
                    "-reconnect_streamed",
                    "1",
                    "-reconnect_delay_max",
                    "5",
                    "-rw_timeout",
                    "15000000",
                ]
            )
    else:
        cmd.extend(["-fflags", "+nobuffer"])
    cmd.extend(
        [
            "-i",
            input_arg,
            "-vn",
            "-af",
            "aresample=16000,aformat=sample_fmts=s16:channel_layouts=mono",
            "-f",
            "s16le",
            "pipe:1",
        ]
    )
    return cmd


def _iter_pcm_from_ffmpeg_stdout(ff_proc: subprocess.Popen[bytes]) -> Iterator[bytes]:
    assert ff_proc.stdout is not None
    pcm_chunk = 16384 - (16384 % 2)  # trozos grandes → menos overhead WiFi al ESP
    first = _read_first_chunk(ff_proc.stdout, pcm_chunk, STREAM_START_TIMEOUT_SEC)
    if not first:
        err = _proc_error_tail(ff_proc)
        raise RuntimeError(err or "ffmpeg no devolvió audio")
    yield first
    while True:
        chunk = ff_proc.stdout.read(pcm_chunk)
        if not chunk:
            break
        yield chunk
    rc = ff_proc.wait(timeout=10)
    if rc != 0:
        err = _proc_error_tail(ff_proc)
        if err:
            log.warning("ffmpeg stream exit %s: %s", rc, err[:200])


def _iter_pcm_from_direct_url(stream_url: str) -> Iterator[bytes]:
    ff_proc = _popen_tracked(
        _ffmpeg_pcm_cmd(input_arg=stream_url, from_pipe=False),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        yield from _iter_pcm_from_ffmpeg_stdout(ff_proc)
    finally:
        _terminate_proc(ff_proc)


def _iter_pcm_from_ytdlp_pipe(page_url: str) -> Iterator[bytes]:
    yt_proc: subprocess.Popen[bytes] | None = None
    ff_proc: subprocess.Popen[bytes] | None = None
    try:
        yt_proc = _popen_tracked(
            _ytdlp_pipe_cmd(page_url),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        assert yt_proc.stdout is not None
        ff_proc = _popen_tracked(
            _ffmpeg_pcm_cmd(input_arg="pipe:0", from_pipe=True),
            stdin=yt_proc.stdout,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        yt_proc.stdout.close()
        yield from _iter_pcm_from_ffmpeg_stdout(ff_proc)
    finally:
        _terminate_proc(ff_proc)
        _terminate_proc(yt_proc)


def iter_pcm_16k_stream(video_id: str) -> Iterator[bytes]:
    """PCM s16le 16 kHz mono — ffmpeg sobre URL directa (pipe solo si ALLOW_YTDLP_PIPE=1)."""
    if not has_ffmpeg():
        raise RuntimeError("ffmpeg no está en PATH — instalalo para reproducir música")
    if not has_yt_dlp():
        raise RuntimeError("yt-dlp no instalado — pip install yt-dlp")

    page_url = _video_url(video_id)
    stream_url = _resolve_stream_url(video_id)
    if stream_url:
        log.info("music stream: URL directa id=%s", video_id)
        yield from _iter_pcm_from_direct_url(stream_url)
        return
    if ALLOW_YTDLP_PIPE:
        log.info("music stream: pipe yt-dlp id=%s", video_id)
        yield from _iter_pcm_from_ytdlp_pipe(page_url)
        return
    node = _node_exe()
    hint = "Node.js en PATH y pip install -U yt-dlp" if not node else "pip install -U yt-dlp o cookies YT Music"
    raise RuntimeError(
        f"No se pudo obtener audio en {STREAM_URL_TIMEOUT_SEC}s ({hint}). "
        "Para modo lento: $env:YTMUSIC_ALLOW_YTDLP_PIPE='1'"
    )


def _open_file_pcm(
    path: Path, *, realtime: bool = False
) -> tuple[Iterator[bytes], bytes, list[subprocess.Popen[Any]]]:
    procs: list[subprocess.Popen[Any]] = []
    try:
        ff_proc = _popen_local(
            procs,
            _ffmpeg_pcm_cmd(input_arg=str(path), from_pipe=False, realtime=realtime, local_file=True),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        assert ff_proc.stdout is not None
        first = _read_first_chunk(ff_proc.stdout, 32768, STREAM_START_TIMEOUT_SEC, procs)
        if not first:
            err = _proc_error_tail(ff_proc)
            raise RuntimeError(err or "ffmpeg no devolvió audio (archivo)")
        return _ffmpeg_pcm_tail(ff_proc, first), first, procs
    except Exception:
        _kill_procs(procs)
        raise


def _open_download_pcm(video_id: str) -> tuple[Iterator[bytes], bytes, list[subprocess.Popen[Any]]]:
    path = _download_audio_ytdlp(video_id)
    return _open_file_pcm(path, realtime=False)


def _open_cached_file_pcm(video_id: str) -> tuple[Iterator[bytes], bytes, list[subprocess.Popen[Any]]]:
    path = _download_audio_ytdlp(video_id)
    log.info("music stream cache file id=%s name=%s", video_id, path.name)
    return _open_file_pcm(path, realtime=False)


def _open_direct_url_pcm(stream_url: str) -> tuple[Iterator[bytes], bytes, list[subprocess.Popen[Any]]]:
    procs: list[subprocess.Popen[Any]] = []
    try:
        ff_proc = _popen_local(
            procs,
            _ffmpeg_pcm_cmd(input_arg=stream_url, from_pipe=False, realtime=True),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        assert ff_proc.stdout is not None
        first = _read_first_chunk(ff_proc.stdout, 32768, STREAM_START_TIMEOUT_SEC, procs)
        if not first:
            err = _proc_error_tail(ff_proc)
            raise RuntimeError(err or "ffmpeg no devolvió audio")
        return _ffmpeg_pcm_tail(ff_proc, first), first, procs
    except Exception:
        _kill_procs(procs)
        raise


def _open_url_pcm(video_id: str) -> tuple[Iterator[bytes], bytes, list[subprocess.Popen[Any]]]:
    ytdlp_procs: list[subprocess.Popen[Any]] = []
    try:
        stream_url = _resolve_stream_url_ytdlp(video_id, ytdlp_procs)
        return _open_direct_url_pcm(stream_url)
    except Exception:
        _kill_procs(ytdlp_procs)
        raise


def _open_pipe_pcm(page_url: str) -> tuple[Iterator[bytes], bytes, list[subprocess.Popen[Any]]]:
    procs: list[subprocess.Popen[Any]] = []
    try:
        yt_proc = _popen_local(
            procs,
            _ytdlp_pipe_cmd(page_url),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        assert yt_proc.stdout is not None
        ff_proc = _popen_local(
            procs,
            _ffmpeg_pcm_cmd(input_arg="pipe:0", from_pipe=True),
            stdin=yt_proc.stdout,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        yt_proc.stdout.close()
        assert ff_proc.stdout is not None
        first = _read_first_chunk(ff_proc.stdout, 32768, STREAM_START_TIMEOUT_SEC, procs)
        if not first:
            err = _proc_error_tail(ff_proc) or _proc_error_tail(yt_proc)
            if yt_proc.poll() not in (None, 0):
                err = err or f"yt-dlp exit {yt_proc.returncode}"
            raise RuntimeError(err or "ffmpeg no devolvió audio (pipe)")
        return _ffmpeg_pcm_tail(ff_proc, first), first, procs
    except Exception:
        _kill_procs(procs)
        raise


def _ffmpeg_pcm_tail(
    ff_proc: subprocess.Popen[bytes], first: bytes
) -> Iterator[bytes]:
    assert ff_proc.stdout is not None

    def _gen() -> Iterator[bytes]:
        yield first
        try:
            while True:
                chunk = ff_proc.stdout.read(32768)
                if not chunk:
                    break
                yield chunk
            rc = ff_proc.wait(timeout=10)
            if rc != 0:
                err = _proc_error_tail(ff_proc)
                if err:
                    log.warning("ffmpeg stream exit %s: %s", rc, err[:200])
        finally:
            _terminate_proc(ff_proc)

    return _gen()


def warm_ytdlp_ejs() -> None:
    """Precarga scripts EJS de YouTube (opcional; pesado — omitir si pytubefix basta)."""
    global _ytdlp_warmed
    if _ytdlp_warmed or not has_yt_dlp():
        return
    if not should_warm_ytdlp_ejs():
        log.info("yt-dlp EJS warm omitido (pytubefix/YouTube API activos o YTMUSIC_WARM_EJS=0)")
        _ytdlp_warmed = True
        return
    _ytdlp_warmed = True
    log.info("yt-dlp: precalentando EJS (1ª vez ~1-3 min en segundo plano)...")
    try:
        cmd = _ytdlp_cli_base() + [
            "--skip-download",
            "--print",
            "id",
            "--no-playlist",
            "https://www.youtube.com/watch?v=jNQXAC9IVRw",
        ]
        proc = subprocess.run(cmd, capture_output=True, timeout=240)
        if proc.returncode == 0:
            log.info("yt-dlp EJS precalentado OK")
        else:
            err = proc.stderr.decode("utf-8", errors="replace").strip()
            log.warning("yt-dlp warm exit %s: %s", proc.returncode, (err or "sin stderr")[:300])
    except subprocess.TimeoutExpired:
        log.warning("yt-dlp warm timeout 240s — se reintentará al reproducir")
    except Exception as e:
        log.warning("yt-dlp warm failed: %s", e)


def should_warm_ytdlp_ejs() -> bool:
    """EJS warm obligatorio con pytubefix off (solo queda yt-dlp download)."""
    if skip_pytubefix() and has_yt_dlp():
        return True
    if not warm_ytdlp_ejs_enabled():
        return False
    if has_pytubefix() and has_youtube_api():
        return False
    if has_pytubefix() and os.environ.get("YTMUSIC_SKIP_EJS_WARM", "1").strip().lower() in (
        "1",
        "true",
        "yes",
    ):
        return False
    return True


def _open_pcm_stream_impl(video_id: str) -> tuple[Iterator[bytes], bytes, list[subprocess.Popen[Any]]]:
    """Carrera en paralelo: piped, ytmusic, yt-dlp URL, yt-dlp pipe — gana el primero con audio."""
    ensure_playback_allowed()

    page_url = _video_url(video_id)
    if skip_pytubefix() and has_yt_dlp():
        log.info("music open download-only id=%s", video_id)
        return _open_cached_file_pcm(video_id)

    result_q: queue.Queue[tuple[str, Iterator[bytes], bytes, list[subprocess.Popen[Any]]]] = (
        queue.Queue(maxsize=1)
    )
    errors: list[str] = []
    err_lock = threading.Lock()
    worker_procs: dict[str, list[subprocess.Popen[Any]]] = {}

    def _worker(kind: str, opener) -> None:
        try:
            it, first, procs = opener()
            worker_procs[kind] = procs
            result_q.put((kind, it, first, procs), block=False)
        except Exception as e:
            with err_lock:
                errors.append(f"{kind}: {e}")
            log.info("music open %s falló id=%s: %s", kind, video_id, e)

    def _direct_worker(label: str, getter) -> None:
        try:
            url = getter()
            if not url:
                return
            it, first, procs = _open_direct_url_pcm(url)
            worker_procs[label] = procs
            result_q.put((label, it, first, procs), block=False)
        except Exception as e:
            with err_lock:
                errors.append(f"{label}: {e}")
            log.info("music open %s falló id=%s: %s", label, video_id, e)

    threads: list[threading.Thread] = []
    if has_pytubefix() and not SKIP_PYTUBEFIX:
        for yt_client in PYTUBEFIX_CLIENTS or ["ANDROID"]:
            label = f"pytubefix-{yt_client.lower()}"
            threads.append(
                threading.Thread(
                    target=_direct_worker,
                    args=(label, lambda c=yt_client: _resolve_stream_url_pytubefix(video_id, c)),
                    daemon=True,
                )
            )
    light = LIGHT_OPEN_RACE and (has_pytubefix() or SKIP_PYTUBEFIX)
    if light:
        # Descarga a cache: más fiable que pipe stdout (Windows / EJS lento).
        if has_yt_dlp():
            threads.append(
                threading.Thread(
                    target=_worker,
                    args=("download", lambda: _open_download_pcm(video_id)),
                    daemon=True,
                )
            )
        # Pipe yt-dlp|ffmpeg evita 403 de URLs googlevideo (ytmusic sin cookies).
        if ALLOW_YTDLP_PIPE and has_yt_dlp():
            threads.append(
                threading.Thread(
                    target=_worker,
                    args=("pipe", lambda: _open_pipe_pcm(page_url)),
                    daemon=True,
                )
            )
        threads.append(
            threading.Thread(
                target=_direct_worker,
                args=("piped", lambda: _resolve_stream_url_piped(video_id)),
                daemon=True,
            )
        )
        if TRY_URL_PATH and has_yt_dlp():
            threads.append(
                threading.Thread(
                    target=_worker,
                    args=("url", lambda: _open_url_pcm(video_id)),
                    daemon=True,
                )
            )
        # ytmusic solo con cookies; sin ellas las URLs dan 403 a ffmpeg.
        if cookies_path():
            threads.append(
                threading.Thread(
                    target=_direct_worker,
                    args=("ytmusic", lambda: _resolve_stream_url_ytmusic(video_id)),
                    daemon=True,
                )
            )
    else:
        threads.append(
            threading.Thread(
                target=_direct_worker,
                args=("piped", lambda: _resolve_stream_url_piped(video_id)),
                daemon=True,
            )
        )
        if cookies_path():
            threads.append(
                threading.Thread(
                    target=_direct_worker,
                    args=("ytmusic", lambda: _resolve_stream_url_ytmusic(video_id)),
                    daemon=True,
                )
            )
        if TRY_URL_PATH:
            threads.append(
                threading.Thread(
                    target=_worker, args=("url", lambda: _open_url_pcm(video_id)), daemon=True
                )
            )
        if has_yt_dlp():
            threads.append(
                threading.Thread(
                    target=_worker,
                    args=("download", lambda: _open_download_pcm(video_id)),
                    daemon=True,
                )
            )
        if ALLOW_YTDLP_PIPE:
            threads.append(
                threading.Thread(
                    target=_worker, args=("pipe", lambda: _open_pipe_pcm(page_url)), daemon=True
                )
            )

    for t in threads:
        t.start()

    try:
        kind, stream_it, first, win_procs = result_q.get(timeout=STREAM_OPEN_TIMEOUT_SEC)
    except queue.Empty:
        shutdown_all_streams()
        detail = "; ".join(errors) if errors else "sin respuesta de yt-dlp/ffmpeg"
        raise RuntimeError(
            f"timeout ({STREAM_OPEN_TIMEOUT_SEC}s) iniciando audio — {detail}. "
            "Esperá a que termine el precalentamiento EJS en el log."
        ) from None
    finally:
        for t in threads:
            t.join(timeout=0.2)

    for kind_name, procs in worker_procs.items():
        if procs is not win_procs:
            _kill_procs(procs)

    log.info("music open winner=%s id=%s bytes=%d", kind, video_id, len(first))
    return stream_it, first, win_procs


def prefetch_pcm_stream(video_id: str) -> None:
    """Precalienta yt-dlp|ffmpeg al pulsar ▶ en admin (antes del GET del ESP)."""
    vid = (video_id or "").strip()
    if not vid:
        return
    with _prefetch_lock:
        entry = _prefetch.get(vid)
        if entry and entry.get("status") in ("loading", "ready"):
            return
        _prefetch[vid] = {"status": "loading", "result": None, "error": None, "t0": time.monotonic()}

    def _work() -> None:
        t0 = time.monotonic()
        try:
            with _prefetch_lock:
                t0 = float(_prefetch.get(vid, {}).get("t0", t0))
            path = _download_audio_ytdlp(vid)
            with _prefetch_lock:
                _prefetch[vid] = {
                    "status": "ready",
                    "cached_path": str(path),
                    "result": None,
                    "error": None,
                }
            log.info(
                "music prefetch ready id=%s file=%s bytes=%d %.1fs",
                vid,
                path.name,
                path.stat().st_size,
                time.monotonic() - t0,
            )
        except Exception as e:
            with _prefetch_lock:
                _prefetch[vid] = {
                    "status": "error",
                    "result": None,
                    "error": e,
                }
            log.warning("music prefetch failed id=%s: %s", vid, e)

    threading.Thread(target=_work, name=f"prefetch-{vid[:8]}", daemon=True).start()
    log.info("music prefetch started id=%s", vid)


def abort_client_open(video_id: str) -> None:
    """Timeout del GET /music/play — no matar prefetch que sigue cargando."""
    vid = (video_id or "").strip()
    with _prefetch_lock:
        entry = _prefetch.get(vid)
        status = entry.get("status") if entry else None
    if status == "loading":
        log.warning(
            "music/play open timeout id=%s — prefetch sigue en background (no se mata)",
            vid,
        )
        return
    cancel_prefetch_kill(vid)


def cancel_prefetch_kill(video_id: str | None = None) -> None:
    """Cancela prefetch y mata subprocesos colgados (timeout / Ctrl+C)."""
    shutdown_all_streams()
    with _prefetch_lock:
        if video_id:
            vid = (video_id or "").strip()
            entry = _prefetch.get(vid)
            if entry and entry.get("status") == "loading":
                _prefetch[vid] = {
                    "status": "error",
                    "result": None,
                    "error": RuntimeError("cancelado"),
                }
            else:
                _prefetch.pop(vid, None)
        else:
            for vid, entry in list(_prefetch.items()):
                if entry.get("status") == "loading":
                    _prefetch[vid] = {
                        "status": "error",
                        "result": None,
                        "error": RuntimeError("cancelado"),
                    }
                else:
                    _prefetch.pop(vid, None)


def cancel_prefetch(video_id: str | None = None) -> None:
    cancel_prefetch_kill(video_id)


def prefetch_status(video_id: str) -> dict[str, Any]:
    """Estado del prefetch para polling del ESP (ready / loading / error / none)."""
    vid = (video_id or "").strip()
    with _prefetch_lock:
        entry = _prefetch.get(vid)
    if not entry:
        return {"status": "none", "ready": False}
    status = str(entry.get("status") or "")
    if status == "ready":
        return {"status": "ready", "ready": True}
    if status == "error":
        err = entry.get("error")
        return {
            "status": "error",
            "ready": False,
            "error": str(err) if err else "prefetch falló",
        }
    if status == "loading":
        t0 = entry.get("t0")
        elapsed = time.monotonic() - float(t0) if t0 else 0.0
        return {
            "status": "loading",
            "ready": False,
            "elapsed_sec": round(elapsed, 1),
        }
    return {"status": status, "ready": False}


def wait_prefetch_ready(video_id: str, *, timeout_sec: float) -> None:
    """Espera a que prefetch_pcm_stream termine (listo o error)."""
    vid = (video_id or "").strip()
    deadline = time.monotonic() + max(1.0, timeout_sec)
    while time.monotonic() < deadline:
        with _prefetch_lock:
            entry = _prefetch.get(vid)
        if not entry:
            raise RuntimeError("prefetch no iniciado")
        status = entry.get("status")
        if status == "ready":
            log.info("prefetch wait ok id=%s", vid)
            return
        if status == "error":
            err = entry.get("error")
            raise RuntimeError(str(err) if err else "prefetch falló")
        time.sleep(0.25)
    raise TimeoutError(f"prefetch timeout ({timeout_sec:.0f}s)")


def _take_prefetched(video_id: str, *, wait_sec: float) -> tuple[Iterator[bytes], bytes] | None:
    vid = (video_id or "").strip()
    deadline = time.monotonic() + wait_sec
    while time.monotonic() < deadline:
        with _prefetch_lock:
            entry = _prefetch.get(vid)
        if not entry:
            return None
        status = entry.get("status")
        if status == "ready":
            cached = entry.get("cached_path")
            if cached:
                path = Path(str(cached))
                if not path.is_file():
                    with _prefetch_lock:
                        _prefetch.pop(vid, None)
                    raise RuntimeError(f"cache perdido: {cached}")
                with _prefetch_lock:
                    _prefetch.pop(vid, None)
                it, first, _procs = _open_file_pcm(path, realtime=False)
                log.info("music open cache hit id=%s bytes=%d", vid, len(first))
                return it, first
            if entry.get("result"):
                with _prefetch_lock:
                    entry = _prefetch.pop(vid, entry)
                it, first, _procs = entry["result"]
                log.info(
                    "music open prefetch hit id=%s bytes=%d",
                    vid,
                    len(first),
                )
                return it, first
        if status == "error":
            with _prefetch_lock:
                err = _prefetch.pop(vid, {}).get("error")
            if err:
                raise err
            return None
        time.sleep(0.2)
    return None


def open_pcm_stream(video_id: str) -> tuple[Iterator[bytes], bytes]:
    """Pipe yt-dlp|ffmpeg; usa prefetch si el admin ya lo lanzó."""
    vid = (video_id or "").strip()
    hit = _take_prefetched(vid, wait_sec=float(STREAM_OPEN_TIMEOUT_SEC))
    if hit:
        return hit
    with _prefetch_lock:
        loading = _prefetch.get(vid, {}).get("status") == "loading"
    if not loading:
        cancel_prefetch(vid)
    it, first, _procs = _open_pcm_stream_impl(vid)
    return it, first


def _next_chunk(it: Iterator[bytes]) -> bytes | None:
    try:
        return next(it)
    except StopIteration:
        return None


def logged_pcm_stream(video_id: str) -> Iterator[bytes]:
    t0 = time.monotonic()
    pcm_total = 0
    try:
        for chunk in iter_pcm_16k_stream(video_id):
            pcm_total += len(chunk)
            yield chunk
    finally:
        log.info(
            "music/play stream done id=%s %.1fs %d bytes",
            video_id,
            time.monotonic() - t0,
            pcm_total,
        )


def iter_wav_16k_stream(video_id: str) -> Iterator[bytes]:
    """Alias legacy — preferir iter_pcm_16k_stream."""
    yield from iter_pcm_16k_stream(video_id)


# --- Radio / autoplay (parlante inteligente) ---------------------------------

RADIO_QUEUE_SIZE = max(3, min(20, int(os.environ.get("MUSIC_RADIO_QUEUE", "10"))))


def track_payload(track: dict[str, Any]) -> dict[str, str]:
    """Formato unificado para firmware y sesiones."""
    vid = str(track.get("video_id") or track.get("id") or "").strip()
    return {
        "video_id": vid,
        "title": str(track.get("title") or "").strip(),
        "artist": str(track.get("artist") or "").strip(),
    }


def build_radio_queue(
    seed: dict[str, Any],
    query: str,
    *,
    limit: int | None = None,
    exclude_ids: set[str] | None = None,
) -> list[dict[str, str]]:
    """Canciones siguientes del mismo estilo (artista + búsqueda relacionada)."""
    lim = limit or RADIO_QUEUE_SIZE
    exclude = set(exclude_ids or [])
    seed_pay = track_payload(seed)
    if seed_pay["video_id"]:
        exclude.add(seed_pay["video_id"])

    out: list[dict[str, str]] = []
    seen = set(exclude)
    artist = seed_pay["artist"]
    title = seed_pay["title"]
    q0 = (query or "").strip()

    queries: list[str] = []
    if artist:
        queries.append(artist)
        queries.append(f"{artist} mix")
        queries.append(f"más canciones de {artist}")
    if q0 and q0.lower() not in {artist.lower(), title.lower()}:
        queries.append(q0)
    if artist and title:
        queries.append(f"{artist} {title}")

    for q in queries:
        if len(out) >= lim:
            break
        try:
            sr = search(q, limit=15)
        except Exception as e:
            log.warning("radio search %r: %s", q, e)
            continue
        for item in sr.get("results") or []:
            pay = track_payload(item)
            vid = pay["video_id"]
            if not vid or vid in seen:
                continue
            seen.add(vid)
            out.append(pay)
            if len(out) >= lim:
                break
    return out


_radio_lock = threading.Lock()
_radio_sessions: dict[str, dict[str, Any]] = {}


def start_radio_session(device_key: str, seed: dict[str, Any], query: str) -> list[dict[str, str]]:
    """Inicia modo radio para un dispositivo; devuelve cola inicial (sin la pista actual)."""
    key = (device_key or "default").strip()
    seed_pay = track_payload(seed)
    queue = build_radio_queue(seed_pay, query, exclude_ids={seed_pay["video_id"]} if seed_pay["video_id"] else None)
    with _radio_lock:
        _radio_sessions[key] = {
            "query": (query or "").strip(),
            "seed": seed_pay,
            "queue": list(queue),
            "played": [seed_pay["video_id"]] if seed_pay["video_id"] else [],
            "autoplay": True,
        }
    if queue:
        prefetch_pcm_stream(queue[0]["video_id"])
    log.info(
        "music radio start device=%s seed=%r queue=%d",
        key,
        seed_pay.get("title"),
        len(queue),
    )
    return queue


def stop_radio_session(device_key: str) -> None:
    key = (device_key or "default").strip()
    with _radio_lock:
        _radio_sessions.pop(key, None)


def radio_next_track(device_key: str) -> dict[str, str] | None:
    """Siguiente pista del modo radio (refill automático si se agota la cola)."""
    key = (device_key or "default").strip()
    with _radio_lock:
        sess = _radio_sessions.get(key)
        if not sess or not sess.get("autoplay"):
            return None
        queue: list[dict[str, str]] = sess["queue"]
        played: list[str] = sess["played"]

    if not queue:
        seed = sess["seed"]
        more = build_radio_queue(
            seed,
            sess.get("query", ""),
            exclude_ids=set(played),
        )
        with _radio_lock:
            sess = _radio_sessions.get(key)
            if not sess:
                return None
            sess["queue"].extend(more)
            queue = sess["queue"]

    if not queue:
        return None

    with _radio_lock:
        sess = _radio_sessions.get(key)
        if not sess or not sess["queue"]:
            return None
        track = sess["queue"].pop(0)
        vid = track.get("video_id", "")
        if vid:
            sess["played"].append(vid)
        if sess["queue"]:
            prefetch_pcm_stream(sess["queue"][0]["video_id"])

    if not track.get("video_id"):
        return None
    log.info("music radio next device=%s -> %s", key, track.get("title"))
    return track
