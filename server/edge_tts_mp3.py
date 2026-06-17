"""Edge-TTS mínimo para Applio Python — escribe MP3 crudo a stdout.

Corre como subprocess desde applio_rvc_core.py con timeout OS-nivel.
stdin: texto plano
stdout: bytes MP3 crudos
env: EDGE_TTS_VOICE, EDGE_TTS_RATE, EDGE_CONNECT_TIMEOUT, EDGE_RECEIVE_TIMEOUT
"""
import asyncio
import os
import sys

VOICE = os.environ.get("EDGE_TTS_VOICE", "es-MX-JorgeNeural")
RATE = os.environ.get("EDGE_TTS_RATE", "+0%")
CONNECT_TIMEOUT = int(os.environ.get("EDGE_CONNECT_TIMEOUT", "8"))
RECEIVE_TIMEOUT = int(os.environ.get("EDGE_RECEIVE_TIMEOUT", "10"))


async def main() -> None:
    import edge_tts  # noqa: PLC0415

    text = sys.stdin.read().strip()
    if not text:
        print("edge_tts_mp3: texto vacio", file=sys.stderr)
        sys.exit(1)

    communicate = edge_tts.Communicate(
        text,
        VOICE,
        rate=RATE,
        connect_timeout=CONNECT_TIMEOUT,
        receive_timeout=RECEIVE_TIMEOUT,
    )

    mp3 = bytearray()
    async for chunk in communicate.stream():
        if chunk["type"] == "audio":
            mp3.extend(chunk["data"])

    if not mp3:
        print("edge_tts_mp3: no audio recibido", file=sys.stderr)
        sys.exit(1)

    sys.stdout.buffer.write(bytes(mp3))
    sys.stdout.buffer.flush()


asyncio.run(main())
