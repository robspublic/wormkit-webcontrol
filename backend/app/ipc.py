"""Transport to the wkWebControl DLL.

Production uses a Windows named pipe (the backend runs on the same Windows
machine as WA.exe). For development and testing on Linux, a mock transport
records commands and returns a canned turn state, so the whole backend runs and
is testable without the game or Windows.

The concrete transport is chosen at runtime by platform, or forced via config.
"""

from __future__ import annotations

import sys
import threading
from abc import ABC, abstractmethod

from .protocol import CommandMessage, QueryMessage, TurnState


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
    def close(self) -> None: ...


class MockIpcTransport(AbstractIpcTransport):
    """In-process stand-in used on Linux / in tests.

    Records sent commands and returns a configurable turn state so the API,
    turn-gating, and frontend can all be exercised without the game.
    """

    def __init__(self, turn_team: str | None = "Red") -> None:
        self._lock = threading.RLock()
        self.sent: list[CommandMessage] = []
        self._turn = TurnState(
            turn_team=turn_team,
            pos_x=0,
            pos_y=0,
            weapon=0,
            round_active=turn_team is not None,
        )

    def send_command(self, cmd: CommandMessage) -> None:
        with self._lock:
            self.sent.append(cmd)

    def query_turn(self) -> TurnState:
        with self._lock:
            return self._turn.model_copy()

    def set_turn(self, turn_team: str | None) -> None:
        """Test/dev helper to simulate a turn change."""
        with self._lock:
            self._turn = TurnState(
                turn_team=turn_team,
                pos_x=0,
                pos_y=0,
                weapon=0,
                round_active=turn_team is not None,
            )

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
            # Read one line back. Buffer sized to match the DLL's pipe buffers.
            _, data = win32file.ReadFile(self._handle, 4096)
            return TurnState.from_wire(data)

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
