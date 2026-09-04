"""Wire-contract tests for the backend<->DLL IPC protocol.

These pin the exact JSON shape and field names the DLL parses/produces (see
dll/src/Protocol.h). If either side changes the contract, these tests should be
updated in lockstep.
"""

from __future__ import annotations

import json

from app.protocol import (
    CommandMessage,
    ControlAction,
    GameSnapshot,
    QueryMessage,
    TeamInfo,
    TurnState,
    WormInfo,
)

# Action strings must equal the DLL's Protocol::kAction* constants.
EXPECTED_ACTIONS = {
    "move_left",
    "move_right",
    "aim_up",
    "aim_down",
    "select_weapon",
    "fire",
}


def test_action_values_match_contract():
    assert {a.value for a in ControlAction} == EXPECTED_ACTIONS


def test_command_wire_shape():
    line = CommandMessage(team="Red", action=ControlAction.MOVE_LEFT, value=0).to_wire()
    assert line.endswith(b"\n")
    obj = json.loads(line)
    assert obj == {"type": "cmd", "team": "Red", "action": "move_left", "value": 0}


def test_query_wire_shape():
    obj = json.loads(QueryMessage(what="turn").to_wire())
    assert obj == {"type": "query", "what": "turn"}


def test_turn_state_field_names():
    # Field names must match what the DLL emits in turnResponse().
    ts = TurnState(turn_team="Red", pos_x=1, pos_y=2, weapon=3, round_active=True)
    assert set(ts.model_dump().keys()) == {
        "turn_team",
        "pos_x",
        "pos_y",
        "weapon",
        "round_active",
    }


def test_turn_state_parses_dll_response():
    # A response line exactly as the DLL's turnResponse() would produce it.
    line = b'{"turn_team":"Red","pos_x":1234,"pos_y":567,"weapon":3,"round_active":true}'
    ts = TurnState.from_wire(line)
    assert ts.turn_team == "Red"
    assert ts.pos_x == 1234
    assert ts.round_active is True


def test_turn_state_parses_early_game_nulls():
    line = b'{"turn_team":null,"pos_x":null,"pos_y":null,"weapon":null,"round_active":false}'
    ts = TurnState.from_wire(line)
    assert ts.turn_team is None
    assert ts.round_active is False


# --- Monitor snapshot (state query) contract ------------------------------- #
def test_state_query_wire_shape():
    obj = json.loads(QueryMessage(what="state").to_wire())
    assert obj == {"type": "query", "what": "state"}


def test_game_snapshot_field_names():
    # Must match the DLL's stateResponse() serialization.
    assert set(GameSnapshot().model_dump().keys()) == {
        "round_active",
        "before_round_start",
        "num_teams",
        "current_machine",
        "turn_team",
        "turn_time_ms",
        "teams",
    }
    assert set(TeamInfo(team_number=0).model_dump().keys()) == {
        "team_number",
        "owner",
        "current_worm",
        "is_turn_holder",
        "is_local",
        "worms",
    }
    assert set(WormInfo(team=0, worm=1).model_dump().keys()) == {
        "team",
        "worm",
        "active",
        "pos_x",
        "pos_y",
        "weapon",
        "facing",
    }


def test_game_snapshot_parses_dll_response():
    # A line exactly as the DLL's stateResponse() would emit (two teams, worms).
    line = (
        b'{"round_active":true,"before_round_start":false,"num_teams":2,'
        b'"current_machine":0,"turn_team":0,"turn_time_ms":45000,"teams":['
        b'{"team_number":0,"owner":0,"current_worm":1,"is_turn_holder":true,'
        b'"is_local":true,"worms":['
        b'{"team":0,"worm":1,"active":true,"pos_x":1200,"pos_y":800,"weapon":0,"facing":1}]},'
        b'{"team_number":1,"owner":1,"current_worm":1,"is_turn_holder":false,'
        b'"is_local":false,"worms":[]}]}'
    )
    snap = GameSnapshot.from_wire(line)
    assert snap.round_active is True
    assert snap.num_teams == 2
    assert snap.turn_team == 0
    assert len(snap.teams) == 2
    assert snap.teams[0].is_turn_holder is True
    assert snap.teams[0].worms[0].pos_x == 1200
    assert snap.teams[1].worms == []


def test_game_snapshot_no_game():
    # When not in a game the DLL reports round_active false and empty teams.
    line = b'{"round_active":false,"before_round_start":false,"num_teams":0,"current_machine":-1,"turn_team":null,"turn_time_ms":null,"teams":[]}'
    snap = GameSnapshot.from_wire(line)
    assert snap.round_active is False
    assert snap.turn_team is None
    assert snap.teams == []
