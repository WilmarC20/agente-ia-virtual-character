"""Device command queue — ESP consumes via poll, long-poll or WebSocket."""

from __future__ import annotations

import asyncio
import time
from collections import deque
from typing import Any

import server_config as srv_cfg

VALID_DEV_EMOTIONS = frozenset({
    "neutral", "happy", "sad", "angry", "surprised", "thinking", "sleepy",
    "love", "excited", "cool", "confused", "dizzy", "vibing",
})

NOTIFY_KIND_EMOTIONS: dict[str, str] = {
    "agent_blocked": "thinking",
    "ask_question": "confused",
    "subagent_done": "happy",
    "ci_failed": "sad",
    "approval_needed": "surprised",
    "stop_failure": "sad",
    "elicitation": "confused",
    "task_completed": "happy",
    "agent": "thinking",
}


class DeviceManager:
    def __init__(self) -> None:
        self._queue: deque[dict[str, Any]] = deque()
        self._lock = asyncio.Lock()
        self._notify_last: dict[str, float] = {}
        self._notify_cooldown_s = 45.0
        self._waiters: list[asyncio.Event] = []

    def with_device_context(self, payload: dict) -> dict:
        payload["personality"] = srv_cfg.current_personality_id()
        payload["presentation"] = srv_cfg.get_presentation()
        return payload

    def _wake_waiters(self) -> None:
        for ev in self._waiters:
            ev.set()
        self._waiters.clear()

    async def poll(self) -> dict:
        async with self._lock:
            payload = self.with_device_context({})
            if not self._queue:
                payload["cmd"] = None
                return payload
            payload["cmd"] = self._queue.popleft()
            return payload

    async def poll_wait(self, timeout_s: float = 25.0) -> dict:
        deadline = time.monotonic() + max(0.5, min(60.0, timeout_s))
        while True:
            async with self._lock:
                if self._queue:
                    payload = self.with_device_context({})
                    payload["cmd"] = self._queue.popleft()
                    return payload
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return self.with_device_context({"cmd": None})
            ev = asyncio.Event()
            self._waiters.append(ev)
            try:
                await asyncio.wait_for(ev.wait(), timeout=remaining)
            except asyncio.TimeoutError:
                return self.with_device_context({"cmd": None})
            finally:
                if ev in self._waiters:
                    self._waiters.remove(ev)

    async def _enqueue(self, cmd: dict[str, Any], *, front: bool = False) -> int:
        async with self._lock:
            if front:
                self._queue.appendleft(cmd)
            else:
                self._queue.append(cmd)
            qlen = len(self._queue)
        self._wake_waiters()
        return qlen

    async def queue_face(self, body: dict[str, Any]) -> tuple[dict[str, Any] | None, int, str | None]:
        emotion = str(body.get("emotion", "neutral")).strip().lower()
        if emotion not in VALID_DEV_EMOTIONS:
            return None, 0, f"unknown emotion: {emotion}"
        bored = bool(body.get("bored", False))
        hold_ms = max(1000, min(120000, int(body.get("hold_ms", 8000))))
        cmd: dict[str, Any] = {"type": "face", "emotion": emotion, "bored": bored, "hold_ms": hold_ms}
        for key, lo, hi in (("vibing_mic", 50, 300), ("vibing_floor", 0, 500), ("vibing_ceil", 200, 900)):
            val = body.get(key)
            if val is not None:
                cmd[key] = max(lo, min(hi, int(val)))
        qlen = await self._enqueue(cmd)
        return cmd, qlen, None

    async def queue_speak(self, body: dict[str, Any]) -> tuple[dict[str, Any] | None, int, str | None]:
        text = str(body.get("text", "")).strip()
        if not text:
            return None, 0, "empty text"
        if len(text) > 500:
            text = text[:497] + "..."
        emotion = str(body.get("emotion", "happy")).strip().lower()
        if emotion not in VALID_DEV_EMOTIONS:
            emotion = "happy"
        cmd = {"type": "speak", "text": text, "emotion": emotion}
        qlen = await self._enqueue(cmd)
        return cmd, qlen, None

    async def queue_scene(self, body: dict[str, Any]) -> tuple[dict[str, Any] | None, int, str | None]:
        scene = str(body.get("scene", "idle")).strip().lower()
        valid = {
            "boot", "idle", "conversation", "programming", "music", "guardian",
            "emergency", "camera", "dashboard", "sleep", "shutdown",
        }
        if scene not in valid:
            return None, 0, f"unknown scene: {scene}"
        cmd: dict[str, Any] = {
            "type": "scene",
            "scene": scene,
            "transition_ms": max(0, min(5000, int(body.get("transition_ms", 0)))),
        }
        title = str(body.get("title", "")).strip()
        if title:
            cmd["title"] = title[:120]
        qlen = await self._enqueue(cmd)
        return cmd, qlen, None

    async def queue_story(self, cmd: dict[str, Any], *, priority: bool) -> int:
        return await self._enqueue(cmd, front=priority)

    async def queue_raw(self, cmd: dict[str, Any], *, front: bool = False) -> int:
        return await self._enqueue(cmd, front=front)

    async def status(self) -> dict[str, Any]:
        async with self._lock:
            pending = list(self._queue)
        return {"ok": True, "pending": len(pending), "queue": pending}

    async def clear(self) -> int:
        async with self._lock:
            n = len(self._queue)
            self._queue.clear()
        return n

    async def notify(self, body: dict[str, Any], *, cap_speech_text, prepare_spanish_text) -> dict[str, Any]:
        kind = str(body.get("kind", "agent")).strip().lower()[:64] or "agent"
        context = body.get("context") if isinstance(body.get("context"), dict) else {}
        message = str(body.get("message", "")).strip()
        generated = None
        if not message:
            generated = srv_cfg.notify_reply(kind, context=context)
            message = generated["reply"]
        if not message:
            return {"ok": False, "error": "empty message"}
        message = cap_speech_text(prepare_spanish_text(message), sing=False)
        emotion = str(body.get("emotion", "")).strip().lower()
        if not emotion or emotion not in VALID_DEV_EMOTIONS:
            emotion = generated["emotion"] if generated else NOTIFY_KIND_EMOTIONS.get(kind, "thinking")
        if emotion not in VALID_DEV_EMOTIONS:
            emotion = "thinking"
        speak = bool(body.get("speak", True))
        priority = bool(body.get("priority", kind in (
            "agent_blocked", "approval_needed", "ask_question", "stop_failure", "elicitation",
        )))
        pid = srv_cfg.current_personality_id()
        dedupe_key = str(body.get("dedupe_key", f"{kind}:{pid}"))
        now = time.time()
        last = self._notify_last.get(dedupe_key, 0.0)
        if now - last < self._notify_cooldown_s:
            return {"ok": True, "skipped": True, "reason": "cooldown", "kind": kind}
        self._notify_last[dedupe_key] = now
        if speak:
            cmd: dict[str, Any] = {"type": "speak", "text": message, "emotion": emotion}
        else:
            cmd = {
                "type": "face",
                "emotion": emotion,
                "hold_ms": max(3000, min(120000, int(body.get("hold_ms", 15000)))),
            }
        qlen = await self._enqueue(cmd, front=priority)
        return {"ok": True, "queued": qlen, "kind": kind, "personality": pid, "cmd": cmd}


device_manager = DeviceManager()
