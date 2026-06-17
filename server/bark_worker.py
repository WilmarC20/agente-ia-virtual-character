"""Bark singing guide worker — usar con Python 3.11 (.venv-rvc).

Modos:
  python bark_worker.py           — una petición (stdin JSON → stdout WAV)
  python bark_worker.py --daemon  — proceso persistente (modelos cargados una vez)

Request:
{
  "lyrics": "line1\\nline2",
  "history_prompt": "v2/es_speaker_6",
  "warmup": false
}
"""

from __future__ import annotations

import io
import json
import os
import struct
import sys
import wave
from pathlib import Path

# Bark models — XDG_CACHE_HOME (not SUNO_CACHE_DIR). ~5 GB full / ~1 GB small models.
_SERVER = Path(__file__).resolve().parent
_cache_root = _SERVER / ".cache-suno"
_cache_root.mkdir(parents=True, exist_ok=True)
os.environ.setdefault("XDG_CACHE_HOME", str(_cache_root))
os.environ.setdefault("HF_HOME", str(_SERVER / ".hf-cache"))
os.environ.setdefault("HUGGINGFACE_HUB_CACHE", str(_SERVER / ".hf-cache" / "hub"))
os.environ.setdefault("SUNO_USE_SMALL_MODELS", "True")


def _configure_runtime() -> None:
    if os.environ.get("BARK_CPU_ONLY", "0") != "0":
        os.environ.setdefault("SUNO_OFFLOAD_CPU", "True")
    os.environ.setdefault("KMP_DUPLICATE_LIB_OK", "TRUE")
    os.environ.setdefault("OMP_NUM_THREADS", "4")


_configure_runtime()

_torch_patched = False


def _bark_use_gpu() -> bool:
    return os.environ.get("BARK_CPU_ONLY", "0") == "0"


def _patch_torch_load() -> None:
    global _torch_patched
    if _torch_patched:
        return
    import torch

    _orig = torch.load

    def _load(*args, **kwargs):
        if kwargs.get("weights_only") is None:
            kwargs["weights_only"] = False
        return _orig(*args, **kwargs)

    torch.load = _load  # type: ignore[method-assign]
    _torch_patched = True


import numpy as np

_models_ready = False


def _preload() -> None:
    global _models_ready
    if _models_ready:
        return
    _patch_torch_load()
    from bark import preload_models  # type: ignore[import-untyped]

    use_gpu = _bark_use_gpu()
    print(
        f"bark_worker: loading models (gpu={use_gpu}, first run downloads ~2 GB)...",
        file=sys.stderr,
        flush=True,
    )
    preload_models(
        text_use_gpu=use_gpu,
        coarse_use_gpu=use_gpu,
        fine_use_gpu=use_gpu,
        codec_use_gpu=use_gpu,
    )
    _models_ready = True
    print("bark_worker: models ready", file=sys.stderr, flush=True)


def _generate(req: dict) -> bytes:
    from bark import SAMPLE_RATE, generate_audio  # type: ignore[import-untyped]

    _preload()
    lyrics = str(req.get("lyrics", "")).strip()
    if not lyrics:
        raise ValueError("missing lyrics")
    history = str(req.get("history_prompt", "v2/es_speaker_6"))
    prompt = f"♪ [music] [singing] {lyrics} ♪"
    print(f"bark_worker: generate {len(lyrics)} chars history={history}", file=sys.stderr, flush=True)
    audio = generate_audio(prompt, history_prompt=history)
    audio = np.asarray(audio, dtype=np.float32)
    if audio.ndim > 1:
        audio = audio.mean(axis=1)
    audio = np.clip(audio, -1.0, 1.0)
    pcm = (audio * 32767.0).astype(np.int16)
    buf = io.BytesIO()
    with wave.open(buf, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(int(SAMPLE_RATE))
        wf.writeframes(pcm.tobytes())
    return buf.getvalue()


def _handle_request(req: dict) -> bytes:
    if req.get("warmup"):
        _preload()
        return b""
    wav = _generate(req)
    if not wav.startswith(b"RIFF"):
        raise RuntimeError("Bark produced invalid WAV")
    return wav


def run_daemon() -> int:
    try:
        _preload()
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
    raw = sys.stdin.read()
    if not raw.strip():
        print("empty request", file=sys.stderr)
        return 1
    req = json.loads(raw)
    try:
        if req.get("warmup"):
            _preload()
            print("READY", file=sys.stderr, flush=True)
            return 0
        wav = _generate(req)
        if not wav.startswith(b"RIFF"):
            print("Bark produced invalid WAV", file=sys.stderr)
            return 1
        sys.stdout.buffer.write(wav)
        return 0
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--daemon":
        raise SystemExit(run_daemon())
    raise SystemExit(main())
