"""Lee COM18 @ 115200 y escribe líneas a stdout + archivo (para diagnóstico música)."""
from __future__ import annotations

import sys
import time
from datetime import datetime
from pathlib import Path

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM18"
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
SECONDS = int(sys.argv[3]) if len(sys.argv) > 3 else 120
OUT = Path(__file__).resolve().parent / "com18_monitor.log"

KEYWORDS = (
    "music",
    "MUSIC",
    "underrun",
    "starve",
    "PCM",
    "error",
    "WiFi",
    "heap",
    "Guru",
    "abort",
    "watchdog",
)


def main() -> int:
    print(f"[monitor] abriendo {PORT} @ {BAUD} por {SECONDS}s -> {OUT}", flush=True)
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.25)
    except serial.SerialException as exc:
        print(f"[monitor] ERROR abrir puerto: {exc}", flush=True)
        return 1

    buf = b""
    end = time.time() + SECONDS
    highlights: list[str] = []
    OUT.write_text("", encoding="utf-8")

    with OUT.open("a", encoding="utf-8", errors="replace") as log:
        log.write(f"=== {datetime.now().isoformat()} {PORT} {BAUD} ===\n")
        log.flush()
        while time.time() < end:
            chunk = ser.read(4096)
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line_b, buf = buf.split(b"\n", 1)
                line = line_b.decode("utf-8", errors="replace").rstrip("\r")
                if not line:
                    continue
                ts = datetime.now().strftime("%H:%M:%S")
                row = f"{ts} | {line}"
                print(row, flush=True)
                log.write(row + "\n")
                log.flush()
                low = line.lower()
                if any(k.lower() in low for k in KEYWORDS):
                    highlights.append(row)

        if buf.strip():
            tail = buf.decode("utf-8", errors="replace")
            log.write(f"(tail) {tail}\n")

        log.write(f"=== fin {datetime.now().isoformat()} highlights={len(highlights)} ===\n")
        for h in highlights:
            log.write(f"  >> {h}\n")

    ser.close()
    print(f"[monitor] listo. {len(highlights)} líneas destacadas en {OUT}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
