"""Worker RVC local â€” subprocess bajo APPLIO_PYTHON.

stdin JSON (oneshot) o --daemon con framing >I length.
stderr: logs / READY

No depende de Flask ni de ningÃºn servidor en :7860.
"""

from __future__ import annotations

import json
import struct
import sys
import os
from typing import Any

from applio_rvc_core import convert_guide_wav, load_model, text_to_character_wav


def _convert(req: dict[str, Any]) -> bytes:
    if req.get("text"):
        return text_to_character_wav(
            str(req["text"]),
            model_path=str(req["model_path"]),
            index_path=str(req.get("index_path") or ""),
            voice=str(req.get("edge_voice", "es-MX-JorgeNeural")),
            rate=str(req.get("edge_rate", "+0%")),
            pitch=int(req.get("pitch", req.get("f0_up_key", 0))),
            index_rate=float(req.get("index_rate", 0.75)),
            protect=float(req.get("protect", 0.33)),
            f0_method=str(req.get("f0_method", "rmvpe")),
            hop_length=int(req.get("hop_length", 128)),
            embedder_model=str(req.get("embedder_model", "contentvec")),
        )
    return convert_guide_wav(
        input_wav=str(req.get("input_wav", "")),
        model_path=str(req["model_path"]),
        index_path=str(req.get("index_path") or ""),
        pitch=int(req.get("pitch", req.get("f0_up_key", 0))),
        index_rate=float(req.get("index_rate", 0.75)),
        protect=float(req.get("protect", 0.33)),
        f0_method=str(req.get("f0_method", "rmvpe")),
        hop_length=int(req.get("hop_length", 128)),
        embedder_model=str(req.get("embedder_model", "contentvec")),
    )


def _warmup(req: dict[str, Any]) -> None:
    model_path = str(req["model_path"])
    load_model(model_path)
    print(f"applio_rvc: model loaded {model_path}", file=sys.stderr, flush=True)


def _handle(req: dict[str, Any]) -> bytes:
    if req.get("warmup"):
        _warmup(req)
        return b""
    return _convert(req)


def run_daemon() -> int:
    # Pre-cargar modelo antes de anunciar READY — igual que bender_server.py.
    # Asi el primer request es instantaneo en vez de esperar >180s.
    model_path = os.environ.get("RVC_MODEL_PATH", "").strip()
    if model_path:
        print(f"applio_rvc: cargando modelo {model_path}...", file=sys.stderr, flush=True)
        try:
            load_model(model_path)
            print("applio_rvc: modelo listo", file=sys.stderr, flush=True)
        except Exception as e:
            print(f"applio_rvc: error cargando modelo: {e}", file=sys.stderr, flush=True)
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
            wav = _handle(req)
            stdout.write(struct.pack(">I", len(wav)))
            if wav:
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
            _warmup(req)
            print("READY", file=sys.stderr, flush=True)
            return 0
        wav = _convert(req)
        sys.stdout.buffer.write(wav)
        return 0
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--daemon":
        raise SystemExit(run_daemon())
    raise SystemExit(main())
