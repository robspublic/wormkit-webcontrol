"""Backend API + claim + turn-gating tests, using the mock IPC transport.

Identity is by team NUMBER. The mock's default snapshot makes team 0 the current
turn holder (local), team 1 a remote team.
"""

from __future__ import annotations

import pytest
from fastapi.testclient import TestClient

from app.broadcaster import StateBroadcaster
from app.ipc import MockIpcTransport
from app.main import app
from app.store import MappingStore

ADMIN = {"X-Auth-Email": "admin@example.com"}
ALICE = {"X-Auth-Email": "alice@example.com"}
BOB = {"X-Auth-Email": "bob@example.com"}


@pytest.fixture()
def client(tmp_path):
    """A TestClient with an isolated store and mock IPC (team 0 = turn holder)."""
    store = MappingStore(str(tmp_path / "test.db"))
    ipc = MockIpcTransport(turn_team=0)

    with TestClient(app) as c:
        app.state.store = store
        app.state.ipc = ipc
        # Rebind the broadcaster to the test's mock ipc + store so new-game
        # detection (which lives in the broadcaster) uses the test doubles.
        app.state.broadcaster = StateBroadcaster(ipc, on_new_game=lambda _gid: store.clear_all())
        c.store = store  # type: ignore[attr-defined]
        c.ipc = ipc  # type: ignore[attr-defined]
        yield c

    store.close()


def _claim(client, headers):
    return client.post("/api/claim", headers=headers)


def test_health(client):
    r = client.get("/api/health")
    assert r.status_code == 200
    assert r.json()["status"] == "ok"


def test_me_requires_auth(client):
    assert client.get("/api/me").status_code == 401


def test_me_reports_no_team_initially(client):
    r = client.get("/api/me", headers=ALICE)
    assert r.status_code == 200
    body = r.json()
    assert body["email"] == "alice@example.com"
    assert body["team"] is None


def test_monitor_returns_snapshot(client):
    r = client.get("/api/monitor", headers=ALICE)
    assert r.status_code == 200
    snap = r.json()
    assert snap["round_active"] is True
    assert snap["turn_team"] == 0
    assert len(snap["teams"]) == 2


# --- Claim flow ------------------------------------------------------------- #
def test_claim_maps_current_turn_team(client):
    # Team 0 is taking its turn -> Alice claims it.
    r = _claim(client, ALICE)
    assert r.status_code == 200
    assert r.json()["team"] == 0
    # /api/me now reports her team.
    assert client.get("/api/me", headers=ALICE).json()["team"] == 0


def test_claim_rejected_when_user_already_has_team(client):
    assert _claim(client, ALICE).status_code == 200
    r = _claim(client, ALICE)
    assert r.status_code == 409
    assert "already control" in r.json()["detail"]


def test_claim_rejected_when_team_already_claimed(client):
    # Alice claims team 0 (the turn holder); Bob then tries the same team.
    assert _claim(client, ALICE).status_code == 200
    r = _claim(client, BOB)
    assert r.status_code == 409
    assert "already claimed" in r.json()["detail"]


def test_claim_rejected_when_no_active_turn(client):
    client.ipc.set_turn(None)  # type: ignore[attr-defined]
    r = _claim(client, ALICE)
    assert r.status_code == 409
    assert "No team" in r.json()["detail"]


# --- Turn-gated control ----------------------------------------------------- #
def test_control_allows_turn_holder(client):
    # Alice claims team 0 which is the current turn -> command forwarded.
    assert _claim(client, ALICE).status_code == 200
    with client.websocket_connect("/ws/control", headers=ALICE) as ws:
        ws.send_json({"action": "move_left"})
        reply = ws.receive_json()
    assert reply["ok"] is True
    sent = client.ipc.sent  # type: ignore[attr-defined]
    # The press is forwarded; a matching release is auto-sent on disconnect
    # (the client held the input without an explicit release).
    assert sent[0].team == "0"  # team number as string on the wire
    assert sent[0].action.value == "move_left"
    assert sent[0].phase.value == "press"
    assert sent[-1].phase.value == "release"


