r"""IPC protocol shared between the backend and the wkWebControl DLL.

Wire format: newline-delimited JSON over TCP loopback (127.0.0.1:27099).
Each message is a single JSON object on one line.

Backend -> DLL (fire-and-forget commands):
    {"type": "cmd", "team": "Red", "action": "move_left", "value": 0}

DLL -> Backend (push stream, one snapshot line per game logic frame):
    {"game_id": 3, "round_active": true, "num_teams": 2, "teams": [ ... ], ...}
"""

from __future__ import annotations

import json
from enum import StrEnum

from pydantic import BaseModel, Field


class ControlAction(StrEnum):
    """Actions the web UI can request. Values must match the DLL's parser."""

    MOVE_LEFT = "move_left"
    MOVE_RIGHT = "move_right"
    AIM_UP = "aim_up"
    AIM_DOWN = "aim_down"
    SELECT_WEAPON = "select_weapon"
    FIRE = "fire"
    JUMP = "jump"


class ControlPhase(StrEnum):
    """Whether a command is the start (press) or end (release) of a held input.

    Movement and aim are held while the button is down; fire is a charge on
    press and a launch on release. Values must match the DLL's parse_phase.
    """

    PRESS = "press"
    RELEASE = "release"


class CommandMessage(BaseModel):
    """A control command targeting a specific team."""

    type: str = Field(default="cmd", frozen=True)
    team: str
    action: ControlAction
    phase: ControlPhase = ControlPhase.PRESS
    value: int = 0

    def to_wire(self) -> bytes:
        """Serialize to a single newline-terminated JSON line."""
        return (json.dumps(self.model_dump(mode="json")) + "\n").encode("utf-8")


# --------------------------------------------------------------------------- #
# Monitor snapshot (pushed by the DLL, one line per game logic frame)
#
# Field names must match the DLL's Protocol serialization (dll/src/IpcServer.cpp
# stateLine() and dll/src/Protocol.h). Only high-confidence fields are
# included; team NAME and HEALTH are intentionally absent (no reliable offset).
# --------------------------------------------------------------------------- #
class WormInfo(BaseModel):
    team: int
    worm: int
    active: bool = False
    pos_x: int = 0
    pos_y: int = 0
    weapon: int = 0
    facing: int = 0


class TeamInfo(BaseModel):
    team_number: int
    owner: int = 0
    current_worm: int = 0
    is_turn_holder: bool = False
    is_local: bool = False
    worms: list[WormInfo] = Field(default_factory=list)


class WeaponAmmo(BaseModel):
    """Per-weapon availability for the current turn team (from WA's ammo table).

    ammo: -1 = infinite, 0 = unavailable this round, >0 = finite count.
    delay: rounds until the weapon becomes available (0 = now / not deferred).
    """

    id: int
    ammo: int = 0
    delay: int = 0


class GameSnapshot(BaseModel):
    """Full read-only view of game state for the monitor."""

    game_id: int = 0
    round_active: bool = False
    before_round_start: bool = False
    num_teams: int = 0
    current_machine: int = -1
    turn_team: int | None = None
    turn_time_ms: int | None = None
    teams: list[TeamInfo] = Field(default_factory=list)
    # Weapon availability for the turn team (empty if unknown / no turn).
    weapons: list[WeaponAmmo] = Field(default_factory=list)

    @classmethod
    def from_wire(cls, line: bytes) -> "GameSnapshot":
        """Parse a JSON snapshot line from the DLL."""
        return cls.model_validate_json(line.decode("utf-8"))
