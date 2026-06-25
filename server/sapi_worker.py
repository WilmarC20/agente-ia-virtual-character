"""Windows SAPI en subproceso aislado (evita cuelgues CLR en uvicorn)."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

import numpy as np

TTS_SAMPLE_RATE = int(__import__("os").environ.get("TTS_SAMPLE_RATE", "16000"))
TTS_PCM_GAIN = float(__import__("os").environ.get("TTS_PCM_GAIN", "0.55"))
SAPI_TIMEOUT_S = int(__import__("os").environ.get("SAPI_TTS_TIMEOUT_S", "20"))


def _tts_sapi_rate(sing: bool) -> int:
    """SAPI Rate -10..10. TTS_SPEED_PERCENT -50..+50 mapea linealmente (+25% -> Rate 5)."""
    if sing:
        return 2
    env = __import__("os").environ
    if "TTS_SAPI_RATE" in env:
        return max(-10, min(10, int(env["TTS_SAPI_RATE"])))
    pct = int(env.get("TTS_SPEED_PERCENT", "5"))
    return max(-10, min(10, round(pct / 5)))


def _resample_pcm(pcm: np.ndarray, src_rate: int, dst_rate: int) -> np.ndarray:
    if src_rate == dst_rate or len(pcm) == 0:
        return pcm
    n_out = int(len(pcm) * dst_rate / src_rate)
    x_out = np.linspace(0, len(pcm) - 1, n_out)
    return np.interp(x_out, np.arange(len(pcm)), pcm.astype(np.float32)).astype(np.int16)


def _pcm_to_wav(pcm: np.ndarray, sample_rate: int) -> bytes:
    import io

    pcm = np.clip(pcm.astype(np.float32) * TTS_PCM_GAIN, -32768, 32767).astype(np.int16)
    out = io.BytesIO()
    with wave.open(out, "wb") as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm.tobytes())
    return out.getvalue()


def _sanitize(text: str) -> str:
    return (
        text.replace("`", " ")
        .replace("\n", " ")
        .replace("\r", " ")
        .strip()
    )


def synthesize(text: str, sing: bool) -> bytes:
    from text_encoding import prepare_spanish_text

    text = prepare_spanish_text(_sanitize(text))
    if not text:
        text = "Ok."
    rate = _tts_sapi_rate(sing)

    with tempfile.TemporaryDirectory() as td:
        wav_path = Path(td) / "tts_out.wav"
        text_path = Path(td) / "tts_text.txt"
        ps1_path = Path(td) / "speak.ps1"

        # Escribir texto en archivo UTF-8 (evita problemas de encoding con tildes/ñ)
        text_path.write_text(text, encoding="utf-8")

        wav_str = str(wav_path).replace("\\", "\\\\")
        txt_str = str(text_path).replace("\\", "\\\\")

        ps = f"""Add-Type -AssemblyName System.Speech
$s = New-Object System.Speech.Synthesis.SpeechSynthesizer
$pick = $null
foreach ($v in $s.GetInstalledVoices()) {{
  $c = $v.VoiceInfo.Culture.Name
  if ($c -eq 'es-MX' -or $c -eq 'es-ES') {{ $pick = $v.VoiceInfo.Name; break }}
}}
if (-not $pick) {{
  foreach ($v in $s.GetInstalledVoices()) {{
    if ($v.VoiceInfo.Culture.Name -like 'es*') {{ $pick = $v.VoiceInfo.Name; break }}
  }}
}}
if ($pick) {{ $s.SelectVoice($pick) }}
$s.Rate = {rate}
$s.SetOutputToWaveFile('{wav_str}')
$texto = [System.IO.File]::ReadAllText('{txt_str}', [System.Text.Encoding]::UTF8)
$s.Speak($texto)
$s.Dispose()
"""
        # UTF-8 BOM para que PowerShell lea el script correctamente
        ps1_path.write_bytes(b"\xef\xbb\xbf" + ps.encode("utf-8"))

        proc = subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Sta", "-File", str(ps1_path)],
            capture_output=True,
            timeout=SAPI_TIMEOUT_S,
        )
        if proc.returncode != 0:
            err = proc.stderr.decode("utf-8", errors="replace").strip()
            raise RuntimeError(err or f"PowerShell SAPI exit {proc.returncode}")
        if not wav_path.exists() or wav_path.stat().st_size < 44:
            raise RuntimeError("SAPI produced empty WAV")

        with wave.open(str(wav_path), "rb") as wf:
            pcm = np.frombuffer(wf.readframes(wf.getnframes()), dtype=np.int16)
            src_rate = wf.getframerate()
            channels = wf.getnchannels()
        if channels > 1:
            pcm = pcm.reshape(-1, channels)[:, 0]
        pcm = _resample_pcm(pcm, src_rate, TTS_SAMPLE_RATE)
        return _pcm_to_wav(pcm, TTS_SAMPLE_RATE)


def main() -> None:
    raw = sys.stdin.buffer.read()          # leer bytes crudos (evita encoding cp1252 de consola)
    req = json.loads(raw.decode("utf-8"))  # decodificar explicitamente como UTF-8
    wav = synthesize(req.get("text", ""), bool(req.get("sing", False)))
    sys.stdout.buffer.write(wav)


if __name__ == "__main__":
    try:
        main()
    except Exception as e:
        print(str(e), file=sys.stderr)
        sys.exit(1)