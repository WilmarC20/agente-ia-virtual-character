"""Scheduler engine — timed tasks (stub; future cron)."""

from __future__ import annotations

import time
from dataclasses import dataclass, field
from typing import Any, Callable, Awaitable


@dataclass
class ScheduledTask:
    id: str
    run_at: float
    callback: Callable[[], Awaitable[None] | None]
    payload: dict[str, Any] = field(default_factory=dict)


class SchedulerEngine:
    def __init__(self) -> None:
        self._tasks: list[ScheduledTask] = []

    def schedule_in(self, task_id: str, delay_s: float, callback, **payload) -> None:
        self._tasks.append(ScheduledTask(task_id, time.time() + delay_s, callback, payload))

    async def tick(self) -> int:
        now = time.time()
        due = [t for t in self._tasks if t.run_at <= now]
        self._tasks = [t for t in self._tasks if t.run_at > now]
        for task in due:
            result = task.callback()
            if hasattr(result, "__await__"):
                await result
        return len(due)


scheduler = SchedulerEngine()
