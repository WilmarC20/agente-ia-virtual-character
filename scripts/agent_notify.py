#!/usr/bin/env python3
"""Envía avisos al robot agenteIA (POST /api/dev/notify).

El texto lo genera el servidor según la personalidad activa si no pasás --message.

Uso:
  python scripts/agent_notify.py --kind ask_question
  python scripts/agent_notify.py -k agent_blocked --context '{"tool":"Shell"}'

Variables:
  AGENTEIA_NOTIFY_URL  default http://127.0.0.1:8000
"""
from __future__ import annotations

import argparse
import json
import os
import sys
import urllib.error
import urllib.request


def notify(
    *,
    kind: str = "agent",
    message: str | None = None,
    context: dict | None = None,
    emotion: str | None = None,
    speak: bool = True,
    priority: bool | None = None,
    server: str | None = None,
) -> dict:
    base = (server or os.environ.get("AGENTEIA_NOTIFY_URL", "http://127.0.0.1:8000")).rstrip("/")
    payload: dict = {"kind": kind, "speak": speak}
    if message:
        payload["message"] = message
    if context:
        payload["context"] = context
    if emotion:
        payload["emotion"] = emotion
    if priority is not None:
        payload["priority"] = priority
    req = urllib.request.Request(
        f"{base}/api/dev/notify",
        data=json.dumps(payload).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=8) as resp:
        return json.loads(resp.read().decode("utf-8"))


def main() -> int:
    p = argparse.ArgumentParser(description="Notificar al robot agenteIA")
    p.add_argument("--kind", "-k", default="agent", help="agent_blocked, ask_question, ci_failed, …")
    p.add_argument("--message", "-m", default=None, help="Texto fijo (opcional; si falta, usa personalidad)")
    p.add_argument("--context", "-c", default=None, help='JSON, ej. {"tool":"Shell"}')
    p.add_argument("--emotion", "-e", default=None)
    p.add_argument("--no-speak", action="store_true", help="Solo cara, sin TTS")
    p.add_argument("--priority", action="store_true", default=None)
    p.add_argument("--server", default=None)
    args = p.parse_args()
    ctx = None
    if args.context:
        try:
            ctx = json.loads(args.context)
        except json.JSONDecodeError as e:
            print(f"invalid --context JSON: {e}", file=sys.stderr)
            return 1
    try:
        out = notify(
            kind=args.kind,
            message=args.message,
            context=ctx,
            emotion=args.emotion,
            speak=not args.no_speak,
            priority=args.priority,
            server=args.server,
        )
        print(json.dumps(out, ensure_ascii=False))
        return 0
    except urllib.error.URLError as e:
        print(f"notify failed: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
