"""Backend API + turn-gating tests, using the mock IPC transport."""

from __future__ import annotations

import pytest
from fastapi.testclient import TestClient

from app.ipc import MockIpcTransport
from app.main import app
from app.store import MappingStore

ADMIN = {"X-Auth-Email": "admin@example.com"}
ALICE = {"X-Auth-Email": "alice@example.com"}
BOB = {"X-Auth-Email": "bob@example.com"}


@pytest.fixture()
def client(tmp_path):
    """A TestClient with an isolated in-memory-ish store and mock IPC.

    Overrides the app's lifespan-created state so tests don't touch a real DB
    or named pipe.
    """
    store = MappingStore(str(tmp_path / "test.db"))
    ipc = MockIpcTransport(turn_team="Red")

    with TestClient(app) as c:
        # Replace lifespan-created resources with test doubles.
        app.state.store = store
        app.state.ipc = ipc
        c.store = store  # type: ignore[attr-defined]
        c.ipc = ipc  # type: ignore[attr-defined]
        yield c

    store.close()


def test_health(client):
    r = client.get("/api/health")
    assert r.status_code == 200
    assert r.json()["status"] == "ok"


def test_me_requires_auth(client):
    assert client.get("/api/me").status_code == 401


def test_me_reports_teams(client):
    client.put("/api/admin/mappings/Red", json={"email": "alice@example.com"}, headers=ADMIN)
    r = client.get("/api/me", headers=ALICE)
    assert r.status_code == 200
    assert r.json() == {"email": "alice@example.com", "teams": ["Red"]}


def test_admin_mapping_crud(client):
    assert client.get("/api/admin/mappings", headers=ADMIN).json() == {}
    client.put("/api/admin/mappings/Red", json={"email": "ALICE@example.com"}, headers=ADMIN)
    # email is normalized to lowercase
    assert client.get("/api/admin/mappings", headers=ADMIN).json() == {"Red": "alice@example.com"}
    client.delete("/api/admin/mappings/Red", headers=ADMIN)
    assert client.get("/api/admin/mappings", headers=ADMIN).json() == {}


def test_control_allows_turn_holder(client):
    # Red is Alice's team, and it's Red's turn -> allowed, command forwarded.
    client.put("/api/admin/mappings/Red", json={"email": "alice@example.com"}, headers=ADMIN)
    with client.websocket_connect("/ws/control", headers=ALICE) as ws:
        ws.send_json({"action": "move_left"})
        reply = ws.receive_json()
    assert reply["ok"] is True
    sent = client.ipc.sent  # type: ignore[attr-defined]
    assert len(sent) == 1
    assert sent[0].team == "Red"
    assert sent[0].action.value == "move_left"


def test_control_blocks_non_turn_holder(client):
    # Bob owns Blue, but it's Red's turn -> blocked, nothing forwarded.
    client.put("/api/admin/mappings/Red", json={"email": "alice@example.com"}, headers=ADMIN)
    client.put("/api/admin/mappings/Blue", json={"email": "bob@example.com"}, headers=ADMIN)
    with client.websocket_connect("/ws/control", headers=BOB) as ws:
        ws.send_json({"action": "move_right"})
        reply = ws.receive_json()
    assert reply["ok"] is False
    assert reply["error"] == "not your turn"
    assert client.ipc.sent == []  # type: ignore[attr-defined]


def test_control_rejects_unknown_action(client):
    client.put("/api/admin/mappings/Red", json={"email": "alice@example.com"}, headers=ADMIN)
    with client.websocket_connect("/ws/control", headers=ALICE) as ws:
        ws.send_json({"action": "explode_everything"})
        reply = ws.receive_json()
    assert reply["ok"] is False
    assert "unknown action" in reply["error"]
    assert client.ipc.sent == []  # type: ignore[attr-defined]
