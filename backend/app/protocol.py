r"""IPC protocol shared between the backend and the wkWebControl DLL.

Wire format: newline-delimited JSON over a named pipe (\\.\pipe\wkwebcontrol).
Each message is a single JSON object on one line.

Backend -> DLL:
    {"type": "cmd", "team": "Red", "action": "move_left", "value": 0}
    {"type": "query", "what": "turn"}

DLL -> Backend (response to a query):
    {"turn_team": "Red", "pos_x": 1234, "pos_y": 567, "weapon": 3}
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


class CommandMessage(BaseModel):
    """A control command targeting a specific team."""

    type: str = Field(default="cmd", frozen=True)
    team: str
    action: ControlAction
    value: int = 0

    def to_wire(self) -> bytes:
        """Serialize to a single newline-terminated JSON line."""
        return (json.dumps(self.model_dump(mode="json")) + "\n").encode("utf-8")


class QueryMessage(BaseModel):
    """A request for current game state (e.g. whose turn it is)."""

    type: str = Field(default="query", frozen=True)
    what: str = "turn"

    def to_wire(self) -> bytes:
        return (json.dumps(self.model_dump(mode="json")) + "\n").encode("utf-8")


class TurnState(BaseModel):
    """Current turn/worm state reported by the DLL.

    All fields optional so a partial/early-game response still parses.
    """

    turn_team: str | None = None
    pos_x: int | None = None
    pos_y: int | None = None
    weapon: int | None = None
    round_active: bool = False

    @classmethod
    def from_wire(cls, line: bytes) -> "TurnState":
        """Parse a JSON response line from the DLL."""
        return cls.model_validate_json(line.decode("utf-8"))
