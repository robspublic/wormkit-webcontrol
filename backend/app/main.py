"""wkWebControl FastAPI application.

Endpoints:
    GET    /api/health                     - liveness
    GET    /api/me                          - who am I (from proxy header)
    GET    /api/turn                        - current turn state (from DLL)
    GET    /api/admin/mappings              - list team<->email mappings
    PUT    /api/admin/mappings/{team}       - set a mapping        (admin)
    DELETE /api/admin/mappings/{team}       - remove a mapping     (admin)
    WS     /ws/control                      - player control channel
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
from pydantic import BaseModel

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
    teams = get_store().teams_for_email(email)
    return {"email": email, "teams": teams}


@app.get("/api/turn")
def turn(_: str = Depends(require_email)) -> dict[str, object]:
    state = get_ipc().query_turn()
    return state.model_dump()


# --------------------------------------------------------------------------- #
# Admin: team <-> email mapping
# --------------------------------------------------------------------------- #
class MappingBody(BaseModel):
    email: str


@app.get("/api/admin/mappings")
def list_mappings(_: str = Depends(require_admin)) -> dict[str, str]:
    return get_store().all_mappings()


@app.put("/api/admin/mappings/{team}")
def set_mapping(
    team: str, body: MappingBody, _: str = Depends(require_admin)
) -> dict[str, str]:
    get_store().set_mapping(team, body.email)
    return {"team": team, "email": body.email.lower()}


@app.delete("/api/admin/mappings/{team}", status_code=status.HTTP_204_NO_CONTENT)
def delete_mapping(team: str, _: str = Depends(require_admin)) -> None:
    get_store().delete_mapping(team)


# --------------------------------------------------------------------------- #
# Player control WebSocket
# --------------------------------------------------------------------------- #
def _is_users_turn(email: str) -> tuple[bool, str | None]:
    """Return (allowed, turn_team). The user may act only if the current
    turn-holding team maps to their email."""
    state = get_ipc().query_turn()
    turn_team = state.turn_team
    if not turn_team:
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

            # turn_team is guaranteed non-None when allowed is True.
            cmd = CommandMessage(
                team=turn_team,  # type: ignore[arg-type]
                action=action,
                value=int(msg.get("value", 0)),
            )
            get_ipc().send_command(cmd)
            await ws.send_json({"ok": True, "action": action.value})
    except WebSocketDisconnect:
        return
