"""RVC voice conversion worker — usar con Python 3.11 (.venv-rvc).

stdin JSON -> stdout WAV bytes
stderr: logs / READY / errores

Request:
{
  "model_path": "path/to/model.pth",
  "index_path": "path/to/model.index",   // optional
  "device": "cuda:0",
  "f0_method": "rmvpe",
  "f0_up_key": 0,
  "index_rate": 0.75,
  "filter_radius": 3,
  "rms_mix_rate": 0.25,
  "protect": 0.33,
  "input_wav": "path/to/guide.wav",
  "warmup": false
}
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
import wave
from pathlib import Path
from typing import Any

_engine: Any | None = None
_loaded_model: str | None = None
_torch_patched = False


def _patch_torch_load_for_fairseq() -> None:
    """PyTorch >=2.6 defaults weights_only=True; fairseq HuBERT checkpoints need False."""
    global _torch_patched
    if _torch_patched:
        return
    import torch

    _orig_load = torch.load

    def _load(*args: Any, **kwargs: Any) -> Any:
        if kwargs.get("weights_only") is None:
            kwargs["weights_only"] = False
        return _orig_load(*args, **kwargs)

    torch.load = _load  # type: ignore[method-assign]
    _torch_patched = True


def _write_silence_wav(path: str, *, duration_s: float = 0.5, sample_rate: int = 16000) -> None:
    n_frames = max(1, int(sample_rate * duration_s))
    with wave.open(path, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(b"\x00\x00" * n_frames)


def _load_engine(req: dict) -> Any:
    global _engine, _loaded_model
    _patch_torch_load_for_fairseq()
    from rvc_python.infer import RVCInference  # type: ignore[import-untyped]

    model_path = str(req["model_path"])
    device = str(req.get("device", "cuda:0"))

    if _engine is not None and _loaded_model == model_path:
        return _engine

    print(f"rvc_worker: loading {model_path} on {device}", file=sys.stderr, flush=True)
    engine = RVCInference(device=device)
    index_path = str(req.get("index_path") or "")
    if index_path and Path(index_path).is_file():
        try:
            engine.load_model(model_path, index_path=index_path)
        except TypeError:
            engine.load_model(model_path)
    else:
        engine.load_model(model_path)

    if hasattr(engine, "set_params"):
        engine.set_params(
            f0method=str(req.get("f0_method", "rmvpe")),
            f0up_key=int(req.get("f0_up_key", 0)),
            index_rate=float(req.get("index_rate", 0.75)),
            filter_radius=int(req.get("filter_radius", 3)),
            resample_sr=int(req.get("resample_sr", 0)),
            rms_mix_rate=float(req.get("rms_mix_rate", 0.5)),
            protect=float(req.get("protect", 0.5)),
        )

    _engine = engine
    _loaded_model = model_path
    return engine


def _convert(req: dict) -> bytes:
    input_wav = str(req.get("input_wav", ""))
    if not input_wav or not Path(input_wav).is_file():
        raise ValueError("missing or invalid input_wav")

    engine = _load_engine(req)
    f0_up_key = int(req.get("f0_up_key", 0))
    index_rate = float(req.get("index_rate", 0.75))

    if hasattr(engine, "set_params"):
        try:
            engine.set_params(f0up_key=f0_up_key, index_rate=index_rate)
        except TypeError:
            engine.set_params(f0_up_key=f0_up_key, index_rate=index_rate)

    fd, out_path = tempfile.mkstemp(suffix=".wav", prefix="rvc_out_")
    os.close(fd)
    try:
        try:
            engine.infer_file(input_wav, out_path, f0_up_key=f0_up_key)
        except TypeError:
            engine.infer_file(input_wav, out_path)
        return Path(out_path).read_bytes()
    finally:
        try:
            os.unlink(out_path)
        except OSError:
            pass


def _warmup(req: dict) -> None:
    """Load RVC + HuBERT and run a short silent infer (catches PyTorch/fairseq issues early)."""
    _load_engine(req)
    fd, silent = tempfile.mkstemp(suffix=".wav", prefix="rvc_warm_")
    os.close(fd)
    try:
        _write_silence_wav(silent)
        infer_req = dict(req)
        infer_req.pop("warmup", None)
        infer_req["input_wav"] = silent
        wav = _convert(infer_req)
        if not wav.startswith(b"RIFF"):
            raise RuntimeError("warmup infer produced invalid WAV")
        print(
            f"rvc_worker: warmup infer OK ({len(wav)} bytes)",
            file=sys.stderr,
            flush=True,
        )
    finally:
        try:
            os.unlink(silent)
        except OSError:
            pass


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
        if not wav.startswith(b"RIFF"):
            print("RVC produced invalid WAV", file=sys.stderr)
            return 1
        sys.stdout.buffer.write(wav)
        return 0
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr, flush=True)
        return 1


def run_daemon() -> int:
    import struct

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
            if req.get("warmup"):
                _warmup(req)
                wav = b""
            else:
                wav = _convert(req)
                if wav and not wav.startswith(b"RIFF"):
                    raise RuntimeError("RVC produced invalid WAV")
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


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--daemon":
        raise SystemExit(run_daemon())
    raise SystemExit(main())
