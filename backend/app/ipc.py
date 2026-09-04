"""Transport to the wkWebControl DLL.

Production uses a Windows named pipe (the backend runs on the same Windows
machine as WA.exe). For development and testing on Linux, a mock transport
records commands and returns a canned game snapshot, so the whole backend runs
and is testable without the game or Windows.

The concrete transport is chosen at runtime by platform, or forced via config.
"""

from __future__ import annotations

import sys
import threading
from abc import ABC, abstractmethod

from .protocol import (
    CommandMessage,
    GameSnapshot,
    QueryMessage,
    TeamInfo,
    TurnState,
    WormInfo,
)


class AbstractIpcTransport(ABC):
    """Bidirectional channel to the DLL."""

    @abstractmethod
    def send_command(self, cmd: CommandMessage) -> None:
        """Send a control command (fire-and-forget)."""
        ...

    @abstractmethod
    def query_turn(self) -> TurnState:
        """Ask the DLL for the current turn/worm state."""
        ...

    @abstractmethod
    def query_state(self) -> GameSnapshot:
        """Ask the DLL for the full monitor snapshot."""
        ...

    def query_state_raw(self) -> str:
        """Diagnostic: the raw response text for a state query (default: the
        parsed snapshot re-dumped; the pipe transport overrides with real bytes)."""
        return self.query_state().model_dump_json()

    @abstractmethod
    def close(self) -> None: ...


def _turn_from_snapshot(snap: GameSnapshot) -> TurnState:
    """Derive the narrow turn view from a full snapshot (matches the DLL)."""
    turn = TurnState(round_active=snap.round_active)
    if snap.turn_team is None:
        return turn
    turn.turn_team = str(snap.turn_team)
    for team in snap.teams:
        if team.team_number != snap.turn_team:
            continue
        for w in team.worms:
            if w.worm == team.current_worm or w.active:
                turn.pos_x, turn.pos_y, turn.weapon = w.pos_x, w.pos_y, w.weapon
                return turn
    return turn


class MockIpcTransport(AbstractIpcTransport):
    """In-process stand-in used on Linux / in tests.

    Holds a representative game snapshot so the API, turn-gating, monitor, and
    frontend can all be exercised without the game.
    """

    def __init__(self, turn_team: int | None = 0) -> None:
        self._lock = threading.RLock()
        self.sent: list[CommandMessage] = []
        self._snapshot = self._default_snapshot(turn_team)

    @staticmethod
    def _default_snapshot(turn_team: int | None) -> GameSnapshot:
        if turn_team is None:
            return GameSnapshot(round_active=False)
        # Two teams (0 = Red-ish, 1 = Blue-ish), a few worms each, team 0 owned
        # locally and holding the turn.
        return GameSnapshot(
            round_active=True,
            before_round_start=False,
            num_teams=2,
            current_machine=0,
            turn_team=turn_team,
            turn_time_ms=45000,
            teams=[
                TeamInfo(
                    team_number=0,
                    owner=0,
                    current_worm=1,
                    is_turn_holder=(turn_team == 0),
                    is_local=True,
                    worms=[
                        WormInfo(team=0, worm=1, active=True, pos_x=1200, pos_y=800, weapon=0, facing=1),
                        WormInfo(team=0, worm=2, active=False, pos_x=1500, pos_y=810, weapon=0, facing=-1),
                    ],
                ),
                TeamInfo(
                    team_number=1,
                    owner=1,
                    current_worm=1,
                    is_turn_holder=(turn_team == 1),
                    is_local=False,
                    worms=[
                        WormInfo(team=1, worm=1, active=False, pos_x=3000, pos_y=790, weapon=0, facing=-1),
                    ],
                ),
            ],
        )

    def send_command(self, cmd: CommandMessage) -> None:
        with self._lock:
            self.sent.append(cmd)

    def query_state(self) -> GameSnapshot:
        with self._lock:
            return self._snapshot.model_copy(deep=True)

    def query_turn(self) -> TurnState:
        with self._lock:
            return _turn_from_snapshot(self._snapshot)

    def set_turn(self, turn_team: int | None) -> None:
        """Test/dev helper to simulate a turn change (by team number)."""
        with self._lock:
            self._snapshot = self._default_snapshot(turn_team)

    def close(self) -> None:  # nothing to release
        return


class NamedPipeIpcTransport(AbstractIpcTransport):
    """Windows named-pipe client to the DLL.

    Opens the pipe on demand and serializes access with a lock, since the DLL
    side is configured for a single client instance. Reconnects if the game
    (and thus the pipe server) restarts.
    """

    def __init__(self, pipe_name: str = r"\\.\pipe\wkwebcontrol") -> None:
        self._pipe_name = pipe_name
        self._lock = threading.RLock()
        self._handle = None  # opened lazily

    def _ensure_open(self) -> None:
        if self._handle is not None:
            return
        # Imported here so the module loads on non-Windows platforms.
        import win32file  # type: ignore[import-not-found]

        self._handle = win32file.CreateFile(
            self._pipe_name,
            win32file.GENERIC_READ | win32file.GENERIC_WRITE,
            0,
            None,
            win32file.OPEN_EXISTING,
            0,
            None,
        )

    def _read_line(self) -> bytes:
        """Read from the pipe until a newline (the DLL delimits each response
        with '\\n'). The state snapshot can exceed one buffer, so accumulate."""
        import win32file  # type: ignore[import-not-found]

        chunks = bytearray()
        while b"\n" not in chunks:
            _, data = win32file.ReadFile(self._handle, 8192)
            if not data:
                break
            chunks.extend(data)
        return bytes(chunks).split(b"\n", 1)[0]

    def send_command(self, cmd: CommandMessage) -> None:
        import win32file  # type: ignore[import-not-found]

        with self._lock:
            self._ensure_open()
            win32file.WriteFile(self._handle, cmd.to_wire())

    def query_turn(self) -> TurnState:
        import win32file  # type: ignore[import-not-found]

        with self._lock:
            self._ensure_open()
            win32file.WriteFile(self._handle, QueryMessage(what="turn").to_wire())
            return TurnState.from_wire(self._read_line())

    def query_state(self) -> GameSnapshot:
        return GameSnapshot.from_wire(self.query_state_raw().encode("utf-8"))

    def query_state_raw(self) -> str:
        import win32file  # type: ignore[import-not-found]

        with self._lock:
            self._ensure_open()
            win32file.WriteFile(self._handle, QueryMessage(what="state").to_wire())
            return self._read_line().decode("utf-8", errors="replace")

    def close(self) -> None:
        with self._lock:
            if self._handle is not None:
                import win32file  # type: ignore[import-not-found]

                win32file.CloseHandle(self._handle)
                self._handle = None


def create_transport(use_mock: bool, pipe_name: str) -> AbstractIpcTransport:
    """Factory: pick the transport for the current platform / config."""
    if use_mock or sys.platform != "win32":
        return MockIpcTransport()
    return NamedPipeIpcTransport(pipe_name)
