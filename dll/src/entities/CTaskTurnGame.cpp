#include "CTaskTurnGame.h"

#include "../Log.h"

void CTaskTurnGame::install() {
    // Read-only monitor needs no hook; the turn-game task is reached via
    // W2App::getAddrTurnGame() and traversed for teams/worms.
    Log::info("CTaskTurnGame::install (read-only, no hook)");
}
