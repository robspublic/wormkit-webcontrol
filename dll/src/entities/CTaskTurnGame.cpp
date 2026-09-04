#include "CTaskTurnGame.h"

#include "../Log.h"

void CTaskTurnGame::install() {
    // TODO(offsets): optionally hook the turn-game message handler to react to
    // turn start/end. Read-only turn queries just read the fields below.
    Log::info("CTaskTurnGame::install (stub)");
}

int CTaskTurnGame::currentTurnTeamNumber() {
    if (its_before_round_start_dword140) return -1;
    if (turn_paused_dword150) return -1;
    // TODO(offsets): the reference exposes the active team via
    // current_team_1_* / current_team_2_*. Determine which encodes the team
    // number vs. a pointer and return the team index here.
    return -1;
}
