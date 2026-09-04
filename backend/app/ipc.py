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

from .protocol import CommandMessage, GameSnapshot, TeamInfo, WormInfo


class GameOfflineError(RuntimeError):
    """The DLL isn't reachable (game not running yet / closed) or hasn't sent a
    frame yet."""


class AbstractIpcTransport(ABC):
    """Channel to the DLL.

    Push model: the DLL streams a game snapshot per frame; the transport keeps
    the latest one. Commands go the other way (fire-and-forget).
    """

    def start(self) -> None:
        """Begin receiving (spawn the reader thread). No-op for the mock."""

    @abstractmethod
    def send_command(self, cmd: CommandMessage) -> None:
        """Send a control command (fire-and-forget)."""
        ...

    @abstractmethod
    def latest_state(self) -> GameSnapshot | None:
        """The most recent snapshot pushed by the DLL, or None if offline / no
        frame received yet."""
        ...

    def is_online(self) -> bool:
        return self.latest_state() is not None

    @abstractmethod
    def close(self) -> None: ...


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
            game_id=1,
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

    def latest_state(self) -> GameSnapshot | None:
        with self._lock:
            return self._snapshot.model_copy(deep=True)

    def set_turn(self, turn_team: int | None) -> None:
        """Test/dev helper to simulate a turn change (by team number)."""
        with self._lock:
            gid = self._snapshot.game_id
            self._snapshot = self._default_snapshot(turn_team)
            self._snapshot.game_id = gid  # a turn change is not a new game

    def new_game(self, game_id: int, turn_team: int | None = 0) -> None:
        """Test/dev helper to simulate a new game (bumps game_id)."""
        with self._lock:
            self._snapshot = self._default_snapshot(turn_team)
            self._snapshot.game_id = game_id

    def close(self) -> None:  # nothing to release
        return


class TcpIpcTransport(AbstractIpcTransport):
    """Client to the DLL's push stream (connects to 127.0.0.1:<port>).

    The DLL (inside WA under Wine/Proton) listens and, on each game frame, sends
    a snapshot line. A background reader thread connects, consumes the stream,
    and keeps the latest snapshot. Commands go back on the same socket. Wine
    maps Winsock TCP onto the host stack, so loopback works across the boundary.
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 27099) -> None:
        self._host = host
        self._port = port
        self._lock = threading.RLock()
        self._sock: "socket.socket | None" = None  # write side (commands)
        self._latest: GameSnapshot | None = None
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()
        self._offline_logged = False

    def start(self) -> None:
        if self._thread is not None:
            return
        self._thread = threading.Thread(
            target=self._reader_loop, name="wkwc-ipc-reader", daemon=True
        )
        self._thread.start()

    def _reader_loop(self) -> None:
        import socket

        while not self._stop.is_set():
            try:
                s = socket.create_connection((self._host, self._port), timeout=2.0)
                s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            except OSError:
                if not self._offline_logged:
                    print(
                        f"wkWebControl: waiting for the WormKit plugin at "
                        f"{self._host}:{self._port} (start WA with the plugin loaded)…"
                    )
                    self._offline_logged = True
                self._set_latest(None)
                self._stop.wait(1.0)  # retry the connection shortly
                continue

            print("wkWebControl: WormKit plugin connected.")
            self._offline_logged = False
            with self._lock:
                self._sock = s
            try:
                self._consume(s)
            finally:
                with self._lock:
                    self._sock = None
                self._set_latest(None)  # connection dropped -> offline
                try:
                    s.close()
                except OSError:
                    pass
                if not self._stop.is_set():
                    print("wkWebControl: WormKit plugin disconnected.")

    def _consume(self, s) -> None:
        """Read newline-delimited snapshot lines until the socket closes."""
        buf = bytearray()
        s.settimeout(5.0)
        while not self._stop.is_set():
            try:
                data = s.recv(16384)
            except (TimeoutError, OSError):
                break
            if not data:
                break  # DLL closed the connection
            buf.extend(data)
            # Keep only the most recent complete line: the DLL pushes every
            # frame, so intermediate lines are already stale — don't bother
            # parsing them all.
            while b"\n" in buf:
                line, _, rest = buf.partition(b"\n")
                if b"\n" not in rest:
                    # `line` is the last complete line; parse it and keep `rest`
                    # (a partial next line) in the buffer.
                    self._on_line(bytes(line))
                    buf = bytearray(rest)
                    break
                # More complete lines follow; skip this stale one.
                buf = bytearray(rest)

    def _on_line(self, line: bytes) -> None:
        if not line.strip():
            return
        try:
            snap = GameSnapshot.from_wire(line)
        except Exception:
            return  # ignore a malformed/partial line; next frame recovers
        self._set_latest(snap)

    def _set_latest(self, snap: GameSnapshot | None) -> None:
        with self._lock:
            self._latest = snap

    def latest_state(self) -> GameSnapshot | None:
        with self._lock:
            return self._latest

    def send_command(self, cmd: CommandMessage) -> None:
        with self._lock:
            sock = self._sock
        if sock is None:
            raise GameOfflineError("no connection to the WormKit plugin")
        try:
            sock.sendall(cmd.to_wire())
        except OSError as e:
            raise GameOfflineError(str(e)) from None

    def close(self) -> None:
        self._stop.set()
        with self._lock:
            if self._sock is not None:
                try:
                    self._sock.close()
                except OSError:
                    pass
        if self._thread is not None:
            self._thread.join(timeout=2.0)


def create_transport(use_mock: bool, host: str, port: int) -> AbstractIpcTransport:
    """Factory: the mock transport only when explicitly requested; otherwise the
    real TCP transport (works on Linux too, since the DLL streams over TCP)."""
    if use_mock:
        return MockIpcTransport()
    return TcpIpcTransport(host, port)
