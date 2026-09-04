"""Transport to the wkWebControl DLL.

Production connects over TCP loopback to the DLL's IPC server (the DLL runs
inside WA under Wine/Proton on the same host; Wine maps Winsock TCP onto the
host stack, so 127.0.0.1 is reachable from the native-Linux backend). For
development/testing without the game, a mock transport returns a canned game
snapshot. The transport is chosen by config (use_mock_ipc), not platform.
"""

from __future__ import annotations

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
        parsed snapshot re-dumped; the TCP transport overrides with real bytes)."""
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


class TcpIpcTransport(AbstractIpcTransport):
    """TCP client to the DLL's IPC server (127.0.0.1:<port>).

    The DLL runs inside WA under Wine/Proton; a Windows named pipe isn't
    reachable from the native-Linux backend, but Wine maps Winsock TCP onto the
    host stack, so loopback TCP works across the boundary. This transport is
    also plain cross-platform Python sockets, so it needs no OS-specific deps.

    A single connection is reused and re-established if the game restarts.
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 27099) -> None:
        self._host = host
        self._port = port
        self._lock = threading.RLock()
        self._sock: "socket.socket | None" = None

    def _ensure_open(self) -> None:
        import socket

        if self._sock is not None:
            return
        s = socket.create_connection((self._host, self._port), timeout=2.0)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._sock = s

    def _reset(self) -> None:
        if self._sock is not None:
            try:
                self._sock.close()
            finally:
                self._sock = None

    def _read_line(self) -> bytes:
        """Read until a newline (the DLL delimits each response with '\\n').
        A state snapshot can span multiple recv() chunks, so accumulate."""
        assert self._sock is not None
        chunks = bytearray()
        while b"\n" not in chunks:
            data = self._sock.recv(8192)
            if not data:
                break
            chunks.extend(data)
        return bytes(chunks).split(b"\n", 1)[0]

    def _request(self, payload: bytes, expect_reply: bool) -> bytes | None:
        """Send a line and optionally read one reply line, reconnecting once if
        the connection was dropped (e.g. game restarted)."""
        with self._lock:
            for attempt in (1, 2):
                try:
                    self._ensure_open()
                    self._sock.sendall(payload)  # type: ignore[union-attr]
                    return self._read_line() if expect_reply else None
                except OSError:
                    self._reset()
                    if attempt == 2:
                        raise
        return None

    def send_command(self, cmd: CommandMessage) -> None:
        self._request(cmd.to_wire(), expect_reply=False)

    def query_turn(self) -> TurnState:
        line = self._request(QueryMessage(what="turn").to_wire(), expect_reply=True)
        return TurnState.from_wire(line or b"{}")

    def query_state(self) -> GameSnapshot:
        return GameSnapshot.from_wire(self.query_state_raw().encode("utf-8"))

    def query_state_raw(self) -> str:
        line = self._request(QueryMessage(what="state").to_wire(), expect_reply=True)
        return (line or b"{}").decode("utf-8", errors="replace")

    def close(self) -> None:
        with self._lock:
            self._reset()


def create_transport(use_mock: bool, host: str, port: int) -> AbstractIpcTransport:
    """Factory: the mock transport only when explicitly requested; otherwise the
    real TCP transport (works on Linux too, since the DLL listens over TCP)."""
    if use_mock:
        return MockIpcTransport()
    return TcpIpcTransport(host, port)
