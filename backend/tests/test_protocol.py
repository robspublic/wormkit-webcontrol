"""Wire-contract tests for the backend<->DLL IPC protocol.

These pin the exact JSON shape and field names the DLL parses/produces (see
dll/src/Protocol.h). If either side changes the contract, these tests should be
updated in lockstep.
"""

from __future__ import annotations

import json

from app.protocol import CommandMessage, ControlAction, QueryMessage, TurnState

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
