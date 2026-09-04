"""wkWebControl FastAPI application.

Endpoints:
    GET    /api/health                       - liveness
    GET    /api/me                            - who am I + my claimed team
    GET    /api/turn                          - current turn state (from DLL)
    GET    /api/monitor                       - full game snapshot (from DLL)
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
    WebSocket,
    WebSocketDisconnect,
    status,
)

from .auth import require_admin, require_email
from .config import settings
from .ipc import AbstractIpcTransport, create_transport
from .protocol import CommandMessage, ControlAction
from .store import MappingStore


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Create shared resources (store + IPC transport) for the app's lifetime."""
    app.state.store = MappingStore(settings.db_path)
    app.state.ipc = create_transport(settings.use_mock_ipc, settings.pipe_name)
    try:
        yield
    finally:
        app.state.ipc.close()
        app.state.store.close()


app = FastAPI(title="wkWebControl backend", version="0.1.0", lifespan=lifespan)


def get_store() -> MappingStore:
    return app.state.store


def get_ipc() -> AbstractIpcTransport:
    return app.state.ipc


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


@app.get("/api/turn")
def turn(_: str = Depends(require_email)) -> dict[str, object]:
    return get_ipc().query_turn().model_dump()


@app.get("/api/monitor")
def monitor(_: str = Depends(require_email)) -> dict[str, object]:
    """Full read-only game snapshot for the monitor view."""
    return get_ipc().query_state().model_dump()


@app.get("/api/debug/raw-state")
def debug_raw_state(_: str = Depends(require_admin)) -> dict[str, str]:
    """Diagnostic: the exact bytes the DLL returned for a state query, with a
    timestamp so repeated calls can be compared for staleness. Bypasses parsing
    and the frontend to isolate DLL-vs-transport-vs-UI freezes."""
    import time

    return {"ts": str(time.time()), "raw": get_ipc().query_state_raw()}


# --------------------------------------------------------------------------- #
# Claim: an unmapped user claims the team currently taking its turn
# --------------------------------------------------------------------------- #
@app.post("/api/claim")
def claim(email: str = Depends(require_email)) -> dict[str, object]:
    store = get_store()

    # One team per user: refuse if the caller already owns a team.
    existing = store.team_for_email(email)
    if existing is not None:
        raise HTTPException(
            status_code=status.HTTP_409_CONFLICT,
            detail=f"You already control team {existing}",
        )

    state = get_ipc().query_state()
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
    state = get_ipc().query_state()
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
    try:
        while True:
            msg = await ws.receive_json()
            action_raw = msg.get("action")
            try:
                action = ControlAction(action_raw)
            except ValueError:
                await ws.send_json({"ok": False, "error": f"unknown action: {action_raw}"})
                continue

            allowed, turn_team = _is_users_turn(email)
            if not allowed:
                await ws.send_json({"ok": False, "error": "not your turn", "turn_team": turn_team})
                continue

            # turn_team is a non-None int when allowed. The DLL identifies the
            # team by number; CommandMessage.team carries it as a string.
            cmd = CommandMessage(
                team=str(turn_team),
                action=action,
                value=int(msg.get("value", 0)),
            )
            get_ipc().send_command(cmd)
            await ws.send_json({"ok": True, "action": action.value})
    except WebSocketDisconnect:
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