def test_control_blocks_non_turn_holder(client):
    # Alice claims team 0. Then simulate team 1's turn; Alice (team 0) blocked.
    assert _claim(client, ALICE).status_code == 200
    client.ipc.set_turn(1)  # type: ignore[attr-defined]
    with client.websocket_connect("/ws/control", headers=ALICE) as ws:
        ws.send_json({"action": "move_right"})
        reply = ws.receive_json()
    assert reply["ok"] is False
    assert reply["error"] == "not your turn"
    assert client.ipc.sent == []  # type: ignore[attr-defined]


def test_control_rejects_unknown_action(client):
    assert _claim(client, ALICE).status_code == 200
    with client.websocket_connect("/ws/control", headers=ALICE) as ws:
        ws.send_json({"action": "explode_everything"})
        reply = ws.receive_json()
    assert reply["ok"] is False
    assert "unknown action" in reply["error"]
    assert client.ipc.sent == []  # type: ignore[attr-defined]


def test_control_forwards_phase(client):
    # A press then a release are forwarded with their phase intact.
    assert _claim(client, ALICE).status_code == 200
    with client.websocket_connect("/ws/control", headers=ALICE) as ws:
        ws.send_json({"action": "move_left", "phase": "press"})
        assert ws.receive_json()["ok"] is True
        ws.send_json({"action": "move_left", "phase": "release"})
        assert ws.receive_json()["ok"] is True
    sent = client.ipc.sent  # type: ignore[attr-defined]
    assert [(c.action.value, c.phase.value) for c in sent] == [
        ("move_left", "press"),
        ("move_left", "release"),
    ]


def test_control_auto_releases_held_input_on_disconnect(client):
    # Holding an input (press with no release) then dropping the socket should
    # auto-emit a release so the worm doesn't keep moving.
    assert _claim(client, ALICE).status_code == 200
    with client.websocket_connect("/ws/control", headers=ALICE) as ws:
        ws.send_json({"action": "fire", "phase": "press"})
        assert ws.receive_json()["ok"] is True
    # After the context manager closes the socket, the disconnect handler runs.
    sent = client.ipc.sent  # type: ignore[attr-defined]
    assert [(c.action.value, c.phase.value) for c in sent] == [
        ("fire", "press"),
        ("fire", "release"),
    ]


# --- Admin ------------------------------------------------------------------ #
def test_admin_list_and_clear_mappings(client):
    assert _claim(client, ALICE).status_code == 200
    assert client.get("/api/admin/mappings", headers=ADMIN).json() == {"0": "alice@example.com"}
    r = client.post("/api/admin/clear-mappings", headers=ADMIN)
    assert r.status_code == 200
    assert client.get("/api/admin/mappings", headers=ADMIN).json() == {}
    # After clear, Alice can claim again.
    assert _claim(client, ALICE).status_code == 200


def test_ws_state_sends_snapshot_on_connect(client):
    with client.websocket_connect("/ws/state", headers=ALICE) as ws:
        msg = ws.receive_json()
    assert msg["type"] == "state"
    assert msg["snapshot"]["num_teams"] == 2
    assert msg["snapshot"]["turn_team"] == 0


def test_admin_delete_single_mapping(client):
    assert _claim(client, ALICE).status_code == 200
    r = client.delete("/api/admin/mappings/0", headers=ADMIN)
    assert r.status_code == 204
    assert client.get("/api/admin/mappings", headers=ADMIN).json() == {}


def test_new_game_auto_clears_claims(client):
    # Alice claims team 0 in the current game (game_id=1).
    assert _claim(client, ALICE).status_code == 200
    assert client.get("/api/me", headers=ALICE).json()["team"] == 0

    bc = app.state.broadcaster
    # First observation of the current game_id establishes the baseline (no
    # clear on first sight).
    bc._detect_new_game(client.ipc.latest_state())  # type: ignore[attr-defined]
    assert client.get("/api/me", headers=ALICE).json()["team"] == 0

    # A new game starts (game_id bumps) -> broadcaster clears claims.
    client.ipc.new_game(game_id=2, turn_team=0)  # type: ignore[attr-defined]
    bc._detect_new_game(client.ipc.latest_state())  # type: ignore[attr-defined]
    assert client.get("/api/me", headers=ALICE).json()["team"] is None
    assert _claim(client, ALICE).status_code == 200
