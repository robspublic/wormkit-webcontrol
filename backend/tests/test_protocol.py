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
    ControlPhase,
    GameSnapshot,
    TeamInfo,
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
    "jump",
}

# Phase strings must equal the DLL's Protocol::kPhase* constants.
EXPECTED_PHASES = {"press", "release"}


def test_action_values_match_contract():
    assert {a.value for a in ControlAction} == EXPECTED_ACTIONS


def test_phase_values_match_contract():
    assert {p.value for p in ControlPhase} == EXPECTED_PHASES


def test_command_wire_shape():
    line = CommandMessage(team="Red", action=ControlAction.MOVE_LEFT, value=0).to_wire()
    assert line.endswith(b"\n")
    obj = json.loads(line)
    # phase defaults to "press" so simple/older clients still work.
    assert obj == {
        "type": "cmd",
        "team": "Red",
        "action": "move_left",
        "phase": "press",
        "value": 0,
    }


def test_command_release_wire_shape():
    line = CommandMessage(
        team="0", action=ControlAction.FIRE, phase=ControlPhase.RELEASE
    ).to_wire()
    obj = json.loads(line)
    assert obj == {
        "type": "cmd",
        "team": "0",
        "action": "fire",
        "phase": "release",
        "value": 0,
    }


# --- Monitor snapshot (push stream) contract ------------------------------- #
def test_game_snapshot_field_names():
    # Must match the DLL's stateLine() serialization.
    assert set(GameSnapshot().model_dump().keys()) == {
        "game_id",
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
    # A line exactly as the DLL's stateLine() would emit (two teams, worms).
    line = (
        b'{"game_id":3,"round_active":true,"before_round_start":false,"num_teams":2,'
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
