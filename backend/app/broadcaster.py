"""Fan out the latest game snapshot to all connected clients at a fixed rate.

The DLL pushes a snapshot per game frame (~50Hz); the transport keeps the
latest. This broadcaster runs one async loop that, every BROADCAST_MS, reads
that latest snapshot and sends it to every /ws/state subscriber — so the client
count doesn't affect how often we touch the game, and all clients update in
lockstep at a controlled rate.

It also owns new-game detection: when the snapshot's game_id changes, it invokes
an on_new_game callback (used to clear team claims).
"""

from __future__ import annotations

import asyncio
import contextlib
from typing import Awaitable, Callable

from fastapi import WebSocket

from .ipc import AbstractIpcTransport
from .protocol import GameSnapshot

BROADCAST_HZ = 10
BROADCAST_MS = 1000 // BROADCAST_HZ


class StateBroadcaster:
    def __init__(
        self,
        ipc: AbstractIpcTransport,
        on_new_game: Callable[[int], None] | None = None,
    ) -> None:
        self._ipc = ipc
        self._on_new_game = on_new_game
        self._clients: set[WebSocket] = set()
        self._task: asyncio.Task | None = None
        self._last_game_id = 0

    # ---- lifecycle ----
    def start(self) -> None:
        if self._task is None:
            self._task = asyncio.create_task(self._loop())

    async def stop(self) -> None:
        if self._task is not None:
            self._task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._task
            self._task = None

    # ---- client registry ----
    async def connect(self, ws: WebSocket) -> None:
        await ws.accept()
        self._clients.add(ws)
        # Send the current state immediately so a new client isn't blank until
        # the next tick.
        await self._send_one(ws, self._envelope(self._ipc.latest_state()))

    def disconnect(self, ws: WebSocket) -> None:
        self._clients.discard(ws)

    # ---- broadcast loop ----
    async def _loop(self) -> None:
        while True:
            snap = self._ipc.latest_state()
            self._detect_new_game(snap)
            if self._clients:
                msg = self._envelope(snap)
                await asyncio.gather(
                    *(self._send_one(ws, msg) for ws in list(self._clients)),
                    return_exceptions=True,
                )
            await asyncio.sleep(BROADCAST_MS / 1000)

    def _detect_new_game(self, snap: GameSnapshot | None) -> None:
        if snap is None or not snap.game_id:
            return
        if snap.game_id != self._last_game_id:
            if self._last_game_id != 0 and self._on_new_game is not None:
                self._on_new_game(snap.game_id)
            self._last_game_id = snap.game_id

    @staticmethod
    def _envelope(snap: GameSnapshot | None) -> dict:
        if snap is None:
            return {"type": "offline"}
        return {"type": "state", "snapshot": snap.model_dump()}

    async def _send_one(self, ws: WebSocket, msg: dict) -> None:
        try:
            await ws.send_json(msg)
        except Exception:
            # Drop a broken client; its own endpoint handler will clean up.
            self._clients.discard(ws)
