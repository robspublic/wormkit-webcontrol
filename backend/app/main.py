"""wkWebControl FastAPI application.

Endpoints:
    GET    /api/health                       - liveness
    GET    /api/me                            - who am I + my claimed team
    GET    /api/monitor                       - latest game snapshot (REST fallback)
    WS     /ws/state                          - live game-state stream (10Hz)
    POST   /api/claim                         - claim the current turn's team
    GET    /api/admin/mappings                - list team#->email mappings (admin)
    DELETE /api/admin/mappings/{team_number}  - remove one mapping        (admin)
    POST   /api/admin/clear-mappings          - clear all mappings         (admin)
    WS     /ws/control                        - player control channel

Teams are identified by NUMBER (the DLL can't read names). Users don't pick a
number: when the current turn belongs to an unclaimed team, an unmapped user
claims it via POST /api/claim ("This is my team"). One team per user.
"""

from __future__ import annotations

from contextlib import asynccontextmanager

from fastapi import (
    Depends,
    FastAPI,
    HTTPException,
    Request,
    WebSocket,
    WebSocketDisconnect,
    status,
)
from fastapi.responses import JSONResponse

from .auth import require_admin, require_email
from .broadcaster import StateBroadcaster
from .config import settings
from .ipc import AbstractIpcTransport, GameOfflineError, create_transport
from .protocol import CommandMessage, ControlAction, ControlPhase
from .store import MappingStore


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Create shared resources (store, IPC transport, broadcaster) for the app's
    lifetime."""
    app.state.store = MappingStore(settings.db_path)
    app.state.ipc = create_transport(
        settings.use_mock_ipc, settings.game_host, settings.game_port
    )
    app.state.ipc.start()  # spawn the DLL stream reader (no-op for the mock)

    # The broadcaster owns new-game detection; clear claims when game_id changes.
    def _on_new_game(_game_id: int) -> None:
        app.state.store.clear_all()

    app.state.broadcaster = StateBroadcaster(app.state.ipc, on_new_game=_on_new_game)
    app.state.broadcaster.start()
    try:
        yield
    finally:
        await app.state.broadcaster.stop()
        app.state.ipc.close()
        app.state.store.close()


app = FastAPI(title="wkWebControl backend", version="0.1.0", lifespan=lifespan)


@app.exception_handler(GameOfflineError)
def _game_offline_handler(_request: Request, _exc: GameOfflineError) -> JSONResponse:
    """The game/DLL isn't reachable yet. Return a tidy 503 (no traceback) so the
    frontend can show a 'waiting for the game' state and keep polling."""
    return JSONResponse(
        status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
        content={"detail": "Waiting for the WormKit plugin / game to come online"},
    )


def get_store() -> MappingStore:
    return app.state.store


def get_ipc() -> AbstractIpcTransport:
    return app.state.ipc


def get_state():
    """The latest snapshot pushed by the DLL (cached by the transport). Raises
    GameOfflineError if the game isn't connected / hasn't sent a frame yet.
    New-game claim-clearing is handled centrally by the broadcaster."""
    snap = get_ipc().latest_state()
    if snap is None:
        raise GameOfflineError("game not connected")
    return snap


# --------------------------------------------------------------------------- #
# Basic / identity
# --------------------------------------------------------------------------- #
@app.get("/api/health")
def health() -> dict[str, str]:
    return {"status": "ok"}


@app.get("/api/me")
def me(email: str = Depends(require_email)) -> dict[str, object]:
    team = get_store().team_for_email(email)
    is_admin = (not settings.admin_email_set()) or (email in settings.admin_email_set())
    return {"email": email, "team": team, "is_admin": is_admin}


@app.get("/api/monitor")
def monitor(_: str = Depends(require_email)) -> dict[str, object]:
    """Full read-only game snapshot (latest pushed by the DLL). Kept as a REST
    fallback; live clients subscribe to /ws/state instead."""
    return get_state().model_dump()


@app.websocket("/ws/state")
async def ws_state(ws: WebSocket) -> None:
    """Live game-state stream: the broadcaster pushes the latest snapshot to all
    subscribers at a fixed rate. Read-only; requires an authenticated user."""
    email = ws.headers.get(settings.auth_email_header, "").strip().lower()
    if not email:
        await ws.close(code=status.WS_1008_POLICY_VIOLATION)
        return
    bc: StateBroadcaster = app.state.broadcaster
    await bc.connect(ws)
    try:
        # We don't expect inbound messages; just keep the socket open.
        while True:
            await ws.receive_text()
    except WebSocketDisconnect:
        pass
    finally:
        bc.disconnect(ws)


# --------------------------------------------------------------------------- #
# Claim: an unmapped user claims the team currently taking its turn
# --------------------------------------------------------------------------- #
@app.post("/api/claim")
def claim(email: str = Depends(require_email)) -> dict[str, object]:
    store = get_store()

    # Fetch state first: this also clears claims if a new game has started, so a
    # stale prior-game claim doesn't block re-claiming in the new game.
    state = get_state()

    # One team per user: refuse if the caller already owns a team (this game).
    existing = store.team_for_email(email)
    if existing is not None:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail=f"You already control team {existing}",
        )

    turn_team = state.turn_team
    if turn_team is None:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail="No team is currently taking its turn",
        )

    # First-write-wins: claim() fails if the team was just taken by someone else.
    if not store.claim(turn_team, email):
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail=f"Team {turn_team} is already claimed",
        )
    return {"team": turn_team, "email": email}


# --------------------------------------------------------------------------- #
# Admin: view / clear mappings
# --------------------------------------------------------------------------- #
@app.get("/api/admin/mappings")
def list_mappings(_: str = Depends(require_admin)) -> dict[int, str]:
    return get_store().all_mappings()


@app.delete(
    "/api/admin/mappings/{team_number}", status_code=status.HTTP_204_NO_CONTENT
)
def delete_mapping(team_number: int, _: str = Depends(require_admin)) -> None:
    get_store().delete_mapping(team_number)


@app.post("/api/admin/clear-mappings")
def clear_mappings(_: str = Depends(require_admin)) -> dict[str, str]:
    get_store().clear_all()
    return {"status": "cleared"}


# --------------------------------------------------------------------------- #
# Player control WebSocket
# --------------------------------------------------------------------------- #
def _is_users_turn(email: str) -> tuple[bool, int | None]:
    """Return (allowed, turn_team_number). The user may act only if the current
    turn-holding team is mapped to their email."""
    state = get_state()
    turn_team = state.turn_team
    if turn_team is None:
        return False, None
    owner = get_store().email_for_team(turn_team)
    return (owner == email, turn_team)


@app.websocket("/ws/control")
async def control(ws: WebSocket) -> None:
    # The proxy sets X-Auth-Email on the WS upgrade request too.
    email = ws.headers.get(settings.auth_email_header, "").strip().lower()
    if not email:
        await ws.close(code=status.WS_1008_POLICY_VIOLATION)
        return

    await ws.accept()

    # Track which held inputs this client currently has pressed, and the team
    # it was controlling, so we can auto-release them if the socket drops mid
    # hold (otherwise a worm keeps walking / charging). select_weapon is a
    # one-shot and never "held", so it's excluded.
    held: set[ControlAction] = set()
    held_team: int | None = None

    def _release_held() -> None:
        if held_team is None:
            return
        for act in held:
            try:
                get_ipc().send_command(
                    CommandMessage(
                        team=str(held_team), action=act, phase=ControlPhase.RELEASE
                    )
                )
            except GameOfflineError:
                pass
        held.clear()

    try:
        while True:
            msg = await ws.receive_json()
            action_raw = msg.get("action")
            try:
                action = ControlAction(action_raw)
            except ValueError:
                await ws.send_json({"ok": False, "error": f"unknown action: {action_raw}"})
                continue

            phase_raw = msg.get("phase", ControlPhase.PRESS.value)
            try:
                phase = ControlPhase(phase_raw)
            except ValueError:
                await ws.send_json({"ok": False, "error": f"unknown phase: {phase_raw}"})
                continue

            try:
                allowed, turn_team = _is_users_turn(email)
                if not allowed:
                    # If the user just lost the turn while holding something,
                    # release it so the worm doesn't keep moving.
                    _release_held()
                    await ws.send_json(
                        {"ok": False, "error": "not your turn", "turn_team": turn_team}
                    )
                    continue

                # turn_team is a non-None int when allowed. The DLL identifies
                # the team by number; CommandMessage.team carries it as a string.
                cmd = CommandMessage(
                    team=str(turn_team),
                    action=action,
                    phase=phase,
                    value=int(msg.get("value", 0)),
                )
                get_ipc().send_command(cmd)

                # Track held state for auto-release (fire and the directions).
                if action is not ControlAction.SELECT_WEAPON:
                    held_team = turn_team
                    if phase is ControlPhase.PRESS:
                        held.add(action)
                    else:
                        held.discard(action)

                await ws.send_json(
                    {"ok": True, "action": action.value, "phase": phase.value}
                )
            except GameOfflineError:
                await ws.send_json({"ok": False, "error": "game offline"})
    except WebSocketDisconnect:
        _release_held()
        return


# --------------------------------------------------------------------------- #
# Static frontend (single-origin production serving)
#
# When frontend/dist exists, serve it so nginx proxies one origin: /api and /ws
# are handled above (registered first, so they win), and everything else falls
# through to the SPA. In dev you normally run Vite separately and this mount is
# simply absent (no dist build), which is fine.
# --------------------------------------------------------------------------- #
def _mount_frontend() -> None:
    from pathlib import Path

    from fastapi.responses import FileResponse
    from fastapi.staticfiles import StaticFiles

    dist = Path(settings.frontend_dist)
    if not dist.is_dir():
        return

    # Hashed build assets (JS/CSS) live under dist/assets.
    assets = dist / "assets"
    if assets.is_dir():
        app.mount("/assets", StaticFiles(directory=assets), name="assets")

    index = dist / "index.html"

    # Catch-all for client-side routes (e.g. /admin): serve index.html so the
    # SPA router can take over. Registered last, so API routes take precedence.
    @app.get("/{full_path:path}")
    def spa(full_path: str):  # noqa: ANN202 - FastAPI route
        candidate = dist / full_path
        if full_path and candidate.is_file():
            return FileResponse(candidate)
        return FileResponse(index)


_mount_frontend()
